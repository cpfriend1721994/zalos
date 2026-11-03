#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVariant>

// Cryptographic primitives of the Zalo Web protocol (ported from zca-js utils.ts).
namespace Zalos::Core::Network::ZaloCrypto {

// AES-CBC with a zero IV and PKCS#7 padding. Key length selects AES-128/192/256.
QByteArray aesCbcEncrypt(const QByteArray& key, const QByteArray& plain);
QByteArray aesCbcDecrypt(const QByteArray& key, const QByteArray& cipher, bool* ok = nullptr);

// AES-GCM decrypt; the 16-byte tag is expected at the end of `cipherWithTag`.
QByteArray aesGcmDecrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& aad,
                         const QByteArray& cipherWithTag, bool* ok = nullptr);

// zlib (RFC 1950) inflate, equivalent to pako.inflate.
QByteArray zlibInflate(const QByteArray& compressed, bool* ok = nullptr);

QString md5Hex(const QByteArray& data);

// Session-level request encryption using the base64 `zpw_enk` secret key.
QString encodeAES(const QString& secretKeyB64, const QByteArray& plain);
QByteArray decodeAES(const QString& secretKeyB64, const QString& dataB64, bool* ok = nullptr);

// Login-time response decryption with a raw UTF-8 key (decryptResp in zca-js).
QByteArray decodeRespAES(const QByteArray& utf8Key, const QString& dataB64, bool* ok = nullptr);

// MD5("zsecure" + type + values sorted by key).
QString signKey(const QString& type, const QMap<QString, QString>& params);

// Decodes a websocket event payload ({data, encrypt}) into raw JSON bytes.
QByteArray decodeEventData(const QString& data, int encryptType, const QString& cipherKeyB64, bool* ok = nullptr);

// Device identifier format used by Zalo Web: <uuid-v4>-<md5(userAgent)>.
QString generateImei(const QString& userAgent);

// zcid / encrypt-key derivation for the getLoginInfo request.
class ParamsEncryptor {
public:
    ParamsEncryptor(int apiType, const QString& imei, qint64 firstLaunchTime, const QString& zcidExt = QString());

    QString zcid() const { return m_zcid; }
    QString zcidExt() const { return m_zcidExt; }
    QString encVersion() const { return QStringLiteral("v2"); }
    QString encryptKey() const { return m_encryptKey; }

    // Base64 AES-CBC of `plain` with the derived encrypt key.
    QString encrypt(const QByteArray& plain) const;

    static QString randomHex(int minLen = 6, int maxLen = 12);

private:
    QString m_zcid;
    QString m_zcidExt;
    QString m_encryptKey;
};

} // namespace Zalos::Core::Network::ZaloCrypto
