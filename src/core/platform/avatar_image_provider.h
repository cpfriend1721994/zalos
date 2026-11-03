#pragma once

#include <QQuickAsyncImageProvider>

class QNetworkAccessManager;

namespace Zalos::Core::Platform {

// Serves "image://avatar/<percent-encoded url>" as a circular, antialiased image.
// Remote avatars are downloaded once and cached on disk.
class AvatarImageProvider : public QQuickAsyncImageProvider {
public:
    AvatarImageProvider();
    ~AvatarImageProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    static QImage makeCircular(const QImage& source, int diameter);
    static QString cacheDirectory();

    // In-memory LRU cache to eliminate disk I/O and CPU re-cropping during scrolling.
    static bool lookupMemCache(const QString& key, QImage* out);
    static void insertMemCache(const QString& key, const QImage& image);
    static void clearMemCache();

private:
    QNetworkAccessManager* m_nam{nullptr};
};

} // namespace Zalos::Core::Platform
