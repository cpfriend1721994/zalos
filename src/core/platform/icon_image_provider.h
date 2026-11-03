#pragma once

#include <QQuickImageProvider>

namespace Zalos::Core::Platform {

// Renders ZIcon glyphs (24×24 SVG path data) to antialiased images at their real pixel size:
//   image://icon/<f|s>/<colour>/<url-encoded path data>   (f = filled, s = 1.5 px stroke)
// A QtQuick Shape scaled from 24 px rendered jagged edges (no antialiasing on the software renderer and on the
// default geometry renderer without MSAA); QSvgRenderer + QPainter antialias on every backend, and the result is
// cached by the QML image cache.
class IconImageProvider : public QQuickImageProvider {
public:
    IconImageProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // The SVG document for one glyph (exposed for tests).
    static QByteArray svgFor(bool filled, const QColor& color, const QString& pathData);
};

} // namespace Zalos::Core::Platform
