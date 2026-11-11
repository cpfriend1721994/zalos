// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "dsp.h"
#include "jitter.h"
#include "sink.h"
#include "spsc_ring.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace zcall::audio {

struct InPacket {
    int64_t idx{0};
    int64_t src{0};
    double at{0};
    std::vector<uint8_t> payload;
};

class Playout {
public:
    static constexpr double kIdleSec = 5;
    static constexpr double kReportSec = 30;
    static constexpr int kFarRms = 300;
    static constexpr double kFarHoldSec = 0.35;

    using DecoderFactory = std::function<std::unique_ptr<Decoder>()>;

    Playout(Sink* sink, DecoderFactory factory, int depth = 10, int minDelayMs = 100, int maxDelayMs = 0);
    ~Playout();

    void start();
    bool push(int64_t idx, const uint8_t* payload, size_t len, int64_t src, double now);
    void mixOnce(double now, int16_t* out);
    void flush();

    bool farActive() const { return monoSeconds() < m_farUntil.load(std::memory_order_relaxed); }
    Stats& stats() { return m_stats; }
    size_t sourceCount() const { return m_sources.size(); }
    int minTarget() const { return m_minTarget; }
    int maxTarget() const { return m_maxTarget; }
    bool stopped() const { return m_stop.load(); }
    bool waitStop(double seconds);

    struct DiagPeriod {
        std::optional<double> devMin, devMax;
        double mixMax{0};
        std::vector<std::pair<int64_t, int64_t>> buffers;
    };
    DiagPeriod takeDiagPeriod();
    std::vector<std::pair<int64_t, int64_t>> buffers();

private:
    void drainRing();
    void clockLoop();
    void report();
    void publishBuffers();

    Sink* m_sink;
    DecoderFactory m_factory;
    int m_minTarget;
    int m_maxTarget;
    Stats m_stats;
    Limiter m_limiter;
    SpscRing<InPacket> m_ring;
    std::vector<std::pair<int64_t, std::unique_ptr<Stream>>> m_sources;
    std::vector<int32_t> m_acc;
    std::vector<int16_t> m_frame;
    std::atomic<double> m_farUntil{0.0};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_done{false};
    std::mutex m_stopMutex;
    std::condition_variable m_stopCv;
    std::thread m_thread;
    std::mutex m_diagLock;
    std::optional<double> m_devMin, m_devMax;
    double m_mixMax{0};
    std::vector<std::pair<int64_t, int64_t>> m_buffers;
    int m_ticks{0};
};

}
