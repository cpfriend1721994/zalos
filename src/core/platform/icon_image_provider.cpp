#include "icon_image_provider.h"

#include <QColor>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrl>

namespace Zalos::Core::Platform {

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
    // id = "<f|s>/<colour>/<encoded path>"; the path may itself contain '/' only encoded.
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

} // namespace Zalos::Core::Platform
