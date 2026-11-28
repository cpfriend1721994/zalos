#include "avatar_image_provider.h"

#include <QDateTime>
#include <QFileInfo>

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
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QCoreApplication>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace Zalos::Core::Platform {

namespace {

constexpr int kDefaultDiameter = 96;
constexpr int kDefaultCoverWidth = 0;
constexpr int kMaxAvatarCacheKiB = 4 * 1024;
constexpr int kMaxMediaCacheKiB = 8 * 1024;
constexpr int kMaxCachedMediaImageKiB = 4 * 1024;
constexpr int kRetryDelayMs = 750;
constexpr qint64 kTouchIntervalSecs = 24 * 3600;
constexpr qint64 kMaxAvatarBytes = 32LL * 1024 * 1024;

QMutex s_memCacheMutex;
QCache<QString, QImage> s_avatarMemCache(kMaxAvatarCacheKiB);
QCache<QString, QImage> s_mediaMemCache(kMaxMediaCacheKiB);
QMutex s_tooLargeMutex;
QSet<QString> s_tooLargeUrls;

QNetworkAccessManager* sharedNam() {
    static QPointer<QNetworkAccessManager> s_nam;
    if (!s_nam) {
        s_nam = new QNetworkAccessManager(QCoreApplication::instance());
        s_nam->setTransferTimeout(30000);
    }
    return s_nam;
}

class AvatarResponse : public QQuickImageResponse {
public:
    struct State {
        QMutex mutex;
        AvatarResponse* response{nullptr};
        QImage image;
        QString error;
        int diameter{0};
        QSize targetSize;
        bool circular{false};
        QString memKey;
        bool cancelled{false};
    };

    AvatarResponse(const QString& id, const QSize& requestedSize, bool circular)
        : m_circular(circular),
          m_diameter(requestedSize.isValid() && requestedSize.width() > 0
                         ? requestedSize.width()
                         : (circular ? kDefaultDiameter : kDefaultCoverWidth)),
          m_targetSize(requestedSize.width() > 0 ? requestedSize.width() : (circular ? kDefaultDiameter : kDefaultCoverWidth),
                       requestedSize.height() > 0 ? requestedSize.height() : 0) {
        const QUrl url(QUrl::fromPercentEncoding(id.toUtf8()));
        const QString hashHex = QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Md5).toHex());
        m_memKey = hashHex + (circular ? QLatin1Char('@') + QString::number(m_diameter)
                                       : QLatin1Char('#') + QString::number(m_targetSize.width()) + QLatin1Char('x') + QString::number(m_targetSize.height()));
        m_state = std::make_shared<State>();
        m_state->response = this;
        m_state->diameter = m_diameter;
        m_state->targetSize = m_targetSize;
        m_state->circular = m_circular;
        m_state->memKey = m_memKey;

        QImage memCached;
        if (AvatarImageProvider::lookupMemCache(m_memKey, &memCached)) {
            m_state->image = memCached;
            QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
            return;
        }

        const QString cachePath = AvatarImageProvider::cacheDirectory() + "/" + hashHex;

        if (url.isLocalFile() || url.scheme() == "qrc") {
            const QString path = url.isLocalFile() ? url.toLocalFile() : ":" + url.path();
            const QSize targetSize = m_targetSize;
            const bool circularFlag = m_circular;
            const QString memKey = m_memKey;
            const QString urlStr = url.toString();
            const std::weak_ptr<State> weak = m_state;
            decodePool().start([weak, path, targetSize, circularFlag, memKey, urlStr]() {
                if (weak.expired()) return;
                QImage shaped;
                QString error;
                if (!AvatarImageProvider::lookupMemCache(memKey, &shaped)) {
                    QFile file(path);
                    if (!file.open(QIODevice::ReadOnly)) {
                        error = QStringLiteral("cannot read %1").arg(path);
                    } else {
                        QString decodeErr;
                        const QImage decoded = AvatarImageProvider::decodeScaled(&file, targetSize, circularFlag, &decodeErr);
                        shaped = shape(decoded, targetSize, circularFlag);
                        if (!shaped.isNull()) AvatarImageProvider::insertMemCache(memKey, shaped);
                        else {
                            if (decodeErr == QLatin1String("image too large")) AvatarImageProvider::markTooLarge(urlStr);
                            error = decodeErr.isEmpty() ? QStringLiteral("not a decodable image") : decodeErr;
                        }
                    }
                }
                if (const auto s = weak.lock()) {
                    if (shaped.isNull()) failState(s, error);
                    else deliverState(s, shaped);
                }
            });
            return;
        }
        if (QFileInfo::exists(cachePath)) {
            const QSize targetSize = m_targetSize;
            const bool circularFlag = m_circular;
            const QString memKey = m_memKey;
            const std::weak_ptr<State> weak = m_state;
            decodePool().start([weak, url, cachePath, targetSize, circularFlag, memKey]() {
                if (weak.expired()) return;
                QImage shaped;
                QString decodeErr;
                if (!AvatarImageProvider::lookupMemCache(memKey, &shaped)) {
                    QFile cachedFile(cachePath);
                    if (cachedFile.open(QIODevice::ReadOnly)) {
                        const QImage decoded = AvatarImageProvider::decodeScaled(&cachedFile, targetSize, circularFlag, &decodeErr);
                        cachedFile.close();
                        shaped = shape(decoded, targetSize, circularFlag);
                        if (!shaped.isNull()) {
                            AvatarImageProvider::insertMemCache(memKey, shaped);
                            const QDateTime now = QDateTime::currentDateTime();
                            if (QFileInfo(cachePath).lastModified().secsTo(now) > kTouchIntervalSecs) {
                                QFile touched(cachePath);
                                if (touched.open(QIODevice::ReadWrite)) {
                                    touched.setFileTime(now, QFileDevice::FileModificationTime);
                                }
                            }
                        }
                    }
                }
                const auto s = weak.lock();
                if (!s) return;
                if (!shaped.isNull()) {
                    deliverState(s, shaped);
                    return;
                }
                if (decodeErr == QLatin1String("image too large")) {
                    AvatarImageProvider::markTooLarge(url.toString());
                    failState(s, decodeErr);
                    return;
                }
                startNetworkFetch(s, url, cachePath);
            });
            return;
        }
        startNetworkFetch(m_state, url, cachePath);
    }

    ~AvatarResponse() override {
        QMutexLocker locker(&m_state->mutex);
        m_state->cancelled = true;
        m_state->response = nullptr;
    }

    void cancel() override {
        std::shared_ptr<State> s = m_state;
        QMutexLocker locker(&s->mutex);
        s->cancelled = true;
        s->image = QImage();
    }

    static void startNetworkFetch(const std::shared_ptr<State>& s, const QUrl& url, const QString& cachePath) {
        if (!url.isValid() || !url.scheme().startsWith("http")) {
            failState(s, QStringLiteral("unsupported URL"));
            return;
        }

        QNetworkRequest req(url);
        if (url.host().endsWith(QLatin1String("openstreetmap.org"), Qt::CaseInsensitive)) {
#ifdef ZALOS_APP_VERSION
            req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Zalos/") + QLatin1String(ZALOS_APP_VERSION));
#else
            req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Zalos/1.0"));
#endif
            req.setRawHeader("Accept", "image/png,image/*;q=0.8");
        } else {
            req.setHeader(QNetworkRequest::UserAgentHeader,
                          "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36");
            req.setRawHeader("Referer", "https://chat.zalo.me/");
            req.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
        }
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

        const std::weak_ptr<State> weak = s;
        QObject* app = QCoreApplication::instance();
        if (!app) {
            failState(s, QStringLiteral("unsupported URL"));
            return;
        }
        QMetaObject::invokeMethod(app, [weak, req, cachePath]() {
            const auto locked = weak.lock();
            if (!locked) return;
            {
                QMutexLocker locker(&locked->mutex);
                if (locked->cancelled || !locked->response) return;
            }
            QNetworkAccessManager* nam = sharedNam();
            if (!nam) {
                failState(locked, QStringLiteral("not a decodable image"));
                return;
            }
            const QString key = req.url().toString();
            auto& waiters = inflight()[key];
            const bool running = !waiters.isEmpty();
            waiters.append(weak);
            if (!running) fetch(nam, req, cachePath, true);
        }, Qt::QueuedConnection);
    }

    static QHash<QString, QList<std::weak_ptr<State>>>& inflight() {
        static QHash<QString, QList<std::weak_ptr<State>>> map;
        return map;
    }

    static void fetch(QNetworkAccessManager* nam, QNetworkRequest req, const QString& cachePath, bool allowRetry) {
        QNetworkReply* reply = nam->get(req);
        reply->setReadBufferSize(kMaxAvatarBytes + 1);
        auto tooLarge = std::make_shared<bool>(false);
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, tooLarge](qint64 got, qint64 total) {
            if (got > kMaxAvatarBytes || total > kMaxAvatarBytes) {
                *tooLarge = true;
                reply->abort();
            }
        });
        QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, tooLarge]() {
            if (reply->bytesAvailable() > kMaxAvatarBytes) {
                *tooLarge = true;
                reply->abort();
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, reply, [nam, req, reply, cachePath, allowRetry, tooLarge]() {
            reply->deleteLater();
            const QString key = req.url().toString();
            const QNetworkReply::NetworkError error = reply->error();
            const bool retryable = !*tooLarge
                                   && (error == QNetworkReply::RemoteHostClosedError
                                       || error == QNetworkReply::TemporaryNetworkFailureError
                                       || error == QNetworkReply::TimeoutError);
            if (retryable && allowRetry) {
                QTimer::singleShot(kRetryDelayMs, nam, [nam, req, cachePath]() { fetch(nam, req, cachePath, false); });
                return;
            }
            const auto waiters = inflight().take(key);
            if (*tooLarge) {
                AvatarImageProvider::markTooLarge(key);
                for (const auto& w : waiters) {
                    if (const auto s = w.lock()) failState(s, QStringLiteral("image too large"));
                }
                return;
            }
            if (reply->error() != QNetworkReply::NoError || !reply->isOpen()) {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QString reason = reply->error() == QNetworkReply::NoError ? QStringLiteral("not a decodable image")
                                       : status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
                for (const auto& w : waiters) {
                    if (const auto s = w.lock()) failState(s, reason);
                }
                return;
            }
            QVector<Spec> specs;
            for (const auto& w : waiters) {
                const auto s = w.lock();
                if (!s) continue;
                QMutexLocker locker(&s->mutex);
                if (!s->cancelled && s->response) {
                    specs.append(Spec{w, s->targetSize, s->circular, s->memKey});
                }
            }
            if (specs.isEmpty()) return;
            const QByteArray bytes = reply->readAll();
            if (bytes.size() > kMaxAvatarBytes) {
                AvatarImageProvider::markTooLarge(key);
                for (const auto& spec : std::as_const(specs)) {
                    if (const auto s = spec.state.lock()) failState(s, QStringLiteral("image too large"));
                }
                return;
            }
            decodePool().start([bytes, cachePath, specs, key]() {
                bool decodable = false;
                {
                    QBuffer checkBuf;
                    checkBuf.setData(bytes);
                    if (checkBuf.open(QIODevice::ReadOnly)) decodable = QImageReader(&checkBuf).canRead();
                }
                if (decodable) {
                    QSaveFile out(cachePath);
                    if (out.open(QIODevice::WriteOnly)) {
                        if (out.write(bytes) == bytes.size()) out.commit();
                    }
                }
                QVector<QImage> images(specs.size());
                QVector<QString> errors(specs.size());
                if (decodable) {
                    QHash<QString, QImage> decodedBySpec;
                    for (int i = 0; i < specs.size(); ++i) {
                        auto it = decodedBySpec.constFind(specs[i].memKey);
                        if (it != decodedBySpec.constEnd()) {
                            images[i] = *it;
                            continue;
                        }
                        QBuffer buf;
                        buf.setData(bytes);
                        buf.open(QIODevice::ReadOnly);
                        QString decodeErr;
                        const QImage decoded = AvatarImageProvider::decodeScaled(&buf, specs[i].targetSize, specs[i].circular, &decodeErr);
                        images[i] = shape(decoded, specs[i].targetSize, specs[i].circular);
                        if (!images[i].isNull()) {
                            AvatarImageProvider::insertMemCache(specs[i].memKey, images[i]);
                            decodedBySpec.insert(specs[i].memKey, images[i]);
                        } else {
                            errors[i] = decodeErr;
                        }
                    }
                }
                for (int i = 0; i < specs.size(); ++i) {
                    const auto s = specs[i].state.lock();
                    if (!s) continue;
                    if (!decodable) failState(s, QStringLiteral("not a decodable image"));
                    else if (images[i].isNull()) {
                        if (errors[i] == QLatin1String("image too large")) AvatarImageProvider::markTooLarge(key);
                        failState(s, errors[i].isEmpty() ? QStringLiteral("decode failed") : errors[i]);
                    }
                    else deliverState(s, images[i]);
                }
            });
        });
    }

    struct Spec {
        std::weak_ptr<State> state;
        QSize targetSize;
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

    static QImage shape(const QImage& source, const QSize& targetSize, bool circular) {
        if (source.isNull()) return QImage();
        if (circular) return AvatarImageProvider::makeCircular(source, targetSize.width());
        const int maxW = targetSize.width() > 0 ? targetSize.width() : 2048;
        const int maxH = targetSize.height() > 0 ? targetSize.height() : qMax(maxW * 4, 2048);
        if (source.width() > maxW || source.height() > maxH) {
            return source.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return source;
    }

    QQuickTextureFactory* textureFactory() const override {
        QMutexLocker locker(&m_state->mutex);
        return QQuickTextureFactory::textureFactoryForImage(m_state->image);
    }

    QString errorString() const override {
        QMutexLocker locker(&m_state->mutex);
        return m_state->image.isNull()
            ? (m_state->error.isEmpty() ? QStringLiteral("image unavailable") : m_state->error)
            : QString();
    }

private:
    static void failState(const std::shared_ptr<State>& s, const QString& reason) {
        QMutexLocker locker(&s->mutex);
        if (s->cancelled || !s->response) return;
        s->error = reason;
        s->image = QImage();
        QMetaObject::invokeMethod(s->response, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

    static void deliverState(const std::shared_ptr<State>& s, const QImage& shaped) {
        QMutexLocker locker(&s->mutex);
        if (s->cancelled || !s->response) return;
        s->image = shaped;
        QMetaObject::invokeMethod(s->response, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

    bool m_circular;
    int m_diameter;
    QSize m_targetSize;
    QString m_memKey;
    std::shared_ptr<State> m_state;
};

}

AvatarImageProvider::AvatarImageProvider(bool circular)
    : m_circular(circular) {
}

AvatarImageProvider::~AvatarImageProvider() = default;

QQuickImageResponse* AvatarImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    return new AvatarResponse(id, requestedSize, m_circular);
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
    auto& cache = key.contains(QLatin1Char('@')) ? s_avatarMemCache : s_mediaMemCache;
    QImage* img = cache.object(key);
    if (img && !img->isNull()) {
        if (out) *out = *img;
        return true;
    }
    return false;
}

void AvatarImageProvider::insertMemCache(const QString& key, const QImage& image) {
    if (image.isNull()) return;
    const int costKiB = qMax<qsizetype>(1, image.sizeInBytes() / 1024);
    const bool isAvatar = key.contains(QLatin1Char('@'));
    if (!isAvatar && costKiB > kMaxCachedMediaImageKiB) return;
    QMutexLocker locker(&s_memCacheMutex);
    auto& cache = isAvatar ? s_avatarMemCache : s_mediaMemCache;
    cache.insert(key, new QImage(image), costKiB);
}

void AvatarImageProvider::clearMemCache() {
    {
        QMutexLocker locker(&s_memCacheMutex);
        s_avatarMemCache.clear();
        s_mediaMemCache.clear();
    }
    QMutexLocker tl(&s_tooLargeMutex);
    s_tooLargeUrls.clear();
}

void AvatarImageProvider::markTooLarge(const QString& url) {
    if (url.isEmpty()) return;
    QMutexLocker locker(&s_tooLargeMutex);
    if (s_tooLargeUrls.size() >= 256) s_tooLargeUrls.clear();
    s_tooLargeUrls.insert(url);
}

bool AvatarImageProvider::isTooLarge(const QString& url) {
    if (url.isEmpty()) return false;
    QMutexLocker locker(&s_tooLargeMutex);
    return s_tooLargeUrls.contains(url);
}

QImage AvatarImageProvider::decodeScaled(QIODevice* device, const QSize& targetSize, bool circular, QString* errorOut) {
    if (errorOut) errorOut->clear();
    if (!device) return {};
    QImageReader reader(device);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        if (errorOut) *errorOut = QStringLiteral("not a decodable image");
        return {};
    }
    const QSize orig = reader.size();
    if (orig.isValid() && (orig.width() <= 0 || orig.height() <= 0)) {
        if (errorOut) *errorOut = QStringLiteral("not a decodable image");
        return {};
    }
    if (orig.isValid() && (orig.width() > 16384 || orig.height() > 16384
                           || qint64(orig.width()) * qint64(orig.height()) > 64LL * 1024 * 1024)) {
        if (errorOut) *errorOut = QStringLiteral("image too large");
        return {};
    }
    if (orig.isValid() && orig.width() > 0 && orig.height() > 0) {
        if (circular) {
            const int d = qMax(8, targetSize.width());
            const int minSide = qMin(orig.width(), orig.height());
            if (minSide > d) {
                reader.setScaledSize(orig.scaled(d, d, Qt::KeepAspectRatioByExpanding));
            }
        } else {
            const int maxW = qMin(targetSize.width() > 0 ? targetSize.width() : 2048, 4096);
            const int maxH = qMin(targetSize.height() > 0 ? targetSize.height() : qMax(maxW * 4, 2048), 4096);
            if (orig.width() > maxW || orig.height() > maxH) {
                QSize scaled = orig.scaled(maxW, maxH, Qt::KeepAspectRatio);
                scaled.setWidth(qMax(1, scaled.width()));
                scaled.setHeight(qMax(1, scaled.height()));
                reader.setScaledSize(scaled);
            }
        }
    }
    const QImage img = reader.read();
    if (img.isNull() && errorOut && errorOut->isEmpty()) {
        *errorOut = reader.error() == QImageReader::InvalidDataError ? QStringLiteral("not a decodable image")
                                                                    : reader.errorString();
    }
    return img;
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
