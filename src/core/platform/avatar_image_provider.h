#pragma once

#include <QQuickAsyncImageProvider>
#include <QSize>
#include <QString>

class QNetworkAccessManager;

namespace Zalos::Core::Platform {

class AvatarImageProvider : public QQuickAsyncImageProvider {
public:
    explicit AvatarImageProvider(bool circular = true);
    ~AvatarImageProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    static QImage decodeScaled(QIODevice* device, const QSize& targetSize, bool circular, QString* errorOut = nullptr);
    static QImage decodeScaled(QIODevice* device, int targetWidth, bool circular, QString* errorOut = nullptr) {
        return decodeScaled(device, QSize(targetWidth, 0), circular, errorOut);
    }
    static QImage makeCircular(const QImage& source, int diameter);
    static QString cacheDirectory();
    static bool isTooLarge(const QString& url);
    static void markTooLarge(const QString& url);

    static bool lookupMemCache(const QString& key, QImage* out);
    static void insertMemCache(const QString& key, const QImage& image);
    static void clearMemCache();

private:
    bool m_circular{true};
};

}
