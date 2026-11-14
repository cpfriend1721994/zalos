// SPDX-License-Identifier: BSD-3-Clause
#include "call_ui.h"
#include "json_cpp.h"
#include "send_rate.h"
#include "vectors.h"
#include "wire/ipc_crypto.h"
#include "wire/rtcp.h"
#include "wire/rtp.h"
#include "wire/srtp.h"
#include "wire/video.h"
#include "wire/zrtp_packet.h"

#include <QtTest>

using namespace zcall;
using namespace zcall::test;

namespace {

QJsonObject layerJson(const VidLayer& l) {
    return QJsonObject{{"id", l.id}, {"a", double(l.a)}, {"b", double(l.b)}, {"c", double(l.c)}};
}

QJsonValue planJson(const std::optional<GroupVideoPlan>& p) {
    if (!p) return QJsonValue(QJsonValue::Null);
    QJsonArray layers, send, rows;
    for (const auto& l : p->layers) layers.append(layerJson(l));
    for (const auto& l : p->send) send.append(layerJson(l));
    for (const auto& r : p->rows)
        rows.append(QJsonObject{{"width", r.width}, {"height", r.height}, {"fps", r.fps}, {"kbps", double(r.kbps)}});
    QJsonObject encode{{"width", p->encode.width},
                       {"height", p->encode.height},
                       {"fps", p->encode.fps},
                       {"bitrate", double(p->encode.bitrate)},
                       {"keyMs", double(p->encode.keyMs)}};
    QJsonValue sfu = p->hasSfu ? QJsonValue(QJsonObject{{"id", p->sfuId}, {"long", p->sfuLong}}) : QJsonValue(QJsonValue::Null);
    return QJsonObject{{"layers", layers}, {"encode", encode}, {"send", send}, {"sfu", sfu}, {"rows", rows}, {"start", p->start}};
}

QJsonValue sizeJson(const std::optional<PictureSize>& s) {
    if (!s) return QJsonValue(QJsonValue::Null);
    return QJsonObject{{"width", double(s->width)}, {"height", double(s->height)}};
}

QJsonObject unwrapJson(const Unwrapped& p) {
    QJsonObject o;
    auto hexObj = [](const QByteArray& b) { return QJsonObject{{"hex", hexs(b)}}; };
    o["isMedia"] = p.isMedia;
    o["msgType"] = p.msgType;
    if (p.isMedia) {
        if (p.truncated) {
            o["truncated"] = true;
            o["raw"] = hexObj(p.raw);
            return o;
        }
        if (p.msgType != MsgType::AudioRtpDown && p.msgType != MsgType::VideoDown) o["token"] = double(p.token);
        o["payload"] = hexObj(p.payload);
        return o;
    }
    if (p.isControl) {
        o["magic"] = p.magic;
        o["field02"] = double(p.field02);
        o["seq"] = double(p.seq);
        o["uid"] = double(p.uid);
        o["token"] = double(p.token);
        o["cmd"] = p.cmd;
        o["subCmd"] = p.subCmd;
        o["data"] = hexObj(p.data);
        if (p.res) o["res"] = double(*p.res);
        if (p.callId) o["callId"] = double(*p.callId);
        if (p.serverToken) o["serverToken"] = double(*p.serverToken);
        if (p.publicAddr) o["publicAddr"] = QString::fromLatin1(*p.publicAddr);
        if (p.peerUid) o["peerUid"] = double(*p.peerUid);
        if (p.sessId) o["sessId"] = hexObj(*p.sessId);
        if (p.extra) o["extra"] = hexObj(*p.extra);
        return o;
    }
    if (p.msgType == MsgType::P2pExt && p.raw.isEmpty()) {
        o["b1"] = p.b1;
        o["b2"] = p.b2;
        o["field38"] = double(p.field38);
        o["cmd"] = p.cmd;
        o["data"] = hexObj(p.data);
        return o;
    }
    o["raw"] = hexObj(p.raw);
    return o;
}

}

class TestCallWire : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void ipc() {
        const QJsonObject v = loadVectors("ipc.json").toObject();
        QVERIFY(!v.isEmpty());
        const QByteArray key = hx(v["key"]);
        for (const auto& c : v["cases"].toArray()) {
            const QJsonObject o = c.toObject();
            QCOMPARE(QString::fromLatin1(ipcEncrypt(o["text"].toString().toUtf8(), key)), o["hex"].toString());
        }
        QCOMPARE(QString::fromUtf8(*ipcDecrypt(v["cases"].toArray()[2].toObject()["hex"].toString().toLatin1(), key)),
                 v["roundtrip"].toString());
        QJsonArray got;
        FrameParser parser(key, true, [&got](const IpcFrame& f) {
            if (f.isToken) got.append(QJsonObject{{"token", QString::fromLatin1(f.token)}});
            else if (f.isError) got.append(QJsonObject{{"error", true}});
            else got.append(QJsonObject{{"text", QString::fromUtf8(jsonStringify(f.json))}});
        });
        const QByteArray stream = v["stream"].toString().toLatin1();
        for (int i = 0; i < stream.size(); i += 37) parser.push(stream.mid(i, 37));
        const QJsonArray want = v["parsed"].toArray();
        QCOMPARE(got.size(), want.size());
        for (int i = 0; i < want.size(); ++i) {
            const QJsonObject w = want[i].toObject();
            const QJsonObject g = got[i].toObject();
            if (w.contains("text")) {
                QCOMPARE(QJsonDocument::fromJson(g["text"].toString().toUtf8()), QJsonDocument::fromJson(w["text"].toString().toUtf8()));
            } else {
                QCOMPARE(g, w);
            }
        }
        const QByteArray big(9000, 'a');
        const auto parts = ipcChunks(big, 42);
        QCOMPARE(parts.size(), 3);
        QVERIFY(parts[2].endsWith("#42#3#2#$"));
    }

    void srtp() {
        const QJsonObject v = loadVectors("srtp.json").toObject();
        const auto keys = sessionKeys(*masterFromSessId(v["sessId"].toString().toUtf8()));
        QVERIFY(keys);
        QCOMPARE(hexs(keys->enc), v["enc"].toString());
        QCOMPARE(hexs(keys->auth), v["auth"].toString());
        QCOMPARE(hexs(keys->salt), v["salt"].toString());
        for (const auto& e : v["ivs"].toArray()) {
            const QJsonObject o = e.toObject();
            QCOMPARE(hexs(packetIv(keys->salt, u32(o["ssrc"]), uint16_t(o["seq"].toInt()), u32(o["roc"]))), o["iv"].toString());
        }
        for (const auto& e : v["packets"].toArray()) {
            const QJsonObject o = e.toObject();
            const QByteArray out = protectRtp(hx(o["hdr"]), hx(o["payload"]), *keys, uint16_t(o["seq"].toInt()), u32(o["ssrc"]), u32(o["roc"]));
            QCOMPARE(hexs(out), o["out"].toString());
            const auto back = unprotectRtp(out, *keys, u32(o["roc"]));
            QVERIFY(back);
            QCOMPARE(hexs(back->payload), o["unprotected"].toString());
        }
        QCOMPARE(unprotectRtp(hx(v["tampered"]), *keys, 0).has_value(), v["tamperedOk"].toBool());
        QVERIFY(!masterFromSessId(QByteArray(29, 'a')));
    }

    void rtp() {
        const QJsonObject v = loadVectors("rtp.json").toObject();
        auto argsOf = [](const QJsonObject& a) {
            RtpHeaderArgs r;
            r.seq = uint16_t(a["seq"].toInt());
            r.ts = u32(a["ts"]);
            r.ssrc = u32(a["ssrc"]);
            r.twSeq = uint16_t(a["twSeq"].toInt());
            r.marker = a["marker"].toBool();
            r.pt = a.contains("pt") ? a["pt"].toInt() : kAudioPt;
            r.layerSeq = uint16_t(a["layerSeq"].toInt());
            r.captureMs = a["captureMs"].toDouble();
            r.sendMs = a["sendMs"].toDouble();
            r.level = a.contains("level") ? a["level"].toInt() : 127;
            return r;
        };
        for (const auto& e : v["audio"].toArray())
            QCOMPARE(hexs(buildAudioRtpHeader(argsOf(e.toObject()["args"].toObject()))), e.toObject()["out"].toString());
        for (const auto& e : v["groupAudio"].toArray())
            QCOMPARE(hexs(buildGroupAudioRtpHeader(argsOf(e.toObject()["args"].toObject()))), e.toObject()["out"].toString());
        for (const auto& e : v["groupVideo"].toArray()) {
            const QJsonObject a = e.toObject()["args"].toObject();
            const QJsonObject s = a["sfu"].toObject();
            SfuExt sfu;
            sfu.longForm = s["long"].toBool();
            sfu.id = s["id"].toInt();
            QCOMPARE(hexs(buildGroupVideoRtpHeader(argsOf(a), sfu)), e.toObject()["out"].toString());
        }
        for (const auto& e : v["levels"].toArray()) {
            const auto lvl = rtpAudioLevel(hx(QJsonValue(e.toObject()["rtp"].toString() + "00")));
            QCOMPARE(lvl ? QJsonValue(*lvl) : QJsonValue(QJsonValue::Null), e.toObject()["level"]);
        }
    }

    void rtcp() {
        const QJsonObject v = loadVectors("rtcp.json").toObject();
        TwccRecorder rec;
        const QJsonArray adds = v["adds"].toArray();
        const QJsonArray builds = v["builds"].toArray();
        int bi = 0;
        for (int i = 0; i <= adds.size(); ++i) {
            while (bi < builds.size() && builds[bi].toObject()["after"].toInt() == i) {
                QJsonArray got;
                for (const auto& p : rec.build(111, 222)) got.append(hexs(p));
                QCOMPARE(got, builds[bi].toObject()["out"].toArray());
                ++bi;
            }
            if (i < adds.size()) rec.add(uint32_t(adds[i].toArray()[0].toInt()), adds[i].toArray()[1].toDouble());
        }
        QCOMPARE(bi, builds.size());
        for (const auto& e : v["parsed"].toArray()) {
            const auto fb = parseTwcc(hx(e.toObject()["pkt"]));
            QVERIFY(fb);
            const QJsonObject w = e.toObject()["fb"].toObject();
            QCOMPARE(fb->base, uint16_t(w["base"].toInt()));
            QCOMPARE(fb->count, uint16_t(w["count"].toInt()));
            QCOMPARE(double(fb->refUnits), w["refUnits"].toDouble());
            QCOMPARE(int(fb->fbCount), w["fbCount"].toInt());
            QJsonArray st, dq;
            for (int s : fb->status) st.append(s);
            for (int d : fb->deltaQ) dq.append(d);
            QCOMPARE(st, w["status"].toArray());
            QCOMPARE(dq, w["deltaQ"].toArray());
        }
        const QJsonObject misc = v["misc"].toObject();
        const ReportBlock blk{5, 12, -3, 70000, 33, 0xdeadbeef, 99};
        QCOMPARE(hexs(buildSr(0x01020304, 1700000000123.5, 99, 5, 600, {blk})), misc["sr"].toString());
        QCOMPARE(hexs(buildSr(7, 1700000000999, 1, 2, 3, {})), misc["srEmpty"].toString());
        QCOMPARE(hexs(buildRr(9, {blk})), misc["rr"].toString());
        QCOMPARE(hexs(buildSdes(0xabcdef01)), misc["sdes"].toString());
        QCOMPARE(hexs(buildPli(1, 2)), misc["pli"].toString());
        QCOMPARE(hexs(buildNack(1, 2, {65534, 65535, 0, 3, 17, 18, 40})), misc["nack"].toString());

        NackTracker nt;
        for (const auto& e : v["nacks"].toArray()) {
            const QJsonObject o = e.toObject();
            const auto out = nt.note(u32(o["ssrc"]), uint16_t(o["seq"].toInt()), o["now"].toDouble());
            QJsonArray got;
            for (const auto& r : out) {
                QJsonArray seqs;
                for (auto s : r.seqs) seqs.append(int(s));
                got.append(QJsonObject{{"ssrc", double(r.ssrc)}, {"seqs", seqs}});
            }
            QCOMPARE(got, o["out"].toArray());
        }

        RtcpFeedback fb(4242);
        for (const auto& e : v["feed"].toArray()) {
            const QJsonObject o = e.toObject();
            const bool video = o["kind"].toString() == "video";
            const double now = o["now"].toDouble();
            fb.onRtp(video, hx(o["rtp"]), now);
            if (o.contains("rtcp")) {
                fb.onRtcp(false, hx(o["rtcp"]), now);
                fb.onRtcp(true, hx(o["rtcp"]), now);
            }
            if (o.contains("twcc")) {
                QJsonArray got;
                for (const auto& p : fb.twccPackets()) got.append(hexs(p));
                QCOMPARE(got, o["twcc"].toArray());
            }
            if (o.contains("audioReport")) {
                const int i = int(o["rtp"].toString().mid(8, 8).toUInt(nullptr, 16) / 960);
                QCOMPARE(hexs(fb.audioReport(now, uint32_t(i * 320), uint32_t(i), uint32_t(i * 40))), o["audioReport"].toString());
                const QByteArray vr = fb.videoReport(now);
                QCOMPARE(vr.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(hexs(vr)), o["videoReport"]);
                const QByteArray p = fb.pli();
                QCOMPARE(p.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(hexs(p)), o["pli"]);
            }
        }
        const QJsonObject c = v["counters"].toObject();
        QCOMPARE(fb.counters().twcc, c["twcc"].toInt());
        QCOMPARE(fb.counters().reports, c["reports"].toInt());
        QCOMPARE(fb.counters().pli, c["pli"].toInt());
    }

    void zrtp() {
        const QJsonObject v = loadVectors("zrtp.json").toObject();
        const QByteArray sess = QByteArray("sess-") + QByteArray(40, 'x');
        QCOMPARE(hexs(wrapMediaPacket(3, 0xfeedbeef, "abc")), v["media"].toString());
        ControlHeader h;
        h.msgType = 2;
        h.magic = 9;
        h.field02 = 5;
        h.seq = 6;
        h.uid = 7;
        h.token = 8;
        h.cmd = 300;
        h.subCmd = 260;
        QCOMPARE(hexs(buildControlHeader(h)), v["header"].toString());
        QCOMPARE(hexs(buildInitZrtpPacket(true, 11, 22, 33, sess)), v["initCaller"].toString());
        QCOMPARE(hexs(buildInitZrtpPacket(false, 11, 22, 33, sess, "ex")), v["initCallee"].toString());
        for (const auto& e : v["groupInit"].toArray()) {
            const QJsonObject o = e.toObject();
            const bool host = o["host"].toBool();
            QCOMPARE(hexs(buildGroupInitZrtpPacket(host, 5, sess, host, 77, 88, o["layout"].toString())), o["out"].toString());
        }
        QCOMPARE(hexs(buildShareInitZrtpPacket(99, sess)), v["shareInit"].toString());
        QCOMPARE(hexs(buildShareZaviPingPacket(99, 4, sess)), v["sharePing"].toString());
        QCOMPARE(hexs(buildZaviPingPacket(3, 4, sess, true)), v["zaviPing"].toArray()[0].toString());
        QCOMPARE(hexs(buildZaviPingPacket(3, 4, sess, false)), v["zaviPing"].toArray()[1].toString());
        QCOMPARE(hexs(buildPingPacket(3, 4, sess)), v["ping"].toString());
        QCOMPARE(hexs(buildEndCallPacket(3, 4, 0x80000001)), v["endCall"].toString());
        QCOMPARE(hexs(buildReqForwardPacket(3, 4, 9, 10, reqForwardStatus(49, 2, 3))), v["reqForward"].toString());
        QCOMPARE(hexs(buildGroupVidQualReq(3, 4, 1, {{10, 0xff}, {11, 3}})), v["vidQualReq"].toString());
        QCOMPARE(hexs(buildGroupVidQualConfig(3, 4, 2, {{0, 300, 720, 360}, {4, 70000, 1280, 720}})), v["vidQualCfg"].toString());
        QCOMPARE(hexs(buildGroupSyncPacket(3, 4, 5, 14)), v["groupSync"].toString());
        QCOMPARE(hexs(buildP2pBind(true, 0x01020304, 70000, 2)), v["p2pBind"].toString());
        QCOMPARE(hexs(buildP2pPong(false, QByteArray::fromHex("7f00010403020104000100"))), v["p2pPong"].toString());
        QCOMPARE(hexs(wrapP2pAudio(false, 5, QByteArray::fromHex("80"))), v["p2pAudio"].toString());
        QCOMPARE(hexs(wrapP2pVideo(true, 5, QByteArray::fromHex("80"))), v["p2pVideo"].toString());
        QCOMPARE(hexs(wrapP2pRtcp(true, 5, true, QByteArray::fromHex("81"))), v["p2pRtcp"].toArray()[0].toString());
        QCOMPARE(hexs(wrapP2pRtcp(true, 5, false, QByteArray::fromHex("81"))), v["p2pRtcp"].toArray()[1].toString());
        QCOMPARE(hexs(buildP2pBindAnswer(false, 0x01020304, QByteArray::fromHex("7f000004030201020007000200000001"), "10.0.0.5|4200")),
                 v["p2pAnswer"].toString());
        for (const auto& e : v["plans"].toArray()) {
            const QJsonObject o = e.toObject();
            QCOMPARE(planJson(groupVideoPlan(o["config"].toObject())), o["plan"]);
        }
        for (const auto& e : v["unwrap"].toArray()) {
            const QJsonObject o = e.toObject();
            const auto p = unwrapPacket(hx(o["in"]));
            QVERIFY(p);
            QCOMPARE(unwrapJson(*p), o["out"].toObject());
        }
        for (const auto& e : v["room"].toArray()) {
            const auto r = parseRoomUpdated(hx(e.toObject()["in"]));
            QJsonArray got;
            for (auto u : *r) got.append(double(u));
            QCOMPARE(got, e.toObject()["out"].toArray());
        }
    }

    void video() {
        const QJsonObject v = loadVectors("video.json").toObject();
        for (const auto& e : v["sps"].toArray())
            QCOMPARE(sizeJson(spsSize(hx(e.toObject()["in"]))), e.toObject()["out"]);
        const QByteArray sc = QByteArray::fromHex("00000001");
        const QJsonArray nal = v["nal"].toArray();
        QByteArray key;
        for (const auto& n : nal) key += sc + hx(n);
        QCOMPARE(QJsonValue(avcCodecString(key)), v["codec"].toArray()[0]);
        QVERIFY(avcCodecString(QByteArray::fromHex("000001650000000000")).isEmpty());
        QJsonArray gotNal;
        for (const auto& n : nalUnits(key + QByteArray(3, '\0'))) gotNal.append(hexs(n));
        QCOMPARE(gotNal, nal);
        QJsonArray pk;
        for (const auto& p : packetizeFrame(key)) pk.append(hexs(p));
        QCOMPARE(pk, v["packets"].toArray());
        QJsonArray small;
        for (const auto& p : packetizeFrame(QByteArray::fromHex("00000165aa"))) small.append(hexs(p));
        QCOMPARE(small, v["small"].toArray());
        QJsonArray exact;
        for (const auto& p : packetizeFrame(QByteArray(2136, 'x'))) exact.append(int(p.size()));
        QCOMPARE(exact, v["exact"].toArray());

        for (const bool group : {false, true}) {
            const QJsonObject a = v[group ? "groupAsm" : "asm"].toObject();
            double now = 5000;
            QJsonArray frames;
            int gaps = 0;
            VideoAssembler asmb(
                [&](const VideoFrame& f) {
                    frames.append(QJsonObject{{"key", f.key}, {"data", hexs(f.data)}, {"ts", double(f.ts)}, {"at", now}});
                },
                [&now] { return now; }, group, [&gaps] { ++gaps; });
            for (const auto& s : a["steps"].toArray()) {
                const QJsonObject st = s.toObject();
                const QJsonObject p = st["pkt"].toObject();
                now = st["now"].toDouble();
                VideoPacket pkt;
                pkt.ts = u32(p["ts"]);
                pkt.pt = p["pt"].toInt();
                pkt.marker = p["marker"].toBool();
                pkt.payload = hx(p["payload"]);
                pkt.seq = uint16_t(p["seq"].toInt());
                pkt.roc = u32(p["roc"]);
                asmb.push(pkt);
            }
            QCOMPARE(frames.size(), a["frames"].toArray().size());
            QCOMPARE(frames, a["frames"].toArray());
            QCOMPARE(gaps, a["gaps"].toInt());
            const QJsonObject st = a["stats"].toObject();
            QCOMPARE(asmb.stats.frames, st["frames"].toInt());
            QCOMPARE(asmb.stats.dropped, st["dropped"].toInt());
            QCOMPARE(asmb.stats.keys, st["keys"].toInt());
            QCOMPARE(asmb.stats.gaps, st["gaps"].toInt());
            QJsonObject nb;
            for (const auto& [t, n] : asmb.nalBytes) nb.insert(QString::number(t), double(n));
            QCOMPARE(nb, a["nalBytes"].toObject());
            const QJsonObject sz = a["spsSize"].toObject();
            QCOMPARE(asmb.spsSize7.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(asmb.spsSize7), sz["7"]);
            QCOMPARE(asmb.spsSize15.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(asmb.spsSize15), sz["15"]);
        }
    }

    void sendRate() {
        const QJsonArray scenarios = loadVectors("send-rate.json").toArray();
        QVERIFY(!scenarios.isEmpty());
        for (const auto& sv : scenarios) {
            const QJsonObject sc = sv.toObject();
            double now = sc["start"].toDouble();
            QStringList logs;
            QJsonArray profiles;
            SendRate::Options o;
            o.localSsrc = 4242;
            o.log = [&logs](const QString& l) { logs << l; };
            o.onProfile = [&profiles](const SendProfile& p) { profiles.append(p.toJson()); };
            o.now = [&now] { return now; };
            const QJsonObject opts = sc["opts"].toObject();
            if (opts.contains("layers")) {
                QVector<VideoRow> rows;
                for (const auto& l : opts["layers"].toArray()) {
                    const QJsonObject r = l.toObject();
                    rows.push_back(VideoRow{r["width"].toInt(), r["height"].toInt(), r["fps"].toInt(), u32(r["kbps"])});
                }
                o.layers = rows;
                o.startIndex = opts["startIndex"].toInt();
            }
            SendRate sr(sc["cfg"].toObject(), o);
            for (const auto& stv : sc["steps"].toArray()) {
                const QJsonObject st = stv.toObject();
                for (const auto& s : st["sent"].toArray()) {
                    now = s.toArray()[1].toDouble();
                    sr.onSent(uint16_t(s.toArray()[0].toInt()));
                }
                for (const auto& r : st["rtcp"].toArray())
                    sr.onRtcp(r.toArray()[0].toString() == "v" ? 'v' : 'a', hx(r.toArray()[1]));
                now = st["now"].toDouble();
                const QJsonValue dl = st["downLoss"];
                sr.tick(st["sending"].toBool(), dl.isNull() ? std::nullopt : std::optional<double>(dl.toDouble()));
                QCOMPARE(sr.level(), st["level"].toInt());
            }
            const QJsonArray wantLogs = sc["logs"].toArray();
            QCOMPARE(logs.size(), wantLogs.size());
            for (int i = 0; i < logs.size(); ++i) QCOMPARE(logs[i], wantLogs[i].toString());
            QCOMPARE(profiles, sc["profiles"].toArray());
            const QJsonObject t = sc["totals"].toObject();
            QCOMPARE(double(sr.totals().twccPkts), t["twccPkts"].toDouble());
            QCOMPARE(double(sr.totals().twccLost), t["twccLost"].toDouble());
            QCOMPARE(double(sr.totals().rrBlocks), t["rrBlocks"].toDouble());
            QCOMPARE(sr.totals().changes, t["changes"].toInt());
            QCOMPARE(sr.totals().downs, t["downs"].toInt());
        }
    }

    void jsonCppMatches() {
        for (const auto& e : loadVectors("jsoncpp.json").toArray()) {
            const QJsonObject o = e.toObject();
            QCOMPARE(jsonCpp(o["in"]), o["out"].toString());
        }
    }

    void uiStatusCodes() {
        const QStringList specific{
            "ringing", "endedWithDuration", "disconnected", "recipientUnreachable", "recipientNoAnswer", "recipientDeclined",
            "callFailed", "serverUnreachable", "groupUnsupported", "groupCallIgnored", "groupCallFailed",
            "groupCallFailedReason", "groupJoinRejected", "groupDeclined", "groupKicked", "groupNoAnswer", "groupCancelled",
            "groupRinging", "groupWaitingOthers", "groupParticipants", "incomingGroupInvite", "incomingGroupUnsupported",
            "audioMicSpeakerFailed", "audioMicFailed", "audioSpeakerFailed"};
        const QStringList generic{"calling", "connecting", "connected", "ended", "incomingVoice", "incomingVideo",
                                  "groupEnded", "groupCalling"};
        const QString ended = zcall::statusText(QStringLiteral("ended"));
        QVERIFY(!ended.isEmpty());
        for (const QString& code : specific + generic) {
            const QString text = zcall::statusText(code);
            QVERIFY2(!text.isEmpty(), qPrintable(code));
            if (code != QLatin1String("ended")) QVERIFY2(text != ended, qPrintable(code));
        }
        QCOMPARE(zcall::statusText(QStringLiteral("not-a-code")), ended);
        QCOMPARE(zcall::statusText(QString()), QString());
        QCOMPARE(zcall::statusText(QStringLiteral("endedWithDuration"), QJsonObject{{"duration", 42}}),
                 QStringLiteral("Kết thúc — 42 s"));
        QCOMPARE(zcall::statusText(QStringLiteral("groupParticipants"),
                                   QJsonObject{{"count", 3}, {"names", QJsonArray{"Fixture A", QJsonValue()}}}),
                 QStringLiteral("3 người: Bạn, Fixture A, Thành viên"));
        QCOMPARE(zcall::statusText(QStringLiteral("incomingGroupInvite"), QJsonObject{{"inviter", ""}}),
                 QStringLiteral("Ai đó mời bạn vào cuộc gọi nhóm"));
        QCOMPARE(zcall::statusText(QStringLiteral("groupCallFailedReason"), QJsonObject{{"message", "fixture"}}),
                 QStringLiteral("Không gọi được: fixture"));

        QFile overlay(QStringLiteral(ZCALL_VECTORS_DIR "/../../../../src/qml/CallOverlay.qml"));
        if (!overlay.open(QIODevice::ReadOnly)) QSKIP("CallOverlay.qml not in this source tree");
        const QString qml = QString::fromUtf8(overlay.readAll());
        for (const QString& code : specific)
            QVERIFY2(qml.contains(QStringLiteral("case \"%1\"").arg(code)), qPrintable(code));
    }
};

QTEST_GUILESS_MAIN(TestCallWire)
#include "test_call_wire.moc"
