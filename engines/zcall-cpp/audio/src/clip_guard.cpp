// SPDX-License-Identifier: BSD-3-Clause
#include "clip_guard.h"

#include "log.h"
#include "subprocess.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace zcall::audio {

namespace {

std::string strip(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

std::string afterLastSlash(const std::string& s) {
    const size_t slash = s.rfind('/');
    if (slash == std::string::npos) return {};
    return s.substr(slash + 1);
}

class PactlVolume : public VolumeControl {
public:
    bool available() override { return which("pactl").has_value(); }
    std::optional<std::string> getVolume(const std::string& source) override {
        const RunResult r = run({"pactl", "get-source-volume", source}, 3000, true);
        if (!r.ok()) return std::nullopt;
        return r.out;
    }
    void setVolume(const std::string& source, int64_t raw) override {
        const RunResult r = run({"pactl", "set-source-volume", source, std::to_string(raw)}, 3000);
        if (!r.started) logLine("mic volume: " + r.error);
    }
};

}

std::optional<int> parseVolumePercent(const std::string& out) {
    const size_t pct = out.find('%');
    const std::string head = pct == std::string::npos ? out : out.substr(0, pct);
    const size_t slash = head.rfind('/');
    if (slash == std::string::npos) return std::nullopt;
    const std::string v = strip(head.substr(slash + 1));
    if (v.empty()) return std::nullopt;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (!end || *end) return std::nullopt;
    return static_cast<int>(n);
}

std::optional<double> parseVolumeDb(const std::string& out) {
    const size_t db = out.find(" dB");
    const std::string head = db == std::string::npos ? out : out.substr(0, db);
    if (head.rfind('/') == std::string::npos) return std::nullopt;
    const std::string v = strip(afterLastSlash(head));
    if (v.empty()) return std::nullopt;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (!end || *end) return std::nullopt;
    return d;
}

int64_t rawFromDb(double db) { return static_cast<int64_t>(std::nearbyint(65536.0 * std::pow(10.0, db / 60.0))); }

int64_t rawFromPercent(int pct) { return static_cast<int64_t>(std::nearbyint(65536.0 * pct / 100.0)); }

std::string pyFloat(double v) {
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    if (std::isnan(v)) return "nan";
    char buf[64];
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    std::string s = buf;
    if (s.find_first_of(".einn") == std::string::npos) s += ".0";
    return s;
}

std::shared_ptr<VolumeControl> pactlVolumeControl() { return std::make_shared<PactlVolume>(); }

ClipGuard::ClipGuard(std::optional<std::string> source, std::shared_ptr<VolumeControl> control, bool async)
    : m_source(std::move(source)), m_control(std::move(control)), m_async(async) {
    m_on = computeOn();
    if (m_source) {
        const auto d = db();
        logLine("mic gain guard " + std::string(m_on ? "on" : "off") + " for " + *m_source + " (now "
                + (d ? pyFloat(*d) : std::string("None")) + " dB)");
        announce();
    }
}

bool ClipGuard::computeOn() const {
    const char* agc = std::getenv("ZCALL_MIC_AGC");
    return m_source.has_value() && !m_source->empty() && !(agc && std::string(agc) == "0") && m_control->available();
}

std::optional<std::string> ClipGuard::source() const {
    std::lock_guard<std::mutex> lock(m_sourceLock);
    return m_source;
}

void ClipGuard::setSource(std::optional<std::string> source) {
    {
        std::lock_guard<std::mutex> lock(m_sourceLock);
        m_source = std::move(source);
    }
    m_on = computeOn();
}

std::optional<int> ClipGuard::percent() {
    const auto src = source();
    if (!src) return std::nullopt;
    const auto out = m_control->getVolume(*src);
    if (!out) return std::nullopt;
    return parseVolumePercent(*out);
}

std::optional<double> ClipGuard::db() {
    const auto src = source();
    if (!src) return std::nullopt;
    const auto out = m_control->getVolume(*src);
    if (!out) return std::nullopt;
    return parseVolumeDb(*out);
}

void ClipGuard::announce() {
    const auto pct = source() && m_control->available() ? percent() : std::nullopt;
    if (pct) {
        m_announcedPct = pct;
        m_announcedAt = monoSeconds();
        logLine(fmt("mic volume: %d%%", *pct));
    }
}

void ClipGuard::setPercent(int pct) {
    const auto src = source();
    if (!src || !m_control->available()) {
        logLine("mic volume: no PulseAudio source to set");
        return;
    }
    pct = std::max(0, std::min(100, pct));
    std::lock_guard<std::recursive_mutex> lock(m_volLock);
    const double now = monoSeconds();
    if (m_announcedPct && now - m_announcedAt < 0.6 && pct > *m_announcedPct + 2) {
        logLine(fmt("mic volume: ignore %d%% right after %d%%", pct, *m_announcedPct));
        return;
    }
    const auto cur = percent();
    if (cur && std::abs(*cur - pct) <= 2) return;
    m_control->setVolume(*src, rawFromPercent(pct));
    m_manual.store(true);
    m_setDb = db();
    announce();
}

void ClipGuard::feed(const int16_t* pcm, size_t n) {
    int count = 0;
    int hi = -32768, lo = 32767;
    for (size_t i = 0; i < n; ++i) {
        const int v = pcm[i];
        if (v >= kClip || v <= -kClip) ++count;
        hi = std::max(hi, v);
        lo = std::min(lo, v);
    }
    m_clipped += count;
    m_totalClipped.fetch_add(count);
    if (n) {
        const int peak = std::max(hi, -lo);
        if (peak > m_peak.load()) m_peak.store(peak);
    }
    if (++m_frames < kWindow) return;
    const int clipped = m_clipped;
    m_frames = m_clipped = 0;
    const double now = monoSeconds();
    if (m_on && !m_manual.load() && !m_busy.load() && clipped >= kMinClipped && now - m_lastStep >= kStepSec) {
        m_lastStep = now;
        m_busy.store(true);
        if (m_async) std::thread([this, clipped] { step(clipped, kStepDb); }).detach();
        else step(clipped, kStepDb);
    }
}

void ClipGuard::step(int clipped, double stepDb) {
    struct Release {
        std::atomic<bool>& busy;
        ~Release() { busy.store(false); }
    } release{m_busy};
    std::lock_guard<std::recursive_mutex> lock(m_volLock);
    if (m_manual.load()) return;
    const auto cur = db();
    if (!cur || *cur <= kFloorDb + 0.01) return;
    if (m_setDb && *cur > *m_setDb + 2.0)
        logLine(fmt("mic gain guard: volume back at %.1f dB (last set %.1f)", *cur, *m_setDb));
    const double fresh = std::max(kFloorDb, *cur - stepDb);
    const auto src = source();
    if (!src) return;
    m_control->setVolume(*src, rawFromDb(fresh));
    m_steps.fetch_add(1);
    const auto got = db();
    m_setDb = got ? *got : fresh;
    logLine(fmt("mic gain guard: %d clipped samples in 200 ms, %s %.1f -> %s dB", clipped, src->c_str(), *cur,
                got ? pyFloat(*got).c_str() : "None"));
    announce();
}

std::string ClipGuard::report() {
    const int64_t total = m_totalClipped.exchange(0);
    const int peak = m_peak.exchange(0);
    return fmt("clipped %lld, peak %.1f dBFS", static_cast<long long>(total), 20 * std::log10(std::max(peak, 1) / 32768.0));
}

}
