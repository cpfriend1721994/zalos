// SPDX-License-Identifier: BSD-3-Clause
#include "jitter.h"

#include "dsp.h"

#include <algorithm>
#include <cmath>

namespace zcall::audio {

std::array<int64_t, size_t(Stat::Count)> Stats::snapshot() const {
    std::array<int64_t, size_t(Stat::Count)> out{};
    for (size_t i = 0; i < out.size(); ++i) out[i] = m_v[i].load(std::memory_order_relaxed);
    return out;
}

const char* Stats::name(Stat s) {
    switch (s) {
    case Stat::OpusOk: return "opus_ok";
    case Stat::OpusFail: return "opus_fail";
    case Stat::Lost: return "lost";
    case Stat::Fec: return "fec";
    case Stat::Plc: return "plc";
    case Stat::Underrun: return "underrun";
    case Stat::Accelerate: return "accelerate";
    case Stat::AccelerateLoud: return "accelerate_loud";
    case Stat::Reset: return "reset";
    case Stat::Late: return "late";
    case Stat::Dup: return "dup";
    case Stat::Mixed: return "mixed";
    case Stat::Overflow: return "overflow";
    case Stat::Count: break;
    }
    return "?";
}

Stream::Stream(std::unique_ptr<Decoder> decoder, int maxTarget, Stats& stats, int minTarget)
    : m_dec(std::move(decoder)), m_stats(stats) {
    m_minTarget = std::max(2, minTarget ? minTarget : kMinTarget);
    m_maxTarget = std::max(m_minTarget, maxTarget);
    m_target = std::max(kStartTarget, m_minTarget);
    m_last = monoSeconds();
    m_carry.reserve(kMaxDecodeSamples * 2 + kFrame * 2);
    m_decodeBuf.resize(kMaxDecodeSamples);
    m_sorted.reserve(kWindow);
}

std::vector<uint8_t> Stream::takeBuffer() {
    if (m_pool.empty()) return {};
    std::vector<uint8_t> buf = std::move(m_pool.back());
    m_pool.pop_back();
    return buf;
}

void Stream::recycle(std::vector<uint8_t>&& payload) {
    if (m_pool.size() >= kPoolMax) return;
    payload.clear();
    m_pool.push_back(std::move(payload));
}

void Stream::clearPackets() {
    for (auto& entry : m_packets) recycle(std::move(entry.second));
    m_packets.clear();
}

void Stream::push(int64_t idx, const uint8_t* payload, size_t len, double now) {
    m_last = now;
    const bool far = m_hasNext && (idx - m_next > kFarIdx || m_next - idx > kFarIdx);
    if (far) {
        if (++m_farStreak < kFarStreakReset) return;
        m_farStreak = 0;
        clearPackets();
        m_carry.clear();
        m_carryPos = 0;
        m_transit.clear();
        m_hasNext = false;
        m_playing = false;
        m_expand = 0;
        m_stats.add(Stat::Reset);
    } else {
        m_farStreak = 0;
        if (m_hasNext && idx < m_next) {
            m_stats.add(Stat::Late);
            return;
        }
    }
    if (m_packets.count(idx)) {
        m_stats.add(Stat::Dup);
        return;
    }
    std::vector<uint8_t> buf = takeBuffer();
    buf.assign(payload, payload + len);
    m_packets.emplace(idx, std::move(buf));
    m_transit.push_back(now - static_cast<double>(idx) * 0.02);
    if (m_transit.size() > kWindow) m_transit.pop_front();
    if (m_transit.size() >= 20) {
        m_sorted.assign(m_transit.begin(), m_transit.end());
        const size_t at = static_cast<size_t>(static_cast<double>(m_sorted.size()) * 0.95) - 1;
        const auto nth = m_sorted.begin() + static_cast<std::ptrdiff_t>(at);
        std::nth_element(m_sorted.begin(), nth, m_sorted.end());
        const double minVal = *std::min_element(m_sorted.begin(), nth + 1);
        const double spread = m_sorted[at] - minVal;
        m_target = std::min(m_maxTarget, std::max(m_minTarget, static_cast<int>(std::ceil(spread / 0.02)) + 1));
    }
}

void Stream::compactCarry() {
    if (m_carryPos == 0) return;
    if (m_carryPos >= m_carry.size()) {
        m_carry.clear();
    } else {
        m_carry.erase(m_carry.begin(), m_carry.begin() + static_cast<std::ptrdiff_t>(m_carryPos));
    }
    m_carryPos = 0;
}

void Stream::decodeInto(const std::vector<uint8_t>* payload, bool fec) {
    const bool plc = !payload || payload->empty();
    const int size = (fec || plc) ? kFrame : kMaxDecodeSamples;
    const int n = m_dec->decode(plc ? nullptr : payload->data(), plc ? 0 : static_cast<int>(payload->size()),
                                m_decodeBuf.data(), size, fec);
    if (n <= 0) {
        m_stats.add(Stat::OpusFail);
        m_carry.insert(m_carry.end(), size_t(kFrame), int16_t(0));
        return;
    }
    m_carry.insert(m_carry.end(), m_decodeBuf.begin(), m_decodeBuf.begin() + n);
}

void Stream::take(int16_t* out) {
    const size_t avail = carrySize();
    const size_t have = std::min(avail, size_t(kFrame));
    const auto start = m_carry.begin() + static_cast<std::ptrdiff_t>(m_carryPos);
    std::copy(start, start + static_cast<std::ptrdiff_t>(have), out);
    std::fill(out + have, out + kFrame, int16_t(0));
    m_carryPos += have;
    if (m_carryPos == m_carry.size()) {
        m_carry.clear();
        m_carryPos = 0;
    } else if (m_carryPos >= 2048) {
        compactCarry();
    }
}

bool Stream::ensure(size_t samples) {
    while (carrySize() < samples && m_hasNext) {
        auto it = m_packets.find(m_next);
        if (it == m_packets.end()) break;
        std::vector<uint8_t> payload = std::move(it->second);
        m_packets.erase(it);
        decodeInto(&payload, false);
        recycle(std::move(payload));
        ++m_next;
        m_expand = 0;
        m_stats.add(Stat::OpusOk);
    }
    return carrySize() >= samples;
}

int64_t Stream::span() const {
    if (m_packets.empty() || !m_hasNext) return 0;
    return m_packets.rbegin()->first - m_next + 1;
}

int64_t Stream::discardTo(int keep) {
    if (m_packets.empty() || !m_hasNext) return 0;
    const int64_t lastIdx = m_packets.rbegin()->first;
    const int64_t extra = lastIdx - m_next + 1 - keep;
    if (extra <= 0) return 0;
    const int64_t cut = m_next + extra;
    const auto end = m_packets.lower_bound(cut);
    for (auto it = m_packets.begin(); it != end; ++it) recycle(std::move(it->second));
    m_packets.erase(m_packets.begin(), end);
    m_next = m_packets.empty() ? cut : m_packets.begin()->first;
    m_carry.clear();
    m_carryPos = 0;
    m_expand = 0;
    m_stats.add(Stat::Reset);
    return extra;
}

bool Stream::pull(int16_t* out) {
    if (!m_playing) {
        if (!hasCarry() && static_cast<int>(m_packets.size()) < m_target) return false;
        m_playing = true;
        if (!m_packets.empty()) {
            const int64_t first = m_packets.begin()->first;
            if (!m_hasNext || first > m_next) {
                m_next = first;
                m_hasNext = true;
            }
        }
    }
    ++m_sinceDrop;
    int64_t s = span();
    if (s > m_target + 10) {
        const int64_t skipped = discardTo(m_target);
        if (skipped) logLine(fmt("playout: buffer reset, skipped %lld ms", static_cast<long long>(skipped * 20)));
        s = span();
    }
    if (s > m_target + 2 && m_sinceDrop >= 10 && ensure(size_t(kFrame) * 2)) {
        if (rms(m_carry.data() + m_carryPos, size_t(kFrame)) < 800) {
            m_sinceDrop = 0;
            m_stats.add(Stat::Accelerate);
            compactCarry();
            std::vector<int16_t> rest;
            wsolaShrink(m_carry, out, rest);
            m_carry.swap(rest);
            m_carryPos = 0;
            return true;
        }
    }
    if (ensure(size_t(kFrame))) {
        take(out);
        return true;
    }
    if (!m_packets.empty() && (static_cast<int>(m_packets.size()) >= m_target || m_expand >= kMaxExpand)) {
        m_expand = 0;
        m_stats.add(Stat::Lost);
        auto nxt = m_packets.find(m_next + 1);
        ++m_next;
        if (nxt != m_packets.end()) {
            m_stats.add(Stat::Fec);
            decodeInto(&nxt->second, true);
            take(out);
            return true;
        }
        m_stats.add(Stat::Plc);
        decodeInto(nullptr, false);
        take(out);
        return true;
    }
    m_stats.add(Stat::Underrun);
    if (m_expand < kMaxExpand) {
        ++m_expand;
        m_stats.add(Stat::Plc);
        decodeInto(nullptr, false);
        take(out);
        return true;
    }
    m_expand = 0;
    m_playing = false;
    return false;
}

}
