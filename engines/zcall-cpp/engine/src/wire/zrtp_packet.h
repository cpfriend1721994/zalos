// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

namespace zcall {

namespace MsgType {
constexpr int Control = 0x01;
constexpr int ServerReply = 0x02;
constexpr int AudioRtp = 0x03;
constexpr int AudioRtpDown = 0x04;
constexpr int AudioFec = 0x05;
constexpr int AudioRtcp = 0x07;
constexpr int VideoRtp = 0x0d;
constexpr int VideoDown = 0x0e;
constexpr int VideoFec = 0x0f;
constexpr int P2pExt = 0x7f;
}

namespace Cmd {
constexpr int Ping = 2;
constexpr int EndCall = 3;
constexpr int InitCallCaller = 11;
constexpr int InitCallCallee = 12;
constexpr int Echo = 5;
constexpr int ChangeAddress = 14;
constexpr int ReqForward = 32;
constexpr int GroupMessage = 33;
constexpr int RoomUpdated = 50;
constexpr int InitZaviPing = 0x33;
}

namespace P2p {
constexpr int HeaderLen = 9;
constexpr int Bind = 1;
constexpr int BindPeer = 2;
constexpr int BindAnswer = 3;
constexpr int Keepalive = 4;
constexpr int Audio = 6;
constexpr int AudioRtcp = 7;
constexpr int Video = 8;
constexpr int VideoRtcp = 9;
}

constexpr int kMagicByte = 0x7e;
constexpr int kControlHeaderLen = 21;
constexpr int kMediaHeaderLen = 5;

struct ControlHeader {
    int msgType{MsgType::Control};
    int magic{kMagicByte};
    uint32_t field02{0};
    uint32_t seq{0};
    uint32_t uid{0};
    uint32_t token{0};
    int cmd{0};
    int subCmd{0};
};

struct VidQualEntry {
    uint32_t uid{0};
    int qualityId{0};
};

struct VidLayer {
    int id{0};
    uint32_t a{0};
    uint32_t b{0};
    uint32_t c{0};
};

struct VideoRow {
    int width{0};
    int height{0};
    int fps{0};
    uint32_t kbps{0};
};

struct EncodeProfile {
    int width{0};
    int height{0};
    int fps{0};
    int64_t bitrate{0};
    int64_t keyMs{0};
};

struct GroupVideoPlan {
    QVector<VidLayer> layers;
    EncodeProfile encode;
    QVector<VidLayer> send;
    bool hasSfu{false};
    int sfuId{14};
    bool sfuLong{true};
    QVector<VideoRow> rows;
    int start{0};
};

bool isMediaType(int t);
QByteArray wrapMediaPacket(int msgType, uint32_t token, const QByteArray& payload);
QByteArray buildControlHeader(const ControlHeader& h);
QByteArray buildInitZrtpPacket(bool caller, uint32_t uid, uint32_t peerUid, uint32_t callId, const QByteArray& sessId,
                               const QByteArray& extra = {}, int subCmd = 0x02);
QByteArray groupExtra(bool videoOn, bool initZrtp);
extern const QStringList kGroupInitLayouts;
QByteArray buildGroupInitZrtpPacket(bool host, uint32_t uid, const QByteArray& sessId, bool videoOn, uint32_t callId,
                                    uint32_t hostCall, const QString& layout);
QByteArray buildShareInitZrtpPacket(uint32_t uid, const QByteArray& sessId);
QByteArray buildShareZaviPingPacket(uint32_t uid, uint32_t token, const QByteArray& sessId);
QByteArray buildZaviPingPacket(uint32_t uid, uint32_t token, const QByteArray& sessId, bool videoOn);
std::optional<QVector<uint32_t>> parseRoomUpdated(const QByteArray& data);
QByteArray buildPingPacket(uint32_t uid, uint32_t token, const QByteArray& sessId);
QByteArray buildEndCallPacket(uint32_t uid, uint32_t token, uint32_t callId);
QByteArray buildReqForwardPacket(uint32_t uid, uint32_t token, uint32_t seq, int subCmd, const QByteArray& payload);
QByteArray reqForwardStatus(uint32_t rxPerSecond, uint32_t a = 0, uint32_t b5 = 0);
QByteArray buildGroupVidQualReq(uint32_t uid, uint32_t token, uint32_t seq, const QVector<VidQualEntry>& entries);
QByteArray buildGroupVidQualConfig(uint32_t uid, uint32_t token, uint32_t seq, const QVector<VidLayer>& layers);
std::optional<GroupVideoPlan> groupVideoPlan(const QJsonObject& config);
QByteArray buildGroupSyncPacket(uint32_t uid, uint32_t token, uint32_t seq, int subCmd);

QByteArray buildP2pBind(bool callee, uint32_t callId, uint32_t seq, uint32_t index);
QByteArray buildP2pBindAnswer(bool callee, uint32_t callId, const QByteArray& request, const QByteArray& peerAddr);
QByteArray buildP2pPong(bool callee, const QByteArray& ping);
QByteArray wrapP2pAudio(bool callee, uint32_t callId, const QByteArray& rtp);
QByteArray wrapP2pVideo(bool callee, uint32_t callId, const QByteArray& rtp);
QByteArray wrapP2pRtcp(bool callee, uint32_t callId, bool video, const QByteArray& rtcp);

struct Unwrapped {
    bool isMedia{false};
    int msgType{0};
    bool truncated{false};
    uint32_t token{0};
    QByteArray payload;
    bool isControl{false};
    int magic{0};
    uint32_t field02{0};
    uint32_t seq{0};
    uint32_t uid{0};
    int cmd{-1};
    int subCmd{-1};
    QByteArray data;
    std::optional<uint32_t> res;
    std::optional<uint32_t> callId;
    std::optional<uint32_t> serverToken;
    std::optional<QByteArray> publicAddr;
    std::optional<uint32_t> peerUid;
    std::optional<QByteArray> sessId;
    std::optional<QByteArray> extra;
    int b1{0};
    int b2{0};
    uint32_t field38{0};
    QByteArray raw;
};

std::optional<Unwrapped> unwrapPacket(const QByteArray& buf);

int jsTrunc(double v);
int envInt(const char* name, int fallback, bool* present = nullptr);

}
