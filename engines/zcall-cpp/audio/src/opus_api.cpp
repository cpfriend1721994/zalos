// SPDX-License-Identifier: BSD-3-Clause
#include "opus_api.h"

#include <opus.h>

namespace zcall::audio {

std::unique_ptr<OpusVoiceDecoder> OpusVoiceDecoder::create() {
    int err = 0;
    ::OpusDecoder* dec = opus_decoder_create(16000, 1, &err);
    if (err != OPUS_OK || !dec) return nullptr;
    auto d = std::unique_ptr<OpusVoiceDecoder>(new OpusVoiceDecoder);
    d->m_dec = dec;
    return d;
}

OpusVoiceDecoder::~OpusVoiceDecoder() {
    if (m_dec) opus_decoder_destroy(m_dec);
}

int OpusVoiceDecoder::decode(const uint8_t* payload, int len, int16_t* out, int maxSamples, bool fec) {
    return opus_decode(m_dec, payload, payload ? len : 0, out, maxSamples, fec ? 1 : 0);
}

}
