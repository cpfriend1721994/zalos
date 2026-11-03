#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "network/zalo_crypto.h"

using namespace Zalos::Core::Network;

// Vectors generated with crypto-js 4.2.0 / Node crypto using the zca-js algorithms
// (src/utils.ts: ParamsEncryptor, encodeAES, getSignKey, decodeEventData).
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
        // Server payloads may arrive percent-encoded.
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

        // Tampered ciphertext must be rejected by the GCM tag check.
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

    void imeiFormat() {
        const QString ua = "Mozilla/5.0 Test";
        const QString imei = ZaloCrypto::generateImei(ua);
        QVERIFY(imei.endsWith("-" + ZaloCrypto::md5Hex(ua.toUtf8())));
        QCOMPARE(imei.size(), 36 + 1 + 32);
    }
};

QTEST_MAIN(TestProtocol)
#include "test_protocol.moc"
