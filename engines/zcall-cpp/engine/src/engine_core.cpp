// SPDX-License-Identifier: BSD-3-Clause
#include "engine_core.h"

#include "js.h"
#include "json_cpp.h"
#include "log.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>
#include <utility>

#include <algorithm>
#include <cmath>
#include <utility>

namespace zcall {

namespace {

constexpr double kGroupMediaIdleMs = 10000;
constexpr double kEndedCallTtlMs = 60000;
constexpr size_t kMaxEndedCalls = 256;
const QString kCodec = QStringLiteral("[{\"dynamicFptime\":0,\"frmPtime\":20,\"name\":\"opus/16000/1\",\"payload\":112}]\n");

int envMs(const char* name, int fallback) {
    const double d = js::number(QJsonValue(qEnvironmentVariable(name)));
    if (!qEnvironmentVariableIsSet(name)) return fallback;
    return std::isfinite(d) && d != 0 ? int(d) : fallback;
}

const int kCallSetupTimeoutMs = envMs("ZCALL_SETUP_TIMEOUT_MS", 15000);
const int kRingTimeoutMs = envMs("ZCALL_RING_TIMEOUT_MS", 45000);

QString str(const QJsonValue& v) { return js::string(v); }
QString strOr(const QJsonValue& v, const QString& f = QString()) { return js::stringOr(v, f); }
double num(const QJsonValue& v) { return js::number(v); }
double jsOr0(double d) { return (std::isnan(d) || d == 0) ? 0 : d; }
QString fmtNum(double d) { return js::numberString(d); }
bool isNum(const QJsonValue& v, double n) { return v.isDouble() && v.toDouble() == n; }

}

Member* Call::member(const MemberKey& k) {
    for (auto& e : members)
        if (e.first == k) return &e.second;
    return nullptr;
}

void Call::setMember(const MemberKey& k, const Member& m) {
    if (Member* existing = member(k)) {
        *existing = m;
        return;
    }
    members.push_back({k, m});
}

bool Call::removeMember(const MemberKey& k) {
    for (auto it = members.begin(); it != members.end(); ++it)
        if (it->first == k) {
            members.erase(it);
            return true;
        }
    return false;
}

void HeadlessUi::incoming(const UiIncoming&, AcceptFn onAccept, RejectFn) {
    if (qEnvironmentVariableIsSet("ZCALL_AUTO_ANSWER") && !qEnvironmentVariable("ZCALL_AUTO_ANSWER").isEmpty())
        QTimer::singleShot(100, [onAccept] { onAccept(false); });
}

EngineCore::EngineCore(EmitFn sendFn, MediaBackend* media, CallUi* ui, bool group, QObject* parent)
    : QObject(parent), m_emit(std::move(sendFn)), m_media(media), m_ui(ui), m_groupEnabled(group) {
    m_now = [] { return double(QDateTime::currentMSecsSinceEpoch()); };
    m_random = [] { return QRandomGenerator::global()->generateDouble(); };
    m_ui->setListener(this);
    m_media->setListener(this);
}

EngineCore::~EngineCore() = default;

double EngineCore::newCallId() const { return 1000000000 + std::floor(m_random() * (2147483647.0 - 1000000000.0)); }

void EngineCore::send(const QString& type, const QJsonValue& command, const QJsonObject& data) {
    m_emit(QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("command"), command}, {QStringLiteral("data"), data}});
}

void EngineCore::log(const QString& line) const { zlog(line); }

void EngineCore::later(int ms, std::function<void()> fn) { QTimer::singleShot(ms, this, std::move(fn)); }

QTimer* EngineCore::timer(int ms, bool repeat, std::function<void()> fn) {
    auto* t = new QTimer(this);
    t->setSingleShot(!repeat);
    t->setInterval(ms);
    connect(t, &QTimer::timeout, this, std::move(fn));
    t->start();
    return t;
}

QString EngineCore::selfAvatar() const { return strOr(m_local.value(QStringLiteral("avatar"))); }

void EngineCore::start() { send(QStringLiteral("update"), QStringLiteral("native-ready"), {}); }

void EngineCore::onZaloFrame(const QJsonObject& f) {
    const QJsonValue dv = f.value(QStringLiteral("data"));
    const QJsonObject data = dv.isObject() ? dv.toObject() : QJsonObject();
    const QString type = f.value(QStringLiteral("type")).toString();
    const QJsonValue command = f.value(QStringLiteral("command"));
    if (type == QLatin1String("update")) return onUpdate(str(command), data);
    if (type == QLatin1String("request")) return onRequest(str(command), data);
    if (type == QLatin1String("control")) return onControl(data.value(QStringLiteral("act")), data);
    if (type == QLatin1String("recvSignal")) return onRecvSignal(str(command), data);
    if (type == QLatin1String("response")) {
        if (command.toString() == QLatin1String("getAliasName")) onAliasName(data);
        return;
    }
    log(QStringLiteral("unhandled type %1 %2").arg(str(f.value(QStringLiteral("type"))), str(command)));
}

void EngineCore::onUpdate(const QString& command, const QJsonObject& data) {
    if ((command == QLatin1String("init") || command == QLatin1String("updateLocal")) &&
        js::truthy(data.value(QStringLiteral("local")))) {
        const QJsonObject l = data.value(QStringLiteral("local")).toObject();
        for (auto it = l.begin(); it != l.end(); ++it) m_local.insert(it.key(), it.value());
    }
}

void EngineCore::onRequest(const QString& command, const QJsonObject& data) {
    if (command == QLatin1String("makeCall")) return makeCall(data);
    if (command == QLatin1String("endCall")) return hangup(QStringLiteral("zalo"));
    if (command == QLatin1String("listDevice") || command == QLatin1String("ping") || command == QLatin1String("hide")) return;
    log(QStringLiteral("unhandled request %1").arg(command));
}

void EngineCore::makeCall(const QJsonObject& data) {
    if (m_call) {
        log(QStringLiteral("makeCall while in a call; ignored"));
        return;
    }
    const QJsonValue type = data.value(QStringLiteral("type"));
    if (isNum(type, 5) || isNum(type, 6) || js::truthy(data.value(QStringLiteral("groupInfo")))) {
        const QJsonObject g = data.value(QStringLiteral("groupInfo")).toObject();
        if (m_groupEnabled) {
            groupOutgoing(data);
            return;
        }
        send(QStringLiteral("response"), QStringLiteral("show"), {{"error", 0}, {"extendData", "success"}});
        send(QStringLiteral("update"), QStringLiteral("callState"), {{"state", "free"}});
        groupNotSupported(strOr(g.value("name"), strOr(g.value("groupName"), QStringLiteral("Gọi nhóm"))), strOr(g.value("avatar")));
        return;
    }
    const QJsonValue partners = data.value(QStringLiteral("partner"));
    const QJsonObject partner = partners.isArray() ? partners.toArray().at(0).toObject() : QJsonObject();
    auto c = std::make_shared<Call>();
    c->role = QStringLiteral("caller");
    c->peerId = strOr(partner.value("id"));
    c->peerName = strOr(partner.value("name"));
    c->avatar = strOr(partner.value("avatar"));
    c->type = type;
    c->callId = newCallId();
    c->state = QStringLiteral("calling");
    m_call = c;
    send(QStringLiteral("update"), QStringLiteral("callState"), {{"state", "incall"}});
    send(QStringLiteral("response"), QStringLiteral("show"), {{"error", 0}, {"extendData", "success"}});
    send(QStringLiteral("sendSignal"), 401, {{"callId", c->callId}, {"calleeId", c->peerId}, {"codec", "[]\n"}, {"type", c->type}});
    if (isNum(c->type, 3)) camMic();
    UiOpen o;
    o.title = c->peerName.isEmpty() ? QStringLiteral("Zalo") : c->peerName;
    o.text = QStringLiteral("Đang gọi…");
    o.avatar = c->avatar;
    o.video = isNum(c->type, 3);
    o.selfAvatar = selfAvatar();
    m_ui->open(o);
    if (!c->peerId.isEmpty()) send(QStringLiteral("request"), QStringLiteral("getAliasName"), {{"noisedId", c->peerId}});
    setCallTimeout(c, kCallSetupTimeoutMs);
}

void EngineCore::setCallTimeout(const CallPtr& c, int ms) {
    if (c->ringTimer) {
        c->ringTimer->stop();
        c->ringTimer->deleteLater();
        c->ringTimer = nullptr;
    }
    std::weak_ptr<Call> weak = c;
    c->ringTimer = timer(ms, false, [this, weak] {
        const CallPtr c = weak.lock();
        if (!c || m_call != c || c->state == QLatin1String("connected") || c->state == QLatin1String("ended")) return;
        const bool unreachable = c->state == QLatin1String("calling");
        log(QStringLiteral("outgoing call: %1 (timeout)").arg(unreachable ? QStringLiteral("unreachable / no network") : QStringLiteral("no answer")));
        const QString closeText = unreachable ? QStringLiteral("Người nhận không liên lạc được") : QStringLiteral("Người nhận không trả lời");
        m_ui->status(closeText);
        send(QStringLiteral("sendSignal"), 405,
             {{"callId", c->callId}, {"callType", isNum(c->type, 3) ? 1 : 0}, {"status", 0}, {"toId", c->peerId}});
        finish(103, closeText);
    });
}

void EngineCore::onAliasName(const QJsonObject& d) {
    const CallPtr c = m_call;
    const QString alias = strOr(d.value(QStringLiteral("aliasName"))).trimmed();
    if (!c || c->group || alias.isEmpty() || strOr(d.value(QStringLiteral("noisedId"))) != c->peerId || alias == c->peerName) return;
    c->peerName = alias;
    log(QStringLiteral("alias name for the peer received"));
    if (m_ui->rich()) m_ui->rename(alias);
}

void EngineCore::screenShare(bool on) {
    const CallPtr c = m_call;
    if (!c || c->state != QLatin1String("connected")) return;
    c->sharing = on;
    log(QStringLiteral("screen share %1").arg(on ? QStringLiteral("on") : QStringLiteral("off")));
    if (c->group) {
        if (!on) m_media->stopShare();
        if (on && c->shareScreenId > 0) m_media->startShare(js::toUint32(c->shareScreenId));
        const QJsonObject st = groupState(*c);
        send(QStringLiteral("sendSignal"), 12044,
             {{"audioState", st.value("audioState")},
              {"callId", c->callId},
              {"data", jsonCpp(QJsonObject{{"shareScreenId", c->shareScreenId}})},
              {"hostCall", c->hostCall},
              {"status", on ? 1 : 0},
              {"userId", c->localUid},
              {"videoState", st.value("videoState")}});
        return;
    }
    send(QStringLiteral("sendSignal"), 12064, {{"callId", c->callId}, {"status", on ? 1 : 0}, {"uidTo", c->peerId}});
}

void EngineCore::groupNotSupported(const QString& title, const QString& avatar) {
    const QString text = QStringLiteral("Gọi nhóm chưa được hỗ trợ trên Linux");
    log(QStringLiteral("group call: not supported"));
    UiOpen o;
    o.title = title;
    o.text = text;
    o.avatar = avatar;
    m_ui->open(o);
    later(3000, [this, text] {
        if (!m_call) m_ui->close(text);
    });
}

void EngineCore::incomingGroupNotice(const QJsonObject& d) {
    log(QStringLiteral("incoming group call %1 (not supported)").arg(str(d.value("id"))));
    if (m_call) return;
    auto notice = std::make_shared<int>(0);
    m_groupNotice = notice;
    std::weak_ptr<int> weak = notice;
    auto dismiss = [this, weak] {
        const auto n = weak.lock();
        if (!n || m_groupNotice != n) return;
        m_groupNotice.reset();
        if (!m_call) m_ui->close(QStringLiteral("Đã bỏ qua cuộc gọi nhóm"));
    };
    UiIncoming o;
    o.title = strOr(d.value("groupName"), QStringLiteral("Cuộc gọi nhóm"));
    o.text = QStringLiteral("%1 đang gọi nhóm. Gọi nhóm chưa được hỗ trợ trên Linux.").arg(strOr(d.value("Dname"), QStringLiteral("Ai đó")));
    o.avatar = strOr(d.value("groupAvatar"), strOr(d.value("avatar")));
    o.noAnswer = true;
    m_ui->incoming(o, [dismiss](bool) { dismiss(); }, [dismiss](const QString&) { dismiss(); });
    later(45000, dismiss);
}

void EngineCore::camMic(std::optional<bool> camOn) {
    const CallPtr c = m_call;
    if (!c) return;
    if (camOn) c->camReported = *camOn;
    const int cam = c->camReported ? 1 : 0;
    if (c->group) {
        c->cameraOn = cam == 1;
        m_media->setGroupCamera(cam == 1);
        log(QStringLiteral("group camera %1").arg(*c->cameraOn ? QStringLiteral("on") : QStringLiteral("off")));
        if (c->state == QLatin1String("connected")) groupBroadcast();
        return;
    }
    send(QStringLiteral("sendSignal"), 12006,
         {{"callId", c->callId}, {"calleeId", c->peerId}, {"data", ""}, {"isOnCam", cam}, {"isOnMic", -1}});
}

void EngineCore::micState(bool micOn) {
    const CallPtr c = m_call;
    if (!c || c->state != QLatin1String("connected")) return;
    send(QStringLiteral("sendSignal"), 12006,
         {{"callId", c->callId}, {"calleeId", c->peerId}, {"data", ""}, {"isOnCam", -1}, {"isOnMic", micOn ? 0 : 1}});
}

void EngineCore::onControl(const QJsonValue& actv, const QJsonObject& data) {
    const CallPtr c = m_call;
    const QJsonValue dv = data.value(QStringLiteral("data"));
    const QJsonObject d = dv.isObject() ? dv.toObject() : QJsonObject();
    const QString act = actv.isString() ? actv.toString() : QString();
    if (actv.isString() && act.startsWith(QLatin1String("group_"))) {
        if (m_groupEnabled) {
            onGroupControl(act, d);
        } else if (act == QLatin1String("group_request")) {
            incomingGroupNotice(d);
        } else if (m_groupNotice) {
            m_groupNotice.reset();
            m_ui->close(QStringLiteral("Đã bỏ qua cuộc gọi nhóm"));
        }
        return;
    }
    QJsonValue callId = d.value(QStringLiteral("callId"));
    if (js::nullish(callId)) callId = d.value(QStringLiteral("id"));
    if (js::nullish(callId)) callId = data.value(QStringLiteral("callId"));
    if (js::nullish(callId)) callId = data.value(QStringLiteral("id"));
    const bool hasId = !callId.isUndefined();
    const bool otherCall = c && hasId && str(callId) != fmtNum(c->callId);
    if (act == QLatin1String("request")) return incoming(d);
    if (act == QLatin1String("ring_ring")) {
        if (c && c->state == QLatin1String("calling")) {
            c->state = QStringLiteral("ringing");
            m_ui->status(QStringLiteral("Đang đổ chuông…"));
            setCallTimeout(c, kRingTimeoutMs);
        }
        return;
    }
    if (act == QLatin1String("answer")) {
        if (hasId) rememberEndedCall(callId);
        if (otherCall) return;
        if (c && c->state == QLatin1String("incoming")) {
            log(QStringLiteral("incoming call handled on another device (answer)"));
            finish(0, QString(), false);
            return;
        }
        if (!c || c->role != QLatin1String("caller") || c->state == QLatin1String("connected")) return;
        const QJsonObject params = js::parseMaybeJson(d.value(QStringLiteral("params")));
        const QJsonValue sv = js::truthy(d.value("status")) ? d.value("status") : params.value("status");
        const double sd = js::truthy(sv) ? num(sv) : 0;
        if (sd != 0) {
            log(QStringLiteral("peer declined, answer status %1").arg(fmtNum(sd)));
            finish(160, sd == 3 ? QStringLiteral("Người nhận đã từ chối") : QStringLiteral("Không liên lạc được"));
            return;
        }
        connectCaller(strOr(params.value(QStringLiteral("rtpSerIp"))));
        return;
    }
    if (act == QLatin1String("end_call") || act == QLatin1String("cancel") || act == QLatin1String("reject") ||
        act == QLatin1String("busy")) {
        if (hasId) rememberEndedCall(callId);
        if (otherCall) return;
        if (c && c->state == QLatin1String("incoming")) {
            log(QStringLiteral("incoming call ended before answer: %1").arg(act));
            finish(0, QString(), false);
            return;
        }
        peerEnded(act);
        return;
    }
    if (act == QLatin1String("answer_ack")) return;
    if (act == QLatin1String("onoff_camera") || act == QLatin1String("mute_audio") || act == QLatin1String("share_screen")) {
        if (!c || (js::truthy(d.value("callId")) && str(d.value("callId")) != fmtNum(c->callId))) return;
        const QJsonObject p = js::parseMaybeJson(d.value(QStringLiteral("params")));
        const double type = num(p.contains("type") ? p.value("type") : p.value("status"));
        if (act == QLatin1String("onoff_camera")) c->peerCamOff = type == 0;
        else if (act == QLatin1String("mute_audio")) c->peerMuted = type == 1;
        else {
            c->peerSharing = type == 1;
            log(QStringLiteral("peer share_screen %1").arg(QString::fromUtf8(jsonStringify(p))));
        }
        if (m_ui->rich()) m_ui->peer(c->peerCamOff, c->peerMuted, c->peerSharing);
        return;
    }
    log(QStringLiteral("unhandled control %1").arg(str(actv)));
}

void EngineCore::rememberEndedCall(const QJsonValue& callId) {
    const double id = num(callId);
    if (!std::isfinite(id) || id != std::trunc(id) || std::fabs(id) > 9007199254740991.0 || id <= 0) return;
    pruneEndedCalls();
    m_endedCalls.erase(std::remove_if(m_endedCalls.begin(), m_endedCalls.end(), [id](const auto& e) { return e.first == id; }),
                       m_endedCalls.end());
    m_endedCalls.push_back({id, now()});
    if (m_endedCalls.size() > kMaxEndedCalls) m_endedCalls.erase(m_endedCalls.begin());
}

void EngineCore::pruneEndedCalls() {
    const double cutoff = now() - kEndedCallTtlMs;
    while (!m_endedCalls.empty() && m_endedCalls.front().second <= cutoff) m_endedCalls.erase(m_endedCalls.begin());
}

bool EngineCore::isEndedCall(double id) {
    return std::any_of(m_endedCalls.begin(), m_endedCalls.end(), [id](const auto& e) { return e.first == id; });
}

void EngineCore::incoming(const QJsonObject& d) {
    pruneEndedCalls();
    if (isEndedCall(num(d.value("callId")))) {
        log(QStringLiteral("incoming request for an ended call; ignored"));
        return;
    }
    if (m_call) {
        log(QStringLiteral("incoming call while in a call; ignored"));
        return;
    }
    m_groupNotice.reset();
    const QJsonObject params = js::parseMaybeJson(d.value("params"));
    const QJsonObject ext = js::parseMaybeJson(params.value("extendData"));
    const QJsonValue sa = ext.value("serverAddr");
    const QJsonObject addr = sa.isArray() ? sa.toArray().at(0).toObject() : QJsonObject();
    auto c = std::make_shared<Call>();
    c->role = QStringLiteral("callee");
    c->peerId = strOr(d.value("uidN"));
    c->peerUid = jsOr0(num(d.value("uidFrom")));
    c->localUid = jsOr0(num(d.value("uidTo")));
    c->peerName = strOr(params.value("Dname"));
    c->avatar = strOr(params.value("avatar"));
    c->type = isNum(ext.value("callType"), 1) ? 3 : 1;
    c->callId = num(d.value("callId"));
    c->sessId = js::truthy(d.value("session")) ? d.value("session") : params.value("sessId");
    c->server.insert("rtpaddr", d.value("rtpAddress"));
    c->server.insert("rtcpaddr", d.value("rtcpAddress"));
    c->server.insert("rtpIPv6", strOr(addr.value("rtpIPv6")));
    c->server.insert("rtcpIPv6", strOr(addr.value("rtcpIPv6")));
    c->config = js::truthy(params.value("zrtc_config")) ? params.value("zrtc_config").toObject() : QJsonObject();
    c->p2p = ext.value("p2p").isArray() ? ext.value("p2p").toArray() : QJsonArray();
    c->state = QStringLiteral("incoming");
    m_call = c;
    send(QStringLiteral("update"), QStringLiteral("callState"), {{"state", "incall"}});
    send(QStringLiteral("request"), QStringLiteral("getAliasName"), {{"noisedId", c->peerId}});
    send(QStringLiteral("sendSignal"), 407, {{"callId", c->callId}, {"callerId", c->peerId}});
    UiIncoming o;
    o.title = c->peerName.isEmpty() ? QStringLiteral("Zalo") : c->peerName;
    o.text = isNum(c->type, 3) ? QStringLiteral("Cuộc gọi video đến") : QStringLiteral("Cuộc gọi thoại đến");
    o.avatar = c->avatar;
    o.video = isNum(c->type, 3);
    o.selfAvatar = selfAvatar();
    m_ui->incoming(o, [this](bool camStartOff) { answer(camStartOff); }, [this](const QString& why) { reject(why); });
}

void EngineCore::answer(bool camStartOff) {
    const CallPtr c = m_call;
    if (!c || c->role != QLatin1String("callee") || c->state != QLatin1String("incoming")) return;
    if (camStartOff) c->cameraOff = true;
    c->state = QStringLiteral("connected");
    c->startedAt = now();
    send(QStringLiteral("request"), QStringLiteral("action"), {{"id", 5}});
    NegotiatedParams p;
    p.muted = c->muted;
    p.speakerOff = c->speakerOff;
    p.servers = QJsonArray{c->server};
    p.config = c->config;
    p.srtpMode = c->config.value("srtpMode");
    p.sessId = js::truthy(c->sessId) ? str(c->sessId) : QString();
    p.callId = c->callId;
    p.role = c->role;
    p.localUid = c->localUid;
    p.peerUid = c->peerUid;
    p.p2pCandidates = c->p2p;
    m_media->onNegotiated(p);
    m_ui->status(QStringLiteral("Đang kết nối…"));
    std::weak_ptr<Call> weak = c;
    m_media->prepare(QStringLiteral("callee"), [this, weak](std::optional<InitReply> best) {
        const CallPtr c = weak.lock();
        if (!c || m_call != c || c->state == QLatin1String("ended")) return;
        const QJsonArray p2p = m_media->hasLocalCandidates()
                                   ? m_media->localCandidates(best ? best->publicAddr : std::nullopt)
                                   : QJsonArray();
        QJsonObject extra{{"numServers", 0}, {"p2p", p2p}, {"sP2P", 1}, {"select2side", 1}, {"supportCallBusy", 0}};
        QJsonObject data{{"callId", c->callId},
                         {"callerId", c->peerId},
                         {"codec", kCodec},
                         {"extendData", jsonCpp(extendData(*c, c->server, std::nullopt, extra))},
                         {"session", c->sessId},
                         {"status", 0}};
        data.insert("rtcpAddress", c->server.value("rtcpaddr"));
        data.insert("rtpAddress", c->server.value("rtpaddr"));
        send(QStringLiteral("sendSignal"), 402, data);
        if (!best && m_media->sendUdp()) {
            m_ui->status(QStringLiteral("Không kết nối được máy chủ"));
            hangup(QStringLiteral("no-server"));
            return;
        }
        m_media->start(QStringLiteral("callee"), strOr(c->server.value("rtpaddr")));
        m_ui->status(QStringLiteral("Đã kết nối"), c->startedAt);
        if (c->cameraOff) camMic(false);
    });
}

void EngineCore::reject(const QString& why) {
    const CallPtr c = m_call;
    if (!c || c->role != QLatin1String("callee") || c->state != QLatin1String("incoming")) return;
    log(QStringLiteral("reject: %1").arg(why));
    if (why == QLatin1String("no-device")) {
        finish(0, QString(), false);
        return;
    }
    send(QStringLiteral("request"), QStringLiteral("action"), {{"id", 8}});
    send(QStringLiteral("sendSignal"), 402,
         {{"callId", c->callId},
          {"callerId", c->peerId},
          {"codec", "[]\n"},
          {"extendData", "{}\n"},
          {"rtcpAddress", ""},
          {"rtpAddress", ""},
          {"session", c->sessId},
          {"status", 3}});
    finish(160);
}

void EngineCore::requestKeyFrame(const QString& why) {
    const CallPtr c = m_call;
    if (!c || c->state != QLatin1String("connected") || !m_ui->rich()) return;
    const double t = now();
    if (c->keyAskedAt != 0 && t - c->keyAskedAt < 500) return;
    c->keyAskedAt = t;
    if (why != QLatin1String("start")) log(QStringLiteral("key frame requested: %1").arg(why));
    m_ui->requestKeyFrame();
}

void EngineCore::askPeerKey(const QString& why, const QJsonValue& src) {
    const CallPtr c = m_call;
    if (!c || c->state != QLatin1String("connected")) return;
    m_media->requestPeerKeyFrame(why, src);
}

void EngineCore::noteVideoRx(Call& c) {
    const double t = now();
    if (!c.hasVideoRx) {
        c.hasVideoRx = true;
        c.videoRxSince = t;
        c.videoRxFrames = 0;
        c.videoRxTotal = 0;
    }
    ++c.videoRxFrames;
    ++c.videoRxTotal;
    if (t - c.videoRxSince < 5000) return;
    const AssemblerStats& s = c.video->stats;
    log(QStringLiteral("video rx: %1 fps, %2 frames, %3 key, %4 gaps, %5 dropped")
            .arg(js::toFixed1(c.videoRxFrames * 1000.0 / (t - c.videoRxSince)))
            .arg(c.videoRxTotal)
            .arg(s.keys)
            .arg(s.gaps)
            .arg(s.dropped));
    c.videoRxSince = t;
    c.videoRxFrames = 0;
}

void EngineCore::noteMemberRx(MemberVideo& v, uint32_t uid) {
    const double t = now();
    if (v.since == 0) v.since = t;
    ++v.frames;
    if (t - v.since < 5000) return;
    const AssemblerStats& s = v.assembler->stats;
    QStringList nal;
    for (const auto& [type, n] : v.assembler->nalBytes) nal << QStringLiteral("%1:%2k").arg(type).arg(js::numberString(double(std::llround(double(n) / 1024.0))));
    v.assembler->nalBytes.clear();
    QString line = QStringLiteral("video rx member %1: %2 fps, %3 frames, %4 key, %5 gaps, %6 dropped, %7 without codec; nal %8; sps %9")
                       .arg(uid)
                       .arg(js::toFixed1(v.frames * 1000.0 / (t - v.since)))
                       .arg(s.frames)
                       .arg(s.keys)
                       .arg(s.gaps)
                       .arg(s.dropped)
                       .arg(v.noCodec)
                       .arg(nal.isEmpty() ? QStringLiteral("-") : nal.join(QLatin1Char(' ')),
                            v.assembler->spsSize7.isEmpty() ? QStringLiteral("-") : v.assembler->spsSize7);
    line += QStringLiteral(", subset sps %1").arg(v.assembler->spsSize15.isEmpty() ? QStringLiteral("-") : v.assembler->spsSize15);
    if (m_media->hasVidAsked())
        line += QStringLiteral("; asked quality 0x%1").arg(QString::number(m_media->vidAsked(uid).value_or(0xff), 16));
    log(line);
    v.since = t;
    v.frames = 0;
}

void EngineCore::onVideoPacket(const VideoPacket& pkt) {
    const CallPtr c = m_call;
    if (!c || c->state != QLatin1String("connected") || !m_ui->rich()) return;
    if (c->group) {
        if (double(pkt.ssrc) == c->localUid || (c->shareScreenId != 0 && double(pkt.ssrc) == c->shareScreenId)) return;
        auto it = c->videos.find(pkt.ssrc);
        if (it == c->videos.end()) {
            const uint32_t ssrc = pkt.ssrc;
            MemberVideo& v = c->videos[ssrc];
            MemberVideo* vp = &v;
            std::weak_ptr<Call> weak = c;
            v.assembler = std::make_unique<VideoAssembler>(
                [this, vp, ssrc, weak](const VideoFrame& f) {
                    if (!weak.lock()) return;
                    if (f.key) {
                        const QString codec = avcCodecString(f.data);
                        if (!codec.isEmpty()) vp->codec = codec;
                    }
                    if (vp->codec.isEmpty()) {
                        ++vp->noCodec;
                        return;
                    }
                    noteMemberRx(*vp, ssrc);
                    m_ui->video(UiVideo{f.key, vp->codec, f.data, ssrc});
                },
                [this] { return now(); }, true, [this, ssrc] { askPeerKey(QStringLiteral("gap"), QJsonValue(double(ssrc))); });
            it = c->videos.find(ssrc);
        }
        it->second.assembler->push(pkt);
        return;
    }
    if (!c->video) {
        Call* cp = c.get();
        c->video = std::make_unique<VideoAssembler>(
            [this, cp](const VideoFrame& f) {
                if (f.key) {
                    const QString codec = avcCodecString(f.data);
                    if (!codec.isEmpty()) cp->videoCodec = codec;
                }
                noteVideoRx(*cp);
                m_ui->video(UiVideo{f.key, cp->videoCodec.isEmpty() ? QStringLiteral("avc1.64001e") : cp->videoCodec, f.data, std::nullopt});
            },
            [this] { return now(); }, false, [this] { askPeerKey(QStringLiteral("gap")); });
    }
    c->video->push(pkt);
}

void EngineCore::connectCaller(const QString& rtpSerIp) {
    const CallPtr c = m_call;
    if (!c || c->role != QLatin1String("caller") || c->state == QLatin1String("connected") || c->state == QLatin1String("ended")) return;
    if (c->ringTimer) {
        c->ringTimer->stop();
        c->ringTimer->deleteLater();
        c->ringTimer = nullptr;
    }
    c->state = QStringLiteral("connected");
    c->startedAt = now();
    m_media->start(c->role, rtpSerIp);
    send(QStringLiteral("sendSignal"), 408, {{"callId", c->callId}, {"calleeId", c->peerId}});
    if (isNum(c->type, 3)) {
        camMic();
        std::weak_ptr<Call> weak = c;
        later(2000, [this, weak] {
            const CallPtr c = weak.lock();
            if (c && m_call == c && c->state == QLatin1String("connected")) camMic();
        });
    }
    m_ui->status(QStringLiteral("Đã kết nối"), c->startedAt);
}

void EngineCore::earlyAudio(const QString& phase) {
    const CallPtr c = m_call;
    if (!c || c->group || c->role != QLatin1String("caller") || c->state == QLatin1String("connected") || c->state == QLatin1String("ended")) return;
    if (phase == QLatin1String("first") && c->state == QLatin1String("calling")) {
        c->state = QStringLiteral("ringing");
        m_ui->status(QStringLiteral("Đang đổ chuông…"));
        log(QStringLiteral("peer audio during setup, no ring signal yet"));
        setCallTimeout(c, kRingTimeoutMs);
        return;
    }
    if (phase == QLatin1String("stream")) {
        log(QStringLiteral("answer signal missing, connecting on peer audio"));
        connectCaller(QString());
    }
}

void EngineCore::audioFault(const QString& kind) {
    const CallPtr c = m_call;
    if (!c || c->state == QLatin1String("ended")) return;
    if (kind == QLatin1String("audio")) {
        c->audioFaults.insert(QStringLiteral("mic"));
        c->audioFaults.insert(QStringLiteral("speaker"));
    } else {
        c->audioFaults.insert(kind);
    }
    const bool mic = c->audioFaults.contains(QStringLiteral("mic"));
    const bool speaker = c->audioFaults.contains(QStringLiteral("speaker"));
    const QString text = (mic && speaker ? QStringLiteral("Không mở được micro và loa")
                          : mic          ? QStringLiteral("Không mở được micro, bên kia không nghe thấy bạn")
                                         : QStringLiteral("Không phát được âm thanh ra loa")) +
                         QStringLiteral(". Kiểm tra PipeWire / PulseAudio.");
    if (text == c->audioNotice) return;
    c->audioNotice = text;
    QStringList faults;
    for (const QString& k : {QStringLiteral("mic"), QStringLiteral("speaker")})
        if (c->audioFaults.contains(k)) faults << k;
    log(QStringLiteral("audio fault: %1").arg(faults.join(QLatin1Char(' '))));
    if (m_ui->rich()) m_ui->notice(text);
}

void EngineCore::peerEnded(const QString& why) {
    const CallPtr c = m_call;
    if (!c || c->state == QLatin1String("ended")) return;
    log(QStringLiteral("peer ended: %1").arg(why));
    if (c->state == QLatin1String("connected")) {
        send(QStringLiteral("request"), QStringLiteral("showNotification"),
             {{"isGroup", false}, {"reason", "partner_endcall"}, {"toUid", c->peerId}, {"userId", c->peerId}});
    }
    finish(c->state == QLatin1String("connected") ? 50 : 103);
}

void EngineCore::onRecvSignal(const QString& command, const QJsonObject& data) {
    const CallPtr c = m_call;
    if (command == QLatin1String("401")) {
        if (!c || c->state != QLatin1String("calling")) return;
        const QJsonValue servers = data.value("servers");
        if (js::truthy(data.value("status")) || !servers.isArray() || servers.toArray().isEmpty()) {
            log(QStringLiteral("recvSignal 401 without servers %1 %2").arg(str(data.value("status")), str(data.value("msg"))));
            m_ui->status(QStringLiteral("Không gọi được"));
            finish(103);
            return;
        }
        c->sessId = data.value("sessId");
        c->localUid = jsOr0(num(data.value("fromId")));
        if (c->localUid == 0) c->localUid = jsOr0(num(m_local.value("id")));
        const QJsonValue zc = data.value("zrtc_config");
        NegotiatedParams p;
        p.muted = c->muted;
        p.speakerOff = c->speakerOff;
        p.servers = servers.toArray();
        p.config = js::truthy(zc) ? zc.toObject() : QJsonObject();
        p.srtpMode = js::truthy(zc) ? zc.toObject().value("srtpMode") : QJsonValue();
        p.sessId = js::truthy(c->sessId) ? str(c->sessId) : QString();
        p.callId = c->callId;
        p.role = c->role;
        p.localUid = c->localUid;
        p.peerUid = jsOr0(num(data.value("toId")));
        m_media->onNegotiated(p);
        std::weak_ptr<Call> weak = c;
        const QJsonObject first = servers.toArray().at(0).toObject();
        m_media->prepare(c->role, [this, weak, first](std::optional<InitReply> best) {
            const CallPtr c = weak.lock();
            if (!c || m_call != c || c->state == QLatin1String("ended")) return;
            const QJsonObject chosen = best ? best->server.raw : first;
            if (!best && m_media->sendUdp()) {
                m_ui->status(QStringLiteral("Không kết nối được máy chủ"));
                hangup(QStringLiteral("no-server"));
                return;
            }
            send416(c, chosen, best ? best->publicAddr : std::nullopt);
        });
        return;
    }
    if (command == QLatin1String("12433")) {
        if (c && c->group && c->role == QLatin1String("caller") && c->state == QLatin1String("calling")) groupRequestAnswered(data);
        return;
    }
    if (command == QLatin1String("12434") || command == QLatin1String("12437")) {
        if (c && c->group) {
            const QJsonValue src = js::truthy(data.value("params")) ? data.value("params") : data.value("data");
            log(QStringLiteral("recvSignal %1 %2").arg(command, QString::fromUtf8(jsonStringify(js::parseMaybeJson(src))).left(300)));
        }
        return;
    }
    if (command == QLatin1String("12097")) {
        if (!c || !c->group || c->state != QLatin1String("connected")) return;
        const QJsonObject r = js::parseMaybeJson(data.value("data"));
        if (num(r.value("error")) == -3 && c->role == QLatin1String("caller") && !c->answered) return;
        if (r.contains("error") && num(r.value("error")) != 0) {
            const double lastRx = m_media->lastRxAt();
            const double idle = lastRx != 0 ? now() - lastRx : INFINITY;
            const QString msg = strOr(r.value("message"));
            if (idle < kGroupMediaIdleMs) {
                log(QStringLiteral("group ping fail: %1 %2 (media still flowing; staying)").arg(str(r.value("error")), msg));
                return;
            }
            log(QStringLiteral("group ping fail: %1 %2").arg(str(r.value("error")), msg));
            groupFinish(50, QStringLiteral("Cuộc gọi nhóm đã kết thúc"), true);
        }
        return;
    }
    if (command == QLatin1String("409") || command == QLatin1String("405")) {
        if (c && c->state != QLatin1String("ended")) {
            if (c->state == QLatin1String("incoming")) finish(0, QString(), false);
            else peerEnded(QStringLiteral("recvSignal %1").arg(command));
        }
        return;
    }
    static const QStringList acks{"402", "407", "416", "408", "406", "12006", "12064"};
    if (acks.contains(command)) return;
    if (command == QLatin1String("12044")) {
        if (!c || !c->group) return;
        QJsonObject r = js::parseMaybeJson(js::truthy(data.value("data")) ? data.value("data") : data.value("params"));
        if (r.value("params").isString()) r = js::parseMaybeJson(r.value("params"));
        const double id = num(r.value("shareScreenId"));
        log(QStringLiteral("group share response %1").arg(QString::fromUtf8(jsonStringify(r)).left(300)));
        if (!(id > 0)) return;
        const double prev = c->shareScreenId;
        c->shareScreenId = id;
        c->removeMember(MemberKey::ofUid(id));
        c->videos.erase(js::toUint32(id));
        groupStatus();
        if (c->sharing && prev != id) screenShare(true);
        return;
    }
    log(QStringLiteral("recvSignal %1").arg(command));
}

QJsonObject EngineCore::extendData(const Call& c, const QJsonObject& server, const std::optional<QByteArray>& publicAddr,
                                   const QJsonObject& extra) const {
    QJsonArray p2p;
    if (publicAddr && publicAddr->contains('|')) {
        const QList<QByteArray> parts = publicAddr->split('|');
        p2p.append(QJsonObject{{"ip", QString::fromLatin1(parts.value(0))},
                               {"port", js::number(QJsonValue(QString::fromLatin1(parts.value(1))))},
                               {"type", 1}});
    }
    QJsonObject sa;
    sa.insert("rtcp", server.value("rtcpaddr"));
    sa.insert("rtcpIPv6", server.value("rtcpIPv6"));
    sa.insert("rtp", server.value("rtpaddr"));
    sa.insert("rtpIPv6", server.value("rtpIPv6"));
    sa.insert("tpType", 0);
    QJsonObject o{{"callType", isNum(c.type, 3) ? 1 : 0},
                  {"fecTP", 0},
                  {"gccAudio", 1},
                  {"gccEarlyCall", 0},
                  {"gccMode", 1},
                  {"gccSVLR", 1},
                  {"maxFT", 60},
                  {"newZrtc", 1},
                  {"p2p", p2p},
                  {"packetMode", 2},
                  {"platform", 2},
                  {"sP2P", 0},
                  {"serverAddr", QJsonArray{sa}},
                  {"spTcp", 1},
                  {"srtcp", 0},
                  {"srtpMode", 1},
                  {"supportCallBusy", 1},
                  {"supportHevcDecode", 0},
                  {"tpType", 0},
                  {"video", QJsonObject{{"codec", QJsonArray{QJsonObject{{"name", "h264"}, {"payload", 97}}}}}}};
    for (auto it = extra.begin(); it != extra.end(); ++it) o.insert(it.key(), it.value());
    return o;
}

void EngineCore::send416(const CallPtr& c, const QJsonObject& server, const std::optional<QByteArray>& publicAddr) {
    QJsonObject data{{"callId", c->callId},
                     {"calleeId", c->peerId},
                     {"codec", kCodec},
                     {"extendData", jsonCpp(extendData(*c, server, publicAddr))},
                     {"session", c->sessId}};
    data.insert("rtcpAddress", server.value("rtcpaddr"));
    data.insert("rtpAddress", server.value("rtpaddr"));
    send(QStringLiteral("sendSignal"), 416, data);
}

void EngineCore::hangup(const QString& why) {
    const CallPtr c = m_call;
    if (!c || c->state == QLatin1String("ended")) return;
    log(QStringLiteral("hangup: %1").arg(why));
    if (c->group) {
        groupHangup(why);
        return;
    }
    if (c->state == QLatin1String("incoming")) {
        reject(why);
        return;
    }
    if (c->state == QLatin1String("connected")) {
        send(QStringLiteral("sendSignal"), 409, {{"callId", c->callId}, {"toId", c->peerId}});
    } else {
        send(QStringLiteral("sendSignal"), 405,
             {{"callId", c->callId}, {"callType", isNum(c->type, 3) ? 1 : 0}, {"status", 0}, {"toId", c->peerId}});
    }
    finish(c->state == QLatin1String("connected") ? 50 : 103);
}

void EngineCore::onGroupControl(const QString& act, const QJsonObject& d) {
    const CallPtr c = m_call;
    if (act == QLatin1String("group_request")) return groupIncoming(d);
    if (!c || !c->group) return;
    const QJsonValue id = d.contains("id") ? d.value("id") : d.value("callId");
    if (!id.isUndefined() && num(id) != c->callId) return;
    const double who = jsOr0(num(js::truthy(d.value("userId")) ? d.value("userId") : d.value("fromId")));
    if (act == QLatin1String("group_broadcast")) {
        if (who != 0 && who != c->localUid)
            groupMember(who, MemberInfo{num(d.value("callState")) == 3 ? QStringLiteral("incall") : QStringLiteral("ringing"),
                                        num(d.value("audioState")) == 1, num(d.value("videoState")) == 1});
        return;
    }
    if (act == QLatin1String("group_answer")) {
        if (c->state == QLatin1String("incoming") && (who == 0 || who == c->localUid)) {
            log(QStringLiteral("group call over before we answered: answered on another device"));
            groupFinish(0, QStringLiteral("Cuộc gọi nhóm đã kết thúc"), false);
            return;
        }
        if (who != 0 && who != c->localUid) {
            groupMember(who, MemberInfo{QStringLiteral("incall"), std::nullopt, std::nullopt});
            groupGreet(who);
        }
        return;
    }
    if (act == QLatin1String("group_ring_ring")) {
        if (who != 0 && who != c->localUid) {
            const Member* m = c->member(MemberKey::ofUid(who));
            if (!m || m->state != QLatin1String("incall")) groupMember(who, MemberInfo{QStringLiteral("ringing"), std::nullopt, std::nullopt});
        }
        return;
    }
    if (act == QLatin1String("group_end_call") || act == QLatin1String("group_cancel")) {
        if (c->state == QLatin1String("incoming") && (who == 0 || who == c->hostCall || who == c->localUid)) {
            log(QStringLiteral("group call over before we answered: %1 %2")
                    .arg(act, who == c->localUid ? QStringLiteral("handled on another device") : QStringLiteral("cancelled")));
            groupFinish(0, QStringLiteral("Cuộc gọi nhóm đã kết thúc"), false);
            return;
        }
        if (who == c->localUid) {
            log(QStringLiteral("group call: Zalo reports our own end_call"));
            groupFinish(c->state == QLatin1String("connected") ? 50 : 0, QStringLiteral("Cuộc gọi nhóm đã kết thúc"), false);
            return;
        }
        if (who != 0) groupMemberLeft(who, act);
        return;
    }
    log(QStringLiteral("group control %1").arg(act));
}

void EngineCore::groupOutgoing(const QJsonObject& data) {
    const QJsonObject g = data.value("groupInfo").toObject();
    const QJsonValue pv = data.value("partner");
    QJsonArray partners;
    if (pv.isArray()) partners = pv.toArray();
    else if (js::truthy(pv)) partners.append(pv);
    auto c = std::make_shared<Call>();
    QStringList ids;
    std::vector<Call::InvitePartner> invite;
    for (int i = 0; i < partners.size(); ++i) {
        const QJsonObject p = partners[i].toObject();
        const QString id = strOr(p.value("id"));
        if (!id.isEmpty()) ids << id;
        const QString key = js::truthy(p.value("id")) ? str(p.value("id")) : QString::number(i + 1);
        Member m;
        m.name = strOr(p.value("name"));
        m.avatar = strOr(p.value("avatar"));
        m.state = QStringLiteral("calling");
        m.camOff = true;
        c->setMember(MemberKey::ofStr(key), m);
        invite.push_back({m.name, m.avatar, id});
    }
    c->group = true;
    c->role = QStringLiteral("caller");
    c->state = QStringLiteral("calling");
    c->callId = newCallId();
    c->groupIdStr = strOr(g.value("id"));
    c->callType = isNum(data.value("type"), 6) ? 1 : 0;
    c->sessId = QString();
    c->title = strOr(g.value("name"), strOr(g.value("groupName"), QStringLiteral("Cuộc gọi nhóm")));
    c->avatar = strOr(g.value("avatar"));
    const double mu = jsOr0(num(g.value("maxUsers")));
    c->maxUsers = mu != 0 ? mu : 8;
    c->invited = ids;
    c->invitePartners = invite;
    c->pingMs = 9000;
    m_call = c;
    log(QStringLiteral("outgoing group call %1: %2 member(s)").arg(fmtNum(c->callId)).arg(ids.size()));
    send(QStringLiteral("update"), QStringLiteral("callState"), {{"state", "incall"}});
    send(QStringLiteral("response"), QStringLiteral("show"), {{"error", 0}, {"extendData", "success"}});
    const QJsonArray idArr = QJsonArray::fromStringList(ids);
    send(QStringLiteral("sendSignal"), 12433,
         {{"callId", c->callId},
          {"data", jsonCpp(QJsonObject{{"extraData", ""},
                                       {"groupAvatar", c->avatar},
                                       {"groupId", c->groupIdStr},
                                       {"groupName", c->title},
                                       {"maxUsers", c->maxUsers},
                                       {"noiseId", jsonCpp(idArr)}})},
          {"groupId", c->groupIdStr},
          {"partners", jsonCpp(idArr)},
          {"typeRequest", 1}});
    QJsonArray initial;
    for (const auto& [k, m] : c->members)
        initial.append(QJsonObject{{"src", k.toJson()}, {"name", m.name}, {"avatar", m.avatar}, {"muted", m.muted}, {"camOff", m.camOff}, {"state", m.state}});
    UiOpen o;
    o.title = c->title;
    o.text = QStringLiteral("Đang gọi nhóm…");
    o.avatar = c->avatar;
    o.video = true;
    o.group = true;
    o.selfAvatar = selfAvatar();
    o.members = initial;
    m_ui->open(o);
    groupMembersToUi();
    std::weak_ptr<Call> weak = c;
    c->ringTimer = timer(envMs("ZCALL_GROUP_RING_MS", 60000), false, [this, weak] {
        const CallPtr c = weak.lock();
        if (c && m_call == c && !c->answered && c->state != QLatin1String("ended")) {
            log(QStringLiteral("group call: nobody answered"));
            groupHangup(QStringLiteral("no-answer"));
        }
    });
}

void EngineCore::groupRequestAnswered(const QJsonObject& data) {
    const CallPtr c = m_call;
    QJsonObject p = js::parseMaybeJson(data.value("data"));
    const QJsonObject params = js::parseMaybeJson(data.value("params"));
    for (auto it = params.begin(); it != params.end(); ++it) p.insert(it.key(), it.value());
    const QJsonObject setting = js::parseMaybeJson(p.value("callSetting"));
    QJsonObject summary{{"keys", QJsonArray::fromStringList(p.keys())}, {"settingKeys", QJsonArray::fromStringList(setting.keys())}};
    summary.insert("status", p.value("status"));
    summary.insert("msg", p.value("msg"));
    log(QStringLiteral("group call request answered: %1").arg(QString::fromUtf8(jsonStringify(summary))));
    const double status = jsOr0(num(p.contains("status") ? p.value("status") : data.value("status")));
    c->hostCall = jsOr0(num(js::truthy(data.value("hostCall")) ? data.value("hostCall") : p.value("hostCall")));
    c->localUid = c->hostCall;
    if (js::truthy(p.value("callId"))) c->callId = num(p.value("callId"));
    if (js::truthy(p.value("groupId")) && num(p.value("groupId")) < 0x7fffffff) c->groupId = num(p.value("groupId"));
    if (js::truthy(p.value("interval"))) {
        const double iv = num(p.value("interval"));
        c->pingMs = iv < 1000 ? iv * 1000 : iv;
    }
    c->sessId = strOr(setting.value("session"), strOr(p.value("session")));
    c->servers = setting.value("servers").isArray() ? setting.value("servers").toArray() : QJsonArray();
    c->config = js::parseMaybeJson(setting.value("zrtcConfig"));
    if (status != 0 || str(c->sessId).isEmpty() || c->servers.isEmpty() || c->localUid == 0) {
        log(QStringLiteral("group call request refused / incomplete"));
        c->state = QStringLiteral("failed");
        const QString msg = strOr(p.value("msg"));
        m_ui->status(!msg.isEmpty() ? QStringLiteral("Không gọi được: %1").arg(msg) : QStringLiteral("Không gọi được nhóm"));
        later(3000, [this] { groupFinish(0, QStringLiteral("Không gọi được nhóm"), false); });
        return;
    }
    c->state = QStringLiteral("connecting");
    NegotiatedParams np;
    np.muted = c->muted;
    np.speakerOff = c->speakerOff;
    np.group = true;
    np.host = true;
    np.servers = c->servers;
    np.config = c->config;
    np.sessId = str(c->sessId);
    np.callId = c->callId;
    np.role = QStringLiteral("caller");
    np.localUid = c->localUid;
    np.hostCall = c->hostCall;
    m_media->onNegotiated(np);
    groupCameraProfile();
    std::weak_ptr<Call> weak = c;
    m_media->prepare(QStringLiteral("caller"), [this, weak](std::optional<InitReply> best) {
        const CallPtr c = weak.lock();
        if (!c || m_call != c || c->state != QLatin1String("connecting")) return;
        if (!best && m_media->sendUdp()) {
            c->state = QStringLiteral("failed");
            m_ui->status(QStringLiteral("Không vào được cuộc gọi nhóm (máy chủ từ chối)"));
            later(3000, [this] { groupFinish(0, QStringLiteral("Không vào được cuộc gọi nhóm"), false); });
            return;
        }
        m_media->start(QStringLiteral("caller"), QString());
        c->state = QStringLiteral("connected");
        c->startedAt = now();
        QJsonObject srv = best && !best->server.raw.isEmpty() ? best->server.raw : c->servers.at(0).toObject();
        auto pick = [&srv](const char* a, const char* b) {
            const QJsonValue v = srv.value(QLatin1String(a));
            if (js::truthy(v)) return v;
            if (b) {
                const QJsonValue w = srv.value(QLatin1String(b));
                if (js::truthy(w)) return w;
            }
            return QJsonValue(QString());
        };
        const QJsonObject inner{{"groupAvatar", c->avatar},
                                {"groupName", c->title},
                                {"hostCall", c->hostCall},
                                {"maxUsers", c->maxUsers},
                                {"noiseId", QJsonArray::fromStringList(c->invited)}};
        const QJsonObject outer{{"codec", ""},
                                {"data", jsonCpp(inner)},
                                {"extendData", ""},
                                {"rtcpAddress", pick("rtcpaddr", nullptr)},
                                {"rtcpAddressIPv6", pick("rtcpaddrIPv6", "rtcpIPv6")},
                                {"rtpAddress", pick("rtpaddr", nullptr)},
                                {"rtpAddressIPv6", pick("rtpaddrIPv6", "rtpIPv6")}};
        send(QStringLiteral("sendSignal"), 12434,
             {{"callId", c->callId},
              {"callType", c->callType},
              {"data", jsonCpp(outer)},
              {"groupId", c->groupIdStr},
              {"partners", jsonCpp(QJsonArray::fromStringList(c->invited))},
              {"session", c->sessId}});
        groupBroadcast();
        c->pingTimer = timer(int(c->pingMs), true, [this] { groupPing(); });
        groupStatus();
    });
}

void EngineCore::groupIncoming(const QJsonObject& d) {
    if (m_call) {
        log(QStringLiteral("group call while in a call; ignored %1").arg(str(d.value("id"))));
        return;
    }
    m_groupNotice.reset();
    const QJsonObject setting = js::parseMaybeJson(d.value("callSetting"));
    const double localUid = jsOr0(num(d.value("receiverId")));
    auto c = std::make_shared<Call>();
    if (d.value("partnerInfo").isArray()) {
        for (const QJsonValue& pv : d.value("partnerInfo").toArray()) {
            const QJsonObject p = pv.toObject();
            const double uid = jsOr0(num(p.value("userId")));
            if (uid == 0 || uid == localUid) continue;
            Member m;
            m.name = strOr(p.value("name"));
            m.avatar = strOr(p.value("avatar"));
            m.state = num(p.value("callState")) == 3 ? QStringLiteral("incall") : QStringLiteral("ringing");
            m.muted = num(p.value("audioState")) == 1;
            m.camOff = num(p.value("videoState")) == 1;
            c->setMember(MemberKey::ofUid(uid), m);
        }
    }
    const double interval = jsOr0(num(d.value("interval")));
    c->group = true;
    c->role = QStringLiteral("callee");
    c->state = QStringLiteral("incoming");
    c->callId = num(d.value("id"));
    c->hostCall = jsOr0(num(d.value("hostCall")));
    c->groupId = jsOr0(num(d.value("groupId")));
    c->localUid = localUid;
    c->callType = num(d.value("callType")) == 1 ? 1 : 0;
    c->sessId = strOr(setting.value("session"), strOr(d.value("session")));
    c->servers = setting.value("servers").isArray() ? setting.value("servers").toArray() : QJsonArray();
    c->config = js::parseMaybeJson(setting.value("zrtcConfig"));
    c->title = strOr(d.value("groupName"), QStringLiteral("Cuộc gọi nhóm"));
    c->avatar = strOr(d.value("groupAvatar"));
    c->pingMs = interval != 0 ? (interval < 1000 ? interval * 1000 : interval) : 10000;
    m_call = c;
    log(QStringLiteral("incoming group call %1: %2 server(s), %3 member(s), session %4 chars")
            .arg(fmtNum(c->callId))
            .arg(c->servers.size())
            .arg(c->members.size())
            .arg(str(c->sessId).size()));
    if (str(c->sessId).isEmpty() || c->servers.isEmpty() || c->localUid == 0) {
        log(QStringLiteral("group_request without session / servers / receiverId; not answering"));
        m_call.reset();
        incomingGroupNotice(d);
        return;
    }
    send(QStringLiteral("update"), QStringLiteral("callState"), {{"state", "incall"}});
    send(QStringLiteral("sendSignal"), 12439,
         {{"callId", c->callId},
          {"data", jsonCpp(QJsonObject{{"callType", 1}, {"extraData", ""}, {"status", 1}})},
          {"hostCall", c->hostCall},
          {"session", c->sessId},
          {"status", 1}});
    const Member* hm = c->member(MemberKey::ofUid(c->hostCall));
    const QString host = hm && !hm->name.isEmpty() ? hm->name : strOr(d.value("Dname"), QStringLiteral("Ai đó"));
    UiIncoming o;
    o.title = c->title;
    o.text = QStringLiteral("%1 mời bạn vào cuộc gọi nhóm").arg(host);
    o.inviter = host;
    o.avatar = c->avatar;
    o.video = true;
    o.group = true;
    o.selfAvatar = selfAvatar();
    m_ui->incoming(o, [this](bool camStartOff) { groupAnswer(camStartOff); }, [this](const QString& why) { groupReject(why); });
}

void EngineCore::groupAnswer(bool camStartOff) {
    const CallPtr c = m_call;
    if (!c || !c->group || c->state != QLatin1String("incoming")) return;
    c->cameraOn = !camStartOff;
    m_media->setGroupCamera(!camStartOff);
    c->state = QStringLiteral("connecting");
    NegotiatedParams np;
    np.muted = c->muted;
    np.speakerOff = c->speakerOff;
    np.group = true;
    np.host = false;
    np.servers = c->servers;
    np.config = c->config;
    np.srtpMode = c->config.value("srtpMode");
    np.sessId = str(c->sessId);
    np.callId = c->callId;
    np.role = QStringLiteral("callee");
    np.localUid = c->localUid;
    np.hostCall = c->hostCall;
    m_media->onNegotiated(np);
    m_ui->status(QStringLiteral("Đang kết nối…"));
    groupCameraProfile();
    std::weak_ptr<Call> weak = c;
    m_media->prepare(QStringLiteral("callee"), [this, weak](std::optional<InitReply> best) {
        const CallPtr c = weak.lock();
        if (!c || m_call != c || c->state != QLatin1String("connecting")) return;
        if (!best && m_media->sendUdp()) {
            c->state = QStringLiteral("failed");
            m_ui->status(QStringLiteral("Không vào được cuộc gọi nhóm (máy chủ từ chối)"));
            groupSendAnswer(*c, 3);
            later(3000, [this] { groupFinish(0, QStringLiteral("Không vào được cuộc gọi nhóm"), false); });
            return;
        }
        m_media->start(QStringLiteral("callee"), QString());
        c->state = QStringLiteral("connected");
        c->startedAt = now();
        groupSendAnswer(*c, 0);
        groupBroadcast();
        c->pingTimer = timer(int(c->pingMs), true, [this] { groupPing(); });
        groupStatus();
    });
}

void EngineCore::groupReject(const QString& why) {
    const CallPtr c = m_call;
    if (!c || !c->group || c->state != QLatin1String("incoming")) return;
    log(QStringLiteral("group reject: %1").arg(why));
    groupSendAnswer(*c, 3);
    groupFinish(0, QStringLiteral("Đã từ chối cuộc gọi nhóm"), false);
}

void EngineCore::groupSendAnswer(const Call& c, int status) {
    send(QStringLiteral("sendSignal"), 12436,
         {{"callId", c.callId},
          {"data", jsonCpp(QJsonObject{{"callType", c.callType}, {"codec", ""}, {"extendData", ""}, {"groupId", c.groupId}, {"status", status}})},
          {"hostCall", c.hostCall},
          {"session", c.sessId},
          {"status", status}});
}

void EngineCore::groupCameraProfile() {
    const GroupVideoPlan* plan = m_media->videoPlan();
    if (plan && m_ui->rich()) {
        m_ui->setCameraEncode(QJsonObject{{"width", plan->encode.width},
                                          {"height", plan->encode.height},
                                          {"fps", plan->encode.fps},
                                          {"bitrate", double(plan->encode.bitrate)},
                                          {"keyMs", double(plan->encode.keyMs)}});
    }
    const QJsonObject cfg = m_call ? m_call->config : (m_media->hasParams() ? m_media->paramsConfig() : QJsonObject());
    const double envRes = js::number(QJsonValue(qEnvironmentVariable("ZCALL_SHARE_RES")));
    const double maxShort = qEnvironmentVariableIsSet("ZCALL_SHARE_RES") && envRes > 0 ? std::trunc(envRes) : 720;
    const double br = num(cfg.value("maxShrScreenBr"));
    const double kbps = br > 0 ? std::trunc(br) : 1500;
    if (m_ui->rich()) m_ui->setScreenEncode(QJsonObject{{"maxShort", maxShort}, {"bitrate", kbps * 1000}});
}

QJsonObject EngineCore::groupState(const Call& c) const {
    return QJsonObject{{"audioState", c.muted ? 1 : 0},
                       {"callId", c.callId},
                       {"callState", 3},
                       {"hostCall", c.hostCall},
                       {"userId", c.localUid},
                       {"videoState", c.cameraOn.value_or(false) ? 0 : 1}};
}

void EngineCore::groupGreet(double uid) {
    const CallPtr c = m_call;
    if (!c || !c->group || c->state != QLatin1String("connected")) return;
    if (uid == c->shareScreenId) return;
    if (c->greeted.contains(uid)) return;
    c->greeted.insert(uid);
    log(QStringLiteral("group: member %1 joined, our state again").arg(fmtNum(uid)));
    groupBroadcast();
    std::weak_ptr<Call> weak = c;
    later(2000, [this, weak] {
        if (weak.lock() && m_call == weak.lock()) groupBroadcast();
    });
}

void EngineCore::groupBroadcast() {
    const CallPtr c = m_call;
    if (c && c->group && c->state == QLatin1String("connected")) send(QStringLiteral("sendSignal"), 12098, groupState(*c));
}

void EngineCore::groupPing() {
    const CallPtr c = m_call;
    if (c && c->group && c->state == QLatin1String("connected")) send(QStringLiteral("sendSignal"), 12097, groupState(*c));
}

void EngineCore::groupMember(double uid, const MemberInfo& info) {
    const CallPtr c = m_call;
    if (!c || !c->group || uid == c->localUid || (c->shareScreenId != 0 && uid == c->shareScreenId)) return;
    const MemberKey key = MemberKey::ofUid(uid);
    std::optional<Member> m;
    if (Member* e = c->member(key)) m = *e;
    if (!m) {
        const MemberKey skey = MemberKey::ofStr(fmtNum(uid));
        if (Member* s = c->member(skey)) {
            m = *s;
            c->removeMember(skey);
        } else if (c->invitePartners && c->invitePartners->size() == 1) {
            for (auto it = c->members.begin(); it != c->members.end(); ++it) {
                if (it->second.state == QLatin1String("calling") || it->second.state == QLatin1String("ringing")) {
                    m = it->second;
                    c->members.erase(it);
                    break;
                }
            }
        }
    }
    Member next = m.value_or(Member{});
    if (!m) {
        next.name = QString();
        next.avatar = QString();
        next.state = QString();
    }
    if (info.state) next.state = *info.state;
    if (info.muted) next.muted = *info.muted;
    if (info.camOff) next.camOff = *info.camOff;
    if (next.name.isEmpty() && c->invitePartners && c->invitePartners->size() == 1) {
        next.name = c->invitePartners->front().name;
        next.avatar = c->invitePartners->front().avatar;
    }
    c->setMember(key, next);
    if (info.state && *info.state == QLatin1String("incall")) {
        c->answered = true;
        if (c->ringTimer) {
            c->ringTimer->stop();
            c->ringTimer->deleteLater();
            c->ringTimer = nullptr;
        }
    }
    groupStatus();
}

void EngineCore::groupMembersToUi() {
    const CallPtr c = m_call;
    if (!c || !c->group || !m_ui->rich()) return;
    QJsonArray list;
    for (const auto& [k, m] : c->members) {
        if (m.state != QLatin1String("incall") && m.state != QLatin1String("ringing") && m.state != QLatin1String("calling")) continue;
        if (c->shareScreenId != 0 && k.numeric && k.uid == c->shareScreenId) continue;
        list.append(QJsonObject{{"src", k.toJson()}, {"name", m.name}, {"avatar", m.avatar}, {"muted", m.muted}, {"camOff", m.camOff}, {"state", m.state}});
    }
    const QString key = QString::fromUtf8(jsonStringify(list));
    if (key == c->membersKey && !c->membersKey.isEmpty()) return;
    c->membersKey = key;
    m_ui->members(list);
}

void EngineCore::groupMemberIn(double uid) {
    const CallPtr c = m_call;
    if (!c || !c->group || uid == c->localUid || (c->shareScreenId != 0 && uid == c->shareScreenId)) return;
    const Member* m = c->member(MemberKey::ofUid(uid));
    if (!m || m->state != QLatin1String("incall")) groupMember(uid, MemberInfo{QStringLiteral("incall"), std::nullopt, std::nullopt});
}

void EngineCore::groupRoster(const QVector<uint32_t>& uids) {
    const CallPtr c = m_call;
    if (!c || !c->group) return;
    QVector<double> inRoom;
    for (uint32_t u : uids)
        if (!inRoom.contains(double(u))) inRoom.push_back(double(u));
    for (double uid : inRoom) {
        if (uid == c->localUid || (c->shareScreenId != 0 && uid == c->shareScreenId)) continue;
        const Member* m = c->member(MemberKey::ofUid(uid));
        if (!m || m->state != QLatin1String("incall")) groupGreet(uid);
        groupMember(uid, MemberInfo{QStringLiteral("incall"), std::nullopt, std::nullopt});
    }
    for (auto& [k, m] : c->members)
        if (m.state == QLatin1String("incall") && !(k.numeric && inRoom.contains(k.uid))) m.state = QStringLiteral("left");
    groupStatus();
    groupCheckEmpty(QStringLiteral("roster"));
}

void EngineCore::groupMemberLeft(double uid, const QString& why) {
    const CallPtr c = m_call;
    if (!c || !c->group || uid == c->localUid || (c->shareScreenId != 0 && uid == c->shareScreenId)) return;
    std::optional<Member> m;
    if (Member* e = c->member(MemberKey::ofUid(uid))) m = *e;
    else if (Member* s = c->member(MemberKey::ofStr(fmtNum(uid)))) m = *s;
    if (!m || m->state == QLatin1String("left")) return;
    log(QStringLiteral("group member left: %1 %2").arg(fmtNum(uid), why));
    Member next = *m;
    next.state = QStringLiteral("left");
    c->setMember(MemberKey::ofUid(uid), next);
    groupStatus();
    groupCheckEmpty(why);
}

void EngineCore::groupCheckEmpty(const QString& why) {
    const CallPtr c = m_call;
    if (!c || !c->group || c->state != QLatin1String("connected")) return;
    for (const auto& [k, m] : c->members) {
        if (m.state != QLatin1String("incall") && m.state != QLatin1String("ringing") && m.state != QLatin1String("calling")) continue;
        if (c->shareScreenId != 0 && k.numeric && k.uid == c->shareScreenId) continue;
        return;
    }
    log(QStringLiteral("group call: nobody left %1").arg(why));
    groupHangup(QStringLiteral("alone"));
}

void EngineCore::groupStatus() {
    const CallPtr c = m_call;
    if (!c || !c->group || (c->state != QLatin1String("connected") && c->state != QLatin1String("calling"))) return;
    groupMembersToUi();
    QStringList names;
    bool hasRinging = false;
    for (const auto& [k, m] : c->members) {
        if (m.state == QLatin1String("ringing")) hasRinging = true;
        if (m.state != QLatin1String("incall")) continue;
        if (c->shareScreenId != 0 && k.numeric && k.uid == c->shareScreenId) continue;
        names << (m.name.isEmpty() ? QStringLiteral("Thành viên") : m.name);
    }
    const QString waiting = c->role == QLatin1String("caller") && !c->answered
                                ? (hasRinging ? QStringLiteral("Đang đổ chuông…") : QStringLiteral("Đang gọi nhóm…"))
                                : QStringLiteral("Đang chờ người khác…");
    const QString text = !names.isEmpty() ? QStringLiteral("%1 người: Bạn, %2").arg(names.size() + 1).arg(names.join(QStringLiteral(", "))) : waiting;
    if (!c->hasStatusText || text != c->statusText) {
        c->hasStatusText = true;
        c->statusText = text;
        const double since = c->answered && !names.isEmpty() ? c->startedAt : 0;
        m_ui->status(text, since);
    }
}

void EngineCore::groupEnded(const QString& why) {
    const CallPtr c = m_call;
    if (!c || !c->group || c->state == QLatin1String("ended")) return;
    log(QStringLiteral("group call ended by the server: %1").arg(why));
    groupFinish(c->state == QLatin1String("connected") ? 50 : 0,
                why == QLatin1String("kicked") ? QStringLiteral("Bạn đã bị mời ra khỏi cuộc gọi") : QStringLiteral("Cuộc gọi nhóm đã kết thúc"));
}

void EngineCore::groupHangup(const QString& why) {
    const CallPtr c = m_call;
    if (!c || c->state == QLatin1String("ended")) return;
    if (c->state == QLatin1String("incoming")) {
        groupReject(why);
        return;
    }
    if (c->role == QLatin1String("caller") && !c->answered) {
        groupCancel(why);
        return;
    }
    groupFinish(c->state == QLatin1String("connected") ? 50 : 0, QString());
}

void EngineCore::groupCancel(const QString& why) {
    const CallPtr c = m_call;
    log(QStringLiteral("group call cancelled: %1").arg(why));
    if (c->hostCall != 0) {
        send(QStringLiteral("sendSignal"), 12437,
             {{"callId", c->callId},
              {"data", jsonCpp(QJsonObject{{"callType", c->callType}, {"duration", 0}, {"extraData", ""}, {"groupId", c->groupId}})},
              {"hostCall", c->hostCall}});
    }
    groupFinish(0, why == QLatin1String("no-answer") ? QStringLiteral("Không ai trả lời") : QStringLiteral("Đã huỷ cuộc gọi nhóm"), false);
}

void EngineCore::clearCallTimers(Call& c) {
    for (QTimer** t : {&c.ringTimer, &c.pingTimer}) {
        if (*t) {
            (*t)->stop();
            (*t)->deleteLater();
            *t = nullptr;
        }
    }
}

void EngineCore::groupFinish(int status, const QString& closeText, bool emitSignals) {
    const CallPtr c = m_call;
    if (!c || c->state == QLatin1String("ended")) return;
    const bool connected = c->state == QLatin1String("connected");
    c->state = QStringLiteral("ended");
    clearCallTimers(*c);
    const MediaStats stats = connected ? m_media->stats().value_or(MediaStats{}) : MediaStats{};
    m_media->stop();
    const double duration = connected ? double(std::llround((now() - c->startedAt) / 1000.0)) : 0;
    if (emitSignals && connected) {
        send(QStringLiteral("sendSignal"), 12438,
             {{"callId", c->callId},
              {"data", jsonCpp(QJsonObject{{"callType", c->callType}, {"duration", duration}, {"extraData", ""}, {"groupId", c->groupId}, {"status", 0}})},
              {"hostCall", c->hostCall}});
        const QJsonObject stat{{"Codec", QJsonArray{"opus/16000/1", "20"}}, {"RxTotalPkt", stats.rx}, {"TxTotalPkt", stats.tx}, {"sysInfo", "native-engine"}};
        send(QStringLiteral("sendSignal"), 12446,
             {{"callId", c->callId},
              {"callType", c->callType},
              {"data", QString::fromUtf8(jsonStringify(stat))},
              {"duration", duration},
              {"hostCall", c->hostCall},
              {"joinTime", c->startedAt},
              {"status", status}});
    }
    send(QStringLiteral("update"), QStringLiteral("callState"), {{"state", "free"}});
    m_ui->close(!closeText.isEmpty() ? closeText
                                     : (connected ? QStringLiteral("Kết thúc — %1 s").arg(fmtNum(duration)) : QStringLiteral("Đã kết thúc")));
    m_call.reset();
}

void EngineCore::finish(int status, const QString& closeText, bool emitSignals) {
    const CallPtr c = m_call;
    if (!c || c->state == QLatin1String("ended")) return;
    rememberEndedCall(QJsonValue(c->callId));
    const bool connected = c->state == QLatin1String("connected");
    c->state = QStringLiteral("ended");
    clearCallTimers(*c);
    const MediaStats stats = js::truthy(c->sessId) ? m_media->stats().value_or(MediaStats{}) : MediaStats{};
    m_media->stop();
    const double duration = connected ? double(std::llround((now() - c->startedAt) / 1000.0)) : 0;
    const bool role = c->role == QLatin1String("caller");
    if (emitSignals) {
        send(QStringLiteral("update"), QStringLiteral("bubble"), {{"duration", duration}, {"partnerId", c->peerId}, {"role", role}});
        const QJsonObject stat{{"Codec", QJsonArray{"opus/16000/1", "20"}},
                               {"NewZRTC", 1},
                               {"RxTotalPkt", stats.rx},
                               {"SrtpMode", 1},
                               {"TxTotalPkt", stats.tx},
                               {"sysInfo", "native-engine"}};
        send(QStringLiteral("sendSignal"), 406,
             {{"callId", c->callId},
              {"data", QString::fromUtf8(jsonStringify(stat))},
              {"duration", duration},
              {"partnerId", c->peerId},
              {"role", role},
              {"status", status}});
    }
    send(QStringLiteral("update"), QStringLiteral("callState"), {{"state", "free"}});
    m_ui->close(!closeText.isEmpty() ? closeText
                                     : (connected ? QStringLiteral("Kết thúc — %1 s").arg(fmtNum(duration)) : QStringLiteral("Đã kết thúc")));
    m_call.reset();
}

void EngineCore::mediaTimeout() { hangup(QStringLiteral("media-timeout")); }
void EngineCore::mediaFailed(const QString& why) { hangup(QStringLiteral("media-%1").arg(why)); }
void EngineCore::mediaPeerEnd(const QString& why) {
    if (m_call && m_call->group) groupEnded(why.isEmpty() ? QStringLiteral("server") : why);
    else peerEnded(QStringLiteral("server"));
}
void EngineCore::mediaEarlyAudio(const QString& phase) { earlyAudio(phase); }
void EngineCore::mediaAudioFault(const QString& kind) { audioFault(kind); }
void EngineCore::mediaMicVolume(int pct) {
    if (m_ui->rich()) m_ui->micVolume(pct);
}
void EngineCore::mediaMicLevel(int level) {
    if (m_ui->rich()) m_ui->micLevel(level);
}
void EngineCore::mediaRoster(const QVector<uint32_t>& uids) { groupRoster(uids); }
void EngineCore::mediaMemberLeft(uint32_t uid) { groupMemberLeft(double(uid), QStringLiteral("server")); }
void EngineCore::mediaMemberAudio(uint32_t uid) { groupMemberIn(double(uid)); }
void EngineCore::mediaMemberSpeaking(uint32_t uid, bool on) {
    if (m_call && m_call->group && m_ui->rich()) m_ui->speaking(uid, on);
}
void EngineCore::mediaVideo(const VideoPacket& pkt) { onVideoPacket(pkt); }
void EngineCore::mediaKeyframe(const QString& why) { requestKeyFrame(why); }
void EngineCore::mediaSendProfile(const SendProfile& p) {
    if (m_call && m_ui->rich()) m_ui->setCameraEncode(p.toJson());
}

void EngineCore::uiHangup() { hangup(QStringLiteral("local")); }
void EngineCore::uiMute(bool on) {
    m_media->setMute(on);
    if (m_call) m_call->muted = on;
    if (m_call && m_call->group) {
        log(QStringLiteral("group mute %1").arg(on ? QStringLiteral("on") : QStringLiteral("off")));
        groupBroadcast();
    } else {
        micState(!on);
    }
}
void EngineCore::uiSpeaker(bool on) {
    if (m_call) m_call->speakerOff = on;
    m_media->setSpeaker(on);
}
void EngineCore::uiVideoFrame(const OutVideoFrame& f) {
    if (m_call && m_call->state == QLatin1String("connected")) m_media->sendVideoFrame(f);
}
void EngineCore::uiCamera(bool on) { camMic(on); }
void EngineCore::uiDevice(const QString& kind, const QString& name) { m_media->setDevice(kind, name); }
void EngineCore::uiMicVolume(double pct) { m_media->setMicVolume(pct); }
void EngineCore::uiScreen(bool on) { screenShare(on); }
void EngineCore::uiNeedKey(const QString& why, const QJsonValue& src) { askPeerKey(QStringLiteral("window ") + why, src); }
void EngineCore::uiTile(uint32_t src, double width) {
    if (m_call && m_call->group) m_media->setMemberRenderWidth(src, width);
}

}
