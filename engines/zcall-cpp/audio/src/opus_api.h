// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <opus.h>

#include <cstdint>
#include <memory>

namespace zcall::audio {

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual int decode(const uint8_t* payload, int len, int16_t* out, int maxSamples, bool fec) = 0;
};

class OpusVoiceDecoder : public Decoder {
public:
    static std::unique_ptr<OpusVoiceDecoder> create();
    ~OpusVoiceDecoder() override;
    int decode(const uint8_t* payload, int len, int16_t* out, int maxSamples, bool fec) override;

private:
    ::OpusDecoder* m_dec{nullptr};
};

}
