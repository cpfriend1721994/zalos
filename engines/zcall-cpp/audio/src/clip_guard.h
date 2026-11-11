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

    void setSource(std::optional<std::string> source);
    void setPercent(int pct);
    void feed(const int16_t* pcm, size_t n);
    std::string report();
    void announce();

    bool on() const { return m_on; }
    bool manual() const { return m_manual.load(); }
    int steps() const { return m_steps.load(); }
    std::optional<std::string> source() const;

private:
    std::optional<int> percent();
    std::optional<double> db();
    void step(int clipped, double step);
    bool computeOn() const;

    mutable std::mutex m_sourceLock;
    std::optional<std::string> m_source;
    std::shared_ptr<VolumeControl> m_control;
    bool m_async;
    bool m_on{false};
    int m_frames{0};
    int m_clipped{0};
    double m_lastStep{-1e9};
    std::atomic<bool> m_busy{false};
    std::atomic<int64_t> m_totalClipped{0};
    std::atomic<int> m_peak{0};
    std::atomic<int> m_steps{0};
    std::atomic<bool> m_manual{false};
    std::recursive_mutex m_volLock;
    std::optional<int> m_announcedPct;
    double m_announcedAt{0};
    std::optional<double> m_setDb;
};

}
