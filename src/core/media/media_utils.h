#pragma once

#include <QString>
#include <QByteArray>

namespace Zalos::Core::Media {

class MediaUtils {
public:
    static bool generateThumbnailFs(const QString& inputPath, const QString& outputPath, int width, int height, int quality = 85);

    static QByteArray generateThumbnailBuffer(const QByteArray& inputBuffer, int width, int height, int quality = 85);

    static bool extractVideoThumbnail(const QString& videoPath, const QString& outputPath, qint64 timeOffsetMs = 1000);

    static bool isJxlSupported();

    static bool isIrCamera(const QString& deviceLabel);

    static QString recognizeTextFromImage(const QString& imagePath);
};

}
