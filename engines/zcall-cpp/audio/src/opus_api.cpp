// SPDX-License-Identifier: BSD-3-Clause
#include "opus_api.h"

#include <dlfcn.h>

namespace zcall::audio {

namespace {
template <typename T>
bool bind(void* handle, const char* name, T& out) {
    out = reinterpret_cast<T>(::dlsym(handle, name));
    return out != nullptr;
}
}

std::shared_ptr<OpusApi> OpusApi::load(std::string* error) {
    void* handle = nullptr;
    for (const char* name : {"libopus.so.0", "libopus.so"}) {
        handle = ::dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (handle) break;
    }
    if (!handle) {
        if (error) {
            const char* e = ::dlerror();
            *error = e ? e : "libopus.so.0: cannot open shared object file";
        }
        return nullptr;
    }
    auto api = std::shared_ptr<OpusApi>(new OpusApi);
    api->m_handle = handle;
    const bool ok = bind(handle, "opus_decoder_create", api->decoderCreate) && bind(handle, "opus_decode", api->decode)
        && bind(handle, "opus_decoder_destroy", api->decoderDestroy)
        && bind(handle, "opus_encoder_create", api->encoderCreate) && bind(handle, "opus_encode", api->encode)
        && bind(handle, "opus_encoder_ctl", api->encoderCtl) && bind(handle, "opus_encoder_destroy", api->encoderDestroy);
    if (!ok) {
        if (error) *error = "libopus.so.0 lacks the Opus API";
        return nullptr;
    }
    return api;
}

std::unique_ptr<OpusDecoder> OpusDecoder::create(std::shared_ptr<OpusApi> api) {
    int err = 0;
    void* dec = api->decoderCreate(16000, 1, &err);
    if (err != 0 || !dec) return nullptr;
    auto d = std::unique_ptr<OpusDecoder>(new OpusDecoder);
    d->m_api = std::move(api);
    d->m_dec = dec;
    return d;
}

OpusDecoder::~OpusDecoder() {
    if (m_dec) m_api->decoderDestroy(m_dec);
}

int OpusDecoder::decode(const uint8_t* payload, int len, int16_t* out, int maxSamples, bool fec) {
    return m_api->decode(m_dec, payload, payload ? len : 0, out, maxSamples, fec ? 1 : 0);
}

}
