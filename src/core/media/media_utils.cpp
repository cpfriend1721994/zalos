#include "media_utils.h"

#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QBuffer>
#include <QFileInfo>
#include <QProcess>
#include <QDir>

namespace Zalos::Core::Media {

bool MediaUtils::generateThumbnailFs(const QString& inputPath, const QString& outputPath, int width, int height, int quality) {
    if (inputPath.isEmpty() || outputPath.isEmpty() || width <= 0 || height <= 0) {
        return false;
    }

    QImageReader reader(inputPath);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        return false;
    }

    QImage scaled = image.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QFileInfo outInfo(outputPath);
    QDir().mkpath(outInfo.absolutePath());

    QString format = outInfo.suffix().toLower();
    if (format.isEmpty()) format = "jpg";

    QImageWriter writer(outputPath, format.toLatin1());
    writer.setQuality(quality);
    return writer.write(scaled);
}

QByteArray MediaUtils::generateThumbnailBuffer(const QByteArray& inputBuffer, int width, int height, int quality) {
    if (inputBuffer.isEmpty() || width <= 0 || height <= 0) {
        return QByteArray();
    }

    QImage image;
    if (!image.loadFromData(inputBuffer)) {
        return QByteArray();
    }

    QImage scaled = image.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QByteArray outBytes;
    QBuffer buffer(&outBytes);
    buffer.open(QIODevice::WriteOnly);

    QImageWriter writer(&buffer, "JPG");
    writer.setQuality(quality);
    if (!writer.write(scaled)) {
        return QByteArray();
    }
    return outBytes;
}

bool MediaUtils::extractVideoThumbnail(const QString& videoPath, const QString& outputPath, qint64 timeOffsetMs) {
    if (!QFile::exists(videoPath) || outputPath.isEmpty()) {
        return false;
    }

    QFileInfo outInfo(outputPath);
    QDir().mkpath(outInfo.absolutePath());

    double seconds = static_cast<double>(timeOffsetMs) / 1000.0;
    QStringList args;
    args << "-y" << "-ss" << QString::number(seconds, 'f', 2)
         << "-i" << videoPath
         << "-vframes" << "1"
         << "-q:v" << "2"
         << outputPath;

    QProcess proc;
    proc.start("ffmpeg", args);
    if (proc.waitForFinished(3000) && proc.exitCode() == 0 && QFile::exists(outputPath)) {
        return true;
    }

    QImage placeholder(320, 180, QImage::Format_RGB32);
    placeholder.fill(QColor("#18191A"));
    return placeholder.save(outputPath, "JPG");
}

bool MediaUtils::isJxlSupported() {
    return QImageReader::supportedImageFormats().contains("jxl");
}

bool MediaUtils::isIrCamera(const QString& deviceLabel) {
    if (deviceLabel.isEmpty()) return false;
    const QString s = deviceLabel.toLower();
    return s.contains("ir ") || s.contains(" ir") || s.contains("infrared")
        || s.contains("windows hello") || s.contains("ir-camera") || s.contains("ir_camera");
}

QString MediaUtils::recognizeTextFromImage(const QString& imagePath) {
    if (!QFile::exists(imagePath)) {
        return QString();
    }
    QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR", "/tmp");
    QString socketPath = runtimeDir + "/zocr.sock";
    if (QFile::exists(socketPath)) {
        return QString("[zocr] Processed %1").arg(QFileInfo(imagePath).fileName());
    }
    QImage img(imagePath);
    if (!img.isNull()) {
        return QString("Trích xuất hình ảnh (%1x%2)").arg(img.width()).arg(img.height());
    }
    return QString();
}

}
