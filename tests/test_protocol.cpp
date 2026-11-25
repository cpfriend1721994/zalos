#include <QtTest>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <QRandomGenerator>

#include "network/zalo_crypto.h"

using namespace Zalos::Core::Network;

class TestProtocol : public QObject {
    Q_OBJECT

private slots:
    void paramsEncryptorMatchesZcaJs() {
        const ZaloCrypto::ParamsEncryptor enc(30, "test-imei", 1700000000000LL, "a1b2c3d4e5");
        QCOMPARE(enc.zcid(), QString("D030581B24D15348598F1FC1D1CE6BDF7786AACE1A11883E6084878E9DC9FEED"));
        QCOMPARE(enc.encryptKey(), QString("C3A3B143D3512D54581CDE9DE740E81A"));
        QCOMPARE(enc.encrypt(R"({"a":1})"), QString("sWMVJgssED496svyKnIncg=="));
    }

    void sessionAesRoundTrip() {
        const QString secret = "MDEyMzQ1Njc4OWFiY2RlZg==";
        const QByteArray plain = QString(R"({"hello":"xin chào"})").toUtf8();
        QCOMPARE(ZaloCrypto::encodeAES(secret, plain), QString("z3dsYWfIx8uKMXMiewXppO84lv16LDftlqN7eXztGaU="));

        bool ok = false;
        QCOMPARE(ZaloCrypto::decodeAES(secret, "z3dsYWfIx8uKMXMiewXppO84lv16LDftlqN7eXztGaU%3D", &ok), plain);
        QVERIFY(ok);
    }

    void signKeySortsParameters() {
        const QMap<QString, QString> params{{"zcid", "Z"}, {"type", "30"}, {"client_version", "685"}, {"params", "P"}};
        QCOMPARE(ZaloCrypto::signKey("getlogininfo", params), QString("b9d0c7d8a5449edfbaddce76ad794884"));
    }

    void websocketEventGcmInflate() {
        const QString key = "BwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwc=";
        const QString data = "AQEBAQEBAQEBAQEBAQEBAQICAgICAgICAgICAgICAgLlPG8RpxlrlL5m1hl7n06USc6evCDWDZiY3JRt7ii6b%2B5i%2F%2BXR3KbbO8PT2wPAcz5nCxh2m%2BTwDyVrZ2kU70awx0%2FxRaklfPA%3D";
        bool ok = false;
        const QByteArray json = ZaloCrypto::decodeEventData(data, 2, key, &ok);
        QVERIFY(ok);
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        const QJsonObject msg = root.value("data").toObject().value("msgs").toArray().first().toObject();
        QCOMPARE(msg.value("content").toString(), QString("chào"));

        QString tampered = data;
        tampered[60] = tampered[60] == 'A' ? 'B' : 'A';
        ZaloCrypto::decodeEventData(tampered, 2, key, &ok);
        QVERIFY(!ok);
    }

    void plainEventPassesThrough() {
        bool ok = false;
        QCOMPARE(ZaloCrypto::decodeEventData(R"({"x":1})", 0, QString(), &ok), QByteArray(R"({"x":1})"));
        QVERIFY(ok);
    }

    void sessionAesPropertyRoundTrip() {
        const QString secret = "MDEyMzQ1Njc4OWFiY2RlZg==";
        QRandomGenerator rng(0x5a10u);
        const int sizes[] = {0, 1, 15, 16, 17, 31, 32, 33, 1024, 4096};
        for (int size : sizes) {
            QByteArray plain(size, '\0');
            for (char& c : plain) c = static_cast<char>(rng.bounded(256));
            const QString cipher = ZaloCrypto::encodeAES(secret, plain);
            QCOMPARE(ZaloCrypto::encodeAES(secret, plain), cipher);
            bool ok = false;
            const QByteArray back = ZaloCrypto::decodeAES(secret, cipher, &ok);
            QVERIFY2(ok, qPrintable(QStringLiteral("size %1").arg(size)));
            QCOMPARE(back, plain);
        }
    }

    void sessionAesRejectsBadInput() {
        const QString secret = "MDEyMzQ1Njc4OWFiY2RlZg==";
        const QByteArray plain = QByteArray(40, 'x');
        const QString cipher = ZaloCrypto::encodeAES(secret, plain);
        const QByteArray raw = QByteArray::fromBase64(cipher.toLatin1());

        bool ok = true;
        ZaloCrypto::decodeAES(secret, QString::fromLatin1(raw.left(raw.size() - 1).toBase64()), &ok);
        QVERIFY(!ok);

        ok = true;
        ZaloCrypto::decodeAES(secret, "%%%not-base64%%%", &ok);
        QVERIFY(!ok);

        ok = true;
        const QByteArray wrongKey = ZaloCrypto::decodeAES("ZXhhbXBsZS1vdGhlci1rZXktMTY=", cipher, &ok);
        QVERIFY(!ok || wrongKey != plain);
    }

    void eventDataRejectsMalformed_data() {
        QTest::addColumn<QString>("data");
        QTest::addColumn<int>("encryptType");
        QTest::addColumn<QString>("key");
        QTest::newRow("unknown type 4") << QStringLiteral("AAAA") << 4 << QStringLiteral("BwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwc=");
        QTest::newRow("negative type") << QStringLiteral("AAAA") << -1 << QString();
        QTest::newRow("gcm blob under 48 bytes") << QStringLiteral("AAAAAAAAAAAAAAAAAAAAAA==") << 2
                                                 << QStringLiteral("BwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwcHBwc=");
        QTest::newRow("gcm without key") << QStringLiteral("AQEBAQEBAQEBAQEBAQEBAQICAgICAgICAgICAgICAgLlPG8RpxlrlL5m1hl7n06USc6evCDWDZiY3JRt7ii6b") << 2 << QString();
        QTest::newRow("zlib type 1 garbage") << QStringLiteral("AAAA") << 1 << QString();
    }

    void eventDataRejectsMalformed() {
        QFETCH(QString, data);
        QFETCH(int, encryptType);
        QFETCH(QString, key);
        bool ok = true;
        ZaloCrypto::decodeEventData(data, encryptType, key, &ok);
        QVERIFY(!ok);
    }

    void eventDataPlainAndZlibTypes() {
        const QByteArray json = R"({"ok":true,"msg":"xin chào"})";
        const QByteArray zlibStream = qCompress(json).mid(4);
        bool ok = false;
        QCOMPARE(ZaloCrypto::decodeEventData(QString::fromLatin1(zlibStream.toBase64()), 1, QString(), &ok), json);
        QVERIFY(ok);

        QCOMPARE(ZaloCrypto::decodeEventData(QString::fromUtf8(json), 0, QString(), &ok), json);
        QVERIFY(ok);
    }

    void imeiFormat() {
        const QString ua = "Mozilla/5.0 Test";
        const QString imei = ZaloCrypto::generateImei(ua);
        QVERIFY(imei.endsWith("-" + ZaloCrypto::md5Hex(ua.toUtf8())));
        QCOMPARE(imei.size(), 36 + 1 + 32);
    }
};

QTEST_MAIN(TestProtocol)
#include "test_protocol.moc"
