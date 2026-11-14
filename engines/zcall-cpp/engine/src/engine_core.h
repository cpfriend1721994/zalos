// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "call_ui.h"
#include "media.h"
#include "wire/video.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace zcall {

struct MemberKey {
    bool numeric{true};
    double uid{0};
    QString str;

    static MemberKey ofUid(double u) { return MemberKey{true, u, QString()}; }
    static MemberKey ofStr(const QString& s) { return MemberKey{false, 0, s}; }
    bool operator==(const MemberKey& o) const { return numeric == o.numeric && (numeric ? uid == o.uid : str == o.str); }
    QJsonValue toJson() const { return numeric ? QJsonValue(uid) : QJsonValue(str); }
};

struct Member {
    QString name;
    QString avatar;
    QString state;
    bool muted{false};
    bool camOff{false};
};

struct MemberInfo {
    std::optional<QString> state;
    std::optional<bool> muted;
    std::optional<bool> camOff;
};

struct MemberVideo {
    QString codec;
    double since{0};
    int frames{0};
    int noCodec{0};
    std::unique_ptr<VideoAssembler> assembler;
};

struct Call {
    bool group{false};
    QString role;
    QString state;
    QString peerId;
    double peerUid{0};
    double localUid{0};
    QString peerName;
    QString avatar;
    QJsonValue type;
    double callId{0};
    QJsonValue sessId;
    QJsonObject server;
    QJsonObject config;
    QJsonArray p2p;
    double startedAt{0};
    QTimer* ringTimer{nullptr};
    QTimer* pingTimer{nullptr};
    bool muted{false};
    bool speakerOff{false};
    bool camReported{false};
    bool cameraOff{false};
    std::optional<bool> cameraOn;
    bool sharing{false};
    double keyAskedAt{0};
    bool hasVideoRx{false};
    double videoRxSince{0};
    int videoRxFrames{0};
    int videoRxTotal{0};
    std::unique_ptr<VideoAssembler> video;
    QString videoCodec;
    QSet<QString> audioFaults;
    QString audioNotice;
    bool peerCamOff{false};
    bool peerMuted{false};
    bool peerSharing{false};

    double hostCall{0};
    QString groupIdStr;
    double groupId{0};
    int callType{0};
    QJsonArray servers;
    QString title;
    double maxUsers{8};
    QStringList invited;
    struct InvitePartner {
        QString name;
        QString avatar;
        QString id;
    };
    std::optional<std::vector<InvitePartner>> invitePartners;
    std::vector<std::pair<MemberKey, Member>> members;
    double pingMs{10000};
    bool answered{false};
    double shareScreenId{0};
    QSet<double> greeted;
    QString membersKey;
    QString statusText;
    bool hasStatusText{false};
    std::map<uint32_t, MemberVideo> videos;

    Member* member(const MemberKey& k);
    void setMember(const MemberKey& k, const Member& m);
    bool removeMember(const MemberKey& k);
};

class EngineCore : public QObject, public MediaListener, public UiListener {
    Q_OBJECT

public:
    using EmitFn = std::function<void(const QJsonObject& frame)>;

    EngineCore(EmitFn sendFn, MediaBackend* media, CallUi* ui, bool group, QObject* parent = nullptr);
    ~EngineCore() override;

    void setClock(std::function<double()> now) { m_now = std::move(now); }
    void setRandom(std::function<double()> random) { m_random = std::move(random); }

    void start();
    void onZaloFrame(const QJsonObject& f);
    void hangup(const QString& why);
    void answer(bool camStartOff = false);
    void reject(const QString& why = QStringLiteral("reject"));
    bool inCall() const { return m_call != nullptr; }

    void mediaTimeout() override;
    void mediaFailed(const QString& why) override;
    void mediaPeerEnd(const QString& why) override;
    void mediaEarlyAudio(const QString& phase) override;
    void mediaAudioFault(const QString& kind) override;
    void mediaMicVolume(int pct) override;
    void mediaMicLevel(int level) override;
    void mediaRoster(const QVector<uint32_t>& uids) override;
    void mediaMemberLeft(uint32_t uid) override;
    void mediaMemberAudio(uint32_t uid) override;
    void mediaMemberSpeaking(uint32_t uid, bool on) override;
    void mediaVideo(const VideoPacket& pkt) override;
    void mediaKeyframe(const QString& why) override;
    void mediaSendProfile(const SendProfile& p) override;

    void uiHangup() override;
    void uiMute(bool on) override;
    void uiSpeaker(bool on) override;
    void uiVideoFrame(const OutVideoFrame& f) override;
    void uiCamera(bool on) override;
    void uiDevice(const QString& kind, const QString& name) override;
    void uiMicVolume(double pct) override;
    void uiScreen(bool on) override;
    void uiNeedKey(const QString& why, const QJsonValue& src) override;
    void uiTile(uint32_t src, double width) override;

private:
    using CallPtr = std::shared_ptr<Call>;

    double now() const { return m_now(); }
    double newCallId() const;
    void send(const QString& type, const QJsonValue& command, const QJsonObject& data);
    void log(const QString& line) const;
    void later(int ms, std::function<void()> fn);
    QTimer* timer(int ms, bool repeat, std::function<void()> fn);
    QString selfAvatar() const;

    void onUpdate(const QString& command, const QJsonObject& data);
    void onRequest(const QString& command, const QJsonObject& data);
    void makeCall(const QJsonObject& data);
    void setCallTimeout(const CallPtr& c, int ms);
    void onAliasName(const QJsonObject& d);
    void screenShare(bool on);
    void groupNotSupported(const QString& title, const QString& avatar);
    void incomingGroupNotice(const QJsonObject& d);
    void camMic(std::optional<bool> camOn = std::nullopt);
    void micState(bool micOn);
    void onControl(const QJsonValue& act, const QJsonObject& data);
    void rememberEndedCall(const QJsonValue& callId);
    void pruneEndedCalls();
    bool isEndedCall(double id);
    void incoming(const QJsonObject& d);
    void requestKeyFrame(const QString& why);
    void askPeerKey(const QString& why, const QJsonValue& src = QJsonValue());
    void noteVideoRx(Call& c);
    void noteMemberRx(MemberVideo& v, uint32_t uid);
    void onVideoPacket(const VideoPacket& pkt);
    void connectCaller(const QString& rtpSerIp);
    void earlyAudio(const QString& phase);
    void audioFault(const QString& kind);
    void peerEnded(const QString& why);
    void onRecvSignal(const QString& command, const QJsonObject& data);
    QJsonObject extendData(const Call& c, const QJsonObject& server, const std::optional<QByteArray>& publicAddr,
                           const QJsonObject& extra = {}) const;
    void send416(const CallPtr& c, const QJsonObject& server, const std::optional<QByteArray>& publicAddr);

    void onGroupControl(const QString& act, const QJsonObject& d);
    void groupOutgoing(const QJsonObject& data);
    void groupRequestAnswered(const QJsonObject& data);
    void groupIncoming(const QJsonObject& d);
    void groupAnswer(bool camStartOff);
    void groupReject(const QString& why);
    void groupSendAnswer(const Call& c, int status);
    void groupCameraProfile();
    QJsonObject groupState(const Call& c) const;
    void groupGreet(double uid);
    void groupBroadcast();
    void groupPing();
    void groupMember(double uid, const MemberInfo& info);
    void groupMembersToUi();
    void groupMemberIn(double uid);
    void groupRoster(const QVector<uint32_t>& uids);
    void groupMemberLeft(double uid, const QString& why);
    void groupCheckEmpty(const QString& why);
    void groupStatus();
    void groupEnded(const QString& why);
    void groupHangup(const QString& why);
    void groupCancel(const QString& why);
    void groupFinish(int status, const QString& closeCode = QString(), bool emitSignals = true);
    void finish(int status, const QString& closeCode = QString(), bool emitSignals = true);
    void clearCallTimers(Call& c);

    EmitFn m_emit;
    MediaBackend* m_media;
    CallUi* m_ui;
    bool m_groupEnabled;
    CallPtr m_call;
    std::vector<std::pair<double, double>> m_endedCalls;
    QJsonObject m_local;
    std::shared_ptr<int> m_groupNotice;
    std::function<double()> m_now;
    std::function<double()> m_random;
};

}
