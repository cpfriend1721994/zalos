// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace zcall::audio {

double rms(const int16_t* pcm, size_t n);
int audioLevel(const int16_t* pcm, size_t n);
int pitchLag(const int16_t* samples, size_t n);
void wsolaShrink(const std::vector<int16_t>& in, int16_t* played, std::vector<int16_t>& rest);

class Limiter {
public:
    static constexpr int kLimit = 26000;
    void apply(const int32_t* acc, int voices, int16_t* out);
    double gain() const { return m_lim; }

private:
    double m_lim{1.0};
};

class MicLeveler {
public:
    static constexpr double kTargetDb = -22.0;
    static constexpr double kNoiseCeilDb = -38.0;
    static constexpr double kMaxGainDb = 24.0;
    static constexpr double kSpeechOverNoiseDb = 9.0;
    static constexpr double kFloorDb = -60.0;
    static constexpr size_t kLearnFrames = 20;
    static constexpr double kUpDb = 0.5;
    static constexpr double kDownDb = 1.0;
    static constexpr int kLogEvery = 250;

    MicLeveler(std::function<bool()> farActive, std::optional<std::string> statePath);
    bool process(const int16_t* in, int16_t* out, size_t n);
    double gainDb() const { return m_gainDb; }
    double noiseDb() const { return m_noiseDb; }
    static double levelDb(const int16_t* pcm, size_t n);

private:
    double load();
    void save();

    std::function<bool()> m_farActive;
    std::optional<std::string> m_statePath;
    double m_gainDb{0.0};
    double m_gPrev{1.0};
    double m_noiseDb{-70.0};
    std::deque<double> m_levels;
    int64_t m_n{0};
    int m_speech{0};
    int m_held{0};
    std::vector<double> m_speechDb;
};

}
