// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace zcall {

inline uint8_t u8at(const QByteArray& b, int o) { return static_cast<uint8_t>(b[o]); }

inline uint16_t rd16be(const QByteArray& b, int o) { return static_cast<uint16_t>((u8at(b, o) << 8) | u8at(b, o + 1)); }
inline uint32_t rd24be(const QByteArray& b, int o) {
    return (uint32_t(u8at(b, o)) << 16) | (uint32_t(u8at(b, o + 1)) << 8) | uint32_t(u8at(b, o + 2));
}
inline uint32_t rd32be(const QByteArray& b, int o) {
    return (uint32_t(u8at(b, o)) << 24) | (uint32_t(u8at(b, o + 1)) << 16) | (uint32_t(u8at(b, o + 2)) << 8) |
           uint32_t(u8at(b, o + 3));
}
inline int16_t rd16sbe(const QByteArray& b, int o) { return static_cast<int16_t>(rd16be(b, o)); }
inline uint16_t rd16le(const QByteArray& b, int o) { return static_cast<uint16_t>(u8at(b, o) | (u8at(b, o + 1) << 8)); }
inline uint32_t rd32le(const QByteArray& b, int o) {
    return uint32_t(u8at(b, o)) | (uint32_t(u8at(b, o + 1)) << 8) | (uint32_t(u8at(b, o + 2)) << 16) |
           (uint32_t(u8at(b, o + 3)) << 24);
}

inline void wr8(QByteArray& b, int o, uint32_t v) { b[o] = static_cast<char>(v & 0xff); }
inline void wr16be(QByteArray& b, int o, uint32_t v) {
    b[o] = static_cast<char>((v >> 8) & 0xff);
    b[o + 1] = static_cast<char>(v & 0xff);
}
inline void wr24be(QByteArray& b, int o, uint32_t v) {
    b[o] = static_cast<char>((v >> 16) & 0xff);
    b[o + 1] = static_cast<char>((v >> 8) & 0xff);
    b[o + 2] = static_cast<char>(v & 0xff);
}
inline void wr32be(QByteArray& b, int o, uint32_t v) {
    b[o] = static_cast<char>((v >> 24) & 0xff);
    b[o + 1] = static_cast<char>((v >> 16) & 0xff);
    b[o + 2] = static_cast<char>((v >> 8) & 0xff);
    b[o + 3] = static_cast<char>(v & 0xff);
}
inline void wr16le(QByteArray& b, int o, uint32_t v) {
    b[o] = static_cast<char>(v & 0xff);
    b[o + 1] = static_cast<char>((v >> 8) & 0xff);
}
inline void wr32le(QByteArray& b, int o, uint32_t v) {
    b[o] = static_cast<char>(v & 0xff);
    b[o + 1] = static_cast<char>((v >> 8) & 0xff);
    b[o + 2] = static_cast<char>((v >> 16) & 0xff);
    b[o + 3] = static_cast<char>((v >> 24) & 0xff);
}

inline QByteArray le16(uint32_t v) {
    QByteArray b(2, '\0');
    wr16le(b, 0, v);
    return b;
}
inline QByteArray le32(uint32_t v) {
    QByteArray b(4, '\0');
    wr32le(b, 0, v);
    return b;
}

inline QByteArray u16String(const QByteArray& data) { return le16(uint32_t(data.size())) + data; }

inline QString hexOf(const QByteArray& b, int maxBytes = -1) {
    return QString::fromLatin1((maxBytes >= 0 && b.size() > maxBytes ? b.left(maxBytes) : b).toHex());
}

}
