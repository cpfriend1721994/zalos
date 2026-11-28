#pragma once

#include "h264_codec.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVideoFrame>
#include <atomic>

class QCamera;
class QMediaCaptureSession;
class QProcess;
class QThread;
class QTimer;
class QVideoSink;

namespace Zalos::Core::Call {

class PortalScreenCast;

class CameraSender : public QObject {
    Q_OBJECT

public:
    static constexpr int kKeyIntervalMs = 10000;
    static constexpr int kScreenKeyIntervalMs = 2000;
    static constexpr int kMaxCameraSide = 1920;

    explicit CameraSender(QObject* parent = nullptr);
    ~CameraSender() override;

    static bool cameraAvailable();
    static bool screenAvailable();
    static bool encoderAvailable();

    bool start(QString* error = nullptr);
    bool startScreen(QString* error = nullptr);
    bool startPortalScreen(QString* error = nullptr);
    void stop();
    bool running() const { return m_camera != nullptr; }
    bool screenRunning() const { return m_screenTimer != nullptr || m_portalStreaming; }
    bool portalPending() const { return m_portalPending; }
    void setScreenTarget(const QString& target) { m_screenTarget = target; }
    QString screenTarget() const { return m_screenTarget; }

    static QImage stitchScreenCaptures(const QList<QPair<QRect, QImage>>& parts);
    static QString encodePreviewDataUrl(const QImage& image);

    void setProfile(QSize size, int fps, int bitrate);
    void setScreenProfile(int maxSide, int maxShort, int fps, int bitrate);
    QSize size() const;
    int fps() const;
    int bitrate() const;
    void requestKeyFrame() { m_forceKey = true; }
    void setPreviewSink(QVideoSink* sink) { m_preview = sink; }
    void setPreferredCameraId(const QString& id);
    QString preferredCameraId() const { return m_preferredCameraId; }
    void setEncodingEnabled(bool enabled);
    bool encodingEnabled() const { return m_encodingEnabled.load(); }

    void processFrame(const QVideoFrame& frame);
    void processScreenImage(const QImage& image);

signals:
    void frameEncoded(bool key, const QByteArray& annexB, int width, int height);
    void failed(const QString& reason);
    void portalScreenStarted();
    void portalScreenCancelled();
    void portalScreenUnavailable();
    void portalScreenStopped();

private:
    void ensureEncodeThread();
    void stopEncodeThread();
    void stopLocalCapture();
    void stopPortalCast();
    QImage grabDesktopOrWindow();
    void startWaylandGrab();
    bool screenCaptureExhausted() const;
    void captureScreenFrame();
    void showPreview(const QImage& image);
    void dispatchScreenImage(const QImage& image);
    void encodeFrame(const QVideoFrame& frame, qint64 now);
    void encodeImage(const QImage& image, qint64 now);
    void encodeI420Buffer(const QByteArray& i420, QSize targetSize, int fps, int bitrate,
                          bool needReopen, bool screenContent, qint64 now, bool needRetune = false);

    QCamera* m_camera{nullptr};
    QMediaCaptureSession* m_capture{nullptr};
    QVideoSink* m_sink{nullptr};
    QTimer* m_repeatTimer{nullptr};
    QVideoFrame m_lastCameraFrame;
    QString m_preferredCameraId;
    QTimer* m_screenTimer{nullptr};
    QProcess* m_grabProcess{nullptr};
    QString m_grabTempFile;
    QStringList m_grabTools;
    int m_grabToolIndex{0};
    int m_grabFailures{0};
    bool m_grabToolsInitialized{false};
    bool m_grabDecoding{false};
    std::atomic<bool> m_previewBusy{false};
    QImage m_lastCapturedDesktop;
    QImage m_lastScreenImage;
    QThread* m_encodeThread{nullptr};
    QObject* m_encodeWorker{nullptr};
    std::atomic<bool> m_encodeBusy{false};
    mutable QMutex m_stateMutex;
    QPointer<QVideoSink> m_preview;
    H264Encoder m_encoder;
    QByteArray m_i420Buf;
    QSize m_size{640, 360};
    int m_fps{15};
    int m_bitrate{300000};
    QSize m_camSize{640, 360};
    int m_camFps{15};
    int m_camBitrate{300000};
    int m_screenMaxSide{1280};
    int m_screenMaxShort{720};
    int m_screenFps{10};
    int m_screenBitrate{1500000};
    QSize m_screenEncodedSize;
    bool m_reopen{true};
    bool m_retune{false};
    bool m_screenContent{false};
    std::atomic<bool> m_forceKey{true};
    std::atomic<bool> m_encodingEnabled{true};
    QElapsedTimer m_clock;
    qint64 m_lastFrameMs{-1};
    qint64 m_lastDispatchedKey{0};
    qint64 m_lastKeyMs{-1};
    QString m_screenTarget;
    PortalScreenCast* m_portalCast{nullptr};
    bool m_portalPending{false};
    bool m_portalStreaming{false};
};

}
