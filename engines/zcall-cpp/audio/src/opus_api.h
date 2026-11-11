// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace zcall::audio {

class OpusApi {
public:
    static constexpr int kApplicationVoip = 2048;
    static constexpr int kSetBitrate = 4002;
    static constexpr int kSetComplexity = 4010;
    static constexpr int kSetInbandFec = 4012;
    static constexpr int kSetPacketLossPerc = 4014;
    static constexpr int kSetSignal = 4024;
    static constexpr int kSignalVoice = 3001;

    static std::shared_ptr<OpusApi> load(std::string* error);

    void* (*decoderCreate)(int32_t, int, int*){nullptr};
    int (*decode)(void*, const unsigned char*, int32_t, int16_t*, int, int){nullptr};
    void (*decoderDestroy)(void*){nullptr};
    void* (*encoderCreate)(int32_t, int, int, int*){nullptr};
    int32_t (*encode)(void*, const int16_t*, int, unsigned char*, int32_t){nullptr};
    int (*encoderCtl)(void*, int, ...){nullptr};
    void (*encoderDestroy)(void*){nullptr};

private:
    void* m_handle{nullptr};
};

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual int decode(const uint8_t* payload, int len, int16_t* out, int maxSamples, bool fec) = 0;
};

class OpusDecoder : public Decoder {
public:
    static std::unique_ptr<OpusDecoder> create(std::shared_ptr<OpusApi> api);
    ~OpusDecoder() override;
    int decode(const uint8_t* payload, int len, int16_t* out, int maxSamples, bool fec) override;

private:
    std::shared_ptr<OpusApi> m_api;
    void* m_dec{nullptr};
};

}
