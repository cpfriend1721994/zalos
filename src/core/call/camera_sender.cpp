#include "camera_sender.h"

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>

namespace Zalos::Core::Call {

CameraSender::CameraSender(QObject* parent) : QObject(parent) {
    m_clock.start();
}

CameraSender::~CameraSender() {
    stop();
}

bool CameraSender::cameraAvailable() {
    return !QMediaDevices::videoInputs().isEmpty();
}

bool CameraSender::encoderAvailable() {
    return OpenH264::instance().available();
}

bool CameraSender::start(QString* error) {
    if (m_camera) return true;
    if (!encoderAvailable()) {
        if (error) *error = QStringLiteral("no_encoder");
        return false;
    }
    const QCameraDevice device = QMediaDevices::defaultVideoInput();
    if (device.isNull()) {
        if (error) *error = QStringLiteral("no_camera");
        return false;
    }
    m_camera = new QCamera(device, this);
    m_capture = new QMediaCaptureSession(this);
    m_sink = new QVideoSink(this);
    m_capture->setCamera(m_camera);
    m_capture->setVideoSink(m_sink);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &CameraSender::processFrame);
    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error err, const QString& text) {
        if (err == QCamera::NoError) return;
        const QString reason = text.isEmpty() ? QStringLiteral("camera_error") : text;
        stop();
        emit failed(reason);
    });
    m_forceKey = true;
    m_reopen = true;
    m_lastFrameMs = -1;
    m_camera->start();
    return true;
}

void CameraSender::stop() {
    if (m_sink) disconnect(m_sink, nullptr, this, nullptr);
    if (m_camera) {
        disconnect(m_camera, nullptr, this, nullptr);
        m_camera->stop();
    }
    if (m_capture) m_capture->deleteLater();
    m_capture = nullptr;
    if (m_camera) m_camera->deleteLater();
    m_camera = nullptr;
    if (m_sink) m_sink->deleteLater();
    m_sink = nullptr;
    m_encoder.close();
    if (m_preview) m_preview->setVideoFrame(QVideoFrame());
}

void CameraSender::setProfile(QSize size, int fps, int bitrate) {
    size = QSize(size.width() & ~1, size.height() & ~1);
    if (size.width() < 16 || size.height() < 16) return;
    fps = qBound(1, fps, 30);
    bitrate = qBound(30000, bitrate, 4000000);
    if (size == m_size && fps == m_fps && bitrate == m_bitrate) return;
    m_size = size;
    m_fps = fps;
    m_bitrate = bitrate;
    m_reopen = true;
}

void CameraSender::processFrame(const QVideoFrame& frame) {
    if (!frame.isValid()) return;
    if (m_preview) m_preview->setVideoFrame(frame);
    const qint64 now = m_clock.elapsed();
    if (m_lastFrameMs >= 0 && now - m_lastFrameMs < 1000 / m_fps - 2) return;
    m_lastFrameMs = now;
    if (m_reopen || !m_encoder.isOpen()) {
        m_reopen = false;
        if (!m_encoder.open(m_size, m_fps, m_bitrate)) {
            emit failed(QStringLiteral("encoder_open"));
            return;
        }
        m_forceKey = true;
    }
    const QByteArray i420 = frameToI420(frame, m_size);
    if (i420.isEmpty()) return;
    if (m_lastKeyMs < 0 || now - m_lastKeyMs >= kKeyIntervalMs) m_forceKey = true;
    bool key = false;
    const QByteArray au = m_encoder.encodeI420(i420, now, m_forceKey, &key);
    if (au.isEmpty()) return;
    if (key) {
        m_forceKey = false;
        m_lastKeyMs = now;
    }
    emit frameEncoded(key, au, m_size.width(), m_size.height());
}

}
