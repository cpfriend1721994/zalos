// SPDX-License-Identifier: BSD-3-Clause
#include "playout.h"

#include "dump.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace zcall::audio {

Playout::Playout(Sink* sink, DecoderFactory factory, int depth, int minDelayMs, int maxDelayMs)
    : m_sink(sink), m_factory(std::move(factory)), m_ring(1024) {
    m_minTarget = std::max(2, static_cast<int>(std::ceil(minDelayMs / 20.0)));
    if (maxDelayMs > 0) m_maxTarget = std::max(m_minTarget, static_cast<int>(std::ceil(maxDelayMs / 20.0)));
    else m_maxTarget = std::max(m_minTarget, depth);
    for (auto& slot : m_ring.storage()) slot.payload.reserve(1500);
    m_acc.resize(kFrame);
    m_frame.resize(kFrame);
}

Playout::~Playout() { flush(); }

void Playout::start() {
    if (m_thread.joinable()) return;
    m_thread = std::thread([this] {
        clockLoop();
        {
            std::lock_guard<std::mutex> lock(m_stopMutex);
            m_done.store(true);
        }
        m_stopCv.notify_all();
    });
}

bool Playout::push(int64_t idx, const uint8_t* payload, size_t len, int64_t src, double now) {
    InPacket* slot = m_ring.beginPush();
    if (!slot) {
        m_stats.add(Stat::Overflow);
        return false;
    }
    slot->idx = idx;
    slot->src = src;
    slot->at = now;
    slot->payload.assign(payload, payload + len);
    m_ring.commitPush();
    return true;
}

void Playout::drainRing() {
    while (InPacket* p = m_ring.front()) {
        Stream* stream = nullptr;
        for (auto& [src, s] : m_sources) {
            if (src == p->src) {
                stream = s.get();
                break;
            }
        }
        if (!stream) {
            auto dec = m_factory ? m_factory() : nullptr;
            if (!dec) {
                logLine("opus_decoder_create failed");
                m_ring.pop();
                continue;
            }
            auto s = std::make_unique<Stream>(std::move(dec), m_maxTarget, m_stats, m_minTarget);
            logLine(fmt("playout: new source %lld (buffer %d..%d ms)", static_cast<long long>(p->src), s->target() * 20,
                        s->maxTarget() * 20));
            stream = s.get();
            m_sources.emplace_back(p->src, std::move(s));
        }
        stream->push(p->idx, p->payload.data(), p->payload.size(), p->at);
        m_ring.pop();
    }
}

void Playout::mixOnce(double now, int16_t* out) {
    drainRing();
    int voices = 0;
    for (size_t i = 0; i < m_sources.size();) {
        Stream& s = *m_sources[i].second;
        if (now - s.last() > kIdleSec && !s.hasPackets() && !s.hasCarry()) {
            logLine(fmt("playout: source %lld gone", static_cast<long long>(m_sources[i].first)));
            m_sources.erase(m_sources.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
        if (!s.pull(m_frame.data())) continue;
        if (voices == 0) {
            for (int k = 0; k < kFrame; ++k) m_acc[size_t(k)] = m_frame[size_t(k)];
        } else {
            for (int k = 0; k < kFrame; ++k) m_acc[size_t(k)] += m_frame[size_t(k)];
        }
        ++voices;
    }
    m_stats.add(Stat::Mixed);
    if (voices == 0) {
        std::fill(out, out + kFrame, int16_t(0));
        return;
    }
    m_limiter.apply(m_acc.data(), voices, out);
}

void Playout::publishBuffers() {
    std::vector<std::pair<int64_t, int64_t>> b;
    b.reserve(m_sources.size());
    for (const auto& [src, s] : m_sources) b.emplace_back(src, int64_t(s->target()) * 20);
    std::lock_guard<std::mutex> lock(m_diagLock);
    m_buffers.swap(b);
}

std::vector<std::pair<int64_t, int64_t>> Playout::buffers() {
    std::lock_guard<std::mutex> lock(m_diagLock);
    return m_buffers;
}

Playout::DiagPeriod Playout::takeDiagPeriod() {
    std::lock_guard<std::mutex> lock(m_diagLock);
    DiagPeriod d;
    d.devMin = m_devMin;
    d.devMax = m_devMax;
    d.mixMax = m_mixMax;
    d.buffers = m_buffers;
    m_devMin.reset();
    m_devMax.reset();
    m_mixMax = 0;
    return d;
}

void Playout::report() {
    std::vector<std::pair<std::string, int64_t>> items;
    for (Stat s : {Stat::OpusOk, Stat::Lost, Stat::Fec, Stat::Plc, Stat::Underrun, Stat::Accelerate, Stat::Reset, Stat::Late}) {
        if (const int64_t v = m_stats.get(s)) items.emplace_back(Stats::name(s), v);
    }
    publishBuffers();
    logLine("playout: " + pyDict(items) + " buffer ms " + pyIntDict(buffers()));
}

bool Playout::waitStop(double seconds) {
    std::unique_lock<std::mutex> lock(m_stopMutex);
    return m_stopCv.wait_for(lock, std::chrono::duration<double>(seconds), [this] { return m_stop.load(); });
}

void Playout::clockLoop() {
    const bool paced = m_sink->paced();
    double nxt = monoSeconds();
    double lastReport = nxt;
    std::vector<int16_t> pcm(kFrame);
    while (!m_stop.load()) {
        const double t = monoSeconds();
        mixOnce(t, pcm.data());
        Dump::instance().played(pcm.data(), kFrame);
        if (!m_sink->silent.load() && rms(pcm.data(), kFrame) >= kFarRms)
            m_farUntil.store(monoSeconds() + kFarHoldSec, std::memory_order_relaxed);
        const double mixTime = monoSeconds() - t;
        const auto latency = m_sink->latencyMs();
        {
            std::lock_guard<std::mutex> lock(m_diagLock);
            m_mixMax = std::max(m_mixMax, mixTime);
            if (latency) {
                m_devMin = m_devMin ? std::min(*m_devMin, *latency) : *latency;
                m_devMax = m_devMax ? std::max(*m_devMax, *latency) : *latency;
            }
        }
        if (++m_ticks % 25 == 0) publishBuffers();
        std::string error;
        if (!m_sink->write(pcm.data(), kFrame, &error)) {
            logLine("playout stopped: " + error);
            fault("speaker", error);
            return;
        }
        const double now = monoSeconds();
        if (now - lastReport >= kReportSec) {
            lastReport = now;
            report();
        }
        if (paced) continue;
        nxt += 0.02;
        const double delay = nxt - now;
        if (delay > 0) {
            std::unique_lock<std::mutex> lock(m_stopMutex);
            m_stopCv.wait_for(lock, std::chrono::duration<double>(delay), [this] { return m_stop.load(); });
        } else if (delay < -0.2) {
            nxt = monoSeconds();
        }
    }
}

void Playout::flush() {
    {
        std::lock_guard<std::mutex> lock(m_stopMutex);
        m_stop.store(true);
    }
    m_stopCv.notify_all();
    if (m_thread.joinable()) {
        std::unique_lock<std::mutex> lock(m_stopMutex);
        const bool done = m_stopCv.wait_for(lock, std::chrono::seconds(1), [this] { return m_done.load(); });
        lock.unlock();
        if (!done) {
            m_thread.detach();
            return;
        }
        m_thread.join();
    }
    m_sources.clear();
}

}
