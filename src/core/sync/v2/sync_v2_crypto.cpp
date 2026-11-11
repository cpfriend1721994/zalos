#include "sync/v2/sync_v2_crypto.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>

#include <memory>

namespace Zalos::Core::Sync::V2 {

namespace {

constexpr int kSaltSize = 32;
constexpr int kNonceSize = 12;
constexpr int kTagSize = 16;
constexpr int kKeySize = 32;
constexpr int kSessionIdSize = 32;
constexpr int kRecordKeySize = 32;
constexpr char kMessageInfo[] = "ZaloSyncMsgSecure";
constexpr char kVersionByte = 0x01;

using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

bool deriveMessageKey(const QByteArray& recordKey, const QByteArray& salt, unsigned char* out) {
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr), &EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) <= 0) return false;
    if (EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) <= 0) return false;
    if (EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), reinterpret_cast<const unsigned char*>(salt.constData()),
                                    salt.size()) <= 0)
        return false;
    if (EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), reinterpret_cast<const unsigned char*>(recordKey.constData()),
                                   recordKey.size()) <= 0)
        return false;
    if (EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), reinterpret_cast<const unsigned char*>(kMessageInfo),
                                    static_cast<int>(sizeof(kMessageInfo) - 1)) <= 0)
        return false;
    size_t outLen = kKeySize;
    return EVP_PKEY_derive(ctx.get(), out, &outLen) > 0 && outLen == kKeySize;
}

}  // namespace

CryptoStatus openMessage(const SessionRecord& record, const QByteArray& sealed, QByteArray* plaintext) {
    if (record.sessionId.size() != kSessionIdSize || record.key.size() != kRecordKeySize) return CryptoStatus::BadInput;
    if (sealed.size() < kSaltSize + kNonceSize + kTagSize) return CryptoStatus::BadInput;

    const QByteArray salt = sealed.left(kSaltSize);
    const QByteArray nonce = sealed.mid(kSaltSize, kNonceSize);
    const int bodyLen = sealed.size() - kSaltSize - kNonceSize - kTagSize;
    const QByteArray body = sealed.mid(kSaltSize + kNonceSize, bodyLen);
    const QByteArray tag = sealed.right(kTagSize);

    unsigned char key[kKeySize];
    if (!deriveMessageKey(record.key, salt, key)) return CryptoStatus::BadInput;

    QByteArray aad;
    aad.reserve(kSessionIdSize + 8 + 1 + kSaltSize);
    aad.append(record.sessionId);
    for (int i = 0; i < 8; ++i) aad.append(static_cast<char>((record.recordId >> (8 * i)) & 0xff));
    aad.append(kVersionByte);
    aad.append(salt);

    CipherCtx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx) return CryptoStatus::BadInput;
    if (EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1) return CryptoStatus::BadInput;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, kNonceSize, nullptr) != 1) return CryptoStatus::BadInput;
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key, reinterpret_cast<const unsigned char*>(nonce.constData())) != 1)
        return CryptoStatus::BadInput;
    int outLen = 0;
    if (EVP_DecryptUpdate(ctx.get(), nullptr, &outLen, reinterpret_cast<const unsigned char*>(aad.constData()),
                          aad.size()) != 1)
        return CryptoStatus::BadInput;

    plaintext->resize(bodyLen);
    if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(plaintext->data()), &outLen,
                          reinterpret_cast<const unsigned char*>(body.constData()), body.size()) != 1)
        return CryptoStatus::AuthFailed;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, kTagSize,
                            const_cast<char*>(tag.constData())) != 1)
        return CryptoStatus::BadInput;
    int finalLen = 0;
    unsigned char scratch[16];
    if (EVP_DecryptFinal_ex(ctx.get(), scratch, &finalLen) != 1) {
        plaintext->clear();
        return CryptoStatus::AuthFailed;
    }
    plaintext->resize(outLen);
    return CryptoStatus::Ok;
}

CryptoStatus openMetadata(const QByteArray& envelope, QByteArray* plaintext) {
    Q_UNUSED(envelope)
    Q_UNUSED(plaintext)
    return CryptoStatus::Unsupported;
}

QString cryptoStatusName(CryptoStatus status) {
    switch (status) {
        case CryptoStatus::Ok: return QStringLiteral("ok");
        case CryptoStatus::BadInput: return QStringLiteral("bad_input");
        case CryptoStatus::AuthFailed: return QStringLiteral("auth_failed");
        case CryptoStatus::Unsupported: return QStringLiteral("unsupported");
    }
    return QStringLiteral("unknown");
}

}  // namespace Zalos::Core::Sync::V2
