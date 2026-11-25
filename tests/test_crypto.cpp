#include <QtTest>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include "crypto/db_cross.h"

#include <lzma.h>
#include <openssl/evp.h>

#include <cstring>
#include <fstream>

using namespace Zalos::Core::Crypto;

class TestCrypto : public QObject {
    Q_OBJECT

    static void putU32Be(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>(v >> 24));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
    }

    static std::vector<uint8_t> compress(const std::vector<uint8_t>& in, bool lzmaAlone) {
        lzma_stream strm = LZMA_STREAM_INIT;
        if (lzmaAlone) {
            lzma_options_lzma opt;
            lzma_lzma_preset(&opt, 6);
            if (lzma_alone_encoder(&strm, &opt) != LZMA_OK) return {};
        } else if (lzma_easy_encoder(&strm, 6, LZMA_CHECK_CRC64) != LZMA_OK) {
            return {};
        }
        std::vector<uint8_t> out(in.size() + 4096);
        strm.next_in = in.data();
        strm.avail_in = in.size();
        strm.next_out = out.data();
        strm.avail_out = out.size();
        const lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
        out.resize(out.size() - strm.avail_out);
        lzma_end(&strm);
        return ret == LZMA_STREAM_END ? out : std::vector<uint8_t>{};
    }

    static std::vector<uint8_t> encryptZeroIv(std::vector<uint8_t> pt, const std::vector<uint8_t>& key) {
        while (pt.size() % 16 != 0) pt.push_back(0);
        std::vector<uint8_t> ct(pt.size() + 16);
        const uint8_t iv[16] = {0};
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        int n1 = 0;
        int n2 = 0;
        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        EVP_EncryptUpdate(ctx, ct.data(), &n1, pt.data(), static_cast<int>(pt.size()));
        EVP_EncryptFinal_ex(ctx, ct.data() + n1, &n2);
        EVP_CIPHER_CTX_free(ctx);
        ct.resize(static_cast<size_t>(n1 + n2));
        return ct;
    }

    using Entry = std::pair<std::string, std::vector<uint8_t>>;

    static bool extractZdb4(const std::vector<Entry>& entries, bool lzmaAlone, std::string* error,
                            QTemporaryDir& dir, std::string* outputDir) {
        std::vector<uint8_t> pt = {'Z', 'D', 'B', '4', '.', '0', 0, 0, 0, 0, 0, 0, 0, 0};
        putU32Be(pt, static_cast<uint32_t>(entries.size()));
        std::vector<uint8_t> body;
        for (const Entry& e : entries) {
            putU32Be(pt, static_cast<uint32_t>(e.first.size()));
            pt.insert(pt.end(), e.first.begin(), e.first.end());
            putU32Be(pt, static_cast<uint32_t>(e.second.size()));
            body.insert(body.end(), e.second.begin(), e.second.end());
        }
        const std::vector<uint8_t> packed = compress(body, lzmaAlone);
        if (packed.empty()) return false;
        pt.insert(pt.end(), packed.begin(), packed.end());

        const std::string passphrase = "0123456789abcdef0123456789abcdef";
        const std::vector<uint8_t> ct = encryptZeroIv(pt, DbCross::deriveAes256Key(passphrase));

        if (!dir.isValid()) return false;
        const std::string input = dir.filePath("backup.zdb").toStdString();
        *outputDir = dir.filePath("db").toStdString();
        {
            std::ofstream f(input, std::ios::binary);
            f.write(reinterpret_cast<const char*>(ct.data()), static_cast<std::streamsize>(ct.size()));
        }
        return DbCross::decompressAndDecryptZdb4(input, *outputDir, passphrase, error);
    }

    static bool roundTripZdb4(bool lzmaAlone, std::string* error, QByteArray* extracted) {
        std::vector<uint8_t> db(64, 0);
        std::memcpy(db.data(), "SQLite format 3", 16);
        for (size_t i = 16; i < db.size(); ++i) db[i] = static_cast<uint8_t>(i);
        QTemporaryDir dir;
        std::string out;
        const bool ok = extractZdb4({{"123456.db", db}}, lzmaAlone, error, dir, &out);
        QFile file(QString::fromStdString(out) + "/123456.db");
        if (ok && file.open(QIODevice::ReadOnly)) *extracted = file.readAll();
        return ok;
    }

private slots:
    void testKeyDerivation() {
        std::string privateKey = "0123456789abcdef0123456789abcdef";
        auto key = DbCross::deriveAes256Key(privateKey);
        QCOMPARE(key.size(), 32UL);

        QCOMPARE(static_cast<char>(key[10]), 'A');
        QCOMPARE(static_cast<char>(key[15]), 'F');
    }

    void testAesDecryptionRoundTrip() {
        bool ok = DbCross::verifyCryptoSelfTest();
        QVERIFY(ok);
    }

    void testEmptyPayload() {
        std::vector<uint8_t> empty;
        std::vector<uint8_t> key(32, 0);
        std::vector<uint8_t> iv(16, 0);
        auto result = DbCross::decryptAes256Cbc(empty, key, iv);
        QVERIFY(result.empty());
    }

    void testBinNetTlvParsing() {
        std::vector<uint8_t> tlvPacket;
        tlvPacket.push_back(0x0b); tlvPacket.push_back(0x00); tlvPacket.push_back(0x00); tlvPacket.push_back(0x00);
        std::string testText = "Xin chao Zalo";
        uint32_t textLen = static_cast<uint32_t>(testText.size());
        tlvPacket.push_back(textLen & 0xff);
        tlvPacket.push_back((textLen >> 8) & 0xff);
        tlvPacket.push_back(0x00);
        tlvPacket.push_back(0x00);
        tlvPacket.insert(tlvPacket.end(), testText.begin(), testText.end());

        auto msg = DbCross::parseBinNet(tlvPacket.data(), tlvPacket.size());
        QCOMPARE(QString::fromStdString(msg.text), QString("Xin chao Zalo"));
    }

    void testChunkedResetIvDecryption() {
        std::vector<uint8_t> key(32, 0x41);
        std::vector<uint8_t> emptyCt;
        std::vector<uint8_t> pt;
        bool ok = DbCross::decryptAes256CbcChunkedResetIv(emptyCt, key, pt);
        QVERIFY(ok);
        QVERIFY(pt.empty());
    }

    void testZdb4XzContainer() {
        std::string error;
        QByteArray extracted;
        QVERIFY2(roundTripZdb4(false, &error, &extracted), error.c_str());
        QCOMPARE(extracted.size(), 64);
        QVERIFY(extracted.startsWith("SQLite format 3"));
    }

    void testZdb4HostileNamesRejectedWithoutWriting_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("parent traversal") << QStringLiteral("../escape.db");
        QTest::newRow("nested traversal") << QStringLiteral("a/../../escape.db");
        QTest::newRow("absolute path") << QStringLiteral("/tmp/escape.db");
        QTest::newRow("empty name") << QString();
        QTest::newRow("overlong name") << QString(4097, QLatin1Char('a'));
    }

    void testZdb4HostileNamesRejectedWithoutWriting() {
        QFETCH(QString, name);
        const std::vector<uint8_t> payload(16, 0x42);
        QTemporaryDir dir;
        std::string out;
        std::string error;
        const bool ok = extractZdb4({{name.toStdString(), payload}}, false, &error, dir, &out);
        QVERIFY(!ok);
        QVERIFY(!QFileInfo::exists(dir.filePath("escape.db")));
        QVERIFY(!QFileInfo::exists(QString::fromStdString(out) + "/" + name));
        QVERIFY(QDir(QString::fromStdString(out)).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
    }

    void testZdb4MultiEntryWithEmptyFile() {
        std::vector<uint8_t> first(64, 0);
        std::memcpy(first.data(), "SQLite format 3", 16);
        const std::vector<uint8_t> second(32, 0x7e);
        QTemporaryDir dir;
        std::string out;
        std::string error;
        QVERIFY2(extractZdb4({{"111.db", first}, {"empty.db", {}}, {"222.db", second}}, false, &error, dir, &out),
                 error.c_str());
        QCOMPARE(QFile(QString::fromStdString(out) + "/111.db").size(), 64);
        QCOMPARE(QFile(QString::fromStdString(out) + "/222.db").size(), 32);
        QVERIFY(QFile::exists(QString::fromStdString(out) + "/empty.db"));
        QCOMPARE(QFile(QString::fromStdString(out) + "/empty.db").size(), 0);
    }

    void testZdb4LzmaAloneContainer() {
        std::string error;
        QByteArray extracted;
        QVERIFY2(roundTripZdb4(true, &error, &extracted), error.c_str());
        QCOMPARE(extracted.size(), 64);
        QVERIFY(extracted.startsWith("SQLite format 3"));
    }
};

QTEST_MAIN(TestCrypto)
#include "test_crypto.moc"

