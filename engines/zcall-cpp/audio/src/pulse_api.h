// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zcall::audio {

struct PulseSimpleApi {
    struct Spec {
        int format;
        uint32_t rate;
        uint8_t channels;
    };
    struct Attr {
        uint32_t maxlength, tlength, prebuf, minreq, fragsize;
    };

    void* handle{nullptr};
    void* (*simpleNew)(const char*, const char*, int, const char*, const char*, const Spec*, const void*, const Attr*,
                       int*){nullptr};
    int (*simpleWrite)(void*, const void*, size_t, int*){nullptr};
    int (*simpleRead)(void*, void*, size_t, int*){nullptr};
    void (*simpleFree)(void*){nullptr};
    uint64_t (*simpleGetLatency)(void*, int*){nullptr};

    static std::shared_ptr<PulseSimpleApi> load(std::string* error = nullptr);
};

class PulseApi {
public:
    static bool available();
    static bool isRunning();
    static std::optional<std::string> defaultSource();
    static std::vector<std::string> listNames(bool sources);
    static bool hasSourceAndSink(const std::string& source, const std::string& sink);
    static std::optional<int> getSourceVolumePercent(const std::string& source);
    static bool setSourceVolumeRaw(const std::string& source, uint32_t rawVolume);
};

}
