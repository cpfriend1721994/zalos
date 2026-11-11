// SPDX-License-Identifier: BSD-3-Clause
#include "wire/srtp.h"

#include "wire/bytes.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <memory>

namespace zcall {

namespace {

struct CtxDeleter {
    void operator()(EVP_CIPHER_CTX* c) const { EVP_CIPHER_CTX_free(c); }
};

EVP_CIPHER_CTX* ctrContext() {
    thread_local std::unique_ptr<EVP_CIPHER_CTX, CtxDeleter> ctx(EVP_CIPHER_CTX_new());
    return ctx.get();
}

void ctrXor(const QByteArray& key, const QByteArray& iv16, const char* in, char* out, int n) {
    EVP_CIPHER_CTX* ctx = ctrContext();
    int len = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, reinterpret_cast<const unsigned char*>(key.constData()),
                       reinterpret_cast<const unsigned char*>(iv16.constData()));
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out), &len, reinterpret_cast<const unsigned char*>(in), n);
}

QByteArray kdf(const QByteArray& masterKey, const QByteArray& masterSalt, int label, int outLen) {
    QByteArray x(16, '\0');
    for (int i = 0; i < 14 && i < masterSalt.size(); ++i) x[i] = masterSalt[i];
    x[7] = static_cast<char>(u8at(x, 7) ^ label);
    return aesCmExpand(masterKey, x, outLen);
}

QByteArray hmac10(const QByteArray& key, const char* data, int len) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    HMAC(EVP_sha1(), key.constData(), int(key.size()), reinterpret_cast<const unsigned char*>(data), size_t(len), md,
         &mdLen);
    return QByteArray(reinterpret_cast<const char*>(md), 10);
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
    QByteArray authed;
    authed.reserve(p->headerLen + bodyLen + 4);
    authed.append(rtp.constData(), p->headerLen + bodyLen);
    authed.append(QByteArray(4, '\0'));
    wr32be(authed, p->headerLen + bodyLen, roc);
    const QByteArray mac = hmac10(keys.auth, authed.constData(), int(authed.size()));
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
    out.resize(h + n + 4);
    std::copy(headerAndExt.constBegin(), headerAndExt.constEnd(), out.begin());
    if (n > 0) ctrXor(keys.enc, packetIv(keys.salt, ssrc, seq, roc), payload.constData(), out.data() + h, n);
    wr32be(out, h + n, roc);
    const QByteArray mac = hmac10(keys.auth, out.constData(), h + n + 4);
    out.resize(h + n);
    out.append(mac);
    return out;
}

}
