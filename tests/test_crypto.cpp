#include <QtTest>
#include "crypto/db_cross.h"

using namespace Zalos::Core::Crypto;

class TestCrypto : public QObject {
    Q_OBJECT

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
};

QTEST_MAIN(TestCrypto)
#include "test_crypto.moc"

