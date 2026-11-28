#include "icon_image_provider.h"

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrl>

#include <cmath>

namespace Zalos::Core::Platform {

namespace {

qreal activeDevicePixelRatio() {
    qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    if (dpr <= 1.0) {
        bool ok = false;
        const qreal scale = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&ok);
        if (ok && scale > 0.25 && scale < 1.0) dpr = scale;
    }
    return dpr > 0.0 ? dpr : 1.0;
}

QMutex& cacheMutex() {
    static QMutex mutex;
    return mutex;
}

QHash<quint64, QImage>& pacmanCache() {
    static QHash<quint64, QImage> cache;
    return cache;
}

QHash<QString, QImage>& dotCache() {
    static QHash<QString, QImage> cache;
    return cache;
}

}

IconImageProvider::IconImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QByteArray IconImageProvider::svgFor(bool filled, const QColor& color, const QString& pathData) {
    const QString rgb = color.name(QColor::HexRgb);
    const QString alpha = QString::number(color.alphaF(), 'f', 3);
    const QString paint = filled
        ? QStringLiteral("fill=\"%1\" fill-opacity=\"%2\"").arg(rgb, alpha)
        : QStringLiteral("fill=\"none\" stroke=\"%1\" stroke-opacity=\"%2\" stroke-width=\"1.5\" "
                         "stroke-linejoin=\"round\" stroke-linecap=\"round\"").arg(rgb, alpha);
    QString escaped = pathData;
    escaped.replace('&', "&amp;").replace('<', "&lt;").replace('"', "&quot;");
    return QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"24\" height=\"24\">"
                          "<path d=\"%1\" %2/></svg>").arg(escaped, paint).toUtf8();
}

QImage IconImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    if (id.startsWith(QLatin1String("pacman/"))) {
        const int logicalPx = qBound(8, id.mid(7).toInt(), 128);
        const int phys = qMax(8, int(std::lround(logicalPx * activeDevicePixelRatio())));
        const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(phys, phys);
        if (size) *size = target;
        const quint64 key = (quint64(target.width()) << 16) | quint64(target.height());
        {
            QMutexLocker lock(&cacheMutex());
            const auto it = pacmanCache().constFind(key);
            if (it != pacmanCache().cend()) return it.value();
        }
        QImage image(target, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QSvgRenderer renderer(QStringLiteral(":/Zalos/assets/icons/pacman.svg"));
        if (renderer.isValid()) {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(target)));
        }
        QMutexLocker lock(&cacheMutex());
        if (pacmanCache().size() >= 16) pacmanCache().clear();
        pacmanCache().insert(key, image);
        return image;
    }

    if (id.startsWith(QLatin1String("dot/"))) {
        const QStringList parts = id.split(QLatin1Char('/'));
        const QColor ring = parts.size() > 1 ? QColor(QUrl::fromPercentEncoding(parts[1].toUtf8())) : QColor(Qt::transparent);
        const QColor fill = parts.size() > 2 ? QColor(QUrl::fromPercentEncoding(parts[2].toUtf8())) : QColor(Qt::green);
        const int ratioPermille = parts.size() > 3 ? qBound(300, parts[3].toInt(), 1000) : 1000;
        const qreal ratio = ratioPermille / 1000.0;
        const qreal dpr = activeDevicePixelRatio();
        const int defPx = qMax(6, int(std::lround(14 * dpr)));
        const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(defPx, defPx);
        if (size) *size = target;
        const QString cacheKey = id + QLatin1Char('@') + QString::number(target.width()) + QLatin1Char('x')
                                 + QString::number(target.height());
        {
            QMutexLocker lock(&cacheMutex());
            const auto it = dotCache().constFind(cacheKey);
            if (it != dotCache().cend()) return it.value();
        }
        QImage image(target, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        const QPointF center(target.width() / 2.0, target.height() / 2.0);
        const qreal outerR = (qMin(target.width(), target.height()) - 1.0) / 2.0;
        if (ring.isValid() && ring.alpha() > 0 && ratio < 0.999) {
            painter.setBrush(ring);
            painter.drawEllipse(center, outerR, outerR);
        }
        painter.setBrush(fill.isValid() ? fill : QColor(Qt::green));
        const qreal innerR = outerR * ratio;
        painter.drawEllipse(center, innerR, innerR);
        QMutexLocker lock(&cacheMutex());
        if (dotCache().size() >= 32) dotCache().clear();
        dotCache().insert(cacheKey, image);
        return image;
    }

    const int first = id.indexOf('/');
    const int second = first < 0 ? -1 : id.indexOf('/', first + 1);
    const QSize target = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(24, 24);
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (size) *size = target;
    if (first < 0 || second < 0) return image;
    const bool filled = id.left(first) != QLatin1String("s");
    const QColor color(QUrl::fromPercentEncoding(id.mid(first + 1, second - first - 1).toUtf8()));
    const QString path = QUrl::fromPercentEncoding(id.mid(second + 1).toUtf8());
    if (path.isEmpty()) return image;
    QSvgRenderer renderer(svgFor(filled, color.isValid() ? color : QColor(Qt::black), path));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(target)));
    return image;
}

}
