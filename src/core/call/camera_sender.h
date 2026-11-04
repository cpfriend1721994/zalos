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

class CameraSender : public QObject {
    Q_OBJECT

public:
    static constexpr int kKeyIntervalMs = 10000;

    explicit CameraSender(QObject* parent = nullptr);
    ~CameraSender() override;

    static bool cameraAvailable();
    static bool encoderAvailable();

    bool start(QString* error = nullptr);
    void stop();
    bool running() const { return m_camera != nullptr; }

    void setProfile(QSize size, int fps, int bitrate);
    QSize size() const { return m_size; }
    int fps() const { return m_fps; }
    int bitrate() const { return m_bitrate; }
    void requestKeyFrame() { m_forceKey = true; }
    void setPreviewSink(QVideoSink* sink) { m_preview = sink; }

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

}
