// SPDX-License-Identifier: BSD-3-Clause
#include "wire/video.h"

#include "wire/bytes.h"

#include <algorithm>
#include <stdexcept>

namespace zcall {

namespace {

constexpr int kReorderFrames = 3;
constexpr double kReorderMs = 200;
constexpr size_t kMaxPending = 30;
const QByteArray kStartCode("\x00\x00\x00\x01", 4);

bool isSvcNal(int t) { return t == 14 || t == 15 || t == 20; }

int nalType(const QByteArray& u) { return u8at(u, 0) & 0x1f; }

}

VideoAssembler::VideoAssembler(std::function<void(const VideoFrame&)> onFrame, std::function<double()> now, bool group,
                               std::function<void()> onGap)
    : m_onFrame(std::move(onFrame)), m_now(std::move(now)), m_group(group), m_onGap(std::move(onGap)) {}

std::vector<std::pair<uint32_t, VideoAssembler::Pending>>::iterator VideoAssembler::findPending(uint32_t ts) {
    return std::find_if(m_pending.begin(), m_pending.end(), [ts](const auto& e) { return e.first == ts; });
}

void VideoAssembler::push(const VideoPacket& pkt) {
    if ((!m_group && pkt.pt != 97 && pkt.pt != 98) || pkt.payload.isEmpty()) return;
    const int64_t idx = int64_t(pkt.roc) * 0x10000 + pkt.seq;
    if (idx <= m_lastIdx) return;
    auto it = findPending(pkt.ts);
    if (it == m_pending.end()) {
        m_pending.push_back({pkt.ts, Pending{}});
        if (m_pending.size() > kMaxPending) {
            m_pending.erase(m_pending.begin());
            ++stats.dropped;
        }
        it = findPending(pkt.ts);
        if (it == m_pending.end()) {
            flush();
            return;
        }
    }
    Pending& f = it->second;
    f.parts[idx] = pkt.payload;
    if (pkt.pt == kKeyPt) f.key = true;
    if (pkt.marker) f.end = idx;
    if (f.end) complete(pkt.ts, f);
    flush();
}

void VideoAssembler::complete(uint32_t ts, Pending& f) {
    std::vector<int64_t> idxs;
    for (const auto& p : f.parts) idxs.push_back(p.first);
    const QByteArray& first = f.parts.at(idxs.front());
    if (isFragment(first) && u8at(first, 1) != 0x80) return;
    for (size_t i = 1; i < idxs.size(); ++i)
        if (idxs[i] != idxs[i - 1] + 1) return;
    if (idxs.back() != *f.end) return;
    QByteArray data;
    for (int64_t i : idxs) {
        const QByteArray& p = f.parts.at(i);
        data += isFragment(p) ? p.mid(2) : p;
    }
    bool key = f.key;
    const int64_t last = *f.end;
    m_pending.erase(findPending(ts));
    if (m_group) {
        const QVector<QByteArray> units = nalUnits(data);
        for (const QByteArray& u : units) {
            const int t = nalType(u);
            nalBytes[t] += u.size();
            if (t == 7 || t == 15) {
                if (const auto d = spsSize(u)) {
                    const QString s = QString::number(d->width) + QLatin1Char('x') + QString::number(d->height);
                    (t == 7 ? spsSize7 : spsSize15) = s;
                }
            }
        }
        key = std::any_of(units.begin(), units.end(), [](const QByteArray& u) { return nalType(u) == 7 || nalType(u) == 5; });
        QVector<QByteArray> kept;
        for (const QByteArray& u : units)
            if (!isSvcNal(nalType(u))) kept.push_back(u);
        if (kept.isEmpty()) {
            ++stats.dropped;
            return;
        }
        if (kept.size() != units.size()) {
            data.clear();
            for (const QByteArray& u : kept) data += kStartCode + u;
        }
        for (const QByteArray& u : kept) {
            const int t = nalType(u);
            if (t == 7) m_sps = u;
            else if (t == 8) m_pps = u;
        }
        if (key && !m_sps.isEmpty()) {
            const bool hasSps = std::any_of(kept.begin(), kept.end(), [](const QByteArray& u) { return nalType(u) == 7; });
            const bool hasPps = std::any_of(kept.begin(), kept.end(), [](const QByteArray& u) { return nalType(u) == 8; });
            if (!hasSps || (!hasPps && !m_pps.isEmpty())) {
                QByteArray pre;
                if (!hasSps) pre += kStartCode + m_sps;
                if (!hasPps && !m_pps.isEmpty()) pre += kStartCode + m_pps;
                data = pre + data;
            }
        }
    }
    m_ready[idxs.front()] = Ready{key, data, ts, last, m_now()};
}

void VideoAssembler::flush() {
    for (;;) {
        if (m_ready.empty()) return;
        const auto next = m_ready.find(m_lastIdx + 1);
        if (next != m_ready.end() && m_lastIdx >= 0) {
            emitFrame(next->first, next->second);
            continue;
        }
        const auto oldestIt = m_ready.begin();
        const int64_t firstIdx = oldestIt->first;
        const Ready oldest = oldestIt->second;
        const double waited = m_now() - oldest.since;
        if (m_lastIdx >= 0 && int(m_ready.size()) <= kReorderFrames && waited < kReorderMs) return;
        if (!m_started && !oldest.key) {
            m_ready.erase(oldestIt);
            ++stats.dropped;
            continue;
        }
        if (m_lastIdx >= 0) {
            ++stats.gaps;
            if (m_onGap) m_onGap();
            if (m_group) m_needKey = true;
        }
        emitFrame(firstIdx, oldest);
    }
}

void VideoAssembler::emitFrame(int64_t firstIdx, Ready fr) {
    if (m_group && m_needKey && !fr.key) {
        m_ready.erase(firstIdx);
        m_lastIdx = fr.last;
        ++stats.dropped;
        return;
    }
    m_ready.erase(firstIdx);
    m_lastIdx = fr.last;
    if (fr.key) m_needKey = false;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (!it->second.parts.empty() && it->second.parts.rbegin()->first <= m_lastIdx) {
            it = m_pending.erase(it);
            ++stats.dropped;
        } else {
            ++it;
        }
    }
    m_started = m_started || fr.key;
    ++stats.frames;
    if (fr.key) ++stats.keys;
    m_onFrame(VideoFrame{fr.key, fr.data, fr.ts});
}

QVector<QByteArray> nalUnits(const QByteArray& annexB) {
    QVector<int> starts;
    for (int i = 0; i + 2 < annexB.size(); ++i) {
        if (annexB[i] == 0 && annexB[i + 1] == 0 && annexB[i + 2] == 1) {
            starts.push_back(i + 3);
            i += 2;
        }
    }
    QVector<QByteArray> out;
    for (int k = 0; k < starts.size(); ++k) {
        const int st = starts[k];
        int end = k + 1 < starts.size() ? starts[k + 1] - 3 : int(annexB.size());
        while (end > st && annexB[end - 1] == 0) --end;
        if (end > st) out.push_back(annexB.mid(st, end - st));
    }
    return out;
}

std::optional<PictureSize> spsSize(const QByteArray& nal) {
    std::vector<uint8_t> b;
    for (int i = 1; i < nal.size(); ++i) {
        if (i >= 3 && u8at(nal, i) == 3 && u8at(nal, i - 1) == 0 && u8at(nal, i - 2) == 0) continue;
        b.push_back(u8at(nal, i));
    }
    size_t pos = 0;
    auto bit = [&]() -> int64_t {
        if (pos >= b.size() * 8) throw std::runtime_error("eof");
        const int v = (b[pos >> 3] >> (7 - (pos & 7))) & 1;
        ++pos;
        return v;
    };
    auto bits = [&](int n) -> int64_t {
        int64_t v = 0;
        for (int i = 0; i < n; ++i) v = v * 2 + bit();
        return v;
    };
    auto ue = [&]() -> int64_t {
        int z = 0;
        while (!bit()) {
            if (++z > 31) throw std::runtime_error("ue");
        }
        return bits(z) + ((int64_t(1) << z) - 1);
    };
    auto se = [&]() -> int64_t {
        const int64_t k = ue();
        return (k & 1) ? (k + 1) / 2 : -(k / 2);
    };
    try {
        const int64_t profile = bits(8);
        bits(16);
        ue();
        int64_t chroma = 1;
        static const int highProfiles[] = {100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135};
        if (std::find(std::begin(highProfiles), std::end(highProfiles), profile) != std::end(highProfiles)) {
            chroma = ue();
            if (chroma == 3) bit();
            ue();
            ue();
            bit();
            if (bit()) {
                for (int i = 0; i < (chroma == 3 ? 12 : 8); ++i) {
                    if (!bit()) continue;
                    int64_t last = 8;
                    int64_t next = 8;
                    for (int j = 0; j < (i < 6 ? 16 : 64); ++j) {
                        if (next) next = (last + se() + 256) % 256;
                        last = next ? next : last;
                    }
                }
            }
        }
        ue();
        const int64_t poc = ue();
        if (poc == 0) ue();
        else if (poc == 1) {
            bit();
            se();
            se();
            const int64_t n = ue();
            for (int64_t i = 0; i < n; ++i) se();
        }
        ue();
        bit();
        const int64_t wMbs = ue() + 1;
        const int64_t hMap = ue() + 1;
        const int64_t frameMbsOnly = bit();
        if (!frameMbsOnly) bit();
        bit();
        int64_t cl = 0, cr = 0, ct = 0, cb = 0;
        if (bit()) {
            cl = ue();
            cr = ue();
            ct = ue();
            cb = ue();
        }
        const int64_t cx = chroma == 0 ? 1 : 2;
        const int64_t cy = (chroma == 1 ? 2 : 1) * (2 - frameMbsOnly);
        return PictureSize{wMbs * 16 - cx * (cl + cr), (2 - frameMbsOnly) * hMap * 16 - cy * (ct + cb)};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool isFragment(const QByteArray& p) { return p.size() >= 2 && u8at(p, 0) == 0x1c && (u8at(p, 1) & 0x3f) == 0; }

QString avcCodecString(const QByteArray& annexB) {
    for (int i = 0; i + 7 < annexB.size(); ++i) {
        if (annexB[i] == 0 && annexB[i + 1] == 0 && annexB[i + 2] == 1 && (u8at(annexB, i + 3) & 0x1f) == 7) {
            return QStringLiteral("avc1.") + QString::fromLatin1(annexB.mid(i + 4, 3).toHex());
        }
    }
    return QString();
}

QVector<QByteArray> packetizeFrame(const QByteArray& annexB, int maxChunk) {
    if (annexB.size() <= maxChunk) return {annexB};
    QVector<QByteArray> out;
    for (int off = 0; off < annexB.size(); off += maxChunk) {
        const int end = std::min(off + maxChunk, int(annexB.size()));
        const char flag = char(off == 0 ? 0x80 : end == annexB.size() ? 0x40 : 0x00);
        QByteArray chunk;
        chunk.reserve(2 + end - off);
        chunk.append(char(0x1c));
        chunk.append(flag);
        chunk.append(annexB.constData() + off, end - off);
        out.push_back(chunk);
    }
    return out;
}

}
