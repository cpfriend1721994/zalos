// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "media.h"
#include "send_rate.h"
#include "wire/rtcp.h"
#include "wire/srtp.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QUdpSocket>

#include <functional>
#include <memory>
#include <optional>

namespace zcall {

struct RocTracker {
    std::optional<int64_t> last;
    uint32_t roc{0};
    uint32_t guess(uint16_t seq) const;
    void update(uint16_t seq, uint32_t r);
};

class ZrtcMedia : public QObject, public MediaBackend {
    Q_OBJECT

public:
    ZrtcMedia(bool sendUdp, QObject* parent = nullptr);
    ~ZrtcMedia() override;

    void onNegotiated(const NegotiatedParams& params) override;
    void prepare(const QString& role, std::function<void(std::optional<InitReply>)> done) override;
    void start(const QString& role, const QString& rtpSerIp) override;
    void stop() override;
    void setMute(bool on) override;
    std::optional<MediaStats> stats() const override;
    bool sendUdp() const override { return m_sendUdpEnabled; }
    double lastRxAt() const override { return m_lastRxAt; }
    bool hasLocalCandidates() const override { return true; }
    QJsonArray localCandidates(const std::optional<QByteArray>& publicAddr) const override;
    void setDevice(const QString& kind, const QString& name) override;
    void setMicVolume(double pct) override;
    void setSpeaker(bool off) override;
    void sendVideoFrame(const OutVideoFrame& f) override;
    void setGroupCamera(bool on) override;
    void setMemberRenderWidth(uint32_t uid, double width) override;
    void requestPeerKeyFrame(const QString& why, const QJsonValue& src) override;
    const GroupVideoPlan* videoPlan() const override { return m_plan ? &*m_plan : nullptr; }
    std::optional<int> vidAsked(uint32_t uid) const override;
    bool hasVidAsked() const override { return true; }
    QJsonObject paramsConfig() const override { return m_params.config; }
    bool hasParams() const override { return m_hasParams; }

private:
    using Server = ServerAddr;

    struct Reply {
        Server server;
        uint32_t token{0};
        double rttMs{0};
        std::optional<QByteArray> publicAddr;
        QString hex;
    };
    struct Speak {
        QVector<bool> hist;
        bool on{false};
        double lastLoud{0};
    };
    struct Queued {
        QByteArray pkt;
        Server dst;
    };
    struct Counters {
        uint64_t tx{0};
        uint64_t rx{0};
        uint64_t rxBad{0};
        uint64_t rxSecond{0};
        uint64_t rxP2p{0};
    };

    void resetCounters();
    void initSocket();
    void sendTo(const QByteArray& pkt, const Server& s);
    void onDatagram();
    void onUdp(const QByteArray& msg, const QHostAddress& addr, quint16 port);
    QTimer* timer(int ms, bool repeat, std::function<void()> fn);
    void clearTimers();

    void initZrtp(const QByteArray& packet, std::function<void(std::optional<InitReply>)> done);
    void initSendAll();
    void initRetry();
    void checkInitProgress();
    void finishInit();
    void prepareGroup(int layoutIndex, std::function<void(std::optional<InitReply>)> done);
    void sendEndCall(const Server& s, uint32_t token);

    void sendRtcp(bool video, const QByteArray& rtcp);
    void sendTwcc();
    void sendRtcpReports();
    void peerRtcp(bool video, const QByteArray& buf);
    std::optional<double> downlinkLoss();

    void sendPing();
    void sendStatus();
    void startAudio();
    void stopAudio();
    void onAudioStdout();
    void onAudioStderr();
    void sendAudioFrame(const QByteArray& opus, int level);
    void noteAudioTx();
    void noteVideoTx(bool key);
    void onAudio(const QByteArray& rtp);
    void onVideo(const QByteArray& rtp);
    void onVideoRtcp(const QByteArray& buf);
    void onP2p(const QByteArray& msg, const QHostAddress& addr, quint16 port);
    void p2pUp(const Server& from);
    void p2pStart();
    void p2pBindTick();
    void drainVideo();
    void drainStep();
    void mediaFailed(const QString& why);
    void sendVidQual();
    void sendVidQualConfig();
    void noteVideoMember(uint32_t ssrc);
    int qualityIdFor(uint32_t uid) const;
    void onGroupControl(const Unwrapped& p);
    void applySendProfile(const SendProfile& p);
    bool groupCameraSend() const;
    void noteSpeaking(uint32_t ssrc, const QByteArray& rtp);

    bool m_sendUdpEnabled{false};
    bool m_hasParams{false};
    bool m_active{false};
    bool m_group{false};
    bool m_muted{false};
    bool m_speakerOff{false};
    bool m_plainMedia{false};
    NegotiatedParams m_params;
    std::optional<GroupVideoPlan> m_plan;
    QVector<Server> m_servers;
    std::optional<Server> m_activeServer;
    uint32_t m_serverToken{0};
    std::optional<SrtpKeys> m_srtp;
    std::unique_ptr<RtcpFeedback> m_rtcp;
    std::unique_ptr<NackTracker> m_nack;
    std::unique_ptr<SendRate> m_sendRate;

    QUdpSocket* m_socket{nullptr};
    QList<QTimer*> m_timers;

    QVector<Reply> m_replies;
    QVector<Server> m_refused;
    QSet<QString> m_ended;
    double m_initSentAt{0};
    bool m_initActive{false};
    int m_initTries{0};
    QByteArray m_initPacket;
    QTimer* m_initWait{nullptr};
    std::function<void(std::optional<InitReply>)> m_initDone;

    uint32_t m_seq{0};
    uint32_t m_ts{0};
    uint16_t m_twSeq{1};
    uint32_t m_vSeq{0};
    uint32_t m_vRoc{0};
    uint32_t m_vTsBase{0};
    double m_vStart{0};
    uint32_t m_vLayerSeq{0};
    uint32_t m_ctlSeq{0};
    uint32_t m_roc{0};
    uint32_t m_fwdSeq{1};
    uint64_t m_audioOctets{0};
    bool m_vKeySent{false};
    bool m_groupCameraOn{false};
    bool m_vidCfgLogged{false};
    QSet<uint32_t> m_vidWanted;
    QHash<uint32_t, uint32_t> m_renderWidth;
    QHash<uint32_t, int> m_vidAsked;
    RocTracker m_rxRoc;
    QHash<uint32_t, RocTracker> m_rxRocs;
    RocTracker m_videoRoc;
    QHash<uint32_t, RocTracker> m_videoRocs;
    QHash<uint32_t, int> m_members;
    QHash<uint32_t, Speak> m_speak;
    QHash<uint32_t, double> m_pliAt;
    double m_lastPliAt{0};
    QVector<Queued> m_vOut;
    bool m_vDraining{false};

    Counters m_counters;
    double m_lastRxAt{0};
    double m_startedAt{0};
    std::optional<Server> m_p2pPath;
    bool m_p2pSeen{false};
    uint32_t m_p2pSeq{0};
    double m_p2pBindStart{0};
    QVector<std::pair<Server, uint32_t>> m_p2pCands;

    QProcess* m_audio{nullptr};
    QByteArray m_audioOut;
    QByteArray m_audioErr;
    QString m_mic;
    QString m_speaker;
    int m_micPeak{127};
    double m_micLevelAt{0};
    std::optional<std::pair<double, double>> m_downPrev;
    double m_lastRtcpLogAt{0};
    double m_aTxLast{0};
    uint32_t m_aTxN{0};
    double m_aTxMaxGap{0};
    double m_aTxSince{0};
    double m_vTxLastTx{0};
    double m_vTxSince{0};
    uint32_t m_vTxFrames{0};
    uint32_t m_vTxKeys{0};
    uint64_t m_vTxBytes{0};
    uint32_t m_vTxPkts{0};
};

}
