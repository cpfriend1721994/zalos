// SPDX-License-Identifier: BSD-3-Clause
#include "wire/srtp.h"

#include "wire/bytes.h"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/params.h>

#include <algorithm>
#include <memory>

namespace zcall {

namespace {

struct CtxDeleter {
    void operator()(EVP_CIPHER_CTX* c) const { EVP_CIPHER_CTX_free(c); }
};

struct AesCtrState {
    std::unique_ptr<EVP_CIPHER_CTX, CtxDeleter> ctx;
    QByteArray key;
};

EVP_CIPHER_CTX* aesCtrFor(const QByteArray& key) {
    thread_local AesCtrState st;
    if (!st.ctx) st.ctx.reset(EVP_CIPHER_CTX_new());
    if (!st.ctx) return nullptr;
    if (st.key != key) {
        st.key.clear();
        if (EVP_EncryptInit_ex(st.ctx.get(), EVP_aes_128_ctr(), nullptr, reinterpret_cast<const unsigned char*>(key.constData()),
                               nullptr) != 1) {
            return nullptr;
        }
        st.key = key;
    }
    return st.ctx.get();
}

void ctrXor(const QByteArray& key, const QByteArray& iv16, const char* in, char* out, int n) {
    if (n <= 0 || key.size() != 16 || iv16.size() != 16) return;
    EVP_CIPHER_CTX* ctx = aesCtrFor(key);
    if (!ctx) return;
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, nullptr, reinterpret_cast<const unsigned char*>(iv16.constData())) != 1) return;
    int len = 0;
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out), &len, reinterpret_cast<const unsigned char*>(in), n);
}

QByteArray kdf(const QByteArray& masterKey, const QByteArray& masterSalt, int label, int outLen) {
    QByteArray x(16, '\0');
    for (int i = 0; i < 14 && i < masterSalt.size(); ++i) x[i] = masterSalt[i];
    x[7] = static_cast<char>(u8at(x, 7) ^ label);
    return aesCmExpand(masterKey, x, outLen);
}

struct MacDeleter {
    void operator()(EVP_MAC* m) const { EVP_MAC_free(m); }
};

struct MacCtxDeleter {
    void operator()(EVP_MAC_CTX* c) const { EVP_MAC_CTX_free(c); }
};

struct HmacState {
    std::unique_ptr<EVP_MAC, MacDeleter> mac;
    std::unique_ptr<EVP_MAC_CTX, MacCtxDeleter> ctx;
    QByteArray lastKey;
    bool initialized{false};
    HmacState() {
        mac.reset(EVP_MAC_fetch(nullptr, "HMAC", nullptr));
        if (mac) ctx.reset(EVP_MAC_CTX_new(mac.get()));
    }
};

QByteArray hmac10Pair(const QByteArray& key, const char* data, int len, const char* tail, int tailLen) {
    thread_local HmacState st;
    if (!st.ctx) {
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int mdLen = 0;
        QByteArray joined;
        if (tailLen > 0) {
            joined.reserve(len + tailLen);
            joined.append(data, len);
            joined.append(tail, tailLen);
            data = joined.constData();
            len = int(joined.size());
        }
        HMAC(EVP_sha1(), key.constData(), int(key.size()), reinterpret_cast<const unsigned char*>(data), size_t(len),
             md, &mdLen);
        return QByteArray(reinterpret_cast<const char*>(md), 10);
    }
    int initOk = 0;
    if (!st.initialized || st.lastKey != key) {
        char sha1[] = "SHA1";
        const OSSL_PARAM params[] = {
            OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, sha1, 0),
            OSSL_PARAM_construct_end(),
        };
        initOk = EVP_MAC_init(st.ctx.get(), reinterpret_cast<const unsigned char*>(key.constData()),
                              size_t(key.size()), params);
        st.initialized = (initOk == 1);
        st.lastKey = st.initialized ? key : QByteArray();
    } else {
        initOk = EVP_MAC_init(st.ctx.get(), nullptr, 0, nullptr);
    }
    unsigned char md[EVP_MAX_MD_SIZE];
    size_t mdLen = 0;
    if (initOk != 1 || EVP_MAC_update(st.ctx.get(), reinterpret_cast<const unsigned char*>(data), size_t(len)) != 1
        || (tailLen > 0
            && EVP_MAC_update(st.ctx.get(), reinterpret_cast<const unsigned char*>(tail), size_t(tailLen)) != 1)
        || EVP_MAC_final(st.ctx.get(), md, &mdLen, sizeof(md)) != 1 || mdLen < 10) {
        st.initialized = false;
        st.lastKey.clear();
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(md), 10);
}

QByteArray hmac10(const QByteArray& key, const char* data, int len) {
    return hmac10Pair(key, data, len, nullptr, 0);
}

}

QByteArray aesCmExpand(const QByteArray& key, const QByteArray& iv16, int n) {
    QByteArray iv = iv16;
    iv[14] = 0;
    iv[15] = 0;
    QByteArray zeros(n, '\0');
    QByteArray out(n, '\0');
    ctrXor(key, iv, zeros.constData(), out.data(), n);
    return out;
}

std::optional<SrtpKeys> sessionKeys(const QByteArray& master30) {
    if (master30.size() < 30) return std::nullopt;
    const QByteArray mk = master30.left(16);
    const QByteArray ms = master30.mid(16, 14);
    return SrtpKeys{kdf(mk, ms, 0x00, 16), kdf(mk, ms, 0x01, 20), kdf(mk, ms, 0x02, 14)};
}

std::optional<QByteArray> masterFromSessId(const QByteArray& sessIdUtf8) {
    if (sessIdUtf8.size() < 30) return std::nullopt;
    return sessIdUtf8.left(30);
}

QByteArray packetIv(const QByteArray& sessionSalt, uint32_t ssrc, uint16_t seq, uint32_t roc) {
    const uint64_t idx = uint64_t(roc) * 0x10000ULL + seq;
    const uint64_t shifted = idx * 0x10000ULL;
    QByteArray iv(16, '\0');
    wr32be(iv, 4, ssrc);
    wr32be(iv, 8, uint32_t((shifted >> 32) & 0xffffffffULL));
    wr32be(iv, 12, uint32_t(shifted & 0xffffffffULL));
    for (int i = 0; i < 14 && i < sessionSalt.size(); ++i) iv[i] = static_cast<char>(u8at(iv, i) ^ u8at(sessionSalt, i));
    return iv;
}

std::optional<RtpView> parseRtp(const QByteArray& buf) {
    if (buf.size() < 12) return std::nullopt;
    const int x = (u8at(buf, 0) >> 4) & 1;
    RtpView v;
    v.seq = rd16be(buf, 2);
    v.ssrc = rd32be(buf, 8);
    int off = 12 + 4 * (u8at(buf, 0) & 0x0f);
    if (x) {
        if (buf.size() < off + 4) return std::nullopt;
        const int words = rd16be(buf, off + 2);
        off += 4 + 4 * words;
    }
    if (off > buf.size()) return std::nullopt;
    v.headerLen = off;
    return v;
}

std::optional<Unprotected> unprotectRtp(const QByteArray& rtp, const SrtpKeys& keys, uint32_t roc) {
    const auto p = parseRtp(rtp);
    if (!p) return std::nullopt;
    const int payloadLen = int(rtp.size()) - p->headerLen;
    if (payloadLen < 10) return std::nullopt;
    const int bodyLen = payloadLen - 10;
    char rocBe[4];
    rocBe[0] = static_cast<char>(roc >> 24);
    rocBe[1] = static_cast<char>(roc >> 16);
    rocBe[2] = static_cast<char>(roc >> 8);
    rocBe[3] = static_cast<char>(roc);
    const QByteArray mac = hmac10Pair(keys.auth, rtp.constData(), p->headerLen + bodyLen, rocBe, 4);
    if (mac.size() != 10) return std::nullopt;
    if (CRYPTO_memcmp(mac.constData(), rtp.constData() + rtp.size() - 10, 10) != 0) return std::nullopt;
    Unprotected out;
    out.seq = p->seq;
    out.ssrc = p->ssrc;
    out.payload.resize(bodyLen);
    if (bodyLen > 0)
        ctrXor(keys.enc, packetIv(keys.salt, p->ssrc, p->seq, roc), rtp.constData() + p->headerLen, out.payload.data(),
               bodyLen);
    return out;
}

QByteArray protectRtp(const QByteArray& headerAndExt, const QByteArray& payload, const SrtpKeys& keys, uint16_t seq,
                      uint32_t ssrc, uint32_t roc) {
    const int h = int(headerAndExt.size());
    const int n = int(payload.size());
    QByteArray out;
    out.resize(h + n + 10);
    std::copy(headerAndExt.constBegin(), headerAndExt.constEnd(), out.begin());
    if (n > 0) ctrXor(keys.enc, packetIv(keys.salt, ssrc, seq, roc), payload.constData(), out.data() + h, n);
    wr32be(out, h + n, roc);
    const QByteArray mac = hmac10(keys.auth, out.constData(), h + n + 4);
    if (mac.size() != 10) return QByteArray();
    std::copy(mac.constBegin(), mac.constEnd(), out.begin() + (h + n));
    out.resize(h + n + 10);
    return out;
}

}
