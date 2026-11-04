#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVariant>

namespace Zalos::Core::Network::ZaloCrypto {

QByteArray aesCbcEncrypt(const QByteArray& key, const QByteArray& plain);
QByteArray aesCbcDecrypt(const QByteArray& key, const QByteArray& cipher, bool* ok = nullptr);

QByteArray aesGcmDecrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& aad,
                         const QByteArray& cipherWithTag, bool* ok = nullptr);

QByteArray zlibInflate(const QByteArray& compressed, bool* ok = nullptr);

QString md5Hex(const QByteArray& data);

QString encodeAES(const QString& secretKeyB64, const QByteArray& plain);
QByteArray decodeAES(const QString& secretKeyB64, const QString& dataB64, bool* ok = nullptr);

QByteArray decodeRespAES(const QByteArray& utf8Key, const QString& dataB64, bool* ok = nullptr);

QString signKey(const QString& type, const QMap<QString, QString>& params);

QByteArray decodeEventData(const QString& data, int encryptType, const QString& cipherKeyB64, bool* ok = nullptr);

QString generateImei(const QString& userAgent);

class ParamsEncryptor {
public:
    ParamsEncryptor(int apiType, const QString& imei, qint64 firstLaunchTime, const QString& zcidExt = QString());

    QString zcid() const { return m_zcid; }
    QString zcidExt() const { return m_zcidExt; }
    QString encVersion() const { return QStringLiteral("v2"); }
    QString encryptKey() const { return m_encryptKey; }

    QString encrypt(const QByteArray& plain) const;

    static QString randomHex(int minLen = 6, int maxLen = 12);

private:
    QString m_zcid;
    QString m_zcidExt;
    QString m_encryptKey;
};

}
