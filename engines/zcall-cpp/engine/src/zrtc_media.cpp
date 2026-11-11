// SPDX-License-Identifier: BSD-3-Clause
#include "zrtc_media.h"

#include "js.h"
#include "log.h"
#include "wire/bytes.h"
#include "wire/rtp.h"
#include "wire/video.h"
#include "wire/zrtp_packet.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace zcall {

namespace {

constexpr int kInitWaitMs = 600;
constexpr int kInitRetryMs = 1000;
constexpr int kInitTries = 3;
constexpr int kPingIntervalMs = 6000;
constexpr int kTwccIntervalMs = 50;
constexpr int kRtcpReportIntervalMs = 1000;
constexpr double kPliMinGapMs = 1000;
constexpr double kRtcpLogMs = 5000;
constexpr int kVideoBurst = 8;
constexpr double kVideoTxLogMs = 5000;
constexpr double kAudioTxLogMs = 10000;
constexpr int kSpeakDbov = 50;
constexpr int kStatusIntervalMs = 1000;
constexpr double kMediaTimeoutMs = 20000;
constexpr int kP2pBindFastMs = 100;
constexpr int kP2pBindSlowMs = 400;
constexpr double kP2pBindGiveUpMs = 15000;
constexpr uint32_t kAudioTs = 320;

double nowMs() { return double(QDateTime::currentMSecsSinceEpoch()); }

bool envIs(const char* name, const char* value) { return qEnvironmentVariable(name) == QLatin1String(value); }

int groupPingMs() {
    const QString v = qEnvironmentVariable("ZCALL_GROUP_PING_MS");
    const double d = js::number(QJsonValue(v));
    return v.isEmpty() || !std::isfinite(d) || d <= 0 ? 5000 : int(d);
}

bool parseHostPort(const QString& addr, ServerAddr& out) {
    if (addr.isEmpty()) return false;
    if (addr.startsWith(QLatin1Char('['))) {
        static const QRegularExpression re(QStringLiteral("^\\[([^\\]]+)\\]:(\\d+)$"));
        const auto m = re.match(addr);
        if (!m.hasMatch()) return false;
        out.host = m.captured(1);
        out.port = quint16(m.captured(2).toUInt());
        return true;
    }
    const int i = addr.lastIndexOf(QLatin1Char(':'));
    if (i <= 0) return false;
    static const QRegularExpression digits(QStringLiteral("^\\d+"));
    const auto m = digits.match(addr.mid(i + 1));
    if (!m.hasMatch()) return false;
    out.host = addr.left(i);
    out.port = quint16(m.captured(0).toUInt());
    return true;
}

QStringList callAudioArgs(const QJsonObject& c) {
    QStringList a;
    const double maxDelay = js::number(c.value("maxAudioDelayMs"));
    const double jitterMax = js::number(c.value("audioJitterMax"));
    const double cap = std::isfinite(maxDelay) && maxDelay > 0 ? maxDelay
                       : (std::isfinite(jitterMax) && jitterMax > 0 ? jitterMax : 0);
    if (cap) a << "--max-delay" << QString::number(std::trunc(cap), 'f', 0);
    if (c.contains("audioEchoCancellation") && c.value("audioEchoCancellation") == false) a << "--no-aec";
    if (c.contains("audioNoiseSuppression") && c.value("audioNoiseSuppression") == false) a << "--no-ns";
    if (c.contains("audioHighPassFilter") && c.value("audioHighPassFilter") == false) a << "--no-high-pass";
    if (c.value("audioVoiceDetection") == true) a << "--vad";
    return a;
}

QStringList groupAudioArgs(const QJsonObject& c) {
    QStringList a;
    const double br = js::number(c.value("audioBitrate"));
    if (br > 0) a << "--bitrate" << QString::number(std::trunc(br) * 1000, 'f', 0);
    const double cx = js::number(c.value("opusComplex"));
    if (!c.value("opusComplex").isNull() && !c.value("opusComplex").isUndefined() && std::isfinite(cx) && cx >= 0)
        a << "--complexity" << QString::number(std::trunc(cx), 'f', 0);
    if (c.value("audioFecInband") == false) a << "--no-fec";
    if (c.value("audioHighPassFilter") == false) a << "--no-high-pass";
    return a;
}

std::optional<std::pair<int, int>> sizeOf(int res) {
    switch (res) {
    case 240: return std::make_pair(426, 240);
    case 360: return std::make_pair(640, 360);
    case 480: return std::make_pair(848, 480);
    case 720: return std::make_pair(1280, 720);
    default: return std::nullopt;
    }
}

uint32_t u32mod(double v) { return js::toUint32(v); }

}

uint32_t RocTracker::guess(uint16_t seq) const {
    if (!last) return 0;
    if (*last - seq > 0x8000) return roc + 1;
    if (seq - *last > 0x8000 && roc > 0) return roc - 1;
    return roc;
}

void RocTracker::update(uint16_t seq, uint32_t r) {
    if (!last || int64_t(r) * 0x10000 + seq > int64_t(roc) * 0x10000 + *last) {
        last = seq;
        roc = r;
    }
}

ZrtcMedia::ZrtcMedia(bool sendUdp, QObject* parent) : QObject(parent), m_sendUdpEnabled(sendUdp) {
    m_mic = QString();
    resetCounters();
    m_vTsBase = QRandomGenerator::global()->bounded(0x7fffffffu);
}

ZrtcMedia::~ZrtcMedia() {
    clearTimers();
    stopAudio();
}

void ZrtcMedia::resetCounters() {
    m_seq = QRandomGenerator::global()->bounded(0x8000u);
    m_ts = QRandomGenerator::global()->bounded(0x7fffffffu);
    m_twSeq = 1;
    m_vSeq = QRandomGenerator::global()->bounded(0x8000u);
    m_vRoc = 0;
    m_vTsBase = QRandomGenerator::global()->bounded(0x7fffffffu);
    m_vStart = 0;
    m_vLayerSeq = 0;
    m_ctlSeq = 0;
    m_roc = 0;
    m_fwdSeq = 1;
    m_audioOctets = 0;
    m_vKeySent = false;
    m_groupCameraOn = false;
    m_vidCfgLogged = false;
    m_vidWanted.clear();
    m_renderWidth.clear();
    m_vidAsked.clear();
    m_rxRoc = RocTracker{};
    m_rxRocs.clear();
    m_videoRoc = RocTracker{};
    m_videoRocs.clear();
    m_members.clear();
    m_speak.clear();
    m_pliAt.clear();
    m_lastPliAt = 0;
    m_counters = Counters{};
    m_lastRxAt = 0;
    m_p2pPath.reset();
    m_p2pSeen = false;
    m_p2pSeq = 0;
    m_downPrev.reset();
    m_vTxLastTx = 0;
}

void ZrtcMedia::onNegotiated(const NegotiatedParams& params) {
    m_params = params;
    m_hasParams = true;
    m_group = params.group;
    m_muted = params.muted;
    m_speakerOff = params.speakerOff;
    m_servers.clear();
    for (const QJsonValue& sv : params.servers) {
        const QJsonObject raw = sv.toObject();
        QString addr = js::stringOr(raw.value("rtpaddr"));
        if (addr.isEmpty()) addr = js::stringOr(raw.value("rtp"));
        if (addr.isEmpty()) addr = js::stringOr(raw.value("rtpAddress"));
        ServerAddr s;
        if (parseHostPort(addr, s)) {
            s.raw = raw;
            m_servers.push_back(s);
        }
    }
    m_activeServer.reset();
    m_serverToken = 0;
    m_replies.clear();
    m_ended.clear();
    resetCounters();

    const bool groupRtcp = !m_group || !envIs("ZCALL_GROUP_RTCP", "0");
    const bool rtcpOn = groupRtcp && !envIs("ZCALL_RTCP", "0") && params.localUid != 0;
    m_rtcp = rtcpOn ? std::make_unique<RtcpFeedback>(u32mod(params.localUid)) : nullptr;
    m_nack = rtcpOn ? std::make_unique<NackTracker>() : nullptr;
    if (m_group && rtcpOn) zlog(QStringLiteral("media: group transport-cc feedback on (ZCALL_GROUP_RTCP=0 turns it off)"));
    m_plan = m_group ? groupVideoPlan(params.config) : std::nullopt;

    const bool rateOn = rtcpOn && !envIs("ZCALL_SEND_RATE", "0");
    SendRate::Options opts;
    opts.localSsrc = u32mod(params.localUid);
    opts.log = [](const QString& l) { zlog(l); };
    opts.onProfile = [this](const SendProfile& p) { applySendProfile(p); };
    opts.now = [] { return nowMs(); };
    m_sendRate.reset();
    if (rateOn && m_group && m_plan && !m_plan->rows.isEmpty()) {
        opts.layers = m_plan->rows;
        opts.startIndex = m_plan->start;
        m_sendRate = std::make_unique<SendRate>(params.config, opts);
    } else if (rateOn && !m_group) {
        m_sendRate = std::make_unique<SendRate>(params.config, opts);
    }

    zlog(QStringLiteral("media: negotiated { servers: %1, srtpMode: %2, sendUdp: %3 }")
             .arg(m_servers.size())
             .arg(js::string(params.srtpMode))
             .arg(m_sendUdpEnabled ? QStringLiteral("true") : QStringLiteral("false")));

    m_srtp.reset();
    m_plainMedia = false;
    const QString groupKey = m_group ? js::string(params.config.value("srtpKey")) : QString();
    if (m_group && groupKey.isEmpty()) {
        m_plainMedia = true;
        zlog(QStringLiteral("media: group call without srtpKey: plain RTP, as ZaloCall does"));
    }
    const QString sess = m_group ? groupKey : params.sessId;
    if (sess.toUtf8().size() >= 30) {
        if (const auto master = masterFromSessId(sess.toUtf8())) {
            m_srtp = sessionKeys(*master);
        }
        if (!m_srtp) zlog(QStringLiteral("media: SRTP key setup failed"));
    }
}

void ZrtcMedia::initSocket() {
    if (m_socket || !m_sendUdpEnabled) return;
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress::AnyIPv4, 0);
    connect(m_socket, &QUdpSocket::readyRead, this, &ZrtcMedia::onDatagram);
}

void ZrtcMedia::sendTo(const QByteArray& pkt, const Server& s) {
    if (!m_socket || s.host.isEmpty()) return;
    m_socket->writeDatagram(pkt, QHostAddress(s.host), s.port);
}

void ZrtcMedia::onDatagram() {
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray buf(int(m_socket->pendingDatagramSize()), Qt::Uninitialized);
        QHostAddress addr;
        quint16 port = 0;
        const qint64 n = m_socket->readDatagram(buf.data(), buf.size(), &addr, &port);
        if (n < 0) break;
        buf.resize(int(n));
        onUdp(buf, addr, port);
    }
}

QTimer* ZrtcMedia::timer(int ms, bool repeat, std::function<void()> fn) {
    auto* t = new QTimer(this);
    t->setSingleShot(!repeat);
    t->setInterval(ms);
    m_timers.append(t);
    connect(t, &QTimer::timeout, this, [this, t, repeat, fn] {
        if (!repeat) {
            m_timers.removeOne(t);
            t->deleteLater();
        }
        fn();
    });
    t->start();
    return t;
}

void ZrtcMedia::clearTimers() {
    const QList<QTimer*> list = m_timers;
    m_timers.clear();
    for (QTimer* t : list) {
        t->stop();
        t->deleteLater();
    }
    m_initWait = nullptr;
}

void ZrtcMedia::prepare(const QString& role, std::function<void(std::optional<InitReply>)> done) {
    m_active = true;
    if (!m_sendUdpEnabled) {
        zlog(QStringLiteral("media: observe-only (no --send-udp), not contacting servers"));
        QTimer::singleShot(0, this, [done] { done(std::nullopt); });
        return;
    }
    initSocket();
    if (m_group) {
        prepareGroup(0, std::move(done));
        return;
    }
    const QByteArray packet = buildInitZrtpPacket(role == QLatin1String("caller"), u32mod(m_params.localUid),
                                                  u32mod(m_params.peerUid), u32mod(m_params.callId),
                                                  m_params.sessId.toUtf8());
    initZrtp(packet, std::move(done));
}

void ZrtcMedia::prepareGroup(int i, std::function<void(std::optional<InitReply>)> done) {
    const QString forced = qEnvironmentVariable("ZCALL_GROUP_INIT_LAYOUT");
    const QStringList layouts = forced.isEmpty() ? kGroupInitLayouts : QStringList{forced};
    if (i >= layouts.size() || !m_active) {
        done(std::nullopt);
        return;
    }
    m_replies.clear();
    const QString layout = layouts[i];
    const QByteArray packet = buildGroupInitZrtpPacket(m_params.host, u32mod(m_params.localUid), m_params.sessId.toUtf8(), true,
                                                       u32mod(m_params.callId), u32mod(m_params.hostCall), layout);
    initZrtp(packet, [this, i, done, layout, layouts](std::optional<InitReply> best) {
        if (best) {
            zlog(QStringLiteral("media: group InitZRTP accepted with layout \"%1\"").arg(layout));
            done(best);
            return;
        }
        if (m_refused.isEmpty()) {
            done(std::nullopt);
            return;
        }
        zlog(QStringLiteral("media: group InitZRTP layout \"%1\" refused").arg(layout));
        prepareGroup(i + 1, done);
    });
}

void ZrtcMedia::initZrtp(const QByteArray& packet, std::function<void(std::optional<InitReply>)> done) {
    m_refused.clear();
    m_initPacket = packet;
    m_initDone = std::move(done);
    m_initActive = true;
    m_initTries = 0;
    initSendAll();
    timer(kInitRetryMs, false, [this] { initRetry(); });
}

void ZrtcMedia::initSendAll() {
    m_initTries++;
    m_initSentAt = nowMs();
    for (const Server& s : m_servers) {
        const bool answered = std::any_of(m_replies.begin(), m_replies.end(), [&s](const Reply& r) { return r.server == s; });
        if (!answered) sendTo(m_initPacket, s);
    }
}

void ZrtcMedia::initRetry() {
    if (!m_initActive || !m_replies.isEmpty() || !m_refused.isEmpty()) return;
    if (m_initTries >= kInitTries) {
        finishInit();
        return;
    }
    initSendAll();
    timer(kInitRetryMs, false, [this] { initRetry(); });
}

void ZrtcMedia::checkInitProgress() {
    if (!m_initActive) return;
    if (m_replies.size() + m_refused.size() >= m_servers.size()) {
        finishInit();
        return;
    }
    if (!m_initWait) m_initWait = timer(kInitWaitMs, false, [this] { finishInit(); });
}

void ZrtcMedia::finishInit() {
    if (!m_initActive) return;
    m_initActive = false;
    if (m_initWait) {
        m_initWait->stop();
        m_timers.removeOne(m_initWait);
        m_initWait->deleteLater();
        m_initWait = nullptr;
    }
    auto done = std::move(m_initDone);
    m_initDone = nullptr;
    if (!done) return;
    if (m_replies.isEmpty()) {
        zlog(m_refused.isEmpty() ? QStringLiteral("media: no server answered InitZRTP")
                                 : QStringLiteral("media: every server refused InitZRTP"));
        done(std::nullopt);
        return;
    }
    const Reply* best = &m_replies.first();
    for (const Reply& r : m_replies)
        if (r.rttMs < best->rttMs) best = &r;
    m_activeServer = best->server;
    m_serverToken = best->token;
    zlog(QStringLiteral("media: %1/%2 servers answered, best rtt %3 ms")
             .arg(m_replies.size())
             .arg(m_servers.size())
             .arg(js::numberString(best->rttMs)));
    InitReply r;
    r.server = best->server;
    r.token = best->token;
    r.rttMs = best->rttMs;
    r.publicAddr = best->publicAddr;
    r.hex = best->hex;
    done(r);
}

void ZrtcMedia::sendEndCall(const Server& s, uint32_t token) {
    const QString key = s.host + QLatin1Char(':') + QString::number(s.port);
    if (m_ended.contains(key)) return;
    m_ended.insert(key);
    sendTo(buildEndCallPacket(u32mod(m_params.localUid), token, u32mod(m_params.callId)), s);
}

void ZrtcMedia::start(const QString& role, const QString& rtpSerIp) {
    m_active = true;
    if (!m_sendUdpEnabled) {
        zlog(QStringLiteral("media: start role=%1 (observe-only)").arg(role));
        return;
    }
    ServerAddr want;
    const bool hasWant = parseHostPort(rtpSerIp, want);
    if (hasWant) {
        for (const Reply& r : m_replies) {
            if (r.server.host == want.host && r.server.port == want.port) {
                m_activeServer = r.server;
                m_serverToken = r.token;
                break;
            }
        }
        if (!m_activeServer) zlog(QStringLiteral("media: peer chose a server we have no token for; keeping ours"));
    }
    if (!m_activeServer) {
        zlog(QStringLiteral("media: no server"));
        mediaFailed(QStringLiteral("no-server"));
        return;
    }
    const Server active = *m_activeServer;
    for (const Reply& r : m_replies)
        if (!(r.server == active)) sendEndCall(r.server, r.token);
    zlog(QStringLiteral("media: started"));
    m_startedAt = nowMs();
    m_lastRxAt = nowMs();
    if (m_group) {
        sendPing();
        timer(groupPingMs(), true, [this] { sendPing(); });
        timer(3000, false, [this] {
            const auto& c = m_counters;
            if (m_plainMedia) zlog(QStringLiteral("media: group plain RTP: %1 good, %2 bad, %3 member(s) heard").arg(c.rx).arg(c.rxBad).arg(m_members.size()));
            else if (c.rx) zlog(QStringLiteral("media: group SRTP key OK (%1 good, %2 bad, %3 member(s) heard)").arg(c.rx).arg(c.rxBad).arg(m_members.size()));
            else zlog(QStringLiteral("media: no group audio yet (members muted, or nothing forwarded)"));
        });
        if (m_rtcp) {
            timer(kTwccIntervalMs, true, [this] { sendTwcc(); });
            timer(kRtcpReportIntervalMs, true, [this] { sendRtcpReports(); });
        }
        startAudio();
        return;
    }
    timer(kPingIntervalMs, true, [this] { sendPing(); });
    timer(kStatusIntervalMs, true, [this] { sendStatus(); });
    timer(200, false, [this] {
        const QByteArray payload(QByteArray::fromHex("0400000000000000"));
        sendTo(buildReqForwardPacket(u32mod(m_params.localUid), m_serverToken, 0, 8, payload), *m_activeServer);
    });
    timer(2000, true, [this] {
        if (nowMs() - m_lastRxAt > kMediaTimeoutMs) {
            zlog(QStringLiteral("media: no audio from peer"));
            if (m_listener) m_listener->mediaTimeout();
        }
    });
    if (m_rtcp) {
        timer(kTwccIntervalMs, true, [this] { sendTwcc(); });
        timer(kRtcpReportIntervalMs, true, [this] { sendRtcpReports(); });
    }
    startAudio();
    p2pStart();
}

void ZrtcMedia::mediaFailed(const QString& why) {
    if (m_listener) m_listener->mediaFailed(why);
}

void ZrtcMedia::p2pStart() {
    m_p2pCands.clear();
    const QJsonArray list = m_params.p2pCandidates;
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject c = list.at(i).toObject();
        const QString ip = js::stringOr(c.value("ip"));
        const double port = js::number(c.value("port"));
        static const QRegularExpression v4(QStringLiteral("^\\d+\\.\\d+\\.\\d+\\.\\d+$"));
        if (!ip.isEmpty() && v4.match(ip).hasMatch() && port > 0) {
            Server s;
            s.host = ip;
            s.port = quint16(port);
            m_p2pCands.push_back({s, uint32_t(i)});
        }
    }
    if (m_p2pCands.isEmpty()) return;
    m_p2pBindStart = nowMs();
    zlog(QStringLiteral("media: P2P binding to %1 candidate(s)").arg(m_p2pCands.size()));
    p2pBindTick();
}

void ZrtcMedia::p2pBindTick() {
    if (!m_active || m_p2pPath || nowMs() - m_p2pBindStart > kP2pBindGiveUpMs) return;
    const bool callee = m_params.role == QLatin1String("callee");
    for (const auto& [s, index] : m_p2pCands)
        sendTo(buildP2pBind(callee, u32mod(m_params.callId), m_p2pSeq, index), s);
    m_p2pSeq++;
    timer(nowMs() - m_p2pBindStart < 1000 ? kP2pBindFastMs : kP2pBindSlowMs, false, [this] { p2pBindTick(); });
}

void ZrtcMedia::p2pUp(const Server& from) {
    if (m_p2pPath && m_p2pPath->host == from.host && m_p2pPath->port == from.port) return;
    m_p2pPath = from;
    zlog(QStringLiteral("media: P2P path up"));
}

void ZrtcMedia::onP2p(const QByteArray& msg, const QHostAddress& addr, quint16 port) {
    const Server from{addr.toString(), port, {}};
    const int kind = u8at(msg, 7);
    const bool callee = m_params.role == QLatin1String("callee");
    const uint32_t callId = u32mod(m_params.callId);
    if (!m_p2pSeen) {
        m_p2pSeen = true;
        zlog(QStringLiteral("media: first P2P packet from the peer (kind %1)").arg(kind));
    }
    if (kind == P2p::BindPeer && msg.size() >= 19) {
        const QByteArray peerAddr = from.host.toLatin1() + '|' + QByteArray::number(port);
        sendTo(buildP2pBindAnswer(callee, callId, msg, peerAddr), from);
    } else if (kind == P2p::Keepalive && u8at(msg, 9) == 1) {
        sendTo(buildP2pPong(callee, msg), from);
        p2pUp(from);
    } else if (kind == P2p::Video && msg.size() > P2p::HeaderLen + 12) {
        p2pUp(from);
        onVideo(msg.mid(P2p::HeaderLen));
    } else if (kind == P2p::VideoRtcp && msg.size() > P2p::HeaderLen) {
        onVideoRtcp(msg.mid(P2p::HeaderLen));
        peerRtcp(true, msg.mid(P2p::HeaderLen));
    } else if (kind == P2p::AudioRtcp && msg.size() > P2p::HeaderLen) {
        peerRtcp(false, msg.mid(P2p::HeaderLen));
    } else if (kind == P2p::Audio && msg.size() > P2p::HeaderLen + 12) {
        m_counters.rxP2p++;
        p2pUp(from);
        onAudio(msg.mid(P2p::HeaderLen));
    }
}

QJsonArray ZrtcMedia::localCandidates(const std::optional<QByteArray>& publicAddr) const {
    QJsonArray out;
    const int port = m_socket ? m_socket->localPort() : 0;
    if (port) {
        for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
            if (!iface.flags().testFlag(QNetworkInterface::IsUp) || iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
            for (const QNetworkAddressEntry& e : iface.addressEntries()) {
                if (e.ip().protocol() != QAbstractSocket::IPv4Protocol || e.ip().isLoopback()) continue;
                out.append(QJsonObject{{"ip", e.ip().toString()}, {"port", port}, {"type", 0}});
            }
        }
    }
    if (publicAddr && publicAddr->contains('|')) {
        const QList<QByteArray> parts = publicAddr->split('|');
        out.append(QJsonObject{{"ip", QString::fromLatin1(parts.value(0))}, {"port", parts.value(1).toInt()}, {"type", 1}});
    }
    return out;
}

void ZrtcMedia::setMute(bool on) {
    m_muted = on;
    if (m_audio) m_audio->write(QByteArray(1, char(0x4d)) + QByteArray(1, char(on ? 1 : 0)));
}

void ZrtcMedia::setMicVolume(double pct) {
    const int p = std::clamp(int(std::lround(pct)), 0, 100);
    if (m_audio) m_audio->write(QByteArray(1, char(0x56)) + QByteArray(1, char(p)));
}

void ZrtcMedia::setSpeaker(bool off) {
    m_speakerOff = off;
    if (m_audio) m_audio->write(QByteArray(1, char(0x53)) + QByteArray(1, char(off ? 1 : 0)));
}

void ZrtcMedia::setDevice(const QString& kind, const QString& name) {
    if (kind != QLatin1String("mic") && kind != QLatin1String("speaker")) return;
    if (kind == QLatin1String("mic")) m_mic = name;
    else m_speaker = name;
    if (!m_audio) return;
    const QByteArray n = name.toUtf8();
    QByteArray h(4, '\0');
    h[0] = char(0x44);
    h[1] = char(kind == QLatin1String("mic") ? 0x69 : 0x6f);
    h[2] = char((n.size() >> 8) & 0xff);
    h[3] = char(n.size() & 0xff);
    m_audio->write(h + n);
}

std::optional<MediaStats> ZrtcMedia::stats() const {
    return MediaStats{double(m_counters.rx), double(m_counters.tx)};
}

void ZrtcMedia::stop() {
    if (m_sendRate) {
        zlog(QStringLiteral("send-rate: call totals %1").arg(m_sendRate->totalsJson()));
        m_sendRate.reset();
    }
    m_vOut.clear();
    if (m_active && m_hasParams) {
        if (m_activeServer && m_serverToken) sendEndCall(*m_activeServer, m_serverToken);
        for (const Reply& r : m_replies) sendEndCall(r.server, r.token);
    }
    if (m_startedAt) {
        zlog(QStringLiteral("media: stopped { rx: %1, tx: %2 }").arg(m_counters.rx).arg(m_counters.tx));
    }
    m_startedAt = 0;
    m_active = false;
    m_initActive = false;
    m_initDone = nullptr;
    clearTimers();
    stopAudio();
    if (QUdpSocket* sock = m_socket) {
        m_socket = nullptr;
        QTimer::singleShot(100, sock, [sock] {
            sock->close();
            sock->deleteLater();
        });
    }
}

void ZrtcMedia::startAudio() {
    const QString exec = QCoreApplication::applicationDirPath() + QStringLiteral("/zalos-call-audio");
    if (!QFileInfo(exec).isExecutable()) {
        zlog(QStringLiteral("media: audio engine missing: %1").arg(exec));
        return;
    }
    QStringList args;
    if (!m_mic.isEmpty()) args << "--mic" << m_mic;
    if (!m_speaker.isEmpty()) args << "--speaker" << m_speaker;
    if (m_group) args << "--mix";
    const double minDelay = js::number(m_params.config.value("minAudioDelayMs"));
    if (minDelay > 0) args << "--min-delay" << QString::number(std::trunc(minDelay), 'f', 0);
    const QString extra = qEnvironmentVariable("ZCALL_AUDIO_ARGS");
    if (!extra.isEmpty()) {
        args << extra.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    } else {
        args << callAudioArgs(m_params.config);
        if (m_group) args << groupAudioArgs(m_params.config);
    }

    stopAudio();
    m_audio = new QProcess(this);
    m_audio->setProcessChannelMode(QProcess::SeparateChannels);
    QProcess* proc = m_audio;
    connect(proc, &QProcess::readyReadStandardOutput, this, &ZrtcMedia::onAudioStdout);
    connect(proc, &QProcess::readyReadStandardError, this, &ZrtcMedia::onAudioStderr);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus status) {
        zlog(QStringLiteral("media: audio-io exited %1").arg(status == QProcess::CrashExit ? QStringLiteral("(signal)") : QString::number(code)));
        if (m_audio == proc) {
            m_audio = nullptr;
            if (m_listener) m_listener->mediaAudioFault(QStringLiteral("audio"));
        }
        proc->deleteLater();
    });
    proc->start(exec, args);
    if (m_muted) setMute(true);
    if (m_speakerOff) setSpeaker(true);
}

void ZrtcMedia::stopAudio() {
    if (!m_audio) return;
    QProcess* a = m_audio;
    m_audio = nullptr;
    a->closeWriteChannel();
    QTimer::singleShot(1000, a, [a] {
        if (a->state() != QProcess::NotRunning) a->kill();
    });
    m_audioOut.clear();
    m_audioErr.clear();
}

void ZrtcMedia::onAudioStderr() {
    if (!m_audio) return;
    m_audioErr += m_audio->readAllStandardError();
    for (int i = m_audioErr.indexOf('\n'); i >= 0; i = m_audioErr.indexOf('\n')) {
        const QString line = QString::fromUtf8(m_audioErr.left(i));
        m_audioErr.remove(0, i + 1);
        if (line.trimmed().isEmpty()) continue;
        zlog(QString(line).replace(QRegularExpression(QStringLiteral("^\\[audio-io\\] ")), QStringLiteral("audio: ")));
        static const QRegularExpression fault(QStringLiteral("^\\[audio-io\\] fault: (mic|speaker)\\b"));
        static const QRegularExpression vol(QStringLiteral("^\\[audio-io\\] mic volume: (\\d+)%"));
        const auto fm = fault.match(line);
        if (fm.hasMatch() && m_listener) m_listener->mediaAudioFault(fm.captured(1));
        const auto vm = vol.match(line);
        if (vm.hasMatch() && m_listener) m_listener->mediaMicVolume(vm.captured(1).toInt());
    }
}

void ZrtcMedia::onAudioStdout() {
    if (!m_audio) return;
    m_audioOut += m_audio->readAllStandardOutput();
    for (;;) {
        if (m_audioOut.size() >= 3 && u8at(m_audioOut, 0) == 0x41) {
            const int n = rd16be(m_audioOut, 1);
            if (m_audioOut.size() < 3 + n) break;
            sendAudioFrame(m_audioOut.mid(3, n), 127);
            m_audioOut.remove(0, 3 + n);
        } else if (m_audioOut.size() >= 4 && u8at(m_audioOut, 0) == 0x4c) {
            const int n = rd16be(m_audioOut, 2);
            if (m_audioOut.size() < 4 + n) break;
            const int level = u8at(m_audioOut, 1);
            sendAudioFrame(m_audioOut.mid(4, n), level);
            const double t = nowMs();
            m_micPeak = std::min(m_micPeak, level);
            if (t - m_micLevelAt >= 100) {
                m_micLevelAt = t;
                if (m_listener) m_listener->mediaMicLevel(m_micPeak);
                m_micPeak = 127;
            }
            m_audioOut.remove(0, 4 + n);
        } else {
            break;
        }
    }
}

void ZrtcMedia::sendAudioFrame(const QByteArray& opus, int level) {
    if (!m_active || !m_activeServer || (!m_srtp && !m_plainMedia)) return;
    const uint32_t ssrc = u32mod(m_params.localUid);
    RtpHeaderArgs a;
    a.seq = uint16_t(m_seq);
    a.ts = m_ts;
    a.ssrc = ssrc;
    a.twSeq = m_twSeq;
    a.level = level;
    const QByteArray hdr = m_group ? buildGroupAudioRtpHeader(a) : buildAudioRtpHeader(a);
    const QByteArray rtp = m_plainMedia ? hdr + opus : protectRtp(hdr, opus, *m_srtp, uint16_t(m_seq), ssrc, m_roc);
    const uint32_t callId = u32mod(m_params.callId);
    const bool callee = m_params.role == QLatin1String("callee");
    sendTo(wrapMediaPacket(MsgType::AudioRtp, m_serverToken, rtp), *m_activeServer);
    if (m_p2pPath) sendTo(wrapP2pAudio(callee, callId, rtp), *m_p2pPath);
    if (m_sendRate) m_sendRate->onSent(m_twSeq);
    m_counters.tx++;
    m_audioOctets += uint64_t(opus.size());
    noteAudioTx();
    m_seq = (m_seq + 1) & 0xffff;
    if (m_seq == 0) m_roc++;
    m_ts = (m_ts + kAudioTs) & 0xffffffffu;
    m_twSeq = uint16_t(m_twSeq + 1);
}

void ZrtcMedia::noteAudioTx() {
    const double now = nowMs();
    if (m_aTxLast) m_aTxMaxGap = std::max(m_aTxMaxGap, now - m_aTxLast);
    m_aTxLast = now;
    m_aTxN++;
    if (!m_aTxSince) m_aTxSince = now;
    if (now - m_aTxSince < kAudioTxLogMs) return;
    zlog(QStringLiteral("audio tx: %1 frames in %2 s, max gap %3 ms, relay%4")
             .arg(m_aTxN)
             .arg(js::toFixed1((now - m_aTxSince) / 1000.0))
             .arg(js::numberString(std::round(m_aTxMaxGap)))
             .arg(m_p2pPath ? QStringLiteral(" + P2P") : QString()));
    m_aTxN = 0;
    m_aTxMaxGap = 0;
    m_aTxSince = now;
}

void ZrtcMedia::setGroupCamera(bool on) { m_groupCameraOn = on; }

bool ZrtcMedia::groupCameraSend() const { return m_group && m_plainMedia && !envIs("ZCALL_GROUP_CAMERA", "0"); }

void ZrtcMedia::applySendProfile(const SendProfile& p) {
    if (m_group && m_plan) {
        const int kbps = int(std::lround(p.bitrate / 1000.0));
        m_plan->encode = EncodeProfile{p.width, p.height, int(p.fps), int64_t(p.bitrate), int64_t(m_plan->encode.keyMs)};
        m_plan->send = {VidLayer{0, uint32_t(kbps), uint32_t(p.width), uint32_t(p.height)}};
        m_vidCfgLogged = false;
        if (m_active && m_activeServer) sendVidQualConfig();
    }
    if (m_listener) m_listener->mediaSendProfile(p);
}

void ZrtcMedia::sendVideoFrame(const OutVideoFrame& f) {
    if (m_group && f.screen) return;
    const bool groupCam = groupCameraSend();
    if (!m_active || !m_activeServer || !m_startedAt) return;
    if (!m_srtp && !groupCam) return;
    if (!f.key && !m_vKeySent) {
        if (m_listener) m_listener->mediaKeyframe(QStringLiteral("start"));
        return;
    }
    if (f.key) m_vKeySent = true;
    const uint32_t ssrc = u32mod(m_params.localUid);
    const double now = nowMs();
    if (!m_vStart) m_vStart = now;
    const uint32_t ts = m_vTsBase + u32mod(std::floor(now - m_vStart) * 90.0);
    const QVector<QByteArray> chunks = packetizeFrame(f.data);
    const bool sfuOn = groupCam && m_plan && m_plan->hasSfu;
    const SfuExt sfu{m_plan ? m_plan->sfuId : 14, m_plan ? m_plan->sfuLong : true};
    const double captureMs = now;
    const bool callee = m_params.role == QLatin1String("callee");
    const uint32_t callId = u32mod(m_params.callId);
    for (int i = 0; i < chunks.size(); ++i) {
        RtpHeaderArgs a;
        a.seq = uint16_t(m_vSeq);
        a.ts = ts;
        a.ssrc = ssrc;
        a.twSeq = m_twSeq;
        a.marker = i == chunks.size() - 1;
        a.pt = f.key ? kKeyPt : kDeltaPt;
        if (sfuOn) {
            m_vLayerSeq = (m_vLayerSeq + 1) & 0xffff;
            a.layerSeq = uint16_t(m_vLayerSeq);
        }
        a.captureMs = captureMs;
        a.sendMs = nowMs();
        const QByteArray hdr = sfuOn ? buildGroupVideoRtpHeader(a, sfu) : buildAudioRtpHeader(a);
        const QByteArray rtp = groupCam ? hdr + chunks[i] : protectRtp(hdr, chunks[i], *m_srtp, uint16_t(m_vSeq), ssrc, m_vRoc);
        const QByteArray relay = wrapMediaPacket(MsgType::VideoRtp, m_serverToken, rtp);
        if (!m_group && (chunks.size() > kVideoBurst || !m_vOut.isEmpty())) {
            m_vOut.append(Queued{relay, *m_activeServer});
            if (m_p2pPath) m_vOut.append(Queued{wrapP2pVideo(callee, callId, rtp), *m_p2pPath});
        } else {
            sendTo(relay, *m_activeServer);
            if (m_p2pPath) sendTo(wrapP2pVideo(callee, callId, rtp), *m_p2pPath);
        }
        m_vTxBytes += uint64_t(rtp.size());
        m_vTxPkts++;
        if (groupCam) m_vTxLastTx = nowMs();
        if (m_sendRate) m_sendRate->onSent(m_twSeq);
        m_vSeq = (m_vSeq + 1) & 0xffff;
        if (m_vSeq == 0) m_vRoc++;
        m_twSeq = uint16_t(m_twSeq + 1);
    }
    if (!m_vOut.isEmpty()) drainVideo();
    if (!m_group) noteVideoTx(f.key);
}

void ZrtcMedia::drainVideo() {
    if (m_vDraining) return;
    m_vDraining = true;
    drainStep();
}

void ZrtcMedia::drainStep() {
    if (!m_active) {
        m_vOut.clear();
        m_vDraining = false;
        return;
    }
    for (int n = 0; n < kVideoBurst * 2 && !m_vOut.isEmpty(); ++n) {
        const Queued q = m_vOut.takeFirst();
        sendTo(q.pkt, q.dst);
    }
    if (!m_vOut.isEmpty()) {
        QTimer::singleShot(1, this, [this] { drainStep(); });
    } else {
        m_vDraining = false;
    }
}

void ZrtcMedia::noteVideoTx(bool key) {
    const double now = nowMs();
    m_vTxFrames++;
    if (key) m_vTxKeys++;
    m_vTxLastTx = now;
    if (!m_vTxSince) m_vTxSince = now;
    if (now - m_vTxSince < kVideoTxLogMs) return;
    const double sec = (now - m_vTxSince) / 1000.0;
    zlog(QStringLiteral("video tx: %1 fps, %2 kbps, %3 key, %4 pkts%5")
             .arg(js::toFixed1(m_vTxFrames / sec))
             .arg(std::round(m_vTxBytes * 8 / sec / 1000))
             .arg(m_vTxKeys)
             .arg(m_vTxPkts)
             .arg(m_sendRate ? QStringLiteral(", rung %1").arg(js::string(m_sendRate->profile().rung)) : QString()));
    m_vTxFrames = 0;
    m_vTxKeys = 0;
    m_vTxBytes = 0;
    m_vTxPkts = 0;
    m_vTxSince = now;
}

void ZrtcMedia::sendRtcp(bool video, const QByteArray& rtcp) {
    if (!m_active || !m_activeServer || !m_startedAt) return;
    sendTo(wrapMediaPacket(video ? MsgType::VideoFec : MsgType::AudioFec, m_serverToken, rtcp), *m_activeServer);
    if (m_p2pPath)
        sendTo(wrapP2pRtcp(m_params.role == QLatin1String("callee"), u32mod(m_params.callId), video, rtcp), *m_p2pPath);
}

void ZrtcMedia::sendTwcc() {
    if (!m_rtcp) return;
    for (const QByteArray& pk : m_rtcp->twccPackets()) sendRtcp(false, pk);
}

std::optional<double> ZrtcMedia::downlinkLoss() {
    if (!m_rtcp || !m_rtcp->audio().started()) return std::nullopt;
    RxStats& a = m_rtcp->audio();
    const double expected = double(a.cycles() + a.maxSeq() - a.baseSeq() + 1);
    const std::pair<double, double> prev = m_downPrev ? *m_downPrev : std::make_pair(expected, double(a.received()));
    m_downPrev = std::make_pair(expected, double(a.received()));
    const double exp = expected - prev.first;
    const double rec = double(a.received()) - prev.second;
    if (exp < 10) return std::nullopt;
    return std::clamp((exp - rec) / exp, 0.0, 1.0);
}

void ZrtcMedia::sendRtcpReports() {
    if (!m_rtcp) return;
    const double now = nowMs();
    if (m_sendRate) m_sendRate->tick(now - m_vTxLastTx < 1500, downlinkLoss());
    if (m_group) {
        if (now - m_lastRtcpLogAt >= kRtcpLogMs) {
            m_lastRtcpLogAt = now;
            zlog(QStringLiteral("rtcp: group, sent twcc %1, nack %2, pli %3").arg(m_rtcp->counters().twcc).arg(m_rtcp->counters().nack).arg(m_rtcp->counters().pli));
        }
        return;
    }
    sendRtcp(false, m_rtcp->audioReport(now, m_ts, uint32_t(m_counters.tx), uint32_t(m_audioOctets)));
    const QByteArray v = m_rtcp->videoReport(now);
    if (!v.isEmpty()) sendRtcp(true, v);
    if (now - m_lastRtcpLogAt >= kRtcpLogMs) {
        m_lastRtcpLogAt = now;
        zlog(QStringLiteral("rtcp: sent twcc %1, reports %2, pli %3, nack %4")
                 .arg(m_rtcp->counters().twcc)
                 .arg(m_rtcp->counters().reports)
                 .arg(m_rtcp->counters().pli)
                 .arg(m_rtcp->counters().nack));
    }
}

void ZrtcMedia::peerRtcp(bool video, const QByteArray& buf) {
    if (m_rtcp) m_rtcp->onRtcp(video, buf, nowMs());
    if (m_sendRate) m_sendRate->onRtcp(video ? 'v' : 'a', buf);
}

void ZrtcMedia::requestPeerKeyFrame(const QString& why, const QJsonValue& src) {
    if (!m_rtcp) return;
    const double now = nowMs();
    const double n = js::number(src);
    const uint32_t key = (std::isfinite(n) && n > 0) ? u32mod(n) : 0;
    const double stamp = key ? m_pliAt.value(key, 0) : m_lastPliAt;
    if (now - stamp < kPliMinGapMs) return;
    const QByteArray pli = key ? buildPli(m_rtcp->localSsrc(), key) : m_rtcp->pli();
    if (pli.isEmpty()) return;
    if (key) {
        m_pliAt[key] = now;
        m_rtcp->counters().pli++;
    } else {
        m_lastPliAt = now;
    }
    sendRtcp(true, pli);
    zlog(QStringLiteral("rtcp: PLI to %1 (%2)").arg(key ? QString::number(key) : QStringLiteral("the phone"), why));
}

void ZrtcMedia::onVideoRtcp(const QByteArray& buf) {
    const uint32_t ours = u32mod(m_params.localUid);
    for (int o = 0; o + 12 <= buf.size();) {
        if ((u8at(buf, o) >> 6) != 2) return;
        const int len = (rd16be(buf, o + 2) + 1) * 4;
        const int fmtv = u8at(buf, o) & 0x1f;
        if (u8at(buf, o + 1) == 206 && (fmtv == 1 || (fmtv == 4 && o + 16 <= buf.size()))) {
            const uint32_t target = fmtv == 1 ? rd32be(buf, o + 8) : rd32be(buf, o + 12);
            if (target == ours && m_listener) m_listener->mediaKeyframe(fmtv == 1 ? QStringLiteral("pli") : QStringLiteral("fir"));
        }
        if (len <= 0) return;
        o += len;
    }
}

void ZrtcMedia::onAudio(const QByteArray& rtp) {
    if ((!m_srtp && !m_plainMedia) || rtp.size() < 12) return;
    const uint16_t seq = rd16be(rtp, 2);
    const uint32_t ssrc = rd32be(rtp, 8);
    RocTracker* tracker = &m_rxRoc;
    if (m_group) {
        if (m_params.localUid && ssrc == u32mod(m_params.localUid)) return;
        tracker = &m_rxRocs[ssrc];
    }
    const uint32_t roc = tracker->guess(seq);
    std::optional<QByteArray> opus;
    if (m_plainMedia) {
        if ((u8at(rtp, 0) >> 6) == 2) {
            if (const auto p = parseRtp(rtp)) {
                if (rtp.size() > p->headerLen) opus = rtp.mid(p->headerLen);
            }
        }
    } else if (const auto got = unprotectRtp(rtp, *m_srtp, roc)) {
        opus = got->payload;
    }
    if (!opus) {
        m_counters.rxBad++;
        return;
    }
    tracker->update(seq, roc);
    if (m_group) {
        const int n = m_members.value(ssrc, 0) + 1;
        m_members[ssrc] = n;
        if (n == 1) {
            zlog(QStringLiteral("media: first audio from member %1").arg(ssrc));
            if (m_listener) m_listener->mediaMemberAudio(ssrc);
        }
        noteSpeaking(ssrc, rtp);
    }
    if (m_rtcp) m_rtcp->onRtp(false, rtp, nowMs());
    m_counters.rx++;
    m_counters.rxSecond++;
    m_lastRxAt = nowMs();
    if (m_counters.rx == 1) zlog(QStringLiteral("media: first audio from peer"));
    if (!m_startedAt && !m_group) {
        if (m_counters.rx == 1 && m_listener) m_listener->mediaEarlyAudio(QStringLiteral("first"));
        if (m_counters.rx == 50 && m_listener) m_listener->mediaEarlyAudio(QStringLiteral("stream"));
    }
    if (m_audio) {
        QByteArray h(11, '\0');
        h[0] = char(0x46);
        wr32be(h, 1, uint32_t(roc * 0x10000u + seq));
        wr32be(h, 5, ssrc);
        wr16be(h, 9, uint32_t(opus->size()));
        m_audio->write(h + *opus);
    }
}

void ZrtcMedia::noteSpeaking(uint32_t ssrc, const QByteArray& rtp) {
    const auto level = rtpAudioLevel(rtp);
    if (!level) return;
    const double now = nowMs();
    Speak& sp = m_speak[ssrc];
    const bool loud = *level <= kSpeakDbov;
    sp.hist.push_back(loud);
    if (sp.hist.size() > 10) sp.hist.removeFirst();
    if (loud) sp.lastLoud = now;
    const int loudCount = int(std::count(sp.hist.begin(), sp.hist.end(), true));
    const bool on = sp.on ? now - sp.lastLoud < 600 : loudCount >= 4;
    if (on != sp.on) {
        sp.on = on;
        if (m_listener) m_listener->mediaMemberSpeaking(ssrc, on);
    }
}

void ZrtcMedia::onVideo(const QByteArray& rtp) {
    if ((!m_srtp && !m_plainMedia) || rtp.size() < 12) return;
    const uint16_t seq = rd16be(rtp, 2);
    RocTracker* tracker = &m_videoRoc;
    if (m_group) {
        const uint32_t ssrc = rd32be(rtp, 8);
        if (m_params.localUid && ssrc == u32mod(m_params.localUid)) return;
        tracker = &m_videoRocs[ssrc];
    }
    const uint32_t roc = tracker->guess(seq);
    std::optional<QByteArray> payload;
    uint32_t ssrc = rd32be(rtp, 8);
    if (m_plainMedia) {
        if ((u8at(rtp, 0) >> 6) == 2) {
            if (const auto p = parseRtp(rtp)) {
                if (rtp.size() > p->headerLen) payload = rtp.mid(p->headerLen);
            }
        }
    } else if (const auto got = unprotectRtp(rtp, *m_srtp, roc)) {
        payload = got->payload;
        ssrc = got->ssrc;
    }
    if (!payload) {
        m_counters.rxBad++;
        return;
    }
    tracker->update(seq, roc);
    m_lastRxAt = nowMs();
    if (m_rtcp) {
        m_rtcp->onRtp(true, rtp, m_lastRxAt);
        if (m_nack) {
            for (const NackRequest& item : m_nack->note(ssrc, seq, m_lastRxAt)) {
                sendRtcp(true, buildNack(m_rtcp->localSsrc(), item.ssrc, item.seqs));
                m_rtcp->counters().nack++;
            }
        }
    }
    const int pt = u8at(rtp, 1) & 0x7f;
    const bool marker = (u8at(rtp, 1) & 0x80) != 0;
    VideoPacket pkt;
    pkt.seq = seq;
    pkt.ts = rd32be(rtp, 4);
    pkt.pt = pt;
    pkt.marker = marker;
    pkt.payload = *payload;
    pkt.roc = roc;
    pkt.ssrc = ssrc;
    if (m_listener) m_listener->mediaVideo(pkt);
    if (m_group) noteVideoMember(ssrc);
}

void ZrtcMedia::noteVideoMember(uint32_t ssrc) {
    if (!ssrc || ssrc == u32mod(m_params.localUid)) return;
    if (m_vidWanted.contains(ssrc)) return;
    m_vidWanted.insert(ssrc);
    sendVidQual();
}

int ZrtcMedia::qualityIdFor(uint32_t uid) const {
    if (envIs("ZCALL_GROUP_QUALITY", "0") || !m_plan || m_plan->layers.isEmpty()) return 0xff;
    const uint32_t width = m_renderWidth.value(uid, 0);
    const QVector<VidLayer>& layers = m_plan->layers;
    QVector<VidLayer> spatial;
    QSet<uint32_t> seen;
    for (const VidLayer& L : layers) {
        const uint32_t s = L.id / 3;
        if (seen.contains(s)) continue;
        seen.insert(s);
        spatial.push_back(L);
    }
    if (!width) return spatial.first().id & 0xff;
    int top = int(spatial.size());
    if (spatial.size() >= 3) {
        const VidLayer& hi = spatial.last();
        if (width < std::max(hi.b, hi.c)) top = int(spatial.size()) - 1;
    }
    VidLayer pick = spatial.first();
    for (int i = top - 1; i >= 0; --i) {
        const VidLayer& L = spatial[i];
        if (std::min(L.b, L.c) <= width) {
            pick = L;
            break;
        }
    }
    return int(pick.id & 0xff);
}

void ZrtcMedia::sendVidQual() {
    if (!m_group || !m_activeServer || m_vidWanted.isEmpty()) return;
    QVector<VidQualEntry> entries;
    for (uint32_t uid : m_vidWanted) entries.push_back(VidQualEntry{uid, qualityIdFor(uid)});
    sendTo(buildGroupVidQualReq(u32mod(m_params.localUid), m_serverToken, m_ctlSeq++, entries), *m_activeServer);
    for (const VidQualEntry& e : entries) {
        const auto it = m_vidAsked.find(e.uid);
        if (it != m_vidAsked.end() && *it == e.qualityId) continue;
        m_vidAsked[e.uid] = e.qualityId;
        zlog(QStringLiteral("media: VidQualReq member %1: quality id 0x%2").arg(e.uid).arg(QString::number(e.qualityId, 16)));
    }
}

void ZrtcMedia::sendVidQualConfig() {
    if (!m_group || !m_activeServer || !m_plan || m_plan->layers.isEmpty()) return;
    QVector<VidLayer> layers = m_plan->send;
    if (layers.isEmpty()) {
        for (const VidLayer& L : m_plan->layers)
            if (L.id == 0) layers.push_back(L);
    }
    const QByteArray pkt = buildGroupVidQualConfig(u32mod(m_params.localUid), m_serverToken, m_ctlSeq++, layers);
    sendTo(pkt, *m_activeServer);
    if (!m_vidCfgLogged) {
        m_vidCfgLogged = true;
        const EncodeProfile& e = m_plan->encode;
        zlog(QStringLiteral("media: VidQualConfig %1 layer(s), encode %2x%3 %4 bps %5 fps")
                 .arg(layers.size())
                 .arg(e.width)
                 .arg(e.height)
                 .arg(e.bitrate)
                 .arg(e.fps));
    }
}

std::optional<int> ZrtcMedia::vidAsked(uint32_t uid) const {
    const auto it = m_vidAsked.find(uid);
    if (it == m_vidAsked.end()) return std::nullopt;
    return *it;
}

void ZrtcMedia::setMemberRenderWidth(uint32_t uid, double width) {
    const uint32_t w = u32mod(std::round(width));
    if (!uid || !m_group) return;
    if (m_renderWidth.value(uid, UINT32_MAX) == w) return;
    m_renderWidth[uid] = w;
    if (m_vidWanted.contains(uid) && qualityIdFor(uid) != m_vidAsked.value(uid, -1)) sendVidQual();
}

void ZrtcMedia::sendPing() {
    if (m_group) {
        const bool videoOn = groupCameraSend() && m_groupCameraOn;
        if (m_activeServer) {
            sendTo(buildZaviPingPacket(u32mod(m_params.localUid), m_serverToken, m_params.sessId.toUtf8(), videoOn), *m_activeServer);
            sendVidQual();
            sendVidQualConfig();
        }
        return;
    }
    if (m_activeServer)
        sendTo(buildPingPacket(u32mod(m_params.localUid), m_serverToken, m_params.sessId.toUtf8()), *m_activeServer);
}

void ZrtcMedia::sendStatus() {
    const uint32_t rx = uint32_t(m_counters.rxSecond);
    m_counters.rxSecond = 0;
    if (m_activeServer)
        sendTo(buildReqForwardPacket(u32mod(m_params.localUid), m_serverToken, m_fwdSeq++, 10, reqForwardStatus(rx)), *m_activeServer);
}

void ZrtcMedia::onGroupControl(const Unwrapped& p) {
    const uint32_t ours = u32mod(m_params.localUid);
    if (p.cmd == Cmd::RoomUpdated) {
        if (p.uid != ours) return;
        const auto uids = parseRoomUpdated(p.data);
        if (!uids) return;
        QVector<uint32_t> filtered;
        for (uint32_t u : *uids)
            if (u && u != ours) filtered.push_back(u);
        zlog(QStringLiteral("media: room %1").arg(uids->size()));
        if (m_listener) m_listener->mediaRoster(filtered);
        return;
    }
    if (p.uid == ours) {
        if (p.subCmd != 1 && p.subCmd != 2) {
            zlog(QStringLiteral("media: CMD_CLOSE about us, reason %1 (ignored, as ZaloCall)").arg(p.subCmd));
            return;
        }
        zlog(QStringLiteral("media: removed from the room, reason %1").arg(p.subCmd));
        if (m_listener) m_listener->mediaPeerEnd(p.subCmd == 2 ? QStringLiteral("kicked") : QStringLiteral("host-ended"));
    } else {
        zlog(QStringLiteral("media: member left %1 reason %2").arg(p.uid).arg(p.subCmd));
        if (m_listener) m_listener->mediaMemberLeft(p.uid);
    }
}

void ZrtcMedia::onUdp(const QByteArray& msg, const QHostAddress& addr, quint16 port) {
    if (msg.isEmpty()) return;
    const int t = u8at(msg, 0);
    if (t == MsgType::P2pExt) {
        if (m_active && m_hasParams) onP2p(msg, addr, port);
        return;
    }
    if (t == MsgType::VideoFec && msg.size() > 5) {
        if (m_active && m_startedAt) {
            onVideoRtcp(msg.mid(5));
            peerRtcp(true, msg.mid(5));
        }
        return;
    }
    if (t == MsgType::AudioFec && msg.size() > 5) {
        if (m_active && m_startedAt) peerRtcp(false, msg.mid(5));
        return;
    }
    const auto p = unwrapPacket(msg);
    if (!p) return;
    const Server from{addr.toString(), port, {}};
    if (p->msgType == MsgType::ServerReply && (p->cmd == Cmd::InitCallCaller || p->cmd == Cmd::InitCallCallee)) {
        const bool known = std::any_of(m_servers.begin(), m_servers.end(), [&from](const Server& s) { return s.host == from.host && s.port == from.port; });
        if (!known) return;
        Server server;
        for (const Server& s : m_servers)
            if (s.host == from.host && s.port == from.port) server = s;
        const bool replied = std::any_of(m_replies.begin(), m_replies.end(), [&server](const Reply& r) { return r.server == server; });
        if (replied) return;
        if (p->res && *p->res != 0) {
            const bool refused = std::any_of(m_refused.begin(), m_refused.end(), [&server](const Server& s) { return s == server; });
            if (refused) return;
            zlog(QStringLiteral("media: InitZRTP refused, res = %1").arg(*p->res));
            m_refused.push_back(server);
            checkInitProgress();
            return;
        }
        Reply r;
        r.server = server;
        r.token = p->serverToken.value_or(0);
        r.rttMs = std::round(nowMs() - m_initSentAt);
        r.publicAddr = p->publicAddr;
        r.hex = hexOf(msg);
        m_replies.push_back(r);
        checkInitProgress();
        return;
    }
    if (m_group && p->msgType == MsgType::ServerReply && (p->cmd == Cmd::EndCall || p->cmd == Cmd::RoomUpdated)) {
        if (m_active && m_startedAt) onGroupControl(*p);
        return;
    }
    if (p->msgType == MsgType::ServerReply && p->cmd == Cmd::EndCall) {
        const bool activeFrom = m_activeServer && m_activeServer->host == from.host && m_activeServer->port == from.port;
        const uint32_t got = p->callId.value_or(0);
        const uint32_t ours = u32mod(m_params.callId);
        const bool isPeer = activeFrom && (!got || !ours || got == ours);
        if (isPeer) {
            if (m_active && m_startedAt) {
                zlog(QStringLiteral("media: EndCall from the active server"));
                if (m_listener) m_listener->mediaPeerEnd(QStringLiteral("server"));
            }
        } else {
            zlog(QStringLiteral("media: EndCall ignored from %1:%2 (not the active server)").arg(from.host).arg(port));
        }
        return;
    }
    if (p->isMedia && p->msgType == MsgType::AudioRtpDown) onAudio(p->payload);
    else if (p->isMedia && p->msgType == MsgType::VideoDown) onVideo(p->payload);
}

}
