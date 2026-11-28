#include "camera_sender.h"
#include "portal_screen_cast.h"
#include "platform/multimedia_support.h"
#include "core/fs/file_utils.h"
#include "core/media/media_utils.h"

#include <QBuffer>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMutexLocker>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace Zalos::Core::Call {

namespace {
QVideoFrame previewFrameFor(const QImage& img) {
    if (img.isNull()) return {};
    constexpr int kPreviewMaxSide = 640;
    QImage argb = (img.width() > kPreviewMaxSide || img.height() > kPreviewMaxSide)
        ? img.scaled(kPreviewMaxSide, kPreviewMaxSide, Qt::KeepAspectRatio, Qt::FastTransformation)
        : img;
    auto fmt = QVideoFrameFormat::pixelFormatFromImageFormat(argb.format());
    if (fmt == QVideoFrameFormat::Format_Invalid || argb.depth() != 32) {
        argb = argb.convertToFormat(QImage::Format_ARGB32);
        fmt = QVideoFrameFormat::pixelFormatFromImageFormat(argb.format());
    }
    if (fmt == QVideoFrameFormat::Format_Invalid) return {};
    QVideoFrame previewFrame(QVideoFrameFormat(argb.size(), fmt));
    if (!previewFrame.map(QVideoFrame::WriteOnly)) return {};
    const int bpl = previewFrame.bytesPerLine(0);
    for (int y = 0; y < argb.height(); ++y) {
        std::memcpy(previewFrame.bits(0) + y * bpl, argb.constScanLine(y), size_t(argb.width() * 4));
    }
    previewFrame.unmap();
    return previewFrame;
}

QImage decodeGrabOutput(bool ppm, const QByteArray& bytes, const QString& file) {
    if (ppm) {
        QImage captured;
        captured.loadFromData(bytes, "PPM");
        return captured;
    }
    if (file.isEmpty() || !QFile::exists(file)) return {};
    QImage captured(file);
    QFile::remove(file);
    return captured;
}
}

CameraSender::CameraSender(QObject* parent) : QObject(parent) {
    m_clock.start();
}

CameraSender::~CameraSender() {
    stop();
}

bool CameraSender::cameraAvailable() {
    if (!Platform::MultimediaSupport::available()) return false;
    const auto inputs = QMediaDevices::videoInputs();
    for (const auto& dev : inputs) {
        if (!Media::MediaUtils::isIrCamera(dev.description())) return true;
    }
    return false;
}

bool CameraSender::screenAvailable() {
    return QGuiApplication::primaryScreen() != nullptr || PortalScreenCast::available();
}

QImage CameraSender::stitchScreenCaptures(const QList<QPair<QRect, QImage>>& parts) {
    QRect bounds;
    for (const auto& part : parts) {
        if (!part.first.isEmpty() && !part.second.isNull()) {
            bounds = bounds.united(part.first);
        }
    }
    if (bounds.isEmpty()) return {};

    QImage canvas(bounds.size(), QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    for (const auto& part : parts) {
        if (part.first.isEmpty() || part.second.isNull()) continue;
        const QRect dst(part.first.topLeft() - bounds.topLeft(), part.first.size());
        painter.drawImage(dst, part.second);
    }
    return canvas;
}

QString CameraSender::encodePreviewDataUrl(const QImage& image) {
    if (image.isNull()) return {};
    const QImage thumb = image.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buf(&bytes);
    if (!buf.open(QIODevice::WriteOnly)) return {};
    if (!thumb.save(&buf, "JPEG", 75)) return {};
    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(bytes.toBase64());
}

bool CameraSender::encoderAvailable() {
    return OpenH264::instance().available();
}

void CameraSender::ensureEncodeThread() {
    if (m_encodeThread) return;
    m_encodeThread = new QThread(this);
    m_encodeThread->setObjectName(QStringLiteral("zalos-cam-enc"));
    m_encodeWorker = new QObject;
    m_encodeWorker->moveToThread(m_encodeThread);
    connect(m_encodeThread, &QThread::finished, m_encodeWorker, &QObject::deleteLater);
    m_encodeBusy.store(false);
    m_encodeThread->start();
}

void CameraSender::stopEncodeThread() {
    if (m_encodeThread) {
        m_encodeThread->quit();
        m_encodeThread->wait();
        m_encodeThread->deleteLater();
        m_encodeThread = nullptr;
        m_encodeWorker = nullptr;
    }
    m_encodeBusy.store(false);
    m_previewBusy.store(false);
    m_grabDecoding = false;
}

bool CameraSender::start(QString* error) {
    if (m_camera) return true;
    if (m_screenTimer || m_portalCast) stop();
    if (!encoderAvailable()) {
        if (error) *error = QStringLiteral("no_encoder");
        return false;
    }
    if (!Platform::MultimediaSupport::available()) {
        if (error) *error = QStringLiteral("no_multimedia");
        return false;
    }
    QCameraDevice device;
    const auto inputs = QMediaDevices::videoInputs();
    if (!m_preferredCameraId.isEmpty()) {
        for (const auto& dev : inputs) {
            if (QString::fromUtf8(dev.id()) == m_preferredCameraId || dev.description() == m_preferredCameraId) {
                device = dev;
                break;
            }
        }
    }
    if (device.isNull()) {
        for (const auto& dev : inputs) {
            if (!Media::MediaUtils::isIrCamera(dev.description())) {
                device = dev;
                break;
            }
        }
    }
    if (device.isNull()) device = QMediaDevices::defaultVideoInput();
    if (device.isNull()) {
        if (error) *error = QStringLiteral("no_camera");
        return false;
    }
    bool wantBig = false;
    {
        QMutexLocker lock(&m_stateMutex);
        wantBig = m_camSize.width() > 640;
    }
    const int targetArea = wantBig ? (1280 * 720) : (640 * 360);
    QCameraFormat bestFormat;
    int bestScore = std::numeric_limits<int>::max();
    const auto formatRank = [](QVideoFrameFormat::PixelFormat pf) -> int {
        switch (pf) {
        case QVideoFrameFormat::Format_YUV420P: return 0;
        case QVideoFrameFormat::Format_NV12: return 1;
        case QVideoFrameFormat::Format_YUYV: return 2;
        case QVideoFrameFormat::Format_UYVY: return 3;
        default: return 10;
        }
    };
    for (const QCameraFormat& fmt : device.videoFormats()) {
        const QSize res = fmt.resolution();
        const int w = res.width();
        const int h = res.height();
        if (w <= 0 || h <= 0 || w > 1280 || h > 720) continue;
        const int areaDiff = std::abs(w * h - targetArea);
        const int score = formatRank(fmt.pixelFormat()) * 500000 + areaDiff;
        if (score < bestScore) {
            bestScore = score;
            bestFormat = fmt;
        }
    }
    m_camera = new QCamera(device, this);
    if (!bestFormat.isNull()) m_camera->setCameraFormat(bestFormat);
    m_capture = new QMediaCaptureSession(this);
    m_sink = new QVideoSink(this);
    m_capture->setCamera(m_camera);
    m_capture->setVideoSink(m_sink);
    m_lastCameraFrame = QVideoFrame();
    ensureEncodeThread();

    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        if (!frame.isValid()) return;
        m_lastCameraFrame = frame;
        if (m_preview) m_preview->setVideoFrame(frame);
        if (!m_encodingEnabled.load()) return;
        const qint64 now = m_clock.elapsed();
        int fps = 15;
        {
            QMutexLocker lock(&m_stateMutex);
            fps = m_fps;
        }
        if (m_lastFrameMs >= 0 && now - m_lastFrameMs < 1000 / fps - 2) return;
        if (m_encodeBusy.exchange(true)) return;
        m_lastFrameMs = now;
        if (!m_encodeWorker) {
            m_encodeBusy.store(false);
            return;
        }
        QMetaObject::invokeMethod(m_encodeWorker, [this, frame, now]() {
            encodeFrame(frame, now);
            m_encodeBusy.store(false);
        }, Qt::QueuedConnection);
    });
    m_repeatTimer = new QTimer(this);
    m_repeatTimer->setInterval(250);
    connect(m_repeatTimer, &QTimer::timeout, this, [this]() {
        if (!m_camera || !m_encodingEnabled.load() || !m_lastCameraFrame.isValid() || m_lastFrameMs < 0) return;
        const qint64 now = m_clock.elapsed();
        if (now - m_lastFrameMs < 600) return;
        if (m_encodeBusy.exchange(true)) return;
        if (!m_encodeWorker) {
            m_encodeBusy.store(false);
            return;
        }
        m_lastFrameMs = now;
        const QVideoFrame held = m_lastCameraFrame;
        QMetaObject::invokeMethod(m_encodeWorker, [this, held, now]() {
            encodeFrame(held, now);
            m_encodeBusy.store(false);
        }, Qt::QueuedConnection);
    });
    m_repeatTimer->start();
    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error err, const QString& text) {
        if (err == QCamera::NoError) return;
        const QString reason = text.isEmpty() ? QStringLiteral("camera_error") : text;
        stop();
        emit failed(reason);
    });
    m_forceKey.store(true);
    {
        QMutexLocker lock(&m_stateMutex);
        m_screenContent = false;
        m_size = m_camSize;
        m_fps = m_camFps;
        m_bitrate = m_camBitrate;
        m_reopen = true;
    }
    m_lastFrameMs = -1;
    m_camera->start();
    return true;
}

void CameraSender::startWaylandGrab() {
    if (m_grabProcess || m_grabDecoding || !m_screenTimer) return;
    if (!m_grabToolsInitialized) {
        m_grabToolsInitialized = true;
        m_grabToolIndex = 0;
        m_grabTools.clear();
        for (const QString& candidate : {QStringLiteral("grim"), QStringLiteral("spectacle"), QStringLiteral("gnome-screenshot")}) {
            if (!QStandardPaths::findExecutable(candidate).isEmpty()) m_grabTools << candidate;
        }
    }
    if (m_grabToolIndex >= m_grabTools.size()) return;

    const QString tool = m_grabTools.at(m_grabToolIndex);
    QStringList args;
    QString outFile;
    if (tool == QLatin1String("grim")) {
        if (m_screenTarget.startsWith(QLatin1String("screen:"))) {
            bool okIdx = false;
            const int idx = m_screenTarget.mid(7).toInt(&okIdx);
            const QList<QScreen*> screens = QGuiApplication::screens();
            if (okIdx && idx >= 0 && idx < screens.size() && !screens.at(idx)->name().isEmpty()) {
                args << QStringLiteral("-o") << screens.at(idx)->name();
            }
        }
        args << QStringLiteral("-t") << QStringLiteral("ppm") << QStringLiteral("-");
    } else {
        if (m_grabTempFile.isEmpty()) {
            const QString rt = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
            const QString base = rt.isEmpty() ? QDir::tempPath() : rt;
            m_grabTempFile = base + QStringLiteral("/zalos_screen_%1.png").arg(qint64(QCoreApplication::applicationPid()));
        }
        outFile = m_grabTempFile;
        QFile::remove(outFile);
        if (tool == QLatin1String("spectacle")) {
            args << QStringLiteral("-b") << QStringLiteral("-n") << QStringLiteral("-f") << QStringLiteral("-o") << outFile;
        } else {
            args << QStringLiteral("-f") << outFile;
        }
    }

    auto* proc = new QProcess(this);
    proc->setProcessEnvironment(Fs::FileUtils::launchEnvironment(QProcessEnvironment::systemEnvironment()));
    m_grabProcess = proc;
    const QPointer<CameraSender> self(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, self, proc, tool, outFile](int exitCode, QProcess::ExitStatus) {
        const bool ppm = tool == QLatin1String("grim");
        const QByteArray stdoutBytes = exitCode == 0 && ppm ? proc->readAllStandardOutput() : QByteArray();
        const QString stderrText = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        if (!self) return;
        if (m_grabProcess == proc) m_grabProcess = nullptr;
        if (exitCode != 0 || !m_encodeWorker) {
            qWarning().noquote() << "screen capture" << tool << "exit" << exitCode << stderrText.left(300);
            if (++m_grabFailures >= 3) {
                m_grabFailures = 0;
                ++m_grabToolIndex;
            }
            return;
        }
        m_grabDecoding = true;
        QMetaObject::invokeMethod(m_encodeWorker, [this, ppm, stdoutBytes, outFile, tool]() {
            const QImage captured = decodeGrabOutput(ppm, stdoutBytes, outFile);
            QMetaObject::invokeMethod(this, [this, captured, tool]() {
                m_grabDecoding = false;
                if (captured.isNull()) {
                    qWarning().noquote() << "screen capture" << tool << "produced no image";
                    if (++m_grabFailures >= 3) {
                        m_grabFailures = 0;
                        ++m_grabToolIndex;
                    }
                } else {
                    m_grabFailures = 0;
                    m_lastCapturedDesktop = captured;
                }
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, self, proc, outFile](QProcess::ProcessError) {
        if (!outFile.isEmpty()) QFile::remove(outFile);
        proc->deleteLater();
        if (!self) return;
        if (m_grabProcess == proc) m_grabProcess = nullptr;
        ++m_grabToolIndex;
    });
    proc->start(tool, args);
}

QImage CameraSender::grabDesktopOrWindow() {
    QScreen* scr = QGuiApplication::primaryScreen();
    const bool isWayland = QGuiApplication::platformName().contains(QLatin1String("wayland"));
    if (scr && !isWayland) {
        if (m_screenTarget == QLatin1String("all")) {
            QList<QPair<QRect, QImage>> parts;
            for (QScreen* s : QGuiApplication::screens()) {
                if (!s) continue;
                const QPixmap pix = s->grabWindow(0);
                if (!pix.isNull()) parts.append(qMakePair(s->geometry(), pix.toImage()));
            }
            const QImage stitched = stitchScreenCaptures(parts);
            if (!stitched.isNull()) return stitched;
        } else if (m_screenTarget.startsWith(QLatin1String("screen:"))) {
            bool okIdx = false;
            const int idx = m_screenTarget.mid(7).toInt(&okIdx);
            const QList<QScreen*> screens = QGuiApplication::screens();
            QScreen* target = (okIdx && idx >= 0 && idx < screens.size()) ? screens.at(idx) : scr;
            if (target) {
                const QPixmap pix = target->grabWindow(0);
                if (!pix.isNull()) {
                    const QImage img = pix.toImage();
                    if (!img.isNull()) return img;
                }
            }
        }
        const QPixmap pix = scr->grabWindow(0);
        if (!pix.isNull()) {
            const QImage img = pix.toImage();
            if (!img.isNull()) return img;
        }
    }
    if (isWayland || m_lastCapturedDesktop.isNull()) {
        startWaylandGrab();
        if (!m_lastCapturedDesktop.isNull()) return m_lastCapturedDesktop;
    }
    return {};
}

bool CameraSender::screenCaptureExhausted() const {
    return m_grabToolsInitialized && m_grabToolIndex >= m_grabTools.size() && !m_grabProcess && !m_grabDecoding;
}

void CameraSender::showPreview(const QImage& image) {
    if (!m_preview) return;
    if (!m_encodeWorker) {
        const QVideoFrame frame = previewFrameFor(image);
        if (frame.isValid()) m_preview->setVideoFrame(frame);
        return;
    }
    if (m_previewBusy.exchange(true)) return;
    QMetaObject::invokeMethod(m_encodeWorker, [this, image]() {
        const QVideoFrame frame = previewFrameFor(image);
        QMetaObject::invokeMethod(this, [this, frame]() {
            m_previewBusy.store(false);
            if (m_preview && frame.isValid()) m_preview->setVideoFrame(frame);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void CameraSender::dispatchScreenImage(const QImage& img) {
    if (img.isNull()) return;
    showPreview(img);
    if (!m_encodingEnabled.load()) return;
    const qint64 now = m_clock.elapsed();
    int fps = 10;
    {
        QMutexLocker lock(&m_stateMutex);
        fps = m_screenFps;
    }
    if (m_lastFrameMs >= 0 && now - m_lastFrameMs < 1000 / qBound(1, fps, 30) - 2) return;
    if (m_encodeBusy.exchange(true)) return;
    m_lastFrameMs = now;
    if (!m_encodeWorker) {
        m_encodeBusy.store(false);
        return;
    }
    QMetaObject::invokeMethod(m_encodeWorker, [this, img, now]() {
        encodeImage(img, now);
        m_encodeBusy.store(false);
    }, Qt::QueuedConnection);
}

void CameraSender::captureScreenFrame() {
    const bool encodeBlocked = !m_encodingEnabled.load() || m_encodeBusy.load();
    if (encodeBlocked && (m_previewBusy.load() || !m_preview)) return;
    QImage img = grabDesktopOrWindow();
    if (img.isNull() && screenCaptureExhausted()) {
        stop();
        emit failed(QStringLiteral("screen_capture_unavailable"));
        return;
    }
    if (!img.isNull()) {
        m_lastScreenImage = img;
    } else {
        img = m_lastScreenImage;
    }
    if (img.isNull()) return;
    constexpr qint64 kUnchangedRefreshMs = 600;
    if (img.cacheKey() == m_lastDispatchedKey && m_lastFrameMs >= 0
        && m_clock.elapsed() - m_lastFrameMs < kUnchangedRefreshMs) {
        return;
    }
    m_lastDispatchedKey = img.cacheKey();
    dispatchScreenImage(img);
}

bool CameraSender::startScreen(QString* error) {
    if (m_screenTimer) return true;
    stopPortalCast();
    if (m_camera) stopLocalCapture();
    if (!encoderAvailable()) {
        if (error) *error = QStringLiteral("no_encoder");
        return false;
    }
    if (!screenAvailable()) {
        if (error) *error = QStringLiteral("screen_capture_unavailable");
        return false;
    }
    ensureEncodeThread();
    int intervalMs = 100;
    {
        QMutexLocker lock(&m_stateMutex);
        m_screenContent = true;
        m_screenEncodedSize = QSize();
        m_reopen = true;
        intervalMs = qMax(33, 1000 / qBound(1, m_screenFps, 30));
    }
    m_grabToolsInitialized = false;
    m_grabToolIndex = 0;
    m_lastCapturedDesktop = QImage();
    m_lastScreenImage = QImage();
    m_forceKey.store(true);
    m_lastFrameMs = -1;

    m_screenTimer = new QTimer(this);
    m_screenTimer->setTimerType(Qt::PreciseTimer);
    m_screenTimer->setInterval(intervalMs);
    connect(m_screenTimer, &QTimer::timeout, this, &CameraSender::captureScreenFrame);
    m_screenTimer->start();
    QTimer::singleShot(0, this, [this]() {
        if (m_screenTimer) captureScreenFrame();
    });
    return true;
}

bool CameraSender::startPortalScreen(QString* error) {
    if (m_portalCast) return true;
    if (!encoderAvailable()) {
        if (error) *error = QStringLiteral("no_encoder");
        return false;
    }
    if (!PortalScreenCast::available()) {
        if (error) *error = QStringLiteral("portal_unavailable");
        return false;
    }

    auto* cast = new PortalScreenCast(this);
    m_portalCast = cast;
    m_portalPending = true;
    m_portalStreaming = false;
    const QPointer<CameraSender> self(this);

    connect(cast, &PortalScreenCast::started, this, [this, self, cast]() {
        if (!self || m_portalCast != cast) return;
        m_portalPending = false;
        m_portalStreaming = true;
        stopLocalCapture();
        ensureEncodeThread();
        {
            QMutexLocker lock(&m_stateMutex);
            m_screenContent = true;
            m_screenEncodedSize = QSize();
            m_reopen = true;
        }
        m_forceKey.store(true);
        m_lastFrameMs = -1;
        emit portalScreenStarted();
    });

    connect(cast, &PortalScreenCast::frameReady, this, [this, self, cast](const QImage& img) {
        if (!self || m_portalCast != cast || !m_portalStreaming) return;
        dispatchScreenImage(img);
    });

    connect(cast, &PortalScreenCast::cancelled, this, [this, self, cast]() {
        if (!self || m_portalCast != cast) return;
        m_portalCast = nullptr;
        m_portalPending = false;
        m_portalStreaming = false;
        cast->disconnect(this);
        cast->deleteLater();
        emit portalScreenCancelled();
    });

    connect(cast, &PortalScreenCast::failed, this, [this, self, cast](const QString&) {
        if (!self || m_portalCast != cast) return;
        const bool wasStreaming = m_portalStreaming;
        m_portalCast = nullptr;
        m_portalPending = false;
        m_portalStreaming = false;
        cast->disconnect(this);
        cast->deleteLater();
        if (wasStreaming) emit portalScreenStopped();
        else emit portalScreenUnavailable();
    });

    connect(cast, &PortalScreenCast::stoppedBySystem, this, [this, self, cast]() {
        if (!self || m_portalCast != cast) return;
        m_portalCast = nullptr;
        m_portalPending = false;
        m_portalStreaming = false;
        cast->disconnect(this);
        cast->deleteLater();
        emit portalScreenStopped();
    });

    if (!cast->start()) {
        stopPortalCast();
        if (error) *error = QStringLiteral("portal_unavailable");
        return false;
    }
    return true;
}

void CameraSender::stopPortalCast() {
    m_portalPending = false;
    m_portalStreaming = false;
    if (m_portalCast) {
        PortalScreenCast* cast = m_portalCast;
        m_portalCast = nullptr;
        cast->disconnect(this);
        cast->stop();
        cast->deleteLater();
    }
}

void CameraSender::setPreferredCameraId(const QString& id) {
    if (m_preferredCameraId == id) return;
    m_preferredCameraId = id;
    if (m_camera) {
        stop();
        start();
    }
}

void CameraSender::stopLocalCapture() {
    if (m_repeatTimer) {
        m_repeatTimer->stop();
        m_repeatTimer->deleteLater();
        m_repeatTimer = nullptr;
    }
    m_lastCameraFrame = QVideoFrame();
    if (m_screenTimer) {
        m_screenTimer->stop();
        m_screenTimer->deleteLater();
        m_screenTimer = nullptr;
    }
    if (m_grabProcess) {
        QProcess* proc = m_grabProcess;
        m_grabProcess = nullptr;
        disconnect(proc, nullptr, this, nullptr);
        if (proc->state() == QProcess::NotRunning) {
            proc->deleteLater();
        } else {
            connect(proc, &QProcess::stateChanged, proc, [proc](QProcess::ProcessState state) {
                if (state == QProcess::NotRunning) proc->deleteLater();
            });
            proc->kill();
        }
    }
    if (!m_grabTempFile.isEmpty()) {
        QFile::remove(m_grabTempFile);
        m_grabTempFile.clear();
    }
    m_lastCapturedDesktop = QImage();
    m_lastScreenImage = QImage();
    if (m_sink) disconnect(m_sink, nullptr, this, nullptr);
    if (m_camera) {
        disconnect(m_camera, nullptr, this, nullptr);
        m_camera->stop();
    }
    stopEncodeThread();
    {
        QMutexLocker lock(&m_stateMutex);
        m_screenEncodedSize = QSize();
    }
    m_i420Buf.clear();
    if (m_capture) m_capture->deleteLater();
    m_capture = nullptr;
    if (m_camera) m_camera->deleteLater();
    m_camera = nullptr;
    if (m_sink) m_sink->deleteLater();
    m_sink = nullptr;
    m_encoder.close();
    if (m_preview) m_preview->setVideoFrame(QVideoFrame());
}

void CameraSender::stop() {
    stopPortalCast();
    stopLocalCapture();
}

void CameraSender::setProfile(QSize size, int fps, int bitrate) {
    size = QSize(size.width() & ~1, size.height() & ~1);
    if (size.width() < 16 || size.height() < 16 || size.width() > kMaxCameraSide || size.height() > kMaxCameraSide) return;
    fps = qBound(1, fps, 30);
    bitrate = qBound(30000, bitrate, 4000000);
    QMutexLocker lock(&m_stateMutex);
    m_camSize = size;
    m_camFps = fps;
    m_camBitrate = bitrate;
    if (!m_screenContent) {
        if (size == m_size && fps == m_fps && bitrate == m_bitrate) return;
        if (size == m_size) m_retune = true;
        else m_reopen = true;
        m_size = size;
        m_fps = fps;
        m_bitrate = bitrate;
    }
}

void CameraSender::setScreenProfile(int maxSide, int maxShort, int fps, int bitrate) {
    maxSide = qBound(320, maxSide & ~1, 1920);
    maxShort = qBound(240, maxShort & ~1, 1080);
    fps = qBound(1, fps, 30);
    bitrate = qBound(100000, bitrate, 4000000);
    QMutexLocker lock(&m_stateMutex);
    if (maxSide == m_screenMaxSide && maxShort == m_screenMaxShort
        && fps == m_screenFps && bitrate == m_screenBitrate) {
        return;
    }
    const bool sameBounds = maxSide == m_screenMaxSide && maxShort == m_screenMaxShort;
    m_screenMaxSide = maxSide;
    m_screenMaxShort = maxShort;
    m_screenFps = fps;
    m_screenBitrate = bitrate;
    if (m_screenContent) {
        if (sameBounds) m_retune = true;
        else m_reopen = true;
    }
    if (m_screenTimer) {
        const int intervalMs = qMax(33, 1000 / fps);
        QMetaObject::invokeMethod(m_screenTimer, [timer = QPointer<QTimer>(m_screenTimer), intervalMs]() {
            if (timer) timer->setInterval(intervalMs);
        }, Qt::QueuedConnection);
    }
}

QSize CameraSender::size() const {
    QMutexLocker lock(&m_stateMutex);
    return m_size;
}

int CameraSender::fps() const {
    QMutexLocker lock(&m_stateMutex);
    return m_screenContent ? m_screenFps : m_fps;
}

int CameraSender::bitrate() const {
    QMutexLocker lock(&m_stateMutex);
    return m_screenContent ? m_screenBitrate : m_bitrate;
}

void CameraSender::setEncodingEnabled(bool enabled) {
    const bool prev = m_encodingEnabled.exchange(enabled);
    if (enabled && !prev) m_forceKey.store(true);
}

void CameraSender::processFrame(const QVideoFrame& frame) {
    if (!frame.isValid()) return;
    if (m_preview) m_preview->setVideoFrame(frame);
    if (!m_encodingEnabled.load()) return;
    const qint64 now = m_clock.elapsed();
    int fps = 15;
    {
        QMutexLocker lock(&m_stateMutex);
        fps = m_camFps;
    }
    if (m_lastFrameMs >= 0 && now - m_lastFrameMs < 1000 / fps - 2) return;
    m_lastFrameMs = now;
    encodeFrame(frame, now);
}

void CameraSender::processScreenImage(const QImage& image) {
    if (image.isNull()) return;
    showPreview(image);
    if (!m_encodingEnabled.load()) return;
    const qint64 now = m_clock.elapsed();
    int fps = 10;
    {
        QMutexLocker lock(&m_stateMutex);
        fps = m_screenFps;
        m_screenContent = true;
    }
    if (m_lastFrameMs >= 0 && now - m_lastFrameMs < 1000 / qBound(1, fps, 30) - 2) return;
    m_lastFrameMs = now;
    encodeImage(image, now);
}

void CameraSender::encodeI420Buffer(const QByteArray& i420, QSize targetSize, int fps, int bitrate,
                                    bool needReopen, bool screenContent, qint64 now, bool needRetune) {
    if (i420.isEmpty()) return;
    if (!needReopen && needRetune && m_encoder.isOpen() && !m_encoder.retune(fps, bitrate)) needReopen = true;
    if (needReopen || !m_encoder.isOpen()) {
        if (!m_encoder.open(targetSize, fps, bitrate, screenContent)) {
            emit failed(QStringLiteral("encoder_open"));
            return;
        }
        m_forceKey.store(true);
    }
    const int keyInterval = screenContent ? kScreenKeyIntervalMs : kKeyIntervalMs;
    if (m_lastKeyMs < 0 || now - m_lastKeyMs >= keyInterval) m_forceKey.store(true);
    bool key = false;
    const QByteArray au = m_encoder.encodeI420(i420, now, m_forceKey.load(), &key);
    if (au.isEmpty()) return;
    if (key) {
        m_forceKey.store(false);
        m_lastKeyMs = now;
    }
    emit frameEncoded(key, au, targetSize.width(), targetSize.height());
}

void CameraSender::encodeFrame(const QVideoFrame& frame, qint64 now) {
    QSize targetSize;
    int fps = 15;
    int bitrate = 300000;
    bool needReopen = false;
    bool needRetune = false;
    {
        QMutexLocker lock(&m_stateMutex);
        targetSize = m_camSize;
        fps = m_camFps;
        bitrate = m_camBitrate;
        m_size = targetSize;
        m_fps = fps;
        m_bitrate = bitrate;
        needReopen = m_reopen || m_screenContent;
        needRetune = m_retune;
        m_screenContent = false;
        m_reopen = false;
        m_retune = false;
    }
    if (frameToI420Into(frame, targetSize, m_i420Buf)) {
        encodeI420Buffer(m_i420Buf, targetSize, fps, bitrate, needReopen, false, now, needRetune);
    }
}

void CameraSender::encodeImage(const QImage& image, qint64 now) {
    if (image.isNull()) return;
    int maxSide = 1280;
    int maxShort = 720;
    int fps = 10;
    int bitrate = 1500000;
    bool needReopen = false;
    bool needRetune = false;
    QSize targetSize;
    {
        QMutexLocker lock(&m_stateMutex);
        maxSide = m_screenMaxSide;
        maxShort = m_screenMaxShort;
        fps = m_screenFps;
        bitrate = m_screenBitrate;
        double scale = 1.0;
        const int maxDim = qMax(image.width(), image.height());
        const int minDim = qMin(image.width(), image.height());
        if (maxSide > 0 && maxDim > maxSide) scale = qMin(scale, double(maxSide) / maxDim);
        if (maxShort > 0 && minDim > maxShort) scale = qMin(scale, double(maxShort) / minDim);
        targetSize = QSize(qMax(16, int(std::lround(image.width() * scale)) & ~1),
                           qMax(16, int(std::lround(image.height() * scale)) & ~1));
        needReopen = m_reopen || targetSize != m_screenEncodedSize || !m_screenContent;
        needRetune = m_retune;
        m_screenContent = true;
        m_screenEncodedSize = targetSize;
        m_size = targetSize;
        m_fps = fps;
        m_bitrate = bitrate;
        m_reopen = false;
        m_retune = false;
    }
    if (imageToI420Into(image, targetSize, m_i420Buf)) {
        encodeI420Buffer(m_i420Buf, targetSize, fps, bitrate, needReopen, true, now, needRetune);
    }
}

}
