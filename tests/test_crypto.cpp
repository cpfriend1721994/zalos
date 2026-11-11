#include <QtTest>
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

    static bool roundTripZdb4(bool lzmaAlone, std::string* error, QByteArray* extracted) {
        std::vector<uint8_t> db(64, 0);
        std::memcpy(db.data(), "SQLite format 3", 16);
        for (size_t i = 16; i < db.size(); ++i) db[i] = static_cast<uint8_t>(i);

        std::vector<uint8_t> pt = {'Z', 'D', 'B', '4', '.', '0', 0, 0, 0, 0, 0, 0, 0, 0};
        putU32Be(pt, 1);
        const std::string name = "123456.db";
        putU32Be(pt, static_cast<uint32_t>(name.size()));
        pt.insert(pt.end(), name.begin(), name.end());
        putU32Be(pt, static_cast<uint32_t>(db.size()));
        const std::vector<uint8_t> packed = compress(db, lzmaAlone);
        if (packed.empty()) return false;
        pt.insert(pt.end(), packed.begin(), packed.end());

        const std::string passphrase = "0123456789abcdef0123456789abcdef";
        const std::vector<uint8_t> ct = encryptZeroIv(pt, DbCross::deriveAes256Key(passphrase));

        QTemporaryDir dir;
        if (!dir.isValid()) return false;
        const std::string input = dir.filePath("backup.zdb").toStdString();
        const std::string output = dir.filePath("db").toStdString();
        {
            std::ofstream f(input, std::ios::binary);
            f.write(reinterpret_cast<const char*>(ct.data()), static_cast<std::streamsize>(ct.size()));
        }
        const bool ok = DbCross::decompressAndDecryptZdb4(input, output, passphrase, error);
        QFile out(QString::fromStdString(output) + "/" + QString::fromStdString(name));
        if (ok && out.open(QIODevice::ReadOnly)) *extracted = out.readAll();
        return ok;
    }

private slots:
    void testKeyDerivation() {
        std::string privateKey = "0123456789abcdef0123456789abcdef";
        auto key = DbCross::deriveAes256Key(privateKey);
        QCOMPARE(key.size(), 32UL);

        // Verifies uppercase derivation as per Zalo's reverse-engineered protocol
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
        // Construct a synthetic TLV packet:
        // Key 0x0b (message text), length 12: "Xin chao Zalo" (LE: key=0x0b, 0, 0, 0, len=13, 0, 0, 0)
        std::vector<uint8_t> tlvPacket;
        // Key 0x0b
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
        std::vector<uint8_t> key(32, 0x41); // 'A'
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

