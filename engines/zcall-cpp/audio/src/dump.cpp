// SPDX-License-Identifier: BSD-3-Clause
#include "dump.h"

#include "log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace zcall::audio {

Dump& Dump::instance() {
    static Dump d;
    return d;
}

void Dump::init() {
    const char* on = std::getenv("ZCALL_AUDIO_DUMP");
    if (!on || std::strcmp(on, "1") != 0) return;
    const char* dir = std::getenv("ZCALL_LOG_DIR");
    std::string d = dir && *dir ? dir : (std::getenv("TMPDIR") && *std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp");
    char stamp[64];
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    ::localtime_r(&t, &tm);
    std::strftime(stamp, sizeof stamp, "call-%Y%m%d-%H%M%S", &tm);
    m_stem = d + "/" + stamp;
    m_played = std::fopen((m_stem + "-played.raw").c_str(), "wb");
    m_mic = m_played ? std::fopen((m_stem + "-mic.raw").c_str(), "wb") : nullptr;
    if (!m_played || !m_mic) {
        logLine(std::string("dump unavailable: ") + std::strerror(errno));
        if (m_played) std::fclose(m_played);
        m_played = m_mic = nullptr;
        return;
    }
    logLine("dump: " + m_stem + "-played.raw / -mic.raw (16 kHz mono s16le)");
}

void Dump::write(std::FILE* f, const int16_t* pcm, int samples) {
    if (!f) return;
    std::fwrite(pcm, 2, static_cast<size_t>(samples), f);
}

}
