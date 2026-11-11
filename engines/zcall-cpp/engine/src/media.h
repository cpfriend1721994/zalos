// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "send_rate.h"
#include "wire/video.h"
#include "wire/zrtp_packet.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>
#include <optional>

namespace zcall {

struct NegotiatedParams {
    bool muted{false};
    bool speakerOff{false};
    bool group{false};
    bool host{false};
    QJsonArray servers;
    QJsonObject config;
    QJsonValue srtpMode;
    QString sessId;
    double callId{0};
    QString role;
    double localUid{0};
    double peerUid{0};
    double hostCall{0};
    QJsonArray p2pCandidates;
};

struct ServerAddr {
    QString host;
    uint16_t port{0};
    QJsonObject raw;

    bool operator==(const ServerAddr& o) const { return host == o.host && port == o.port; }
};

struct InitReply {
    int serverIndex{-1};
    ServerAddr server;
    uint32_t token{0};
    double rttMs{0};
    std::optional<QByteArray> publicAddr;
    QString hex;
};

struct MediaStats {
    double rx{0};
    double tx{0};
};

struct OutVideoFrame {
    bool key{false};
    QByteArray data;
    bool screen{false};
    int w{0};
    int h{0};
};

class MediaListener {
public:
    virtual ~MediaListener() = default;
    virtual void mediaTimeout() = 0;
    virtual void mediaFailed(const QString& why) = 0;
    virtual void mediaPeerEnd(const QString& why) = 0;
    virtual void mediaEarlyAudio(const QString& phase) = 0;
    virtual void mediaAudioFault(const QString& kind) = 0;
    virtual void mediaMicVolume(int pct) = 0;
    virtual void mediaMicLevel(int level) = 0;
    virtual void mediaRoster(const QVector<uint32_t>& uids) = 0;
    virtual void mediaMemberLeft(uint32_t uid) = 0;
    virtual void mediaMemberAudio(uint32_t uid) = 0;
    virtual void mediaMemberSpeaking(uint32_t uid, bool on) = 0;
    virtual void mediaVideo(const VideoPacket& pkt) = 0;
    virtual void mediaKeyframe(const QString& why) = 0;
    virtual void mediaSendProfile(const SendProfile& p) = 0;
};

class MediaBackend {
public:
    virtual ~MediaBackend() = default;
    void setListener(MediaListener* l) { m_listener = l; }

    virtual void onNegotiated(const NegotiatedParams& params) = 0;
    virtual void prepare(const QString& role, std::function<void(std::optional<InitReply>)> done) = 0;
    virtual void start(const QString& role, const QString& rtpSerIp) = 0;
    virtual void stop() = 0;
    virtual void setMute(bool) {}
    virtual std::optional<MediaStats> stats() const { return std::nullopt; }
    virtual bool sendUdp() const { return false; }
    virtual double lastRxAt() const { return 0; }
    virtual bool hasLocalCandidates() const { return false; }
    virtual QJsonArray localCandidates(const std::optional<QByteArray>&) const { return {}; }
    virtual bool hasDeviceControl() const { return false; }
    virtual void setDevice(const QString&, const QString&) {}
    virtual void setMicVolume(double) {}
    virtual void setSpeaker(bool) {}
    virtual bool hasVideo() const { return false; }
    virtual void sendVideoFrame(const OutVideoFrame&) {}
    virtual void setGroupCamera(bool) {}
    virtual void startShare(uint32_t) {}
    virtual void stopShare() {}
    virtual void setMemberRenderWidth(uint32_t, double) {}
    virtual void requestPeerKeyFrame(const QString&, const QJsonValue&) {}
    virtual const GroupVideoPlan* videoPlan() const { return nullptr; }
    virtual std::optional<int> vidAsked(uint32_t) const { return std::nullopt; }
    virtual bool hasVidAsked() const { return false; }
    virtual QJsonObject paramsConfig() const { return {}; }
    virtual bool hasParams() const { return false; }

protected:
    MediaListener* m_listener{nullptr};
};

}
