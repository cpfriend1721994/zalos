#include "sync/v2/sync_v2_crypto.h"
#include "sync/v2/sync_v2_frames.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/sha.h>

#include <cstring>
#include <memory>

namespace Zalos::Core::Sync::V2 {

namespace {

constexpr int kSaltSize = 32;
constexpr int kNonceSize = 12;
constexpr int kTagSize = 16;
constexpr int kKeySize = 32;
constexpr int kSessionIdSize = 32;
constexpr int kRecordKeySize = 32;
constexpr int kSskSize = 125;
constexpr char kMessageInfo[] = "ZaloSyncMsgSecure";
constexpr char kTrustedSyncAad[] = "ZaloTrustedSync";
constexpr char kSyncKeyInfo[] = "ZaloSyncKey";
constexpr char kVersionByte = 0x01;

using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using Pkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

bool hkdfSha256(const QByteArray& ikm, const QByteArray& salt, const QByteArray& info, unsigned char* out) {
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr), &EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) <= 0) return false;
    if (EVP_PKEY_CTX_set_hkdf_md(ctx.get(), EVP_sha256()) <= 0) return false;
    if (EVP_PKEY_CTX_set1_hkdf_salt(ctx.get(), reinterpret_cast<const unsigned char*>(salt.constData()),
                                    salt.size()) <= 0)
        return false;
    if (EVP_PKEY_CTX_set1_hkdf_key(ctx.get(), reinterpret_cast<const unsigned char*>(ikm.constData()),
                                   ikm.size()) <= 0)
        return false;
    if (EVP_PKEY_CTX_add1_hkdf_info(ctx.get(), reinterpret_cast<const unsigned char*>(info.constData()),
                                    info.size()) <= 0)
        return false;
    size_t outLen = kKeySize;
    return EVP_PKEY_derive(ctx.get(), out, &outLen) > 0 && outLen == kKeySize;
}

CryptoStatus chacha20Poly1305Decrypt(const unsigned char* key, QByteArrayView nonce,
                                     QByteArrayView body, QByteArrayView tag,
                                     const QByteArray& aad, QByteArray* plaintext) {
    if (nonce.size() != kNonceSize || tag.size() != kTagSize) return CryptoStatus::BadInput;
    CipherCtx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx) return CryptoStatus::BadInput;
    if (EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1)
        return CryptoStatus::BadInput;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, kNonceSize, nullptr) != 1)
        return CryptoStatus::BadInput;
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key,
                           reinterpret_cast<const unsigned char*>(nonce.data())) != 1)
        return CryptoStatus::BadInput;
    int outLen = 0;
    if (!aad.isEmpty() &&
        EVP_DecryptUpdate(ctx.get(), nullptr, &outLen,
                          reinterpret_cast<const unsigned char*>(aad.constData()), aad.size()) != 1) {
        return CryptoStatus::BadInput;
    }

    plaintext->resize(body.size());
    if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(plaintext->data()), &outLen,
                          reinterpret_cast<const unsigned char*>(body.data()), body.size()) != 1) {
        plaintext->clear();
        return CryptoStatus::AuthFailed;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, kTagSize,
                            const_cast<char*>(tag.data())) != 1) {
        plaintext->clear();
        return CryptoStatus::BadInput;
    }
    int finalLen = 0;
    unsigned char scratch[16];
    if (EVP_DecryptFinal_ex(ctx.get(), scratch, &finalLen) != 1) {
        plaintext->clear();
        return CryptoStatus::AuthFailed;
    }
    plaintext->resize(outLen);
    return CryptoStatus::Ok;
}

QByteArray raw32(const QByteArray& key) {
    if (key.size() == 33 && static_cast<unsigned char>(key.at(0)) == 0x05) return key.mid(1);
    return key;
}

void appendU64Le(QByteArray* out, quint64 v) {
    for (int i = 0; i < 8; ++i) out->append(static_cast<char>((v >> (8 * i)) & 0xff));
}

quint32 rotl32(quint32 v, int n) {
    const unsigned s = static_cast<unsigned>(n) & 31u;
    return s ? ((v << s) | (v >> (32u - s))) : v;
}
quint32 rotr32(quint32 v, int n) {
    const unsigned s = static_cast<unsigned>(n) & 31u;
    return s ? ((v >> s) | (v << (32u - s))) : v;
}

void chachaQuarterRound(quint32* s, int a, int b, int c, int d) {
    s[a] += s[b]; s[d] = rotl32(s[d] ^ s[a], 16);
    s[c] += s[d]; s[b] = rotl32(s[b] ^ s[c], 12);
    s[a] += s[b]; s[d] = rotl32(s[d] ^ s[a], 8);
    s[c] += s[d]; s[b] = rotl32(s[b] ^ s[c], 7);
}

void deriveRecordIdMask(quint64 recordId, QByteArray* u64First, QByteArray* u64Second) {
    quint64 state = recordId;
    quint32 keyWords[8];
    for (int i = 0; i < 8; ++i) {
        state = state * 0x5851f42d4c957f2dULL + 0xa17654e46fbe17f3ULL;
        const quint32 xorshifted = static_cast<quint32>(((state >> 18) ^ state) >> 27);
        const int rot = static_cast<int>(state >> 59);
        keyWords[i] = rotr32(xorshifted, rot);
    }
    quint32 st[16] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u,
        keyWords[0], keyWords[1], keyWords[2], keyWords[3],
        keyWords[4], keyWords[5], keyWords[6], keyWords[7],
        0u, 0u, 0u, 0u
    };
    quint32 w[16];
    std::memcpy(w, st, sizeof(st));
    for (int i = 0; i < 10; ++i) {
        chachaQuarterRound(w, 0, 4, 8, 12);
        chachaQuarterRound(w, 1, 5, 9, 13);
        chachaQuarterRound(w, 2, 6, 10, 14);
        chachaQuarterRound(w, 3, 7, 11, 15);
        chachaQuarterRound(w, 0, 5, 10, 15);
        chachaQuarterRound(w, 1, 6, 11, 12);
        chachaQuarterRound(w, 2, 7, 8, 13);
        chachaQuarterRound(w, 3, 4, 9, 14);
    }
    unsigned char block[64];
    for (int i = 0; i < 16; ++i) {
        const quint32 word = w[i] + st[i];
        block[i * 4 + 0] = static_cast<unsigned char>(word & 0xff);
        block[i * 4 + 1] = static_cast<unsigned char>((word >> 8) & 0xff);
        block[i * 4 + 2] = static_cast<unsigned char>((word >> 16) & 0xff);
        block[i * 4 + 3] = static_cast<unsigned char>((word >> 24) & 0xff);
    }
    *u64First = QByteArray(reinterpret_cast<const char*>(block), 8);
    *u64Second = QByteArray(reinterpret_cast<const char*>(block + 8), 8);
}

}

KeyPair generateKeyPair() {
    KeyPair out;
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), &EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) <= 0) return out;
    EVP_PKEY* rawPkey = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &rawPkey) <= 0 || !rawPkey) return out;
    Pkey pkey(rawPkey, &EVP_PKEY_free);

    size_t pubLen = 32;
    size_t privLen = 32;
    QByteArray pub32(32, 0);
    QByteArray priv32(32, 0);
    if (EVP_PKEY_get_raw_public_key(pkey.get(), reinterpret_cast<unsigned char*>(pub32.data()), &pubLen) != 1 ||
        pubLen != 32 ||
        EVP_PKEY_get_raw_private_key(pkey.get(), reinterpret_cast<unsigned char*>(priv32.data()), &privLen) != 1 ||
        privLen != 32) {
        return out;
    }
    out.pub.reserve(33);
    out.pub.append(static_cast<char>(0x05));
    out.pub.append(pub32);
    out.priv = priv32;
    return out;
}

QByteArray calculateAgreement(const QByteArray& priv32, const QByteArray& pub32Or33) {
    const QByteArray pub32 = raw32(pub32Or33);
    if (priv32.size() != 32 || pub32.size() != 32) return {};

    Pkey privKey(EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char*>(priv32.constData()), 32), &EVP_PKEY_free);
    Pkey pubKey(EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr, reinterpret_cast<const unsigned char*>(pub32.constData()), 32), &EVP_PKEY_free);
    if (!privKey || !pubKey) return {};

    PkeyCtx ctx(EVP_PKEY_CTX_new(privKey.get(), nullptr), &EVP_PKEY_CTX_free);
    if (!ctx || EVP_PKEY_derive_init(ctx.get()) <= 0) return {};
    if (EVP_PKEY_derive_set_peer(ctx.get(), pubKey.get()) <= 0) return {};

    size_t secretLen = 0;
    if (EVP_PKEY_derive(ctx.get(), nullptr, &secretLen) <= 0 || secretLen != 32) return {};
    QByteArray secret(32, 0);
    if (EVP_PKEY_derive(ctx.get(), reinterpret_cast<unsigned char*>(secret.data()), &secretLen) <= 0) return {};
    return secret;
}

CryptoStatus openMessage(const SessionRecord& record, QByteArrayView sealed, QByteArray* plaintext) {
    if (record.sessionId.size() != kSessionIdSize || record.key.size() != kRecordKeySize) return CryptoStatus::BadInput;
    if (sealed.size() < kSaltSize + kNonceSize + kTagSize) return CryptoStatus::BadInput;

    const QByteArray salt = sealed.first(kSaltSize).toByteArray();
    const QByteArrayView nonce = sealed.sliced(kSaltSize, kNonceSize);
    const int bodyLen = sealed.size() - kSaltSize - kNonceSize - kTagSize;
    const QByteArrayView body = sealed.sliced(kSaltSize + kNonceSize, bodyLen);
    const QByteArrayView tag = sealed.last(kTagSize);

    unsigned char key[kKeySize];
    if (!hkdfSha256(record.key, salt, QByteArray(kMessageInfo, sizeof(kMessageInfo) - 1), key))
        return CryptoStatus::BadInput;

    QByteArray aad;
    aad.reserve(kSessionIdSize + 8 + 1 + kSaltSize);
    aad.append(record.sessionId);
    appendU64Le(&aad, record.recordId);
    aad.append(kVersionByte);
    aad.append(salt);

    return chacha20Poly1305Decrypt(key, nonce, body, tag, aad, plaintext);
}

CryptoStatus openMetadata(QByteArrayView envelope, const QByteArray& receiverIkPub,
                          const QByteArray& receiverIkPriv, const QByteArray& receiverEkPriv,
                          MetadataPlain* out) {
    if (!out) return CryptoStatus::BadInput;
    const auto fields = parseProto(envelope);
    if (!fields.has_value()) return CryptoStatus::BadInput;

    QByteArray sessionId;
    QByteArray ssk;
    QByteArray senderIkPub;
    QByteArray encData;
    quint64 version = 0;
    quint64 recordId = 0;
    bool hasVersion = false;
    bool hasRecordId = false;
    for (const auto& f : *fields) {
        if (f.number == 1 && f.wireType == 2) sessionId = f.bytes.toByteArray();
        else if (f.number == 2 && f.wireType == 2) ssk = f.bytes.toByteArray();
        else if (f.number == 4 && f.wireType == 2) senderIkPub = f.bytes.toByteArray();
        else if (f.number == 5 && f.wireType == 2) encData = f.bytes.toByteArray();
        else if (f.number == 6 && f.wireType == 0) { version = f.varint; hasVersion = true; }
        else if (f.number == 7 && f.wireType == 0) { recordId = f.varint; hasRecordId = true; }
    }

    if (!hasVersion || version != 1 || !hasRecordId || sessionId.size() != kSessionIdSize ||
        ssk.size() != kSskSize || senderIkPub.size() != 33 ||
        encData.size() < kNonceSize + kTagSize) {
        return CryptoStatus::BadInput;
    }

    const QByteArray rIk32 = raw32(receiverIkPub);
    const QByteArray sIk32 = raw32(senderIkPub);
    if (rIk32.size() != 32 || sIk32.size() != 32 ||
        receiverIkPriv.size() != 32 || receiverEkPriv.size() != 32) {
        return CryptoStatus::BadInput;
    }

    const QByteArray rEkPubPair = [receiverEkPriv]() {
        Pkey priv(EVP_PKEY_new_raw_private_key(
            EVP_PKEY_X25519, nullptr,
            reinterpret_cast<const unsigned char*>(receiverEkPriv.constData()), 32), &EVP_PKEY_free);
        QByteArray pub(32, 0);
        size_t len = 32;
        if (!priv || EVP_PKEY_get_raw_public_key(priv.get(), reinterpret_cast<unsigned char*>(pub.data()), &len) != 1 || len != 32)
            return QByteArray();
        return pub;
    }();
    if (rEkPubPair.size() != 32) return CryptoStatus::BadInput;

    const QByteArray sskSalt = ssk.left(32);
    const QByteArray epub32 = ssk.mid(33, 32);
    const QByteArray sskNonce = ssk.mid(65, kNonceSize);
    const QByteArray sskBody = ssk.mid(77, kSskSize - 77 - kTagSize);
    const QByteArray sskTag = ssk.right(kTagSize);

    const QByteArray dh1 = calculateAgreement(receiverEkPriv, sIk32);
    const QByteArray dh2 = calculateAgreement(receiverIkPriv, sIk32);
    const QByteArray dh3 = calculateAgreement(receiverEkPriv, epub32);
    if (dh1.size() != 32 || dh2.size() != 32 || dh3.size() != 32) return CryptoStatus::BadInput;

    QByteArray dhConcat;
    dhConcat.reserve(96);
    dhConcat.append(dh1);
    dhConcat.append(dh2);
    dhConcat.append(dh3);

    unsigned char ikm1[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(dhConcat.constData()),
           static_cast<size_t>(dhConcat.size()), ikm1);

    QByteArray u64First;
    QByteArray u64Second;
    deriveRecordIdMask(recordId, &u64First, &u64Second);

    QByteArray info1;
    info1.reserve(1 + 32 + 32 + 32 + 32 + 8);
    info1.append(kVersionByte);
    info1.append(sIk32);
    info1.append(rIk32);
    info1.append(rEkPubPair);
    info1.append(epub32);
    info1.append(u64First);

    unsigned char key1[kKeySize];
    if (!hkdfSha256(QByteArray(reinterpret_cast<const char*>(ikm1), SHA256_DIGEST_LENGTH),
                    QByteArray(32, '\0'), info1, key1)) {
        return CryptoStatus::BadInput;
    }

    QByteArray aad1;
    aad1.append(kVersionByte);
    aad1.append(kTrustedSyncAad, sizeof(kTrustedSyncAad) - 1);

    QByteArray sskPlain;
    const CryptoStatus s1 = chacha20Poly1305Decrypt(key1, sskNonce, sskBody, sskTag, aad1, &sskPlain);
    if (s1 != CryptoStatus::Ok) return s1;

    unsigned char sessionKey[kKeySize];
    if (!hkdfSha256(sskPlain, sskSalt, QByteArray(kSyncKeyInfo, sizeof(kSyncKeyInfo) - 1), sessionKey))
        return CryptoStatus::BadInput;

    QByteArray aad2;
    aad2.reserve(kSessionIdSize + 8 + 1);
    aad2.append(sessionId);
    aad2.append(u64Second);
    aad2.append(kVersionByte);

    const QByteArray msgNonce = encData.left(kNonceSize);
    const QByteArray msgBody = encData.mid(kNonceSize, encData.size() - kNonceSize - kTagSize);
    const QByteArray msgTag = encData.right(kTagSize);

    const CryptoStatus s2 = chacha20Poly1305Decrypt(sessionKey, msgNonce, msgBody, msgTag, aad2, &out->plaintext);
    if (s2 != CryptoStatus::Ok) return s2;

    out->session.sessionId = sessionId;
    out->session.key = QByteArray(reinterpret_cast<const char*>(sessionKey), kKeySize);
    out->session.recordId = recordId;
    out->senderIdentity = senderIkPub;
    return CryptoStatus::Ok;
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

}
