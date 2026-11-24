#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTemporaryFile>
#include <QVariantMap>
#include <QVideoSink>
#include <QPointer>
#include "camera_sender.h"

#include <atomic>
#include <functional>
#include <memory>

class QProcess;
class QTcpServer;
class QTcpSocket;
class QThread;
class QTimer;
class QVideoFrame;

namespace Zalos::Core::Network {
class ZaloSession;
}

namespace Zalos::Core::Call {

class PeerVideoDecoder;

class CallBridge : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString peerName READ peerName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString peerAvatar READ peerAvatar NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString statusCode READ statusCode NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap statusParams READ statusParams NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString noticeCode READ noticeCode NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap noticeParams READ noticeParams NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 since READ since NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool muted READ muted NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool speakerOff READ speakerOff NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool peerMuted READ peerMuted NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool canAnswer READ canAnswer NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isVideo READ isVideo NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isGroup READ isGroup NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList groupMembers READ groupMembers NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool cameraOn READ cameraOn NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(bool screenShareOn READ screenShareOn NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(bool screenShareAvailable READ screenShareAvailable NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(QString cameraError READ cameraError NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(bool peerCamOff READ peerCamOff NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool peerSharing READ peerSharing NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool videoSupported READ videoSupported CONSTANT FINAL)
    Q_PROPERTY(bool peerVideoActive READ peerVideoActive NOTIFY peerVideoChanged FINAL)
    Q_PROPERTY(int micVolume READ micVolume NOTIFY micStateChanged FINAL)
    Q_PROPERTY(bool micAutoGain READ micAutoGain NOTIFY micStateChanged FINAL)
    Q_PROPERTY(int micLevel READ micLevel NOTIFY micStateChanged FINAL)
    Q_PROPERTY(QString selectedMicId READ selectedMicId NOTIFY devicesChanged FINAL)
    Q_PROPERTY(QString selectedSpeakerId READ selectedSpeakerId NOTIFY devicesChanged FINAL)
    Q_PROPERTY(QString selectedCameraId READ selectedCameraId NOTIFY devicesChanged FINAL)
    Q_PROPERTY(QVariantList audioInputDeviceList READ audioInputDevices NOTIFY devicesChanged FINAL)
    Q_PROPERTY(QVariantList audioOutputDeviceList READ audioOutputDevices NOTIFY devicesChanged FINAL)
    Q_PROPERTY(QVariantList cameraDeviceList READ cameraDevices NOTIFY devicesChanged FINAL)

public:
    explicit CallBridge(Network::ZaloSession* session, QObject* parent = nullptr);
    ~CallBridge() override;

    bool isAvailable() const { return m_unavailableReason.isEmpty(); }
    QString unavailableReason() const { return m_unavailableReason; }
    QString phase() const { return m_ui.value("phase").toString(); }
    QString peerName() const { return m_ui.value("name").toString(); }
    QString peerAvatar() const { return m_ui.value("avatar").toString(); }
    QString statusText() const { return m_ui.value("text").toString(); }
    QString statusCode() const { return m_ui.value("statusCode").toString(); }
    QVariantMap statusParams() const { return m_ui.value("statusParams").toObject().toVariantMap(); }
    QString noticeCode() const { return m_ui.value("noticeCode").toString(); }
    QVariantMap noticeParams() const { return m_ui.value("noticeParams").toObject().toVariantMap(); }
    qint64 since() const { return m_ui.value("since").toVariant().toLongLong(); }
    bool muted() const { return m_ui.value("muted").toBool(); }
    bool speakerOff() const { return m_ui.value("speakerOff").toBool(); }
    bool peerMuted() const { return m_ui.value("peerMuted").toBool(); }
    bool canAnswer() const { return phase() == "incoming" && !m_ui.value("noAnswer").toBool(); }
    bool isVideo() const { return m_ui.value("video").toBool() || m_isVideoMode; }
    bool isGroup() const { return m_ui.value("group").toBool() || m_isGroupMode; }
    QVariantList groupMembers() const { return m_ui.value("members").toArray().toVariantList(); }
    bool cameraOn() const { return m_cameraOn; }
    bool cameraAvailable() const;
    bool screenShareOn() const { return m_screenShareOn; }
    bool screenShareAvailable() const;
    QString cameraError() const { return m_cameraError; }
    Q_INVOKABLE void setLocalVideoSink(QVideoSink* sink);
    CameraSender* cameraSenderForTesting() { return cameraSender(); }
    bool peerCamOff() const { return m_ui.value("peerCamOff").toBool(); }
    bool peerSharing() const { return m_ui.value("peerSharing").toBool(); }
    static bool videoSupported();
    bool peerVideoActive() const { return m_peerVideoActive; }
    int micVolume() const { return m_micVolume; }
    bool micAutoGain() const { return m_micAutoGain; }
    int micLevel() const { return m_micLevel; }
    QString selectedMicId() const { return m_selectedMicId; }
    QString selectedSpeakerId() const { return m_selectedSpeakerId; }
    QString selectedCameraId() const { return m_selectedCameraId; }

    Q_INVOKABLE QVariantList audioInputDevices() const;
    Q_INVOKABLE QVariantList audioOutputDevices() const;
    Q_INVOKABLE QVariantList cameraDevices() const;
    Q_INVOKABLE void refreshDevicesAsync();
    Q_INVOKABLE void selectAudioDevice(const QString& kind, const QString& id);
    Q_INVOKABLE void selectCameraDevice(const QString& id);
    Q_INVOKABLE void setMicVolume(int pct);
    Q_INVOKABLE void setMicAutoGain(bool on);

    Q_INVOKABLE void setPeerVideoSink(QVideoSink* sink);

    void setLocalUser(const QString& uid, const QString& name, const QString& avatar);
    void setProfileResolver(std::function<QPair<QString, QString>(const QString& uid)> resolver);

    Q_INVOKABLE void startVoiceCall(const QString& peerId, const QString& peerName, const QString& peerAvatar);
    Q_INVOKABLE void startVideoCall(const QString& peerId, const QString& peerName, const QString& peerAvatar);
    Q_INVOKABLE void startGroupCall(const QString& groupId, const QString& groupName, const QString& groupAvatar,
                                   const QVariantList& members, bool isVideo = false);
    Q_INVOKABLE void setCamera(bool on);
    Q_INVOKABLE void setScreenShare(bool on);
    Q_INVOKABLE void accept(bool camStartOff = false);
    Q_INVOKABLE void reject();
    Q_INVOKABLE void hangup();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setSpeakerOff(bool off);

#ifdef ZALOS_TESTING
    void injectUiLineForTesting(const QByteArray& line) { onUiLine(line); }
    void injectUiBytesForTesting(const QByteArray& bytes) { m_uiBuffer += bytes; drainUiBuffer(); }
    void callSendSignalForTesting(int command, const QJsonObject& data) { onSendSignal(command, data); }
    int pendingEngineFrameCountForTesting() const { return m_pending.size(); }
#endif

    void handleVoipControl(const QJsonObject& content);
    void shutdown();
    void refreshAvailability();

    static QString findEngine();
    static QString audioEngineFor(const QString& callEnginePath);
    static bool writeKeyFile(QTemporaryFile* file, const QByteArray& key);

    static QByteArray encodeFrame(const QJsonObject& frame, const QByteArray& key);
    static QList<QJsonObject> decodeFrames(QByteArray& buffer, const QByteArray& key, QHash<QString, QStringList>* chunks);

signals:
    void availabilityChanged();
    void stateChanged();
    void cameraStateChanged();
    void micStateChanged();
    void devicesChanged();
    void incomingCall(const QString& peerName);
    void callFinished(const QString& peerId, bool outgoing, int durationSec, bool isVideo);
    void videoFrameReceived(bool isKey, const QString& codec, const QByteArray& data, int src);
    void peerVideoChanged();
    void peerVideoResetRequested();

private:
    bool ensureEngine();
    void sendToEngine(const QJsonObject& frame);
    void flushPending();
    void onEngineFrame(const QJsonObject& frame);
    void onSendSignal(int command, const QJsonObject& data);
    void sendUi(const QJsonObject& action);
    void onUiLine(const QByteArray& line);
    void drainUiBuffer();
    void resetUi();
    void startVideoPipeline();
    void onPeerFrame(const QVideoFrame& frame, int src);
    void requestKeyFrame(const QString& why, int src);

    Network::ZaloSession* m_session{nullptr};
    std::function<QPair<QString, QString>(const QString&)> m_resolver;
    QString m_unavailableReason;
    QString m_enginePath;
    QString m_keyFilePath;
    QByteArray m_key;
    std::unique_ptr<QTemporaryFile> m_keyFile;
    QString m_token;
    QString m_uiToken;
    QJsonObject m_local;

    QProcess* m_process{nullptr};
    QTcpServer* m_recvServer{nullptr};
    QTcpServer* m_sendServer{nullptr};
    QTcpServer* m_uiServer{nullptr};
    QPointer<QTcpSocket> m_recvSocket;
    QPointer<QTcpSocket> m_sendSocket;
    QPointer<QTcpSocket> m_uiSocket;
    QByteArray m_recvBuffer;
    QByteArray m_uiBuffer;
    qint64 m_uiSkipBytes{0};
    bool m_lastVideoKey{false};
    QString m_lastVideoCodec;
    int m_lastVideoSrc{0};
    bool m_isVideoMode{false};
    bool m_isGroupMode{false};
    bool m_cameraOn{false};
    bool m_prevCameraOn{false};
    bool m_screenShareOn{false};
    bool m_cameraOffAnnounced{false};
    QString m_cameraError;
    CameraSender* m_cameraSender{nullptr};
    QPointer<QVideoSink> m_localSink;
    CameraSender* cameraSender();
    void applyCamera(bool on);
    void applyScreenShare(bool on);
    QThread* m_videoThread{nullptr};
    PeerVideoDecoder* m_videoDecoder{nullptr};
    QPointer<QVideoSink> m_peerSink;
    QTimer* m_peerVideoIdle{nullptr};
    QTimer* m_idleShutdownTimer{nullptr};
    QTimer* m_hangupWatchdog{nullptr};
    bool m_peerVideoActive{false};
    qint64 m_lastKeyRequestMs{0};
    QHash<QString, QStringList> m_chunks;
    bool m_engineReady{false};
    QList<QJsonObject> m_pending;
    quint64 m_engineGen{0};
    std::atomic<int> m_pendingVideoFrames{0};

    QJsonObject m_ui;
    QString m_activePeer;
    QJsonValue m_activeCallId;
    int m_activeCallType{0};
    bool m_activeOutgoing{false};
    bool m_activeConnected{false};
    bool m_endSignalSent{false};
    bool m_bubbleEmitted{false};
    int m_micVolume{100};
    bool m_micAutoGain{true};
    int m_micLevel{60};
    QString m_selectedMicId;
    QString m_selectedSpeakerId;
    QString m_selectedCameraId;
    mutable QVariantList m_cachedAudioInputs;
    mutable QVariantList m_cachedAudioOutputs;
    mutable QVariantList m_cachedCameras;
    mutable std::atomic<bool> m_devicesScanning{false};
    mutable bool m_devicesCached{false};
};

}
