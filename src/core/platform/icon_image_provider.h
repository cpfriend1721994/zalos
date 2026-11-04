#pragma once

#include <QQuickImageProvider>

namespace Zalos::Core::Platform {

class IconImageProvider : public QQuickImageProvider {
public:
    IconImageProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    static QByteArray svgFor(bool filled, const QColor& color, const QString& pathData);
};

}
