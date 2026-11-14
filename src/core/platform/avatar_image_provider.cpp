#include "avatar_image_provider.h"

#include <QDateTime>

#include <QBuffer>
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
#include <QCoreApplication>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

namespace Zalos::Core::Platform {

namespace {

constexpr int kDefaultDiameter = 96;
constexpr int kDefaultCoverWidth = 0;
constexpr int kMaxMemCacheKiB = 16 * 1024;
constexpr int kRetryDelayMs = 750;

QMutex s_memCacheMutex;
QCache<QString, QImage> s_memCache(kMaxMemCacheKiB);

class AvatarResponse : public QQuickImageResponse {
public:
    AvatarResponse(QNetworkAccessManager* nam, const QString& id, const QSize& requestedSize, bool circular)
        : m_circular(circular),
          m_diameter(requestedSize.isValid() && requestedSize.width() > 0
                         ? requestedSize.width()
                         : (circular ? kDefaultDiameter : kDefaultCoverWidth)) {
        const QUrl url(QUrl::fromPercentEncoding(id.toUtf8()));
        const QString hashHex = QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Md5).toHex());
        m_memKey = hashHex + QLatin1Char(circular ? '@' : '#') + QString::number(m_diameter);

        QImage memCached;
        if (AvatarImageProvider::lookupMemCache(m_memKey, &memCached)) {
            m_image = memCached;
            QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
            return;
        }

        const QString cachePath = AvatarImageProvider::cacheDirectory() + "/" + hashHex;

        if (url.isLocalFile() || url.scheme() == "qrc") {
            const QString path = url.isLocalFile() ? url.toLocalFile() : ":" + url.path();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                failWith(QStringLiteral("cannot read %1").arg(path));
                return;
            }
            const QImage local = AvatarImageProvider::decodeScaled(&file, m_diameter, m_circular);
            if (local.isNull()) failWith(QStringLiteral("not a decodable image"));
            else finishWith(local);
            return;
        }
        if (QFile::exists(cachePath)) {
            QFile cachedFile(cachePath);
            if (cachedFile.open(QIODevice::ReadOnly)) {
                const QImage cached = AvatarImageProvider::decodeScaled(&cachedFile, m_diameter, m_circular);
                cachedFile.close();
                if (!cached.isNull()) {
                    QFile touched(cachePath);
                    if (touched.open(QIODevice::ReadWrite)) {
                        touched.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
                    }
                    finishWith(cached);
                    return;
                }
            }
        }
        if (!url.isValid() || !url.scheme().startsWith("http")) {
            failWith(QStringLiteral("unsupported URL"));
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
            const QNetworkReply::NetworkError error = reply->error();
            const bool retryable = error == QNetworkReply::RemoteHostClosedError
                                   || error == QNetworkReply::TemporaryNetworkFailureError
                                   || error == QNetworkReply::TimeoutError;
            if (retryable && allowRetry) {
                QTimer::singleShot(kRetryDelayMs, nam, [nam, req, cachePath]() { fetch(nam, req, cachePath, false); });
                return;
            }
            const auto waiters = inflight().take(key);
            if (reply->error() != QNetworkReply::NoError || !reply->isOpen()) {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QString reason = reply->error() == QNetworkReply::NoError ? QStringLiteral("not a decodable image")
                                       : status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
                for (const auto& w : waiters) {
                    if (w) w->failWith(reason);
                }
                return;
            }
            QVector<Spec> specs;
            for (const auto& w : waiters) {
                if (w) specs.append(Spec{w, w->m_diameter, w->m_circular, w->m_memKey});
            }
            if (specs.isEmpty()) return;
            const QByteArray bytes = reply->readAll();
            decodePool().start([bytes, cachePath, specs]() {
                bool decodable = false;
                {
                    QByteArray probeBytes = bytes;
                    QBuffer checkBuf(&probeBytes);
                    if (checkBuf.open(QIODevice::ReadOnly)) decodable = QImageReader(&checkBuf).canRead();
                }
                if (decodable) {
                    QFile out(cachePath);
                    if (out.open(QIODevice::WriteOnly)) out.write(bytes);
                }
                QVector<QImage> images(specs.size());
                if (decodable) {
                    for (int i = 0; i < specs.size(); ++i) {
                        QByteArray copy = bytes;
                        QBuffer buf(&copy);
                        buf.open(QIODevice::ReadOnly);
                        const QImage decoded = AvatarImageProvider::decodeScaled(&buf, specs[i].diameter, specs[i].circular);
                        images[i] = shape(decoded, specs[i].diameter, specs[i].circular);
                        if (!images[i].isNull()) AvatarImageProvider::insertMemCache(specs[i].memKey, images[i]);
                    }
                }
                QMetaObject::invokeMethod(QCoreApplication::instance(), [specs, images, decodable]() {
                    for (int i = 0; i < specs.size(); ++i) {
                        const QPointer<AvatarResponse>& w = specs[i].response;
                        if (!w) continue;
                        if (!decodable) w->failWith(QStringLiteral("not a decodable image"));
                        else if (images[i].isNull()) w->failWith(QStringLiteral("decode failed"));
                        else w->deliver(images[i]);
                    }
                }, Qt::QueuedConnection);
            });
        });
    }

    struct Spec {
        QPointer<AvatarResponse> response;
        int diameter;
        bool circular;
        QString memKey;
    };

    static QThreadPool& decodePool() {
        static QThreadPool pool;
        static const bool configured = [] {
            pool.setMaxThreadCount(2);
            pool.setExpiryTimeout(30000);
            return true;
        }();
        Q_UNUSED(configured);
        return pool;
    }

    static QImage shape(const QImage& source, int diameter, bool circular) {
        if (source.isNull()) return QImage();
        if (circular) return AvatarImageProvider::makeCircular(source, diameter);
        return (diameter > 0 && source.width() > diameter) ? source.scaledToWidth(diameter, Qt::SmoothTransformation) : source;
    }

    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override {
        return m_image.isNull() ? (m_error.isEmpty() ? QStringLiteral("image unavailable") : m_error) : QString();
    }

private:
    void failWith(const QString& reason) {
        m_error = reason;
        finishWith({});
    }

    void finishWith(const QImage& source) {
        m_image = shape(source, m_diameter, m_circular);
        if (!m_image.isNull()) {
            AvatarImageProvider::insertMemCache(m_memKey, m_image);
        }
        QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

    void deliver(const QImage& shaped) {
        m_image = shaped;
        QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

    bool m_circular;
    int m_diameter;
    QString m_memKey;
    QImage m_image;
    QString m_error;
};

}

AvatarImageProvider::AvatarImageProvider(bool circular)
    : m_nam(new QNetworkAccessManager), m_circular(circular) {
    m_nam->setTransferTimeout(30000);
}

AvatarImageProvider::~AvatarImageProvider() {
    m_nam->deleteLater();
}

QQuickImageResponse* AvatarImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    return new AvatarResponse(m_nam, id, requestedSize, m_circular);
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

QImage AvatarImageProvider::decodeScaled(QIODevice* device, int targetWidth, bool circular) {
    if (!device) return {};
    QImageReader reader(device);
    reader.setAutoTransform(true);
    const QSize orig = reader.size();
    if (orig.isValid() && orig.width() > 0 && orig.height() > 0) {
        if (circular) {
            const int d = qMax(8, targetWidth);
            const int minSide = qMin(orig.width(), orig.height());
            if (minSide > d) {
                reader.setScaledSize(orig.scaled(d, d, Qt::KeepAspectRatioByExpanding));
            }
        } else if (targetWidth > 0 && orig.width() > targetWidth) {
            const int scaledH = qMax(1, int((qint64(orig.height()) * targetWidth + orig.width() / 2) / orig.width()));
            reader.setScaledSize(QSize(targetWidth, scaledH));
        }
    }
    return reader.read();
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
