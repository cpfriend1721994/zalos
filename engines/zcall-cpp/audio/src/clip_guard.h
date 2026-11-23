// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace zcall::audio {

std::optional<int> parseVolumePercent(const std::string& pactlOut);
std::optional<double> parseVolumeDb(const std::string& pactlOut);
int64_t rawFromDb(double db);
int64_t rawFromPercent(int pct);
std::string pyFloat(double v);

class VolumeControl {
public:
    virtual ~VolumeControl() = default;
    virtual bool available() = 0;
    virtual std::optional<std::string> getVolume(const std::string& source) = 0;
    virtual void setVolume(const std::string& source, int64_t raw) = 0;
};

std::shared_ptr<VolumeControl> pactlVolumeControl();

class ClipGuard {
public:
    static constexpr int kWindow = 10;
    static constexpr int kClip = 31000;
    static constexpr int kMinClipped = 16;
    static constexpr double kStepDb = 3.0;
    static constexpr double kStepSec = 1.0;
    static constexpr double kFloorDb = -24.0;

    ClipGuard(std::optional<std::string> source, std::shared_ptr<VolumeControl> control, bool async = true);
    ~ClipGuard();

    void setSource(std::optional<std::string> source);
    void setPercent(int pct);
    void setAutoGain(bool wantAuto);
    void feed(const int16_t* pcm, size_t n);
    std::string report();
    void announce();
    void announceMode() const;

    bool on() const { return m_state->on.load(); }
    bool manual() const { return m_state->manual.load(); }
    bool autoGain() const { return m_state->on.load() && !m_state->manual.load(); }
    int steps() const { return m_state->steps.load(); }
    std::optional<std::string> source() const;

private:
    struct State {
        mutable std::mutex sourceLock;
        std::optional<std::string> source;
        std::shared_ptr<VolumeControl> control;
        std::atomic<bool> on{false};
        std::atomic<bool> busy{false};
        std::atomic<bool> dead{false};
        std::atomic<int64_t> totalClipped{0};
        std::atomic<int> peak{0};
        std::atomic<int> steps{0};
        std::atomic<bool> manual{false};
        std::atomic<bool> resetWindow{false};
        std::recursive_mutex volLock;
        std::optional<int> announcedPct;
        double announcedAt{0};
        std::optional<double> setDb;
    };

    static std::optional<std::string> stateSource(const State& st);
    static std::optional<int> statePercent(State& st);
    static std::optional<double> stateDb(State& st);
    static void announceState(State& st);
    static void stepState(const std::shared_ptr<State>& st, int clipped, double stepDb);
    bool computeOn() const;

    bool m_async;
    int m_frames{0};
    int m_clipped{0};
    double m_lastStep{-1e9};
    std::shared_ptr<State> m_state;
};

}
