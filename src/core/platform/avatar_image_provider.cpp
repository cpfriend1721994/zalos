#include "avatar_image_provider.h"

#include <QDateTime>

#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

namespace Zalos::Core::Platform {

namespace {

constexpr int kDefaultDiameter = 96;
constexpr int kMaxMemCacheKiB = 12 * 1024;

QMutex s_memCacheMutex;
QCache<QString, QImage> s_memCache(kMaxMemCacheKiB);

class AvatarResponse : public QQuickImageResponse {
public:
    AvatarResponse(QNetworkAccessManager* nam, const QString& id, const QSize& requestedSize)
        : m_diameter(requestedSize.isValid() && requestedSize.width() > 0 ? requestedSize.width() : kDefaultDiameter) {
        const QUrl url(QUrl::fromPercentEncoding(id.toUtf8()));
        const QString hashHex = QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Md5).toHex());
        m_memKey = hashHex + QLatin1Char('@') + QString::number(m_diameter);

        QImage memCached;
        if (AvatarImageProvider::lookupMemCache(m_memKey, &memCached)) {
            m_image = memCached;
            QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
            return;
        }

        const QString cachePath = AvatarImageProvider::cacheDirectory() + "/" + hashHex;

        if (url.isLocalFile() || url.scheme() == "qrc") {
            const QString path = url.isLocalFile() ? url.toLocalFile() : ":" + url.path();
            finishWith(QImage(path));
            return;
        }
        if (QFile::exists(cachePath)) {
            QImage cached(cachePath);
            if (!cached.isNull()) {
                QFile touched(cachePath);
                if (touched.open(QIODevice::ReadWrite)) {
                    touched.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
                }
                finishWith(cached);
                return;
            }
        }
        if (!url.isValid() || !url.scheme().startsWith("http")) {
            finishWith({});
            return;
        }

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36");
        req.setRawHeader("Referer", "https://chat.zalo.me/");
        req.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

        QPointer<AvatarResponse> self(this);
        QMetaObject::invokeMethod(nam, [self, nam, req, cachePath]() {
            if (!self) return;
            const QString key = req.url().toString();
            auto& waiters = inflight()[key];
            const bool running = !waiters.isEmpty();
            waiters.append(self);
            if (!running) fetch(nam, req, cachePath, true);
        }, Qt::QueuedConnection);
    }

    static QHash<QString, QList<QPointer<AvatarResponse>>>& inflight() {
        static QHash<QString, QList<QPointer<AvatarResponse>>> map;
        return map;
    }

    static void fetch(QNetworkAccessManager* nam, QNetworkRequest req, const QString& cachePath, bool allowRetry) {
        QNetworkReply* reply = nam->get(req);
        QObject::connect(reply, &QNetworkReply::finished, reply, [nam, req, reply, cachePath, allowRetry]() {
            reply->deleteLater();
            const QString key = req.url().toString();
            const bool http2Failure = reply->error() != QNetworkReply::NoError
                                      && reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
            if (http2Failure && allowRetry) {
                QNetworkRequest retry = req;
                retry.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
                fetch(nam, retry, cachePath, false);
                return;
            }
            QImage image;
            if (reply->error() == QNetworkReply::NoError && reply->isOpen()) {
                const QByteArray bytes = reply->readAll();
                image.loadFromData(bytes);
                if (!image.isNull()) {
                    QFile out(cachePath);
                    if (out.open(QIODevice::WriteOnly)) out.write(bytes);
                }
            }
            const auto waiters = inflight().take(key);
            for (const auto& w : waiters) {
                if (w) w->finishWith(image);
            }
        });
    }

    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override {
        return m_image.isNull() ? QStringLiteral("avatar unavailable") : QString();
    }

private:
    void finishWith(const QImage& source) {
        m_image = source.isNull() ? QImage() : AvatarImageProvider::makeCircular(source, m_diameter);
        if (!m_image.isNull()) {
            AvatarImageProvider::insertMemCache(m_memKey, m_image);
        }
        QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

    int m_diameter;
    QString m_memKey;
    QImage m_image;
};

}

AvatarImageProvider::AvatarImageProvider()
    : m_nam(new QNetworkAccessManager) {
}

AvatarImageProvider::~AvatarImageProvider() {
    m_nam->deleteLater();
}

QQuickImageResponse* AvatarImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    return new AvatarResponse(m_nam, id, requestedSize);
}

QString AvatarImageProvider::cacheDirectory() {
    static const QString dir = [] {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/avatars";
        QDir().mkpath(path);
        return path;
    }();
    return dir;
}

bool AvatarImageProvider::lookupMemCache(const QString& key, QImage* out) {
    QMutexLocker locker(&s_memCacheMutex);
    QImage* img = s_memCache.object(key);
    if (img && !img->isNull()) {
        if (out) *out = *img;
        return true;
    }
    return false;
}

void AvatarImageProvider::insertMemCache(const QString& key, const QImage& image) {
    if (image.isNull()) return;
    QMutexLocker locker(&s_memCacheMutex);
    s_memCache.insert(key, new QImage(image), qMax<qsizetype>(1, image.sizeInBytes() / 1024));
}

void AvatarImageProvider::clearMemCache() {
    QMutexLocker locker(&s_memCacheMutex);
    s_memCache.clear();
}

QImage AvatarImageProvider::makeCircular(const QImage& source, int diameter) {
    const int d = qMax(8, diameter);
    const int side = qMin(source.width(), source.height());
    const QImage square = source.copy((source.width() - side) / 2, (source.height() - side) / 2, side, side)
                              .scaled(d, d, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QImage out(d, d, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, d, d));
    painter.setClipPath(clip);
    painter.drawImage(0, 0, square);
    return out;
}

}
