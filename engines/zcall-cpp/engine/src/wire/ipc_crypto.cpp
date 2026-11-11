// SPDX-License-Identifier: BSD-3-Clause
#include "wire/ipc_crypto.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <openssl/evp.h>

#include <cctype>
#include <memory>

namespace zcall {

namespace {

using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

const unsigned char kZeroIv[16] = {0};

bool isLowerHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

}

std::optional<QByteArray> loadKey(const QString& keyFile, QString* error) {
    QFile f(keyFile);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read key file %1").arg(keyFile);
        return std::nullopt;
    }
    const QByteArray key = QByteArray::fromBase64(f.readAll().trimmed());
    if (key.size() != 16) {
        if (error) *error = QStringLiteral("key file %1 must hold 16 base64-encoded bytes").arg(keyFile);
        return std::nullopt;
    }
    return key;
}

QByteArray ipcEncrypt(const QByteArray& json, const QByteArray& key) {
    CipherCtx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray out(json.size() + 16, '\0');
    int n1 = 0;
    int n2 = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                                   reinterpret_cast<const unsigned char*>(key.constData()), kZeroIv) != 1 ||
        EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &n1,
                          reinterpret_cast<const unsigned char*>(json.constData()), int(json.size())) != 1 ||
        EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + n1, &n2) != 1) {
        return {};
    }
    out.resize(n1 + n2);
    return out.toHex();
}

std::optional<QByteArray> ipcDecrypt(const QByteArray& hex, const QByteArray& key) {
    int valid = 0;
    while (valid + 1 < hex.size() && std::isxdigit(static_cast<unsigned char>(hex[valid])) &&
           std::isxdigit(static_cast<unsigned char>(hex[valid + 1])))
        valid += 2;
    const QByteArray bin = QByteArray::fromHex(hex.left(valid));
    CipherCtx ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    QByteArray out(bin.size() + 16, '\0');
    int n1 = 0;
    int n2 = 0;
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                                   reinterpret_cast<const unsigned char*>(key.constData()), kZeroIv) != 1 ||
        EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &n1,
                          reinterpret_cast<const unsigned char*>(bin.constData()), int(bin.size())) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + n1, &n2) != 1) {
        return std::nullopt;
    }
    out.resize(n1 + n2);
    return out;
}

QVector<QByteArray> ipcChunks(const QByteArray& hex, qint64 msgId) {
    QVector<QByteArray> out;
    if (hex.size() <= 4000) {
        out.push_back(hex + '$');
        return out;
    }
    const int total = int((hex.size() + 3999) / 4000);
    for (int i = 0; i < total; ++i) {
        out.push_back(hex.mid(i * 4000, 4000) + '#' + QByteArray::number(msgId) + '#' + QByteArray::number(total) +
                      '#' + QByteArray::number(i) + "#$");
    }
    return out;
}

FrameParser::FrameParser(QByteArray key, bool expectToken, std::function<void(const IpcFrame&)> onFrame)
    : m_key(std::move(key)), m_needToken(expectToken), m_onFrame(std::move(onFrame)) {}

void FrameParser::push(const QByteArray& chunk) {
    m_buf += chunk;
    if (m_needToken) {
        const int nl = m_buf.indexOf('\n');
        if (nl < 0) return;
        int i = 0;
        while (i < m_buf.size() && isLowerHex(m_buf[i])) ++i;
        const bool hexFrame = i > 0 && i < m_buf.size() && m_buf[i] == '$';
        if (!hexFrame) {
            IpcFrame f;
            f.isToken = true;
            f.token = m_buf.left(nl);
            m_buf.remove(0, nl + 1);
            m_onFrame(f);
        }
        m_needToken = false;
    }
    int i;
    while ((i = m_buf.indexOf('$')) >= 0) {
        QByteArray raw = m_buf.left(i).trimmed();
        m_buf.remove(0, i + 1);
        if (raw.isEmpty()) continue;
        static const QRegularExpression chunkRe(QStringLiteral("^([0-9a-f]*)#(\\d+)#(\\d+)#(\\d+)#$"));
        const auto m = chunkRe.match(QString::fromLatin1(raw));
        if (m.hasMatch()) {
            const QByteArray id = m.captured(2).toLatin1();
            const int total = m.captured(3).toInt();
            const int index = m.captured(4).toInt();
            auto& parts = m_chunks[id];
            if (index >= parts.size()) parts.resize(index + 1);
            parts[index] = m.captured(1).toLatin1();
            int have = 0;
            for (const auto& p : parts)
                if (p) ++have;
            if (have < total) continue;
            raw.clear();
            for (const auto& p : parts)
                if (p) raw += *p;
            m_chunks.remove(id);
        }
        IpcFrame f;
        f.raw = raw;
        const auto text = ipcDecrypt(raw, m_key);
        if (!text) {
            f.isError = true;
            f.error = QStringLiteral("bad decrypt");
            f.raw = raw.left(80);
            m_onFrame(f);
            continue;
        }
        f.text = *text;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(*text, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) f.json = doc.object();
        else if (err.error == QJsonParseError::NoError && doc.isArray()) f.json = doc.array();
        else f.json = QString::fromUtf8(*text);
        m_onFrame(f);
    }
}

}
