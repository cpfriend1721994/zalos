// SPDX-License-Identifier: BSD-3-Clause
#include "clip_guard.h"

#include "log.h"
#include "pulse_api.h"
#include "serial_worker.h"
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

constexpr int kStepKey = 1;

std::string afterLastSlash(const std::string& s) {
    const size_t slash = s.rfind('/');
    if (slash == std::string::npos) return {};
    return s.substr(slash + 1);
}

class PactlVolume : public VolumeControl {
public:
    bool available() override { return PulseApi::available() || which("pactl").has_value(); }
    std::optional<std::string> getVolume(const std::string& source) override {
        if (PulseApi::available()) {
            const auto pct = PulseApi::getSourceVolumePercent(source);
            if (!pct) return std::nullopt;
            return "/ " + std::to_string(*pct) + "%";
        }
        const RunResult r = run({"pactl", "get-source-volume", source}, 3000, true);
        if (!r.ok()) return std::nullopt;
        return r.out;
    }
    void setVolume(const std::string& source, int64_t raw) override {
        if (PulseApi::available()) {
            if (!PulseApi::setSourceVolumeRaw(source, static_cast<uint32_t>(std::max<int64_t>(0, raw))))
                logLine("mic volume: libpulse set source volume failed");
            return;
        }
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
    : m_async(async), m_state(std::make_shared<State>()) {
    m_state->source = std::move(source);
    m_state->control = std::move(control);
    m_state->on.store(computeOn());
    if (m_async) m_steps = std::make_unique<SerialWorker>();
    const auto src = stateSource(*m_state);
    if (src) {
        const auto d = stateDb(*m_state);
        logLine("mic gain guard " + std::string(m_state->on.load() ? "on" : "off") + " for " + *src + " (now "
                + (d ? pyFloat(*d) : std::string("None")) + " dB)");
        announce();
    }
    announceMode();
}

ClipGuard::~ClipGuard() {
    m_state->dead.store(true);
}

void ClipGuard::announceMode() const {
    logLine(std::string("mic mode: ") + ((m_state->on.load() && !m_state->manual.load()) ? "auto" : "manual"));
}

bool ClipGuard::computeOn() const {
    const char* agc = std::getenv("ZCALL_MIC_AGC");
    const auto src = stateSource(*m_state);
    return src.has_value() && !src->empty() && !(agc && std::string(agc) == "0") && m_state->control->available();
}

std::optional<std::string> ClipGuard::stateSource(const State& st) {
    std::lock_guard<std::mutex> lock(st.sourceLock);
    return st.source;
}

std::optional<std::string> ClipGuard::source() const {
    return stateSource(*m_state);
}

void ClipGuard::setSource(std::optional<std::string> source) {
    {
        std::lock_guard<std::mutex> lock(m_state->sourceLock);
        m_state->source = std::move(source);
    }
    m_state->on.store(computeOn());
    m_state->manual.store(false);
    m_state->resetWindow.store(true);
}

void ClipGuard::setAutoGain(bool wantAuto) {
    const bool actualAuto = wantAuto && m_state->on.load();
    const bool prevManual = m_state->manual.exchange(!actualAuto);
    if (actualAuto && prevManual) m_state->resetWindow.store(true);
    announceMode();
}

std::optional<int> ClipGuard::statePercent(State& st) {
    const auto src = stateSource(st);
    if (!src) return std::nullopt;
    const auto out = st.control->getVolume(*src);
    if (!out) return std::nullopt;
    return parseVolumePercent(*out);
}

std::optional<double> ClipGuard::stateDb(State& st) {
    const auto src = stateSource(st);
    if (!src) return std::nullopt;
    const auto out = st.control->getVolume(*src);
    if (!out) return std::nullopt;
    return parseVolumeDb(*out);
}

void ClipGuard::announceState(State& st) {
    const auto pct = stateSource(st) && st.control->available() ? statePercent(st) : std::nullopt;
    if (pct) {
        st.announcedPct.store(*pct);
        st.announcedAt.store(monoSeconds());
        logLine(fmt("mic volume: %d%%", *pct));
    }
}

void ClipGuard::announce() {
    announceState(*m_state);
}

void ClipGuard::setPercent(int pct) {
    const auto src = stateSource(*m_state);
    if (!src || !m_state->control->available()) {
        logLine("mic volume: no PulseAudio source to set");
        return;
    }
    pct = std::max(0, std::min(100, pct));
    std::lock_guard<std::recursive_mutex> lock(m_state->volLock);
    const double now = monoSeconds();
    const int announced = m_state->announcedPct.load();
    if (announced >= 0 && now - m_state->announcedAt.load() < 0.6 && pct > announced + 2) {
        logLine(fmt("mic volume: ignore %d%% right after %d%%", pct, announced));
        return;
    }
    const auto cur = statePercent(*m_state);
    if (cur && std::abs(*cur - pct) <= 2) return;
    m_state->control->setVolume(*src, rawFromPercent(pct));
    const bool prevManual = m_state->manual.exchange(true);
    m_state->setDb = stateDb(*m_state);
    announceState(*m_state);
    if (!prevManual) announceMode();
}

void ClipGuard::feed(const int16_t* pcm, size_t n) {
    if (!pcm || n == 0) return;
    if (m_state->resetWindow.exchange(false)) {
        m_frames = 0;
        m_clipped = 0;
        m_lastStep = -1e9;
    }
    int count = 0;
    int hi = -32768, lo = 32767;
    for (size_t i = 0; i < n; ++i) {
        const int v = pcm[i];
        if (v >= kClip || v <= -kClip) ++count;
        hi = std::max(hi, v);
        lo = std::min(lo, v);
    }
    m_clipped += count;
    m_state->totalClipped.fetch_add(count);
    {
        const int peak = std::max(hi, -lo);
        if (peak > m_state->peak.load()) m_state->peak.store(peak);
    }
    if (++m_frames < kWindow) return;
    const int clipped = m_clipped;
    m_frames = m_clipped = 0;
    const double now = monoSeconds();
    if (m_state->on.load() && !m_state->manual.load() && clipped >= kMinClipped && now - m_lastStep >= kStepSec
        && !m_state->busy.exchange(true)) {
        m_lastStep = now;
        const std::shared_ptr<State> st = m_state;
        if (m_async) m_steps->post([st, clipped] { stepState(st, clipped, kStepDb); }, kStepKey);
        else stepState(st, clipped, kStepDb);
    }
}

void ClipGuard::stepState(const std::shared_ptr<State>& st, int clipped, double stepDb) {
    struct Release {
        std::atomic<bool>& busy;
        ~Release() { busy.store(false); }
    } release{st->busy};
    if (st->dead.load()) return;
    std::lock_guard<std::recursive_mutex> lock(st->volLock);
    if (st->dead.load() || st->manual.load()) return;
    const auto cur = stateDb(*st);
    if (!cur || *cur <= kFloorDb + 0.01) return;
    if (st->setDb && *cur > *st->setDb + 2.0)
        logLine(fmt("mic gain guard: volume back at %.1f dB (last set %.1f)", *cur, *st->setDb));
    const double fresh = std::max(kFloorDb, *cur - stepDb);
    const auto src = stateSource(*st);
    if (!src) return;
    st->control->setVolume(*src, rawFromDb(fresh));
    st->steps.fetch_add(1);
    const auto got = stateDb(*st);
    st->setDb = got ? *got : fresh;
    logLine(fmt("mic gain guard: %d clipped samples in 200 ms, %s %.1f -> %s dB", clipped, src->c_str(), *cur,
                got ? pyFloat(*got).c_str() : "None"));
    announceState(*st);
}

std::string ClipGuard::report() {
    const int64_t total = m_state->totalClipped.exchange(0);
    const int peak = m_state->peak.exchange(0);
    return fmt("clipped %lld, peak %.1f dBFS", static_cast<long long>(total), 20 * std::log10(std::max(peak, 1) / 32768.0));
}

}
