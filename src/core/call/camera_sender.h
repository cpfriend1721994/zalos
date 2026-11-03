#pragma once

#include "h264_codec.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>

class QCamera;
class QMediaCaptureSession;
class QVideoFrame;
class QVideoSink;

namespace Zalos::Core::Call {

// Our camera in a 1-1 video call: QCamera (V4L2 / PipeWire through Qt Multimedia) → I420 → OpenH264 (system
// library, dlopen) → H.264 Annex-B access units for the zcall-native engine ({action:"videoFrame"} on the UI
// socket). Size / fps / bitrate follow the engine's send-rate rung (state.cameraEncode); key frames when the engine
// asks ({type:"keyframe"}: start, PLI/FIR) and at least every kKeyIntervalMs. Frames also go to an optional local
// preview sink (unencoded).
class CameraSender : public QObject {
    Q_OBJECT

public:
    static constexpr int kKeyIntervalMs = 10000;

    explicit CameraSender(QObject* parent = nullptr);
    ~CameraSender() override;

    // A camera exists and OpenH264 can encode.
    static bool cameraAvailable();
    static bool encoderAvailable();

    // Opens the default camera. False with *error when there is none / it cannot be opened.
    bool start(QString* error = nullptr);
    void stop();
    bool running() const { return m_camera != nullptr; }

    // Engine rung (1-1 ladder): even sizes, encoder re-opened at the next frame.
    void setProfile(QSize size, int fps, int bitrate);
    QSize size() const { return m_size; }
    int fps() const { return m_fps; }
    int bitrate() const { return m_bitrate; }
    void requestKeyFrame() { m_forceKey = true; }
    void setPreviewSink(QVideoSink* sink) { m_preview = sink; }

    // Encodes one frame now (camera callback; tests feed synthetic frames). Honours the fps throttle.
    void processFrame(const QVideoFrame& frame);

signals:
    void frameEncoded(bool key, const QByteArray& annexB, int width, int height);
    void failed(const QString& reason);

private:
    QCamera* m_camera{nullptr};
    QMediaCaptureSession* m_capture{nullptr};
    QVideoSink* m_sink{nullptr};
    QPointer<QVideoSink> m_preview;
    H264Encoder m_encoder;
    QSize m_size{640, 360};
    int m_fps{15};
    int m_bitrate{300000};
    bool m_reopen{true};
    bool m_forceKey{true};
    QElapsedTimer m_clock;
    qint64 m_lastFrameMs{-1};
    qint64 m_lastKeyMs{-1};
};

} // namespace Zalos::Core::Call
