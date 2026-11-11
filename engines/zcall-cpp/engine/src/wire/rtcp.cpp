// SPDX-License-Identifier: BSD-3-Clause
#include "wire/rtcp.h"

#include "wire/bytes.h"

#include <algorithm>
#include <cmath>

namespace zcall {

namespace {

constexpr double kNtpEpochOffset = 2208988800.0;
constexpr int64_t kMaxTwccPackets = 400;
constexpr int kNackMaxHole = 64;
constexpr double kNackRetryMs = 40;
constexpr int kNackMaxTries = 5;
constexpr double kNackDropMs = 400;
constexpr size_t kNackKeep = 128;

void reportBlock(QByteArray& buf, int o, const ReportBlock& blk) {
    wr32be(buf, o, blk.ssrc);
    buf[o + 4] = char(blk.fractionLost & 0xff);
    wr24be(buf, o + 5, uint32_t(blk.cumLost) & 0xffffff);
    wr32be(buf, o + 8, blk.extHighSeq);
    wr32be(buf, o + 12, blk.jitter);
    wr32be(buf, o + 16, blk.lsr);
    wr32be(buf, o + 20, blk.dlsr);
}

}

int64_t jsRound(double v) { return int64_t(std::floor(v + 0.5)); }

std::optional<uint16_t> readTwSeq(const QByteArray& rtp) {
    if (rtp.size() < 16 || (u8at(rtp, 0) & 0x10) == 0) return std::nullopt;
    int o = 12 + 4 * (u8at(rtp, 0) & 0x0f);
    if (o + 4 > rtp.size() || rd16be(rtp, o) != 0xbede) return std::nullopt;
    const int end = std::min(int(rtp.size()), o + 4 + 4 * rd16be(rtp, o + 2));
    o += 4;
    while (o < end) {
        const int b = u8at(rtp, o);
        if (b == 0) {
            ++o;
            continue;
        }
        const int id = b >> 4;
        const int len = (b & 0x0f) + 1;
        if (id == 15) return std::nullopt;
        if (id == kTwccExtId && len == 2 && o + 3 <= end) return rd16be(rtp, o + 1);
        o += 1 + len;
    }
    return std::nullopt;
}

int64_t TwccRecorder::unwrap(int64_t seq) const {
    if (!m_top) return seq;
    const int64_t top = *m_top;
    int64_t u = seq + (top - (top & 0xffff));
    if (u - top > 0x8000) u -= 0x10000;
    else if (top - u > 0x8000) u += 0x10000;
    return u;
}

void TwccRecorder::add(uint32_t seq, double nowMs) {
    const int64_t u = unwrap(seq & 0xffff);
    if (!m_next) m_next = u;
    if (u < *m_next || m_arrivals.count(u)) return;
    m_arrivals[u] = jsRound(nowMs * 4);
    if (!m_top || u > *m_top) m_top = u;
}

bool TwccRecorder::pending() const { return m_next && m_top && *m_top >= *m_next; }

QVector<QByteArray> TwccRecorder::build(uint32_t senderSsrc, uint32_t mediaSsrc) {
    QVector<QByteArray> out;
    while (pending()) {
        const int64_t base = *m_next;
        const int64_t end = std::min(*m_top, base + kMaxTwccPackets - 1);
        bool any = false;
        for (int64_t s = base; s <= end && !any; ++s) any = m_arrivals.count(s) > 0;
        if (!any) {
            m_next = end + 1;
            continue;
        }
        out.push_back(encodeTwcc(senderSsrc, mediaSsrc, base, end, m_arrivals, m_fbCount++ & 0xff));
        for (int64_t s = base; s <= end; ++s) m_arrivals.erase(s);
        m_next = end + 1;
        ++m_sent;
    }
    return out;
}

QByteArray encodeTwcc(uint32_t senderSsrc, uint32_t mediaSsrc, int64_t base, int64_t end,
                      const std::unordered_map<int64_t, int64_t>& arrivals, int fbCount) {
    const int64_t count = end - base + 1;
    int64_t firstQ = 0;
    for (int64_t s = base; s <= end; ++s) {
        const auto it = arrivals.find(s);
        if (it != arrivals.end()) {
            firstQ = it->second;
            break;
        }
    }
    const int64_t refUnits = int64_t(std::floor(double(firstQ) / 256.0));
    const int64_t refQ = refUnits * 256;
    std::vector<int> symbols;
    std::vector<int> deltas;
    symbols.reserve(size_t(count));
    int64_t prevQ = refQ;
    for (int64_t s = base; s <= end; ++s) {
        const auto it = arrivals.find(s);
        if (it == arrivals.end()) {
            symbols.push_back(0);
            continue;
        }
        const int64_t q = it->second;
        const int64_t d = q - prevQ;
        prevQ = q;
        if (d >= 0 && d <= 255) {
            symbols.push_back(1);
            deltas.push_back(int(d));
        } else {
            symbols.push_back(2);
            deltas.push_back(int(std::max<int64_t>(-32768, std::min<int64_t>(32767, d))));
        }
    }
    std::vector<uint16_t> chunks;
    for (size_t i = 0; i < symbols.size();) {
        size_t run = 1;
        while (i + run < symbols.size() && symbols[i + run] == symbols[i] && run < 0x1fff) ++run;
        if (run >= 8) {
            chunks.push_back(uint16_t((symbols[i] << 13) | int(run)));
            i += run;
            continue;
        }
        uint32_t v = 0xc000;
        for (size_t k = 0; k < 7; ++k) v |= uint32_t(i + k < symbols.size() ? symbols[i + k] : 0) << (12 - 2 * k);
        chunks.push_back(uint16_t(v));
        i += 7;
    }
    int deltaBytes = 0;
    for (int s : symbols) deltaBytes += s == 1 ? 1 : s == 2 ? 2 : 0;
    const int len = 4 + 8 + 8 + 2 * int(chunks.size()) + deltaBytes;
    const int padded = (len + 3) & ~3;
    QByteArray b(padded, '\0');
    b[0] = char(0x8f);
    b[1] = char(205);
    wr16be(b, 2, uint32_t(padded / 4 - 1));
    wr32be(b, 4, senderSsrc);
    wr32be(b, 8, mediaSsrc);
    wr16be(b, 12, uint32_t(base & 0xffff));
    wr16be(b, 14, uint32_t(count));
    wr24be(b, 16, uint32_t(refUnits & 0xffffff));
    b[19] = char(fbCount & 0xff);
    int o = 20;
    for (uint16_t c : chunks) {
        wr16be(b, o, c);
        o += 2;
    }
    size_t di = 0;
    for (int s : symbols) {
        if (s == 1) b[o++] = char(deltas[di++] & 0xff);
        else if (s == 2) {
            wr16be(b, o, uint32_t(uint16_t(int16_t(deltas[di++]))));
            o += 2;
        }
    }
    return b;
}

std::optional<TwccFeedback> parseTwcc(const QByteArray& b) {
    if (b.size() < 20 || (u8at(b, 0) & 0x1f) != 15 || u8at(b, 1) != 205) return std::nullopt;
    TwccFeedback out;
    out.count = rd16be(b, 14);
    out.senderSsrc = rd32be(b, 4);
    out.mediaSsrc = rd32be(b, 8);
    out.base = rd16be(b, 12);
    out.refUnits = rd24be(b, 16);
    out.fbCount = u8at(b, 19);
    int o = 20;
    while (int(out.status.size()) < out.count && o + 2 <= b.size()) {
        const uint16_t c = rd16be(b, o);
        o += 2;
        if ((c & 0x8000) == 0) {
            const int sym = (c >> 13) & 3;
            const int run = c & 0x1fff;
            for (int i = 0; i < run && int(out.status.size()) < out.count; ++i) out.status.push_back(sym);
        } else if (c & 0x4000) {
            for (int k = 0; k < 7 && int(out.status.size()) < out.count; ++k) out.status.push_back((c >> (12 - 2 * k)) & 3);
        } else {
            for (int k = 0; k < 14 && int(out.status.size()) < out.count; ++k) out.status.push_back((c >> (13 - k)) & 1);
        }
    }
    for (int s : out.status) {
        if (s == 1) {
            if (o >= b.size()) break;
            out.deltaQ.push_back(u8at(b, o++));
        } else if (s == 2) {
            if (o + 2 > b.size()) break;
            out.deltaQ.push_back(rd16sbe(b, o));
            o += 2;
        }
    }
    return out;
}

NtpTime ntpNow(double nowMs) {
    const double sec = std::floor(nowMs / 1000.0);
    const double frac = std::floor((std::fmod(nowMs, 1000.0) / 1000.0) * 4294967296.0);
    return {uint32_t(uint64_t(sec + kNtpEpochOffset) & 0xffffffffULL), uint32_t(uint64_t(frac) & 0xffffffffULL)};
}

QByteArray buildSr(uint32_t ssrc, double nowMs, uint32_t rtpTs, uint32_t packets, uint32_t octets,
                   const QVector<ReportBlock>& blocks) {
    QByteArray b(28 + 24 * blocks.size(), '\0');
    b[0] = char(0x80 | blocks.size());
    b[1] = char(200);
    wr16be(b, 2, uint32_t(b.size() / 4 - 1));
    wr32be(b, 4, ssrc);
    const NtpTime n = ntpNow(nowMs);
    wr32be(b, 8, n.msw);
    wr32be(b, 12, n.lsw);
    wr32be(b, 16, rtpTs);
    wr32be(b, 20, packets);
    wr32be(b, 24, octets);
    for (int i = 0; i < blocks.size(); ++i) reportBlock(b, 28 + 24 * i, blocks[i]);
    return b;
}

QByteArray buildRr(uint32_t ssrc, const QVector<ReportBlock>& blocks) {
    QByteArray b(8 + 24 * blocks.size(), '\0');
    b[0] = char(0x80 | blocks.size());
    b[1] = char(201);
    wr16be(b, 2, uint32_t(b.size() / 4 - 1));
    wr32be(b, 4, ssrc);
    for (int i = 0; i < blocks.size(); ++i) reportBlock(b, 8 + 24 * i, blocks[i]);
    return b;
}

QByteArray buildSdes(uint32_t ssrc) {
    QByteArray b(12, '\0');
    b[0] = char(0x81);
    b[1] = char(202);
    wr16be(b, 2, 2);
    wr32be(b, 4, ssrc);
    b[8] = 1;
    return b;
}

QByteArray buildPli(uint32_t senderSsrc, uint32_t mediaSsrc) {
    QByteArray b(12, '\0');
    b[0] = char(0x81);
    b[1] = char(206);
    wr16be(b, 2, 2);
    wr32be(b, 4, senderSsrc);
    wr32be(b, 8, mediaSsrc);
    return b;
}

QByteArray buildNack(uint32_t senderSsrc, uint32_t mediaSsrc, const QVector<uint16_t>& seqs) {
    QVector<std::pair<uint16_t, uint16_t>> items;
    for (int i = 0; i < seqs.size();) {
        const uint16_t pid = seqs[i];
        uint16_t blp = 0;
        int j = i + 1;
        for (; j < seqs.size(); ++j) {
            const int d = (int(seqs[j]) - int(pid)) & 0xffff;
            if (d < 1 || d > 16) break;
            blp = uint16_t(blp | (1 << (d - 1)));
        }
        items.push_back({pid, blp});
        i = j;
    }
    QByteArray b(12 + 4 * items.size(), '\0');
    b[0] = char(0x81);
    b[1] = char(205);
    wr16be(b, 2, uint32_t(b.size() / 4 - 1));
    wr32be(b, 4, senderSsrc);
    wr32be(b, 8, mediaSsrc);
    for (int i = 0; i < items.size(); ++i) {
        wr16be(b, 12 + 4 * i, items[i].first);
        wr16be(b, 14 + 4 * i, items[i].second);
    }
    return b;
}

QVector<NackRequest> NackTracker::note(uint32_t ssrc, uint16_t seq, double now) {
    auto it = m_streams.find(ssrc);
    if (it == m_streams.end()) {
        Stream st;
        st.highest = seq;
        m_streams.emplace(ssrc, std::move(st));
        return {};
    }
    Stream& st = it->second;
    const int ahead = (int(seq) - int(st.highest)) & 0xffff;
    auto eraseSeq = [&st](uint16_t s) {
        for (auto m = st.miss.begin(); m != st.miss.end(); ++m)
            if (m->first == s) {
                st.miss.erase(m);
                return;
            }
    };
    if (ahead == 0) return due(st, ssrc, now);
    if (ahead >= 0x8000) {
        eraseSeq(seq);
        return due(st, ssrc, now);
    }
    if (ahead > kNackMaxHole) {
        st.highest = seq;
        st.miss.clear();
        return {};
    }
    for (int d = 1; d < ahead; ++d) {
        const uint16_t missing = uint16_t((st.highest + d) & 0xffff);
        bool has = false;
        for (const auto& m : st.miss)
            if (m.first == missing) {
                has = true;
                break;
            }
        if (!has) st.miss.push_back({missing, Miss{now, 0}});
    }
    st.highest = seq;
    eraseSeq(seq);
    while (st.miss.size() > kNackKeep) st.miss.erase(st.miss.begin());
    return due(st, ssrc, now);
}

QVector<NackRequest> NackTracker::due(Stream& st, uint32_t ssrc, double now) {
    QVector<uint16_t> seqs;
    for (auto m = st.miss.begin(); m != st.miss.end();) {
        Miss& info = m->second;
        if (now - info.at > kNackDropMs) {
            m = st.miss.erase(m);
            continue;
        }
        if (info.tries >= kNackMaxTries || now < info.at + info.tries * kNackRetryMs) {
            ++m;
            continue;
        }
        ++info.tries;
        seqs.push_back(m->first);
        ++m;
    }
    if (seqs.isEmpty()) return {};
    const uint16_t high = st.highest;
    std::stable_sort(seqs.begin(), seqs.end(), [high](uint16_t a, uint16_t b) {
        return ((int(high) - int(a)) & 0xffff) > ((int(high) - int(b)) & 0xffff);
    });
    return {NackRequest{ssrc, seqs}};
}

void RxStats::onPacket(const QByteArray& rtp, double nowMs) {
    const int64_t seq = rd16be(rtp, 2);
    const uint32_t ts = rd32be(rtp, 4);
    if (!m_started) {
        m_started = true;
        m_ssrc = rd32be(rtp, 8);
        m_baseSeq = seq;
        m_maxSeq = seq;
    } else {
        const int64_t d = (seq - m_maxSeq) & 0xffff;
        if (d != 0 && d < 0x8000) {
            if (seq < m_maxSeq) m_cycles += 0x10000;
            m_maxSeq = seq;
        }
    }
    const int64_t ext = m_cycles + seq;
    if (m_seen.count(ext)) return;
    m_seen.insert(ext);
    m_seenOrder.push_back(ext);
    if (m_seen.size() > 512) {
        m_seen.erase(m_seenOrder.front());
        m_seenOrder.pop_front();
    }
    ++m_received;
    const double arrival = (nowMs * m_clockHz) / 1000.0;
    const double transit = arrival - double(ts);
    if (m_transit) {
        const double dd = std::fabs(transit - *m_transit);
        m_jitter += (dd - m_jitter) / 16.0;
    }
    m_transit = transit;
}

void RxStats::onSr(const QByteArray& buf, double nowMs) {
    if (buf.size() < 28) return;
    m_lsr = ((rd32be(buf, 8) & 0xffff) << 16) | (rd32be(buf, 12) >> 16);
    m_lsrAt = nowMs;
}

std::optional<ReportBlock> RxStats::block(double nowMs) {
    if (!m_started) return std::nullopt;
    const int64_t extHigh = m_cycles + m_maxSeq;
    const int64_t expected = extHigh - m_baseSeq + 1;
    const int64_t lost = expected - m_received;
    const int64_t expInt = expected - m_expectedPrior;
    const int64_t recInt = m_received - m_receivedPrior;
    m_expectedPrior = expected;
    m_receivedPrior = m_received;
    const int64_t lostInt = expInt - recInt;
    const int fraction = expInt <= 0 || lostInt <= 0 ? 0 : int(std::min<int64_t>(255, (lostInt * 256) / expInt));
    ReportBlock b;
    b.ssrc = m_ssrc;
    b.fractionLost = fraction;
    b.cumLost = std::max<int64_t>(-0x800000, std::min<int64_t>(0x7fffff, lost));
    b.extHighSeq = uint32_t(extHigh & 0xffffffff);
    b.jitter = uint32_t(jsRound(m_jitter));
    b.lsr = m_lsr;
    b.dlsr = m_lsrAt != 0 ? uint32_t(uint64_t(jsRound(((nowMs - m_lsrAt) * 65536.0) / 1000.0)) & 0xffffffffULL) : 0;
    return b;
}

void RtcpFeedback::onRtp(bool video, const QByteArray& rtp, double nowMs) {
    if (rtp.size() < 12) return;
    if (!m_peerSsrc) m_peerSsrc = rd32be(rtp, 8);
    if (const auto tw = readTwSeq(rtp)) m_twcc.add(*tw, nowMs);
    (video ? m_video : m_audio).onPacket(rtp, nowMs);
}

void RtcpFeedback::onRtcp(bool video, const QByteArray& buf, double nowMs) {
    RxStats& st = video ? m_video : m_audio;
    for (int o = 0; o + 4 <= buf.size();) {
        if ((u8at(buf, o) >> 6) != 2) return;
        const int len = (rd16be(buf, o + 2) + 1) * 4;
        if (u8at(buf, o + 1) == 200 && o + len <= buf.size()) st.onSr(buf.mid(o, len), nowMs);
        o += len;
    }
}

QVector<QByteArray> RtcpFeedback::twccPackets() {
    if (!m_peerSsrc) return {};
    const auto pk = m_twcc.build(m_localSsrc, m_peerSsrc);
    m_counters.twcc += pk.size();
    return pk;
}

QByteArray RtcpFeedback::audioReport(double nowMs, uint32_t rtpTs, uint32_t packets, uint32_t octets) {
    const auto blk = m_audio.block(nowMs);
    ++m_counters.reports;
    QVector<ReportBlock> blocks;
    if (blk) blocks.push_back(*blk);
    return buildSr(m_localSsrc, nowMs, rtpTs, packets, octets, blocks) + buildSdes(m_localSsrc);
}

QByteArray RtcpFeedback::videoReport(double nowMs) {
    const auto blk = m_video.block(nowMs);
    if (!blk) return {};
    ++m_counters.reports;
    return buildRr(m_localSsrc, {*blk}) + buildSdes(m_localSsrc);
}

QByteArray RtcpFeedback::pli() {
    if (!m_video.started()) return {};
    ++m_counters.pli;
    return buildPli(m_localSsrc, m_video.ssrc());
}

}
