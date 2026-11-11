// SPDX-License-Identifier: BSD-3-Clause
#include "dsp.h"

#include "log.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace zcall::audio {

double rms(const int16_t* pcm, size_t n) {
    if (n == 0) return 0.0;
    int64_t sum = 0;
    for (size_t i = 0; i < n; ++i) sum += int64_t(pcm[i]) * pcm[i];
    return std::sqrt(static_cast<double>(sum) / static_cast<double>(n));
}

int audioLevel(const int16_t* pcm, size_t n) {
    if (n == 0) return 127;
    const double r = rms(pcm, n);
    if (r < 1) return 127;
    const double v = std::nearbyint(-20.0 * std::log10(r / 32768.0));
    return std::min(127, std::max(0, static_cast<int>(v)));
}

int pitchLag(const int16_t* samples, size_t n) {
    const int window = 80;
    const int limit = std::min<int>(160, static_cast<int>(n) - window - 1);
    if (limit < 40) return 80;
    bool have = false;
    int64_t best = 0;
    int bestLag = 80;
    for (int lag = 40; lag <= limit; lag += 2) {
        int64_t score = 0;
        for (int i = 0; i < window; ++i) score += int64_t(samples[i]) * samples[i + lag];
        if (!have || score > best) {
            have = true;
            best = score;
            bestLag = lag;
        }
    }
    return bestLag;
}

void wsolaShrink(const std::vector<int16_t>& in, int16_t* played, std::vector<int16_t>& rest) {
    const size_t n = in.size();
    if (n < size_t(kFrame) * 2) {
        for (size_t i = 0; i < size_t(kFrame); ++i) played[i] = i < n ? in[i] : 0;
        if (n > size_t(kFrame)) rest.assign(in.begin() + kFrame, in.end());
        else rest.clear();
        return;
    }
    const int lag = pitchLag(in.data(), n);
    std::vector<int16_t> out(n - size_t(lag));
    for (int i = 0; i < lag; ++i) {
        const double w = static_cast<double>(i + 1) / static_cast<double>(lag + 1);
        const double mixed = in[size_t(i)] * (1 - w) + in[size_t(i + lag)] * w;
        const double r = std::nearbyint(mixed);
        out[size_t(i)] = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, r)));
    }
    std::copy(in.begin() + 2 * lag, in.end(), out.begin() + lag);
    std::copy(out.begin(), out.begin() + kFrame, played);
    rest.assign(out.begin() + kFrame, out.end());
}

void Limiter::apply(const int32_t* acc, int voices, int16_t* out) {
    if (voices < 2) {
        for (int i = 0; i < kFrame; ++i) out[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, acc[i])));
        m_lim += (1.0 - m_lim) * 0.1;
        return;
    }
    int32_t peak = 0;
    for (int i = 0; i < kFrame; ++i) peak = std::max(peak, std::abs(acc[i]));
    const double target = peak <= kLimit ? 1.0 : static_cast<double>(kLimit) / peak;
    if (target < m_lim) m_lim = target;
    else m_lim += (target - m_lim) * 0.1;
    const double g = m_lim;
    for (int i = 0; i < kFrame; ++i) {
        const int64_t s = static_cast<int64_t>(acc[i] * g);
        out[i] = static_cast<int16_t>(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

MicLeveler::MicLeveler(std::function<bool()> farActive, std::optional<std::string> statePath)
    : m_farActive(farActive ? std::move(farActive) : std::function<bool()>([] { return false; })),
      m_statePath(std::move(statePath)) {
    m_gainDb = load();
    m_gPrev = std::pow(10.0, m_gainDb / 20.0);
    if (m_gainDb != 0.0) logLine(fmt("mic level: starting at +%.1f dB (the last call)", m_gainDb));
}

double MicLeveler::load() {
    if (!m_statePath) return 0.0;
    QFile f(QString::fromStdString(*m_statePath));
    if (!f.exists()) return 0.0;
    if (!f.open(QIODevice::ReadOnly)) {
        logLine("mic level: ignoring " + *m_statePath + " " + f.errorString().toStdString());
        return 0.0;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    const QJsonValue v = doc.isObject() ? doc.object().value(QStringLiteral("gain_db")) : QJsonValue();
    if (err.error != QJsonParseError::NoError || !v.isDouble()) {
        logLine("mic level: ignoring " + *m_statePath + " 'gain_db'");
        return 0.0;
    }
    return std::max(0.0, std::min(kMaxGainDb, v.toDouble()));
}

void MicLeveler::save() {
    if (!m_statePath) return;
    QFile f(QString::fromStdString(*m_statePath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logLine("mic level: cannot save " + f.errorString().toStdString());
        return;
    }
    const double rounded = std::nearbyint(m_gainDb * 100.0) / 100.0;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("gain_db"), rounded}}).toJson(QJsonDocument::Compact));
}

double MicLeveler::levelDb(const int16_t* pcm, size_t n) {
    const double r = rms(pcm, n);
    return 20.0 * std::log10(std::max(r, 1.0) / 32768.0);
}

bool MicLeveler::process(const int16_t* in, int16_t* out, size_t n) {
    const double level = levelDb(in, n);
    m_levels.push_back(level);
    if (m_levels.size() > 250) m_levels.pop_front();
    ++m_n;
    if (m_levels.size() >= kLearnFrames && (m_n % 25 == 0 || m_levels.size() == kLearnFrames)) {
        std::vector<double> sorted(m_levels.begin(), m_levels.end());
        std::sort(sorted.begin(), sorted.end());
        m_noiseDb = sorted[sorted.size() / 10];
    }
    if (m_levels.size() >= kLearnFrames && level > kFloorDb && level > m_noiseDb + kSpeechOverNoiseDb) {
        if (m_farActive()) {
            ++m_held;
        } else {
            ++m_speech;
            m_speechDb.push_back(level);
            const double ceiling = std::min(kMaxGainDb, std::max(0.0, kNoiseCeilDb - m_noiseDb));
            const double want = std::min(std::max(kTargetDb - level, 0.0), ceiling);
            m_gainDb += std::max(-kDownDb, std::min(kUpDb, want - m_gainDb));
        }
    }
    if (m_n % kLogEvery == 0) {
        std::vector<double> sp = m_speechDb;
        std::sort(sp.begin(), sp.end());
        const std::string held = m_held ? fmt(", held %d%% (far end)", 100 * m_held / kLogEvery) : std::string();
        if (!sp.empty())
            logLine(fmt("mic level: noise %.0f dBFS, speech %.0f dBFS before the gain, gain +%.1f dB, speech in %d%% of frames",
                        m_noiseDb, sp[sp.size() / 2], m_gainDb, 100 * m_speech / kLogEvery) + held);
        else
            logLine(fmt("mic level: noise %.0f dBFS, no speech, gain +%.1f dB", m_noiseDb, m_gainDb) + held);
        m_speech = m_held = 0;
        m_speechDb.clear();
        save();
    }
    double g = std::pow(10.0, m_gainDb / 20.0);
    double g0 = m_gPrev;
    int peak = 0;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, std::abs(int(in[i])));
    if (peak) {
        const double lim = 32000.0 / peak;
        g = std::min(g, lim);
        g0 = std::min(g0, lim);
    }
    m_gPrev = g;
    if (std::abs(g - 1.0) < 0.01 && std::abs(g0 - 1.0) < 0.01) {
        if (out != in) std::copy(in, in + n, out);
        return false;
    }
    const double step = (g - g0) / static_cast<double>(n);
    for (size_t i = 0; i < n; ++i) {
        const int64_t x = static_cast<int64_t>(in[i] * (g0 + step * static_cast<double>(i + 1)));
        out[i] = static_cast<int16_t>(x > 32767 ? 32767 : (x < -32768 ? -32768 : x));
    }
    return true;
}

}
