#pragma once

#include <QQuickAsyncImageProvider>

class QNetworkAccessManager;

namespace Zalos::Core::Platform {

class AvatarImageProvider : public QQuickAsyncImageProvider {
public:
    explicit AvatarImageProvider(bool circular = true);
    ~AvatarImageProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    static QImage decodeScaled(QIODevice* device, int targetWidth, bool circular);
    static QImage makeCircular(const QImage& source, int diameter);
    static QString cacheDirectory();

    static bool lookupMemCache(const QString& key, QImage* out);
    static void insertMemCache(const QString& key, const QImage& image);
    static void clearMemCache();

private:
    QNetworkAccessManager* m_nam{nullptr};
    bool m_circular{true};
};

}
