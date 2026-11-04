#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>
#include <QVector>

namespace Zalos::Core::Call {

enum class CallState {
    Idle,
    CallingOutgoing,
    RingingIncoming,
    Connecting,
    Connected,
    Ended,
    Declined,
    Error
};

enum class CallType {
    Voice,
    Video
};

struct AudioDevice {
    QString id;
    QString name;
    bool isDefault{false};
    bool isInput{false};
};

class ZCallEngine : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int callState READ callStateInt NOTIFY callStateChanged FINAL)
    Q_PROPERTY(QString currentCaller READ currentCaller NOTIFY currentCallerChanged FINAL)
    Q_PROPERTY(QString currentCallId READ currentCallId NOTIFY currentCallIdChanged FINAL)
    Q_PROPERTY(bool isVoiceOnly READ isVoiceOnly NOTIFY callTypeChanged FINAL)
    Q_PROPERTY(bool isMutedMic READ isMutedMic WRITE setMutedMic NOTIFY muteStateChanged FINAL)
    Q_PROPERTY(bool isMutedSpeaker READ isMutedSpeaker WRITE setMutedSpeaker NOTIFY muteStateChanged FINAL)
    Q_PROPERTY(bool isCameraActive READ isCameraActive WRITE setCameraActive NOTIFY cameraStateChanged FINAL)
    Q_PROPERTY(bool isScreenSharing READ isScreenSharing NOTIFY screenShareStateChanged FINAL)
    Q_PROPERTY(int callDurationSeconds READ callDurationSeconds NOTIFY durationChanged FINAL)

public:
    explicit ZCallEngine(QObject* parent = nullptr);

    int callStateInt() const { return static_cast<int>(m_state); }
    CallState callState() const { return m_state; }
    QString currentCaller() const { return m_currentCaller; }
    QString currentCallId() const { return m_currentCallId; }
    bool isVoiceOnly() const { return m_callType == CallType::Voice; }
    bool isMutedMic() const { return m_isMutedMic; }
    void setMutedMic(bool muted);
    bool isMutedSpeaker() const { return m_isMutedSpeaker; }
    void setMutedSpeaker(bool muted);
    bool isCameraActive() const { return m_isCameraActive; }
    void setCameraActive(bool active);
    bool isScreenSharing() const { return m_isScreenSharing; }
    int callDurationSeconds() const { return m_durationSeconds; }

    Q_INVOKABLE void startCall(const QString& peerId, const QString& peerName, bool isVideo = false);
    Q_INVOKABLE void answerCall(bool withVideo = true);
    Q_INVOKABLE void declineCall();
    Q_INVOKABLE void endCall();
    Q_INVOKABLE void toggleMuteMic();
    Q_INVOKABLE void toggleMuteSpeaker();
    Q_INVOKABLE void toggleCamera();
    Q_INVOKABLE void startScreenShare();
    Q_INVOKABLE void stopScreenShare();

    struct SrtpSessionKeys {
        QByteArray encKey;
        QByteArray authKey;
        QByteArray saltKey;
    };

    static QByteArray deriveSrtpMasterKey(const QByteArray& zrtpSecret, const QByteArray& salt);
    static SrtpSessionKeys deriveSrtpSessionKeys(const QByteArray& master30);

    QVector<AudioDevice> getAudioDevices(bool input = true);

signals:
    void callStateChanged(int state);
    void currentCallerChanged(const QString& caller);
    void currentCallIdChanged(const QString& callId);
    void callTypeChanged(bool voiceOnly);
    void muteStateChanged();
    void cameraStateChanged();
    void screenShareStateChanged();
    void durationChanged(int seconds);
    void srtpSessionEstablished(const QString& cipherSuite);

private slots:
    void onDurationTick();

private:
    void setState(CallState state);

    CallState m_state{CallState::Idle};
    CallType m_callType{CallType::Voice};
    QString m_currentCaller;
    QString m_currentCallId;
    bool m_isMutedMic{false};
    bool m_isMutedSpeaker{false};
    bool m_isCameraActive{false};
    bool m_isScreenSharing{false};
    int m_durationSeconds{0};

    QTimer* m_durationTimer{nullptr};
};

}
