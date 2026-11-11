// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "log.h"
#include "opus_api.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zcall::audio {

enum class Stat : int {
    OpusOk,
    OpusFail,
    Lost,
    Fec,
    Plc,
    Underrun,
    Accelerate,
    AccelerateLoud,
    Reset,
    Late,
    Dup,
    Mixed,
    Overflow,
    Count
};

class Stats {
public:
    void add(Stat s, int64_t n = 1) { m_v[size_t(s)].fetch_add(n, std::memory_order_relaxed); }
    int64_t get(Stat s) const { return m_v[size_t(s)].load(std::memory_order_relaxed); }
    std::array<int64_t, size_t(Stat::Count)> snapshot() const;
    static const char* name(Stat s);

private:
    std::array<std::atomic<int64_t>, size_t(Stat::Count)> m_v{};
};

class Stream {
public:
    static constexpr int kMinTarget = 5;
    static constexpr int kStartTarget = 5;
    static constexpr int kMaxExpand = 5;
    static constexpr size_t kWindow = 250;

    Stream(std::unique_ptr<Decoder> decoder, int maxTarget, Stats& stats, int minTarget = 0);

    void push(int64_t idx, const uint8_t* payload, size_t len, double now);
    bool pull(int16_t* out);

    int target() const { return m_target; }
    int maxTarget() const { return m_maxTarget; }
    int minTarget() const { return m_minTarget; }
    double last() const { return m_last; }
    bool hasPackets() const { return !m_packets.empty(); }
    bool hasCarry() const { return !m_carry.empty(); }
    bool playing() const { return m_playing; }
    size_t buffered() const { return m_packets.size(); }

private:
    void decodeInto(const std::vector<uint8_t>* payload, bool fec);
    void take(int16_t* out);
    bool ensure(size_t samples);
    int64_t discardTo(int keep);
    int64_t span() const;

    std::unique_ptr<Decoder> m_dec;
    Stats& m_stats;
    int m_minTarget;
    int m_maxTarget;
    int m_target;
    std::map<int64_t, std::vector<uint8_t>> m_packets;
    bool m_hasNext{false};
    int64_t m_next{0};
    bool m_playing{false};
    int m_expand{0};
    std::vector<int16_t> m_carry;
    std::vector<int16_t> m_decodeBuf;
    std::deque<double> m_transit;
    std::vector<double> m_sorted;
    double m_last;
    int m_sinceDrop{0};
};

}
