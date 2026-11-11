// SPDX-License-Identifier: BSD-3-Clause
#include "wire/zrtp_packet.h"

#include "js.h"
#include "wire/bytes.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace zcall {

const QStringList kGroupInitLayouts{QStringLiteral("zero"), QStringLiteral("sub6"), QStringLiteral("callId"),
                                    QStringLiteral("callIdHost")};

int jsTrunc(double v) { return js::toInt32(std::trunc(v)); }

int envInt(const char* name, int fallback, bool* present) {
    const QByteArray v = qgetenv(name);
    if (present) *present = qEnvironmentVariableIsSet(name);
    if (v.isEmpty()) return fallback;
    const double d = js::number(QJsonValue(QString::fromUtf8(v)));
    return std::isfinite(d) && d != 0 ? int(d) : fallback;
}

bool isMediaType(int t) {
    return t == 0x03 || t == 0x04 || t == 0x05 || t == 0x07 || t == 0x0d || t == 0x0e || t == 0x0f;
}

QByteArray wrapMediaPacket(int msgType, uint32_t token, const QByteArray& payload) {
    QByteArray buf(kMediaHeaderLen, '\0');
    buf[0] = char(msgType);
    wr32le(buf, 1, token);
    buf.append(payload);
    return buf;
}

QByteArray buildControlHeader(const ControlHeader& h) {
    QByteArray buf(kControlHeaderLen, '\0');
    buf[0] = char(h.msgType);
    buf[1] = char(h.magic);
    wr32le(buf, 2, h.field02);
    wr32le(buf, 6, h.seq);
    wr32le(buf, 10, h.uid);
    wr32le(buf, 14, h.token);
    wr16le(buf, 18, uint32_t(h.cmd));
    buf[20] = char(h.subCmd & 0xff);
    return buf;
}

namespace {

ControlHeader header(int magic, uint32_t uid, uint32_t token, int cmd, int subCmd, uint32_t seq = 0) {
    ControlHeader h;
    h.magic = magic;
    h.uid = uid;
    h.token = token;
    h.cmd = cmd;
    h.subCmd = subCmd;
    h.seq = seq;
    return h;
}

QByteArray p2pHeader(bool callee, uint32_t callId, int kind) {
    QByteArray h(P2p::HeaderLen, '\0');
    h[0] = char(MsgType::P2pExt);
    h[2] = char(callee ? 1 : 0);
    wr32le(h, 3, callId);
    h[7] = char(kind);
    return h;
}

}

QByteArray buildInitZrtpPacket(bool caller, uint32_t uid, uint32_t peerUid, uint32_t callId, const QByteArray& sessId,
                               const QByteArray& extra, int subCmd) {
    QByteArray out = buildControlHeader(
        header(kMagicByte, uid, 0, caller ? Cmd::InitCallCaller : Cmd::InitCallCallee, subCmd));
    out += le32(callId);
    out += le32(peerUid);
    out += u16String(sessId);
    if (!extra.isEmpty()) out += u16String(extra);
    return out;
}

QByteArray groupExtra(bool videoOn, bool initZrtp) {
    QByteArray b;
    b.append(char(0));
    b.append(char(5));
    b.append(char(videoOn ? 1 : 0));
    if (initZrtp) b.append(char(1));
    return b;
}

QByteArray buildGroupInitZrtpPacket(bool host, uint32_t uid, const QByteArray& sessId, bool videoOn, uint32_t callId,
                                    uint32_t hostCall, const QString& layout) {
    const bool sub6 = layout == QLatin1String("sub6");
    QByteArray out =
        buildControlHeader(header(kMagicByte, uid, 0, host ? Cmd::InitCallCaller : Cmd::InitCallCallee, sub6 ? 0x06 : 0x04));
    const QByteArray extra = u16String(groupExtra(videoOn, true));
    if (sub6) {
        out += le32(callId);
        out.append(char(host ? 1 : 0));
        out.append(char(5));
        out.append(char(0));
        out += u16String(sessId);
        out += extra;
        return out;
    }
    const uint32_t a = layout == QLatin1String("zero") ? 0 : callId;
    const uint32_t b = layout == QLatin1String("callIdHost") ? hostCall : 0;
    out += le32(a);
    out += le32(b);
    out += u16String(sessId);
    out += extra;
    return out;
}

QByteArray buildShareInitZrtpPacket(uint32_t uid, const QByteArray& sessId) {
    QByteArray out = buildControlHeader(header(kMagicByte, uid, 0, Cmd::InitCallCallee, 0x04));
    out += le32(0);
    out += le32(0);
    out += u16String(sessId);
    out += u16String(QByteArray("\x00\xff\x01\x00", 4));
    return out;
}

QByteArray buildShareZaviPingPacket(uint32_t uid, uint32_t token, const QByteArray& sessId) {
    QByteArray out = buildControlHeader(header(kMagicByte, uid, token, Cmd::InitZaviPing, 0x01));
    out += le32(0);
    out += le32(0);
    out += u16String(sessId);
    return out;
}

QByteArray buildZaviPingPacket(uint32_t uid, uint32_t token, const QByteArray& sessId, bool videoOn) {
    QByteArray out = buildControlHeader(header(kMagicByte, uid, token, Cmd::InitZaviPing, 0x01));
    out += le32(0);
    out += le32(0);
    out += u16String(sessId);
    out += u16String(groupExtra(videoOn, false));
    return out;
}

std::optional<QVector<uint32_t>> parseRoomUpdated(const QByteArray& data) {
    if (data.size() % 4) return std::nullopt;
    QVector<uint32_t> out;
    for (int o = 0; o < data.size(); o += 4) out.push_back(rd32le(data, o));
    return out;
}

QByteArray buildPingPacket(uint32_t uid, uint32_t token, const QByteArray& sessId) {
    return buildControlHeader(header(0x01, uid, token, Cmd::Ping, 0x01)) + u16String(sessId);
}

QByteArray buildEndCallPacket(uint32_t uid, uint32_t token, uint32_t callId) {
    return buildControlHeader(header(kMagicByte, uid, token, Cmd::EndCall, 0)) + le32(callId);
}

QByteArray buildReqForwardPacket(uint32_t uid, uint32_t token, uint32_t seq, int subCmd, const QByteArray& payload) {
    return buildControlHeader(header(0x01, uid, token, Cmd::ReqForward, subCmd, seq)) + payload;
}

QByteArray reqForwardStatus(uint32_t rxPerSecond, uint32_t a, uint32_t b5) {
    QByteArray b(24, '\0');
    const uint32_t v[6] = {20, a, 1, 1, rxPerSecond, b5};
    for (int i = 0; i < 6; ++i) wr32le(b, i * 4, v[i]);
    return b;
}

QByteArray buildGroupVidQualReq(uint32_t uid, uint32_t token, uint32_t seq, const QVector<VidQualEntry>& entries) {
    QByteArray body(1 + entries.size() * 5, '\0');
    body[0] = char(entries.size() & 0xff);
    for (int i = 0; i < entries.size(); ++i) {
        wr32le(body, 1 + i * 5, entries[i].uid);
        body[1 + i * 5 + 4] = char(entries[i].qualityId & 0xff);
    }
    return buildControlHeader(header(0x01, uid, token, Cmd::ReqForward, 0x0d, seq)) + u16String(body);
}

QByteArray buildGroupVidQualConfig(uint32_t uid, uint32_t token, uint32_t seq, const QVector<VidLayer>& layers) {
    QByteArray body(1 + layers.size() * 9, '\0');
    body[0] = char(layers.size() & 0xff);
    for (int i = 0; i < layers.size(); ++i) {
        const int o = 1 + i * 9;
        body[o] = char(layers[i].id & 0xff);
        wr32le(body, o + 1, layers[i].a);
        wr16le(body, o + 5, layers[i].b & 0xffff);
        wr16le(body, o + 7, layers[i].c & 0xffff);
    }
    return buildControlHeader(header(0x01, uid, token, Cmd::ReqForward, 0x0c, seq)) + u16String(body);
}

std::optional<GroupVideoPlan> groupVideoPlan(const QJsonObject& config) {
    const QJsonValue gcv = config.value(QStringLiteral("groupcall"));
    if (!gcv.isObject()) return std::nullopt;
    const QJsonObject gc = gcv.toObject();
    if (!gc.value(QStringLiteral("spatialLayers")).isArray()) return std::nullopt;
    const double tn = js::number(gc.value(QStringLiteral("temporalNum")));
    const int temporal = std::isfinite(tn) ? (js::toInt32(tn) & 0xff) : 0;
    if (!temporal) return std::nullopt;
    const double mw = js::number(config.value(QStringLiteral("maxWidth")));
    const double mh = js::number(config.value(QStringLiteral("maxHeight")));
    const double kf = js::number(config.value(QStringLiteral("keyFrameIntervalMs")));
    const double maxW = mw > 0 ? std::trunc(mw) : 1280;
    const double maxH = mh > 0 ? std::trunc(mh) : 720;
    const int64_t keyMs = kf > 0 ? int64_t(std::trunc(kf)) : 1000;
    GroupVideoPlan plan;
    int spatial = 0;
    for (const QJsonValue& rv : gc.value(QStringLiteral("spatialLayers")).toArray()) {
        if (!rv.isObject() && !rv.isArray()) continue;
        const QJsonObject raw = rv.toObject();
        const double fn = js::number(raw.value(QStringLiteral("factorNum")));
        const double fd = js::number(raw.value(QStringLiteral("factorDen")));
        if (!std::isfinite(fn) || !std::isfinite(fd)) continue;
        const int num = js::toInt32(fn) & 0xff;
        const int den = js::toInt32(fd) & 0xff;
        const double kbps = js::number(raw.value(QStringLiteral("bitrateKbps")));
        const double fps = js::number(raw.value(QStringLiteral("fps")));
        if (!den || !std::isfinite(kbps) || !std::isfinite(fps) || fps <= 0) continue;
        const int width = js::toInt32(std::trunc(maxW * num / den)) & ~1;
        const int height = js::toInt32(std::trunc(maxH * num / den)) & ~1;
        if (width <= 0 || height <= 0) continue;
        const double divided = std::trunc(kbps);
        for (int t = 0; t < temporal; ++t) {
            const int shift = (temporal - 1 - t) & 31;
            const uint32_t wire = js::toUint32(std::trunc(divided / double(js::toInt32(double(1u << shift)))));
            plan.layers.push_back(VidLayer{spatial * 3 + t, wire, uint32_t(width), uint32_t(height)});
            if (t == 0) plan.rows.push_back(VideoRow{width, height, js::toInt32(std::trunc(fps)), wire});
        }
        ++spatial;
    }
    if (plan.layers.isEmpty() || plan.rows.isEmpty()) return std::nullopt;
    const QByteArray envLayer = qgetenv("ZCALL_GROUP_CAM_LAYER");
    const double want = !qEnvironmentVariableIsSet("ZCALL_GROUP_CAM_LAYER")
                            ? 1
                            : js::number(QJsonValue(QString::fromUtf8(envLayer)));
    const double tw = std::trunc(want);
    const int wantI = std::isfinite(tw) && tw != 0 ? int(tw) : 0;
    plan.start = std::min(std::max(0, wantI), int(plan.rows.size()) - 1);
    const VideoRow& row = plan.rows[plan.start];
    plan.encode = EncodeProfile{row.width, row.height, row.fps, int64_t(row.kbps) * 1000, keyMs};
    plan.send = {VidLayer{0, row.kbps, uint32_t(row.width), uint32_t(row.height)}};
    const double codec = js::number(gc.value(QStringLiteral("codec")));
    plan.hasSfu = true;
    plan.sfuId = 14;
    plan.sfuLong = true;
    if (codec == 60 || codec == 61) {
        plan.sfuId = 12;
        plan.sfuLong = false;
    } else if (codec == 4) {
        plan.hasSfu = false;
    }
    return plan;
}

QByteArray buildGroupSyncPacket(uint32_t uid, uint32_t token, uint32_t seq, int subCmd) {
    return buildControlHeader(header(0x01, uid, token, Cmd::GroupMessage, subCmd, seq));
}

QByteArray buildP2pBind(bool callee, uint32_t callId, uint32_t seq, uint32_t index) {
    QByteArray b(6, '\0');
    wr16le(b, 0, seq & 0xffff);
    wr32be(b, 2, index);
    return p2pHeader(callee, callId, P2p::Bind) + b;
}

QByteArray buildP2pBindAnswer(bool callee, uint32_t callId, const QByteArray& request, const QByteArray& peerAddr) {
    return p2pHeader(callee, callId, P2p::BindAnswer) + request.mid(9, 6) + le32(uint32_t(peerAddr.size())) + peerAddr;
}

QByteArray buildP2pPong(bool callee, const QByteArray& ping) {
    QByteArray pong = ping;
    pong[2] = char(callee ? 1 : 0);
    pong[9] = 2;
    return pong;
}

QByteArray wrapP2pAudio(bool callee, uint32_t callId, const QByteArray& rtp) { return p2pHeader(callee, callId, P2p::Audio) + rtp; }

QByteArray wrapP2pVideo(bool callee, uint32_t callId, const QByteArray& rtp) { return p2pHeader(callee, callId, P2p::Video) + rtp; }

QByteArray wrapP2pRtcp(bool callee, uint32_t callId, bool video, const QByteArray& rtcp) {
    return p2pHeader(callee, callId, video ? P2p::VideoRtcp : P2p::AudioRtcp) + rtcp;
}

std::optional<Unwrapped> unwrapPacket(const QByteArray& buf) {
    if (buf.size() < 1) return std::nullopt;
    Unwrapped p;
    p.msgType = u8at(buf, 0);
    if (p.msgType == MsgType::AudioRtpDown || p.msgType == MsgType::VideoDown) {
        p.isMedia = true;
        p.payload = buf.mid(1);
        return p;
    }
    if (isMediaType(p.msgType)) {
        p.isMedia = true;
        if (buf.size() < kMediaHeaderLen) {
            p.truncated = true;
            p.raw = buf;
            return p;
        }
        p.token = rd32le(buf, 1);
        p.payload = buf.mid(5);
        return p;
    }
    if ((p.msgType == MsgType::Control || p.msgType == MsgType::ServerReply) && buf.size() >= kControlHeaderLen) {
        const QByteArray data = buf.mid(kControlHeaderLen);
        p.isControl = true;
        p.magic = u8at(buf, 1);
        p.field02 = rd32le(buf, 2);
        p.seq = rd32le(buf, 6);
        p.uid = rd32le(buf, 10);
        p.token = rd32le(buf, 14);
        p.cmd = rd16le(buf, 18);
        p.subCmd = u8at(buf, 20);
        p.data = data;
        const bool initCmd = p.cmd == Cmd::InitCallCaller || p.cmd == Cmd::InitCallCallee;
        if (initCmd && p.msgType == MsgType::ServerReply && data.size() >= 4) {
            p.res = rd32le(data, 0);
            if (*p.res == 0 && data.size() >= 12) {
                p.callId = rd32le(data, 4);
                p.serverToken = rd32le(data, 8);
                if (data.size() >= 14) {
                    const int n = rd16le(data, 12);
                    p.publicAddr = data.mid(14, n);
                }
            }
        } else if (initCmd && data.size() >= 10) {
            p.callId = rd32le(data, 0);
            p.peerUid = rd32le(data, 4);
            const int n1 = rd16le(data, 8);
            p.sessId = data.mid(10, n1);
            const int off = 10 + n1;
            if (data.size() >= off + 2) p.extra = data.mid(off + 2, rd16le(data, off));
        } else if (p.cmd == Cmd::Ping && data.size() >= 2) {
            p.sessId = data.mid(2, rd16le(data, 0));
        } else if (p.cmd == Cmd::EndCall && data.size() >= 4) {
            p.callId = rd32le(data, 0);
        }
        return p;
    }
    if (p.msgType == MsgType::P2pExt && buf.size() >= 9) {
        p.b1 = u8at(buf, 1);
        p.b2 = u8at(buf, 2);
        p.field38 = rd32le(buf, 3);
        p.cmd = rd16le(buf, 7);
        p.data = buf.mid(9);
        return p;
    }
    p.raw = buf;
    return p;
}

}
