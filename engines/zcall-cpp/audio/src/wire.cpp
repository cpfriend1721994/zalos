// SPDX-License-Identifier: BSD-3-Clause
#include "wire.h"

#include <cstdio>

namespace zcall::audio {

namespace {
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
}

void InParser::feed(const uint8_t* data, size_t n) {
    if (m_pos > 0 && m_pos == m_buf.size()) {
        m_buf.clear();
        m_pos = 0;
    } else if (m_pos > 65536) {
        m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_pos));
        m_pos = 0;
    }
    m_buf.insert(m_buf.end(), data, data + n);
}

bool InParser::next(InMessage& out) {
    if (m_failed) return false;
    const size_t avail = m_buf.size() - m_pos;
    if (avail < 1) return false;
    const uint8_t* p = m_buf.data() + m_pos;
    const uint8_t kind = p[0];
    switch (kind) {
    case 'F': {
        if (avail < 11) return false;
        const uint16_t n = be16(p + 9);
        if (avail < 11u + n) return false;
        out = InMessage{};
        out.kind = InMessage::Kind::Frame;
        out.idx = be32(p + 1);
        out.src = be32(p + 5);
        out.payload.assign(p + 11, p + 11 + n);
        m_pos += 11u + n;
        return true;
    }
    case 'S':
    case 'M':
    case 'V': {
        if (avail < 2) return false;
        out = InMessage{};
        out.kind = kind == 'S' ? InMessage::Kind::Speaker : kind == 'M' ? InMessage::Kind::Mute : InMessage::Kind::Volume;
        out.value = p[1];
        out.on = p[1] != 0;
        m_pos += 2;
        return true;
    }
    case 'D': {
        if (avail < 4) return false;
        const uint16_t n = be16(p + 2);
        if (avail < 4u + n) return false;
        out = InMessage{};
        out.kind = InMessage::Kind::Device;
        out.which = static_cast<char>(p[1]);
        out.name.assign(reinterpret_cast<const char*>(p + 4), n);
        m_pos += 4u + n;
        return true;
    }
    default:
        out = InMessage{};
        out.kind = InMessage::Kind::Bad;
        out.badKind = kind;
        m_failed = true;
        return true;
    }
}

std::vector<uint8_t> encodeLevelFrame(uint8_t level, const uint8_t* opus, uint16_t size) {
    std::vector<uint8_t> out(4u + size);
    out[0] = 'L';
    out[1] = level;
    out[2] = uint8_t(size >> 8);
    out[3] = uint8_t(size & 0xff);
    for (uint16_t i = 0; i < size; ++i) out[4u + i] = opus[i];
    return out;
}

std::vector<uint8_t> encodeInFrame(uint32_t idx, uint32_t src, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    out.reserve(11 + payload.size());
    out.push_back('F');
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(idx >> s));
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(src >> s));
    out.push_back(uint8_t(payload.size() >> 8));
    out.push_back(uint8_t(payload.size() & 0xff));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::string pyBytesRepr(uint8_t b) {
    if (b == '\'') return "b\"'\"";
    if (b == '\\') return "b'\\\\'";
    if (b == '\t') return "b'\\t'";
    if (b == '\n') return "b'\\n'";
    if (b == '\r') return "b'\\r'";
    if (b >= 0x20 && b < 0x7f) return std::string("b'") + char(b) + "'";
    char hex[16];
    std::snprintf(hex, sizeof hex, "b'\\x%02x'", b);
    return hex;
}

}
