// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "opus_api.h"

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <vector>

namespace zcall::audio::test {

class FakeDecoder : public Decoder {
public:
    int decode(const uint8_t* payload, int len, int16_t* out, int maxSamples, bool fec) override {
        ++calls;
        if (payload && len > 3 && payload[3] == 255) return -1;
        if (!payload || len == 0) {
            for (int i = 0; i < maxSamples; ++i) out[i] = static_cast<int16_t>((plc * 53 + i * 3) % 101 - 50);
            ++plc;
            return maxSamples;
        }
        if (fec) {
            ++fecs;
            for (int i = 0; i < maxSamples; ++i) out[i] = static_cast<int16_t>(((payload[0] * 131 + i * 7) % 201 - 100) * payload[2]);
            return maxSamples;
        }
        const int n = 320 * payload[1];
        for (int i = 0; i < n; ++i) out[i] = static_cast<int16_t>(((payload[0] * 131 + i * 17) % 201 - 100) * payload[2]);
        return n;
    }
    int plc{0};
    int fecs{0};
    int calls{0};
};

inline std::vector<uint8_t> packet(int tag, int frames = 1, int amp = 1, int flag = 0) {
    return {uint8_t(tag % 256), uint8_t(frames), uint8_t(amp), uint8_t(flag)};
}

inline std::vector<int16_t> fromHex(const QString& hex) {
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    std::vector<int16_t> out(size_t(raw.size()) / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const auto lo = uint8_t(raw[int(2 * i)]);
        const auto hi = uint8_t(raw[int(2 * i + 1)]);
        out[i] = static_cast<int16_t>(uint16_t(lo | (hi << 8)));
    }
    return out;
}

inline QString toHex(const int16_t* pcm, size_t n) {
    QByteArray raw;
    raw.resize(int(n * 2));
    for (size_t i = 0; i < n; ++i) {
        raw[int(2 * i)] = char(uint16_t(pcm[i]) & 0xff);
        raw[int(2 * i + 1)] = char(uint16_t(pcm[i]) >> 8);
    }
    return QString::fromLatin1(raw.toHex());
}

}
