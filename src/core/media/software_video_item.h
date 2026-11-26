#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickItem>
#include <QSize>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

class QVideoFrame;

namespace Zalos::Core::Media {

class SoftwareVideoItem : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(ZSoftwareVideoItem)
    Q_PROPERTY(int fillMode READ fillMode WRITE setFillMode NOTIFY fillModeChanged FINAL)
    Q_PROPERTY(QVideoSink* videoSink READ videoSink CONSTANT FINAL)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged FINAL)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged FINAL)

public:
    explicit SoftwareVideoItem(QQuickItem* parent = nullptr);
    ~SoftwareVideoItem() override = default;

    int fillMode() const;
    void setFillMode(int mode);
    Q_INVOKABLE QVideoSink* videoSink() const { return m_sink; }
    bool hasFrame() const;
    QSize frameSize() const;

    static void computeRects(const QSizeF& itemSize, const QSize& imageSize, int fillMode,
                             QRectF* targetRect, QRectF* sourceRect);

signals:
    void fillModeChanged();
    void hasFrameChanged();
    void frameSizeChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private slots:
    void onVideoFrameChanged(const QVideoFrame& frame);

private:
    QVideoSink* m_sink{nullptr};
    mutable QMutex m_mutex;
    QImage m_image;
    int m_fillMode{Qt::KeepAspectRatio};
    bool m_dirty{false};
    bool m_hasFrame{false};
    QSize m_frameSize;
};

}
