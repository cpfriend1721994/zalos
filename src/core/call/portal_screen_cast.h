#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace Zalos::Core::Call {

class PortalScreenCast : public QObject {
    Q_OBJECT

public:
    static constexpr quint32 kSpaVideoFormatRGBx = 7;
    static constexpr quint32 kSpaVideoFormatBGRx = 8;
    static constexpr quint32 kSpaVideoFormatRGBA = 11;
    static constexpr quint32 kSpaVideoFormatBGRA = 12;

    explicit PortalScreenCast(QObject* parent = nullptr);
    ~PortalScreenCast() override;

    static bool pipeWireAvailable();
    static bool available();

    bool start();
    void stop();
    bool active() const { return m_active; }

    static QByteArray buildVideoFormatPod();
    static QByteArray buildBuffersParamPod();
    static bool parseVideoFormatPod(const void* pod, quint32* formatOut, int* widthOut, int* heightOut);
    static QImage convertVideoBuffer(quint32 spaFormat, int width, int height,
                                     const uchar* data, int stride, int size);

    void handleStreamParamChanged(quint32 id, const void* param);
    void handleStreamStateChanged(int state, const char* error);
    void handleStreamProcess();

signals:
    void started();
    void cancelled();
    void stoppedBySystem();
    void failed(const QString& reason);
    void frameReady(const QImage& frame);

private slots:
    void onPortalResponse(uint response, const QVariantMap& results);

private:
    enum class Stage {
        Idle,
        CreateSession,
        SelectSources,
        Start,
        OpenRemote,
        Streaming
    };

    void callCreateSession();
    void callSelectSources();
    void callStart();
    void callOpenPipeWireRemote(quint32 nodeId);
    void startPipeWireStream(int fd, quint32 nodeId);
    void teardownPipeWire();
    void closePortalSession();
    void bindPortalRequest(const QString& path);
    void clearPortalRequest();

    quint64 m_generation{0};
    Stage m_stage{Stage::Idle};
    QString m_requestPath;
    QString m_sessionHandle;
    bool m_active{false};
    bool m_streamingStarted{false};
    void* m_pwLoop{nullptr};
    void* m_pwContext{nullptr};
    void* m_pwCore{nullptr};
    void* m_pwStream{nullptr};
    void* m_pwHook{nullptr};
    quint32 m_videoFormat{0};
    int m_videoWidth{0};
    int m_videoHeight{0};
    qint64 m_lastFrameNs{0};
};

}
