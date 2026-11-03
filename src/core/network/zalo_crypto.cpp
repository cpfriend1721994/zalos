#include "zalo_crypto.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUrl>
#include <QUuid>

#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <memory>

namespace Zalos::Core::Network::ZaloCrypto {

namespace {

using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

const EVP_CIPHER* cbcCipherForKey(int keyLen) {
    switch (keyLen) {
        case 16: return EVP_aes_128_cbc();
        case 24: return EVP_aes_192_cbc();
        case 32: return EVP_aes_256_cbc();
        default: return nullptr;
    }
}

const EVP_CIPHER* gcmCipherForKey(int keyLen) {
    switch (keyLen) {
        case 16: return EVP_aes_128_gcm();
        case 24: return EVP_aes_192_gcm();
        case 32: return EVP_aes_256_gcm();
        default: return nullptr;
    }
}

void setOk(bool* ok, bool value) {
    if (ok) *ok = value;
}

QByteArray fromBase64Lenient(const QString& data) {
    // decodeURIComponent() first, as zca-js does for every server payload.
    const QString decoded = data.contains('%') ? QUrl::fromPercentEncoding(data.toLatin1()) : data;
    return QByteArray::fromBase64(decoded.toLatin1());
}

} // namespace

QByteArray aesCbcEncrypt(const QByteArray& key, const QByteArray& plain) {
    const EVP_CIPHER* cipher = cbcCipherForKey(key.size());
    if (!cipher) return {};

    CipherCtx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    const unsigned char iv[16] = {0};
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), cipher, nullptr,
                                   reinterpret_cast<const unsigned char*>(key.constData()), iv) != 1) {
        return {};
    }

    QByteArray out(plain.size() + 16, Qt::Uninitialized);
    int len1 = 0;
    int len2 = 0;
    if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &len1,
                          reinterpret_cast<const unsigned char*>(plain.constData()), plain.size()) != 1 ||
        EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + len1, &len2) != 1) {
        return {};
    }
    out.resize(len1 + len2);
    return out;
}

QByteArray aesCbcDecrypt(const QByteArray& key, const QByteArray& cipherText, bool* ok) {
    setOk(ok, false);
    const EVP_CIPHER* cipher = cbcCipherForKey(key.size());
    if (!cipher || cipherText.isEmpty()) return {};

    CipherCtx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    const unsigned char iv[16] = {0};
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), cipher, nullptr,
                                   reinterpret_cast<const unsigned char*>(key.constData()), iv) != 1) {
        return {};
    }

    QByteArray out(cipherText.size() + 16, Qt::Uninitialized);
    int len1 = 0;
    int len2 = 0;
    if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &len1,
                          reinterpret_cast<const unsigned char*>(cipherText.constData()), cipherText.size()) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + len1, &len2) != 1) {
        return {};
    }
    out.resize(len1 + len2);
    setOk(ok, true);
    return out;
}

QByteArray aesGcmDecrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& aad,
                         const QByteArray& cipherWithTag, bool* ok) {
    setOk(ok, false);
    const EVP_CIPHER* cipher = gcmCipherForKey(key.size());
    if (!cipher || cipherWithTag.size() < 16 || iv.isEmpty()) return {};

    const QByteArray body = cipherWithTag.left(cipherWithTag.size() - 16);
    QByteArray tag = cipherWithTag.right(16);

    CipherCtx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        return {};
    }

    int len = 0;
    if (!aad.isEmpty() &&
        EVP_DecryptUpdate(ctx.get(), nullptr, &len, reinterpret_cast<const unsigned char*>(aad.constData()), aad.size()) != 1) {
        return {};
    }

    QByteArray out(body.size() + 16, Qt::Uninitialized);
    int len1 = 0;
    if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &len1,
                          reinterpret_cast<const unsigned char*>(body.constData()), body.size()) != 1) {
        return {};
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data()) != 1) {
        return {};
    }
    int len2 = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + len1, &len2) != 1) {
        return {};
    }
    out.resize(len1 + len2);
    setOk(ok, true);
    return out;
}

QByteArray zlibInflate(const QByteArray& compressed, bool* ok) {
    setOk(ok, false);
    if (compressed.isEmpty()) return {};

    z_stream stream{};
    // 15 + 32: auto-detect zlib or gzip headers.
    if (inflateInit2(&stream, 15 + 32) != Z_OK) return {};

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    QByteArray out;
    char buffer[16384];
    int ret = Z_OK;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&stream);
            return {};
        }
        out.append(buffer, static_cast<int>(sizeof(buffer) - stream.avail_out));
    } while (ret != Z_STREAM_END && stream.avail_in > 0);

    inflateEnd(&stream);
    setOk(ok, true);
    return out;
}

QString md5Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QString encodeAES(const QString& secretKeyB64, const QByteArray& plain) {
    const QByteArray key = QByteArray::fromBase64(secretKeyB64.toLatin1());
    return QString::fromLatin1(aesCbcEncrypt(key, plain).toBase64());
}

QByteArray decodeAES(const QString& secretKeyB64, const QString& dataB64, bool* ok) {
    const QByteArray key = QByteArray::fromBase64(secretKeyB64.toLatin1());
    return aesCbcDecrypt(key, fromBase64Lenient(dataB64), ok);
}

QByteArray decodeRespAES(const QByteArray& utf8Key, const QString& dataB64, bool* ok) {
    return aesCbcDecrypt(utf8Key, fromBase64Lenient(dataB64), ok);
}

QString signKey(const QString& type, const QMap<QString, QString>& params) {
    // QMap iterates in ascending key order, matching Array.prototype.sort on keys.
    QString material = QStringLiteral("zsecure") + type;
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        material += it.value();
    }
    return md5Hex(material.toUtf8());
}

QByteArray decodeEventData(const QString& data, int encryptType, const QString& cipherKeyB64, bool* ok) {
    setOk(ok, false);
    if (encryptType == 0) {
        setOk(ok, true);
        return data.toUtf8();
    }
    if (encryptType < 0 || encryptType > 3) return {};

    const QByteArray decoded = encryptType == 1 ? QByteArray::fromBase64(data.toLatin1()) : fromBase64Lenient(data);
    QByteArray plain = decoded;

    if (encryptType != 1) {
        if (cipherKeyB64.isEmpty() || decoded.size() < 48) return {};
        bool decrypted = false;
        plain = aesGcmDecrypt(QByteArray::fromBase64(cipherKeyB64.toLatin1()),
                              decoded.mid(0, 16), decoded.mid(16, 16), decoded.mid(32), &decrypted);
        if (!decrypted) return {};
    }

    if (encryptType == 3) {
        setOk(ok, true);
        return plain;
    }
    return zlibInflate(plain, ok);
}

QString generateImei(const QString& userAgent) {
    return QUuid::createUuid().toString(QUuid::WithoutBraces) + "-" + md5Hex(userAgent.toUtf8());
}

ParamsEncryptor::ParamsEncryptor(int apiType, const QString& imei, qint64 firstLaunchTime, const QString& zcidExt)
    : m_zcidExt(zcidExt.isEmpty() ? randomHex() : zcidExt) {
    static const QByteArray kZcidKey = QByteArrayLiteral("3FC4F0D2AB50057BCE0D90D9187A22B1");
    const QByteArray msg = QStringLiteral("%1,%2,%3").arg(apiType).arg(imei).arg(firstLaunchTime).toUtf8();
    m_zcid = QString::fromLatin1(aesCbcEncrypt(kZcidKey, msg).toHex().toUpper());

    // Even/odd character split as in ParamsEncryptor.processStr.
    auto split = [](const QString& s, QString& even, QString& odd) {
        for (int i = 0; i < s.size(); ++i) {
            (i % 2 == 0 ? even : odd).append(s.at(i));
        }
    };
    QString extEven, extOdd, zcidEven, zcidOdd;
    split(md5Hex(m_zcidExt.toUtf8()).toUpper(), extEven, extOdd);
    split(m_zcid, zcidEven, zcidOdd);
    std::reverse(zcidOdd.begin(), zcidOdd.end());
    m_encryptKey = extEven.left(8) + zcidEven.left(12) + zcidOdd.left(12);
}

QString ParamsEncryptor::encrypt(const QByteArray& plain) const {
    return QString::fromLatin1(aesCbcEncrypt(m_encryptKey.toUtf8(), plain).toBase64());
}

QString ParamsEncryptor::randomHex(int minLen, int maxLen) {
    static const char kHex[] = "0123456789abcdef";
    const int len = minLen + QRandomGenerator::global()->bounded(maxLen - minLen + 1);
    QString out;
    out.reserve(len);
    for (int i = 0; i < len; ++i) {
        out.append(QLatin1Char(kHex[QRandomGenerator::global()->bounded(16)]));
    }
    return out;
}

} // namespace Zalos::Core::Network::ZaloCrypto
