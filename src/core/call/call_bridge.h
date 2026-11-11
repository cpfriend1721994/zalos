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
#include <QVideoSink>
#include <QPointer>
#include "camera_sender.h"

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
    Q_PROPERTY(QString cameraError READ cameraError NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(bool peerCamOff READ peerCamOff NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool videoSupported READ videoSupported CONSTANT FINAL)
    Q_PROPERTY(bool peerVideoActive READ peerVideoActive NOTIFY peerVideoChanged FINAL)

public:
    explicit CallBridge(Network::ZaloSession* session, QObject* parent = nullptr);
    ~CallBridge() override;

    bool isAvailable() const { return m_unavailableReason.isEmpty(); }
    QString unavailableReason() const { return m_unavailableReason; }
    QString phase() const { return m_ui.value("phase").toString(); }
    QString peerName() const { return m_ui.value("name").toString(); }
    QString peerAvatar() const { return m_ui.value("avatar").toString(); }
    QString statusText() const { return m_ui.value("text").toString(); }
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
    QString cameraError() const { return m_cameraError; }
    Q_INVOKABLE void setLocalVideoSink(QVideoSink* sink);
    CameraSender* cameraSenderForTesting() { return cameraSender(); }
    bool peerCamOff() const { return m_ui.value("peerCamOff").toBool(); }
    static bool videoSupported();
    bool peerVideoActive() const { return m_peerVideoActive; }

    Q_INVOKABLE void setPeerVideoSink(QVideoSink* sink);

    void setLocalUser(const QString& uid, const QString& name, const QString& avatar);
    void setProfileResolver(std::function<QPair<QString, QString>(const QString& uid)> resolver);

    Q_INVOKABLE void startVoiceCall(const QString& peerId, const QString& peerName, const QString& peerAvatar);
    Q_INVOKABLE void startVideoCall(const QString& peerId, const QString& peerName, const QString& peerAvatar);
    Q_INVOKABLE void startGroupCall(const QString& groupId, const QString& groupName, const QString& groupAvatar,
                                   const QVariantList& members, bool isVideo = false);
    Q_INVOKABLE void setCamera(bool on);
    Q_INVOKABLE void accept(bool camStartOff = false);
    Q_INVOKABLE void reject();
    Q_INVOKABLE void hangup();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setSpeakerOff(bool off);

#ifdef ZALOS_TESTING
    void injectUiLineForTesting(const QByteArray& line) { onUiLine(line); }
    void injectUiBytesForTesting(const QByteArray& bytes) { m_uiBuffer += bytes; drainUiBuffer(); }
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
    void incomingCall(const QString& peerName);
    void callFinished(const QString& peerId, bool outgoing, int durationSec);
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
    bool m_cameraOffAnnounced{false};
    QString m_cameraError;
    CameraSender* m_cameraSender{nullptr};
    QPointer<QVideoSink> m_localSink;
    CameraSender* cameraSender();
    void applyCamera(bool on);
    QThread* m_videoThread{nullptr};
    PeerVideoDecoder* m_videoDecoder{nullptr};
    QPointer<QVideoSink> m_peerSink;
    QTimer* m_peerVideoIdle{nullptr};
    bool m_peerVideoActive{false};
    qint64 m_lastKeyRequestMs{0};
    QHash<QString, QStringList> m_chunks;
    bool m_engineReady{false};
    QList<QJsonObject> m_pending;

    QJsonObject m_ui;
    QString m_activePeer;
    bool m_activeOutgoing{false};
};

}
