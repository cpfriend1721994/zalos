// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QVector>

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace zcall {

constexpr int kTwccExtId = 5;

std::optional<uint16_t> readTwSeq(const QByteArray& rtp);

struct TwccFeedback {
    uint32_t senderSsrc{0};
    uint32_t mediaSsrc{0};
    uint16_t base{0};
    uint16_t count{0};
    uint32_t refUnits{0};
    uint8_t fbCount{0};
    std::vector<int> status;
    std::vector<int> deltaQ;
};

QByteArray encodeTwcc(uint32_t senderSsrc, uint32_t mediaSsrc, int64_t base, int64_t end,
                      const std::unordered_map<int64_t, int64_t>& arrivals, int fbCount);
std::optional<TwccFeedback> parseTwcc(const QByteArray& b);

class TwccRecorder {
public:
    void add(uint32_t seq, double nowMs);
    bool pending() const;
    QVector<QByteArray> build(uint32_t senderSsrc, uint32_t mediaSsrc);
    bool seenAny() const { return m_top.has_value(); }
    int sent() const { return m_sent; }

private:
    int64_t unwrap(int64_t seq) const;
    std::unordered_map<int64_t, int64_t> m_arrivals;
    std::optional<int64_t> m_top;
    std::optional<int64_t> m_next;
    int m_fbCount{0};
    int m_sent{0};
};

struct ReportBlock {
    uint32_t ssrc{0};
    int fractionLost{0};
    int64_t cumLost{0};
    uint32_t extHighSeq{0};
    uint32_t jitter{0};
    uint32_t lsr{0};
    uint32_t dlsr{0};
};

struct NtpTime {
    uint32_t msw{0};
    uint32_t lsw{0};
};
NtpTime ntpNow(double nowMs);

QByteArray buildSr(uint32_t ssrc, double nowMs, uint32_t rtpTs, uint32_t packets, uint32_t octets,
                   const QVector<ReportBlock>& blocks);
QByteArray buildRr(uint32_t ssrc, const QVector<ReportBlock>& blocks);
QByteArray buildSdes(uint32_t ssrc);
QByteArray buildPli(uint32_t senderSsrc, uint32_t mediaSsrc);
QByteArray buildNack(uint32_t senderSsrc, uint32_t mediaSsrc, const QVector<uint16_t>& seqs);

struct NackRequest {
    uint32_t ssrc{0};
    QVector<uint16_t> seqs;
};

class NackTracker {
public:
    QVector<NackRequest> note(uint32_t ssrc, uint16_t seq, double now);

private:
    struct Miss {
        double at{0};
        int tries{0};
    };
    struct Stream {
        uint16_t highest{0};
        std::vector<std::pair<uint16_t, Miss>> miss;
    };
    QVector<NackRequest> due(Stream& st, uint32_t ssrc, double now);
    std::unordered_map<uint32_t, Stream> m_streams;
};

class RxStats {
public:
    explicit RxStats(double clockHz) : m_clockHz(clockHz) {}
    void onPacket(const QByteArray& rtp, double nowMs);
    void onSr(const QByteArray& buf, double nowMs);
    std::optional<ReportBlock> block(double nowMs);

    bool started() const { return m_started; }
    uint32_t ssrc() const { return m_ssrc; }
    int64_t cycles() const { return m_cycles; }
    int64_t maxSeq() const { return m_maxSeq; }
    int64_t baseSeq() const { return m_baseSeq; }
    int64_t received() const { return m_received; }

private:
    double m_clockHz;
    uint32_t m_ssrc{0};
    bool m_started{false};
    int64_t m_baseSeq{0};
    int64_t m_maxSeq{0};
    int64_t m_cycles{0};
    int64_t m_received{0};
    int64_t m_expectedPrior{0};
    int64_t m_receivedPrior{0};
    double m_jitter{0};
    std::optional<double> m_transit;
    std::unordered_set<int64_t> m_seen;
    std::deque<int64_t> m_seenOrder;
    uint32_t m_lsr{0};
    double m_lsrAt{0};
};

struct RtcpCounters {
    int twcc{0};
    int reports{0};
    int pli{0};
    int nack{0};
};

class RtcpFeedback {
public:
    explicit RtcpFeedback(uint32_t localSsrc) : m_localSsrc(localSsrc), m_audio(16000), m_video(90000) {}
    void onRtp(bool video, const QByteArray& rtp, double nowMs);
    void onRtcp(bool video, const QByteArray& buf, double nowMs);
    QVector<QByteArray> twccPackets();
    QByteArray audioReport(double nowMs, uint32_t rtpTs, uint32_t packets, uint32_t octets);
    QByteArray videoReport(double nowMs);
    QByteArray pli();

    uint32_t localSsrc() const { return m_localSsrc; }
    uint32_t peerSsrc() const { return m_peerSsrc; }
    RtcpCounters& counters() { return m_counters; }
    RxStats& audio() { return m_audio; }
    RxStats& video() { return m_video; }
    TwccRecorder& twcc() { return m_twcc; }

private:
    uint32_t m_localSsrc;
    TwccRecorder m_twcc;
    RxStats m_audio;
    RxStats m_video;
    uint32_t m_peerSsrc{0};
    RtcpCounters m_counters;
};

int64_t jsRound(double v);

}
