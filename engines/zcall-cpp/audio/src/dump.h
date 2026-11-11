// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace zcall::audio {

class Dump {
public:
    static Dump& instance();
    void init();
    void played(const int16_t* pcm, int samples) { write(m_played, pcm, samples); }
    void mic(const int16_t* pcm, int samples) { write(m_mic, pcm, samples); }
    bool active() const { return m_played || m_mic; }
    std::string stem() const { return m_stem; }

private:
    static void write(std::FILE* f, const int16_t* pcm, int samples);
    std::FILE* m_played{nullptr};
    std::FILE* m_mic{nullptr};
    std::string m_stem;
};

}
