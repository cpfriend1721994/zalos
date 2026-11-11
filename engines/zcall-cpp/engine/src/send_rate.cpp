// SPDX-License-Identifier: BSD-3-Clause
#include "send_rate.h"

#include "js.h"
#include "wire/bytes.h"
#include "wire/rtcp.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace zcall {

namespace {

constexpr double kStartKbps = 300;
constexpr double kMinKbps = 100;
constexpr double kSafetyKeyMs = 10000;
constexpr double kDownGapMs = 2000;
constexpr int kUpHoldS = 4;
constexpr double kBackoffMinMs = 10000;
constexpr double kBackoffMaxMs = 60000;
constexpr int kFeedbackTimeoutS = 2;
constexpr double kQueueHoldMs = 40;
constexpr double kQueueDownMs = 150;
constexpr double kDownlinkHold = 0.02;
constexpr double kDownlinkDown = 0.10;
constexpr double kBaseWindowS = 15;
constexpr int64_t kMinTwccPackets = 10;
constexpr size_t kSentKeep = 4000;
constexpr double kLogMs = 5000;

std::optional<std::pair<int, int>> sizeFor(double res) {
    if (res == 240) return std::make_pair(426, 240);
    if (res == 360) return std::make_pair(640, 360);
    if (res == 480) return std::make_pair(848, 480);
    if (res == 720) return std::make_pair(1280, 720);
    return std::nullopt;
}

double num(const QJsonValue& v, double d) {
    const double n = js::number(v);
    return std::isfinite(n) && n > 0 ? n : d;
}

struct DefaultRung {
    int id;
    int res;
    int fps;
    int bitrate;
};
const DefaultRung kDefaultLadder[] = {{1, 240, 8, 30},   {2, 240, 10, 100}, {3, 360, 15, 300}, {4, 360, 20, 500},
                                      {5, 360, 24, 700}, {6, 480, 24, 900}, {7, 720, 24, 1100}};

QString fmt(double d) { return js::numberString(d); }

}

QJsonObject SendProfile::toJson() const {
    QJsonObject o;
    o.insert(QStringLiteral("width"), width);
    o.insert(QStringLiteral("height"), height);
    o.insert(QStringLiteral("fps"), fps);
    o.insert(QStringLiteral("bitrate"), bitrate);
    o.insert(QStringLiteral("keyMs"), keyMs);
    o.insert(QStringLiteral("captureBig"), captureBig);
    o.insert(QStringLiteral("rung"), rung);
    return o;
}

SendRate::SendRate(const QJsonObject& c, Options opts)
    : m_localSsrc(opts.localSsrc),
      m_log(opts.log ? std::move(opts.log) : [](const QString&) {}),
      m_onProfile(opts.onProfile ? std::move(opts.onProfile) : [](const SendProfile&) {}),
      m_now(std::move(opts.now)) {
    if (opts.layers && !opts.layers->isEmpty()) {
        for (int i = 0; i < opts.layers->size(); ++i) {
            const VideoRow& p = opts.layers->at(i);
            Rung r;
            r.id = i;
            r.width = p.width;
            r.height = p.height;
            r.fps = p.fps > 0 ? p.fps : 15;
            r.kbps = p.kbps;
            if (r.width > 0 && r.height > 0 && r.kbps > 0) m_ladder.push_back(r);
        }
        m_level = std::min(std::max(0, opts.startIndex), int(m_ladder.size()) - 1);
        m_ceiling = m_level;
        m_armTimeout = true;
    } else {
        const double maxW = num(c.value(QStringLiteral("maxWidth")), 1280);
        const double maxH = num(c.value(QStringLiteral("maxHeight")), 720);
        const double maxFps = num(c.value(QStringLiteral("codecMaxFps")), 24);
        const double maxKbps = num(c.value(QStringLiteral("maxBitrate")), 1500);
        const QJsonValue bw = c.value(QStringLiteral("bwProfiles"));
        QVector<Rung> src;
        if (bw.isArray() && !bw.toArray().isEmpty()) {
            for (const QJsonValue& pv : bw.toArray()) {
                const QJsonObject p = pv.toObject();
                Rung r;
                r.id = p.value(QStringLiteral("id"));
                r.res = js::number(p.value(QStringLiteral("res")));
                r.fps = std::min(num(p.value(QStringLiteral("fps")), 15), maxFps);
                r.kbps = js::number(p.value(QStringLiteral("bitrate")));
                src.push_back(r);
            }
        } else {
            for (const DefaultRung& d : kDefaultLadder) {
                Rung r;
                r.id = d.id;
                r.res = d.res;
                r.fps = std::min(double(d.fps), maxFps);
                r.kbps = d.bitrate;
                src.push_back(r);
            }
        }
        for (const Rung& p : src) {
            const auto sz = sizeFor(p.res);
            if (sz && p.kbps >= kMinKbps && p.kbps <= maxKbps && sz->first <= maxW && sz->second <= maxH)
                m_ladder.push_back(p);
        }
        std::stable_sort(m_ladder.begin(), m_ladder.end(), [](const Rung& a, const Rung& b) { return a.kbps < b.kbps; });
        if (m_ladder.isEmpty()) {
            for (const DefaultRung& d : kDefaultLadder) {
                if (d.bitrate < kMinKbps) continue;
                Rung r;
                r.id = d.id;
                r.res = d.res;
                r.fps = d.fps;
                r.kbps = d.bitrate;
                m_ladder.push_back(r);
            }
        }
        const double startKbps = num(c.value(QStringLiteral("startupBitrate")), kStartKbps);
        int start = 0;
        for (int i = 0; i < m_ladder.size(); ++i)
            if (m_ladder[i].kbps <= startKbps) start = i;
        const QString forced = qEnvironmentVariable("ZCALL_VIDEO_RUNG");
        if (!forced.isEmpty()) {
            for (int i = 0; i < m_ladder.size(); ++i)
                if (js::string(m_ladder[i].id) == forced) {
                    start = i;
                    break;
                }
        }
        m_level = start;
        m_ceiling = int(m_ladder.size()) - 1;
        m_armTimeout = false;
    }
    m_fixed = qEnvironmentVariable("ZCALL_VIDEO_ADAPT") == QLatin1String("0");
    m_highLoss = num(c.value(QStringLiteral("bweSendSideHighLoss")), 10) / 100;
    m_lowLoss = num(c.value(QStringLiteral("bweSendSideLowLoss")), 2) / 100;
    m_keyMs = num(c.value(QStringLiteral("keyFrameIntervalMs")), kSafetyKeyMs);
    m_captureBig = std::any_of(m_ladder.begin(), m_ladder.end(), [this](const Rung& p) { return dims(p).first > 640; });
    QStringList rungs;
    for (const Rung& p : m_ladder) {
        rungs << (p.res ? QStringLiteral("%1:%2p%3/%4k").arg(js::string(p.id), fmt(p.res), fmt(p.fps), fmt(p.kbps))
                        : QStringLiteral("%1:%2x%3/%4fps/%5k")
                              .arg(js::string(p.id))
                              .arg(p.width)
                              .arg(p.height)
                              .arg(fmt(p.fps), fmt(p.kbps)));
    }
    if (m_ladder.isEmpty()) return;
    QString line = QStringLiteral("send-rate: ladder %1, start %2, loss marks %3..%4 %, queue marks %5..%6 ms, key every %7 ms")
                       .arg(rungs.join(QLatin1Char(' ')), name(), fmt(m_lowLoss * 100), fmt(m_highLoss * 100),
                            fmt(kQueueHoldMs), fmt(kQueueDownMs), fmt(m_keyMs));
    if (m_armTimeout) line += QStringLiteral(", no climb past id %1").arg(js::string(m_ladder[m_ceiling].id));
    if (m_fixed) line += QStringLiteral(", adaptation OFF (ZCALL_VIDEO_ADAPT=0)");
    m_log(line);
    m_onProfile(profile());
}

std::pair<int, int> SendRate::dims(const Rung& p) const {
    if (p.width > 0 && p.height > 0) return {p.width, p.height};
    const auto sz = sizeFor(p.res);
    return sz ? *sz : std::make_pair(0, 0);
}

QString SendRate::name(int level) const {
    if (m_ladder.isEmpty()) return QString();
    const Rung& p = m_ladder[level];
    const auto d = dims(p);
    return QStringLiteral("id %1 %2x%3 %4 fps %5 kbps").arg(js::string(p.id)).arg(d.first).arg(d.second).arg(fmt(p.fps), fmt(p.kbps));
}

SendProfile SendRate::profile() const {
    SendProfile out;
    if (m_ladder.isEmpty()) return out;
    const Rung& p = m_ladder[m_level];
    const auto d = dims(p);
    out.width = d.first;
    out.height = d.second;
    out.fps = p.fps;
    out.bitrate = p.kbps * 1000;
    out.keyMs = m_keyMs;
    out.captureBig = m_captureBig;
    out.rung = p.id;
    return out;
}

void SendRate::onSent(uint16_t twSeq, std::optional<double> ms) {
    const double t = ms ? *ms : m_now();
    const auto it = m_sent.find(twSeq);
    if (it != m_sent.end()) {
        it->second = t;
    } else {
        m_sent.emplace(twSeq, t);
        m_sentOrder.push_back(twSeq);
    }
    if (m_sent.size() > kSentKeep) {
        m_sent.erase(m_sentOrder.front());
        m_sentOrder.pop_front();
    }
}

void SendRate::onRtcp(char kind, const QByteArray& buf) {
    for (int o = 0; o + 4 <= buf.size();) {
        if ((u8at(buf, o) >> 6) != 2) return;
        const int len = (rd16be(buf, o + 2) + 1) * 4;
        if (o + len > buf.size()) return;
        const int pt = u8at(buf, o + 1);
        const int fmtv = u8at(buf, o) & 0x1f;
        const QString key = QStringLiteral("%1%2/%3").arg(QLatin1Char(kind)).arg(pt).arg(fmtv);
        auto t = std::find_if(m_types.begin(), m_types.end(), [&key](const auto& e) { return e.first == key; });
        if (t == m_types.end()) m_types.push_back({key, 1});
        else ++t->second;
        const QByteArray pkt = buf.mid(o, len);
        if (pt == 205 && fmtv == 15) {
            onTwcc(pkt);
        } else if (pt == 200 || pt == 201) {
            const int first = pt == 200 ? 28 : 8;
            for (int i = 0; i < fmtv; ++i) {
                const int b = first + 24 * i;
                if (b + 24 > len) break;
                if (rd32be(pkt, b) == m_localSsrc) m_win.rr.push_back(u8at(pkt, b + 4) / 256.0);
            }
        }
        o += len;
    }
}

void SendRate::onTwcc(const QByteArray& pkt) {
    const auto fb = parseTwcc(pkt);
    if (!fb || fb->senderSsrc == m_localSsrc) return;
    double arrivalMs = double(fb->refUnits) * 64;
    size_t d = 0;
    for (size_t i = 0; i < fb->status.size(); ++i) {
        if (fb->status[i] == 0) {
            ++m_win.twccLost;
            continue;
        }
        ++m_win.twccRecv;
        if (d >= fb->deltaQ.size()) continue;
        arrivalMs += fb->deltaQ[d++] / 4.0;
        const auto it = m_sent.find(uint16_t((fb->base + i) & 0xffff));
        if (it != m_sent.end()) m_win.delays.push_back(arrivalMs - it->second);
    }
}

void SendRate::tick(bool sending, std::optional<double> downLoss) {
    if (m_ladder.isEmpty()) return;
    const double now = m_now();
    const Window w = m_win;
    m_win = Window{};
    const int64_t total = w.twccRecv + w.twccLost;
    m_totals.twccPkts += total;
    m_totals.twccLost += w.twccLost;
    m_totals.rrBlocks += int64_t(w.rr.size());
    const bool twcc = total >= kMinTwccPackets;
    std::optional<double> loss;
    QString src;
    if (twcc) {
        loss = double(w.twccLost) / double(total);
        src = QStringLiteral("twcc");
    } else if (!w.rr.empty()) {
        loss = *std::max_element(w.rr.begin(), w.rr.end());
        src = QStringLiteral("rr");
    }
    std::optional<double> queue;
    if (!w.delays.empty()) {
        std::vector<double> sorted = w.delays;
        std::sort(sorted.begin(), sorted.end());
        m_minDelays.push_back({now, sorted.front()});
        m_minDelays.erase(std::remove_if(m_minDelays.begin(), m_minDelays.end(),
                                         [now](const auto& e) { return now - e.first > kBaseWindowS * 1000; }),
                          m_minDelays.end());
        double base = m_minDelays.front().second;
        for (const auto& e : m_minDelays) base = std::min(base, e.second);
        queue = std::max<double>(0, double(jsRound(sorted[sorted.size() / 2] - base)));
    }
    m_noFeedback = twcc ? 0 : m_noFeedback + 1;
    if (twcc) m_seenTwcc = true;
    auto tenth = [](double x) { return fmt(double(jsRound(x * 1000)) / 10); };
    m_history << QStringLiteral("%1/%2/%3")
                     .arg(loss ? tenth(*loss) : QStringLiteral("-"), queue ? fmt(*queue) : QStringLiteral("-"),
                          downLoss ? tenth(*downLoss) : QStringLiteral("-"));
    if (!m_fixed && sending) {
        auto pct = [](double x) { return js::toFixed1(x * 100) + QStringLiteral(" %"); };
        QString down;
        bool hold = false;
        if (m_noFeedback >= kFeedbackTimeoutS && !(m_armTimeout && !m_seenTwcc))
            down = QStringLiteral("no transport-cc for %1 s").arg(m_noFeedback);
        else if (loss && *loss > m_highLoss)
            down = QStringLiteral("loss %1 (%2) > %3 %").arg(pct(*loss), src, fmt(m_highLoss * 100));
        else if (queue && *queue > kQueueDownMs)
            down = QStringLiteral("queue %1 ms > %2 ms").arg(fmt(*queue), fmt(kQueueDownMs));
        else if (downLoss && *downLoss > kDownlinkDown)
            down = QStringLiteral("we lose %1 of the phone's packets").arg(pct(*downLoss));
        else if (!twcc || (loss && *loss >= m_lowLoss) || (queue && *queue > kQueueHoldMs) ||
                 (downLoss && *downLoss > kDownlinkHold))
            hold = true;
        if (!down.isEmpty()) {
            m_good = 0;
            if (m_level > 0 && now - m_lastDownAt >= kDownGapMs) {
                m_backoffMs = now - m_lastUpAt < 15000 ? std::min(kBackoffMaxMs, m_backoffMs * 2) : kBackoffMinMs;
                m_lastDownAt = now;
                ++m_totals.downs;
                set(m_level - 1, down);
            }
        } else if (hold) {
            m_good = 0;
        } else if (++m_good >= kUpHoldS && m_level < m_ceiling && now - m_lastDownAt >= m_backoffMs) {
            m_good = 0;
            m_lastUpAt = now;
            set(m_level + 1, QStringLiteral("clean for %1 s (loss %2, queue %3)")
                                 .arg(kUpHoldS)
                                 .arg(loss ? pct(*loss) : QStringLiteral("0.0 %"),
                                      queue ? fmt(*queue) + QStringLiteral(" ms") : QStringLiteral("-")));
        }
    }
    if (now - m_lastLogAt >= kLogMs) {
        m_lastLogAt = now;
        QStringList types;
        for (const auto& e : m_types) types << QStringLiteral("%1:%2").arg(e.first).arg(e.second);
        m_types.clear();
        m_log(QStringLiteral("send-rate: %1%2; loss%/queue ms/downlink% per s [%3]; rtcp from phone %4")
                  .arg(name(), sending ? QString() : QStringLiteral(" (camera off)"), m_history.join(QLatin1Char(' ')),
                       types.isEmpty() ? QStringLiteral("none") : types.join(QLatin1Char(' '))));
        m_history.clear();
    }
}

void SendRate::set(int level, const QString& why) {
    const QString from = name();
    m_level = level;
    ++m_totals.changes;
    m_log(QStringLiteral("send-rate: %1 -> %2 (%3)").arg(from, name(), why));
    m_onProfile(profile());
}

QString SendRate::totalsJson() const {
    return QStringLiteral("{\"rung\":%1,\"twccPkts\":%2,\"twccLost\":%3,\"rrBlocks\":%4,\"changes\":%5,\"downs\":%6}")
        .arg(js::stringify(profile().rung))
        .arg(m_totals.twccPkts)
        .arg(m_totals.twccLost)
        .arg(m_totals.rrBlocks)
        .arg(m_totals.changes)
        .arg(m_totals.downs);
}

}
