#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTemporaryFile>
#include <QVideoSink> // complete type for the Q_INVOKABLE below
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

// Hosts zcall-native (zalo-for-linux) the way Zalo's own app hosts its call helper:
//  - two loopback TCP sockets carrying AES-128-CBC "call-v2" frames (hex + '$'),
//  - engine "sendSignal" frames -> /api/voicecall/* REST calls -> "recvSignal" replies,
//  - socket cmd 601 "voip" controls (incoming calls, answers, hang-ups) -> "control" frames,
//  - a line-JSON UI socket (ZCALL_UI_PORT) that drives the QML call window.
// The engine is not shipped (zalo-for-linux checkout, see findEngine). The transport key only
// protects the loopback sockets between zalos and the engine, so each session generates a
// random one and hands it to the engine through a private ZALO_MAIN_JS file; no Zalo PC
// install or Zalo secret is needed. An explicit ZALO_MAIN_JS still overrides it.
class CallBridge : public QObject {
    Q_OBJECT
    QML_ANONYMOUS // typed for the QML compiler (reached through ZaloClient properties)
    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged FINAL)
    // zcall-native folder in use ("" when not found) — shown in Settings with an override picker.
    Q_PROPERTY(QString engineDirectory READ engineDirectory NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool engineIsBundled READ engineIsBundled NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged FINAL)          // "", outgoing, incoming, connecting, connected, ended
    Q_PROPERTY(QString peerName READ peerName NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString peerAvatar READ peerAvatar NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 since READ since NOTIFY stateChanged FINAL)           // epoch ms when the call connected
    Q_PROPERTY(bool muted READ muted NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool speakerOff READ speakerOff NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool peerMuted READ peerMuted NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool canAnswer READ canAnswer NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isVideo READ isVideo NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool cameraOn READ cameraOn NOTIFY cameraStateChanged FINAL)
    // A camera exists and OpenH264 can encode (our picture can be sent).
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY cameraStateChanged FINAL)
    // Why the camera is off against the user's wish: "", "no_camera", "no_encoder", or the camera error text.
    Q_PROPERTY(QString cameraError READ cameraError NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(bool peerCamOff READ peerCamOff NOTIFY stateChanged FINAL)
    // OpenH264 found on the system (video pictures can be decoded / encoded).
    Q_PROPERTY(bool videoSupported READ videoSupported CONSTANT FINAL)
    // The peer's pictures are arriving and being shown (false again ~2 s after the last one).
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
    bool cameraOn() const { return m_cameraOn; }
    bool cameraAvailable() const;
    QString cameraError() const { return m_cameraError; }
    // Our own picture (unencoded) for the self-view tile.
    Q_INVOKABLE void setLocalVideoSink(QVideoSink* sink);
    CameraSender* cameraSenderForTesting() { return cameraSender(); }
    bool peerCamOff() const { return m_ui.value("peerCamOff").toBool(); }
    static bool videoSupported();
    bool peerVideoActive() const { return m_peerVideoActive; }

    // The QML VideoOutput's sink that shows the peer's pictures (1-1 call; src 0).
    Q_INVOKABLE void setPeerVideoSink(QVideoSink* sink);

    // Identity of the signed-in user, sent to the engine as its "local" config.
    void setLocalUser(const QString& uid, const QString& name, const QString& avatar);
    // Display name / avatar lookup for the engine's getAliasName requests.
    void setProfileResolver(std::function<QPair<QString, QString>(const QString& uid)> resolver);

    Q_INVOKABLE void startVoiceCall(const QString& peerId, const QString& peerName, const QString& peerAvatar);
    Q_INVOKABLE void startVideoCall(const QString& peerId, const QString& peerName, const QString& peerAvatar);
    Q_INVOKABLE void setCamera(bool on);
    Q_INVOKABLE void accept();
    Q_INVOKABLE void reject();
    Q_INVOKABLE void hangup();
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setSpeakerOff(bool off);

#ifdef ZALOS_TESTING
    // Feeds an engine UI line ({type:"state"|"close", ...}) without a running engine.
    void injectUiLineForTesting(const QByteArray& line) { onUiLine(line); }
    // Feeds raw UI-socket bytes (lines and binary frames) through the real parser.
    void injectUiBytesForTesting(const QByteArray& bytes) { m_uiBuffer += bytes; drainUiBuffer(); }
#endif

    // A "voip" control from the realtime socket (cmd 601).
    void handleVoipControl(const QJsonObject& content);
    void shutdown();
    void refreshAvailability();
    // Optional override (Settings › Cuộc gọi): a newer zalo-for-linux checkout instead of the bundled engine
    // (repo root, its zcall-native or native-engine folder; path or file URL). Returns false when no
    // native-engine/zcall-native.js is inside. An empty string goes back to the bundled engine.
    Q_INVOKABLE bool setEngineDirectory(const QString& urlOrPath);
    QString engineDirectory() const;
    bool engineIsBundled() const;

    // The bundled engine (<bin>/../share/zalos/zcall-native, installed from third_party/zcall-native) unless
    // ZALOS_ZCALL_DIR or the Settings override points elsewhere; dev builds also use the source tree's copy.
    static QString findEngine();
    static QString bundledEngineRoot();
    // <root>/native-engine/zcall-native.js for any of the accepted folder shapes, "" when absent.
    static QString engineIn(const QString& folder);
    // Key from an explicit ZALO_MAIN_JS, else a fresh random key written to a 0600 file.
    static QByteArray findTransportKey(QString* mainJsPath = nullptr);
    // Writes `callV2TransportAesKey:"<base64>"` (the only line zcall-native parses) to `file`.
    static bool writeKeyFile(QTemporaryFile* file, const QByteArray& key);
    static QString findNode();

    // call-v2 transport framing (exposed for tests).
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
    void peerVideoResetRequested(); // to the decoder thread

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
    QString m_mainJsPath;
    QByteArray m_key;
    std::unique_ptr<QTemporaryFile> m_keyFile; // generated key, removed with the bridge
    QString m_token;
    QString m_uiToken;
    QJsonObject m_local;

    QProcess* m_process{nullptr};
    QTcpServer* m_recvServer{nullptr}; // engine -> us
    QTcpServer* m_sendServer{nullptr}; // us -> engine
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
    bool m_cameraOn{false};
    bool m_cameraOffAnnounced{false}; // first state of a video call: camera state told to the engine
    QString m_cameraError;
    CameraSender* m_cameraSender{nullptr}; // created on first use
    QPointer<QVideoSink> m_localSink;
    CameraSender* cameraSender();
    void applyCamera(bool on);
    QThread* m_videoThread{nullptr};
    PeerVideoDecoder* m_videoDecoder{nullptr}; // lives in m_videoThread
    QPointer<QVideoSink> m_peerSink;
    QTimer* m_peerVideoIdle{nullptr};
    bool m_peerVideoActive{false};
    qint64 m_lastKeyRequestMs{0};
    QHash<QString, QStringList> m_chunks;
    bool m_engineReady{false};
    QList<QJsonObject> m_pending;

    QJsonObject m_ui;          // latest UI state from the engine
    QString m_activePeer;
    bool m_activeOutgoing{false};
};

} // namespace Zalos::Core::Call
