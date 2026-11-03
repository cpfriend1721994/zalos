#pragma once

#include <QString>
#include <QByteArray>

namespace Zalos::Core::Media {

class MediaUtils {
public:
    // Replaces nativelibs/zimage thumbnailFs
    static bool generateThumbnailFs(const QString& inputPath, const QString& outputPath, int width, int height, int quality = 85);

    // Replaces nativelibs/zimage thumbnail (buffer to buffer)
    static QByteArray generateThumbnailBuffer(const QByteArray& inputBuffer, int width, int height, int quality = 85);

    // Replaces nativelibs/mp4thumb video thumbnail extraction
    static bool extractVideoThumbnail(const QString& videoPath, const QString& outputPath, qint64 timeOffsetMs = 1000);

    // Replaces nativelibs/zjxl capability check
    static bool isJxlSupported();

    // Filters out infrared cameras (Windows Hello / IR) for video calls
    static bool isIrCamera(const QString& deviceLabel);

    // Replaces zocr-host Optical Character Recognition client
    static QString recognizeTextFromImage(const QString& imagePath);
};

} // namespace Zalos::Core::Media

