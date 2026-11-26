#include "software_video_item.h"

#include "platform/multimedia_support.h"

#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QVideoFrame>

namespace Zalos::Core::Media {

SoftwareVideoItem::SoftwareVideoItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    if (Platform::MultimediaSupport::available()) {
        m_sink = new QVideoSink(this);
        connect(m_sink, &QVideoSink::videoFrameChanged, this, &SoftwareVideoItem::onVideoFrameChanged);
    }
}

int SoftwareVideoItem::fillMode() const {
    QMutexLocker lock(&m_mutex);
    return m_fillMode;
}

void SoftwareVideoItem::setFillMode(int mode) {
    {
        QMutexLocker lock(&m_mutex);
        if (m_fillMode == mode) return;
        m_fillMode = mode;
    }
    emit fillModeChanged();
    update();
}

bool SoftwareVideoItem::hasFrame() const {
    return m_hasFrame;
}

QSize SoftwareVideoItem::frameSize() const {
    return m_frameSize;
}

void SoftwareVideoItem::onVideoFrameChanged(const QVideoFrame& frame) {
    if (!frame.isValid()) {
        {
            QMutexLocker lock(&m_mutex);
            m_image = QImage();
            m_dirty = true;
        }
        const bool hadFrame = m_hasFrame;
        const bool hadSize = !m_frameSize.isEmpty();
        m_hasFrame = false;
        m_frameSize = QSize();
        if (hadFrame) emit hasFrameChanged();
        if (hadSize) emit frameSizeChanged();
        update();
        return;
    }

    const QImage img = frame.toImage();
    if (img.isNull()) return;
    {
        QMutexLocker lock(&m_mutex);
        m_image = img;
        m_dirty = true;
    }
    const bool hadFrame = m_hasFrame;
    const QSize prevSize = m_frameSize;
    m_hasFrame = true;
    m_frameSize = img.size();
    if (!hadFrame) emit hasFrameChanged();
    if (prevSize != m_frameSize) emit frameSizeChanged();
    update();
}

void SoftwareVideoItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

void SoftwareVideoItem::computeRects(const QSizeF& itemSize, const QSize& imageSize, int fillMode,
                                     QRectF* targetRect, QRectF* sourceRect) {
    const qreal w = itemSize.width();
    const qreal h = itemSize.height();
    const qreal iw = imageSize.width();
    const qreal ih = imageSize.height();
    *targetRect = QRectF(0.0, 0.0, w, h);
    *sourceRect = QRectF(0.0, 0.0, iw, ih);
    if (w <= 0.0 || h <= 0.0 || iw <= 0.0 || ih <= 0.0) return;

    if (fillMode == Qt::IgnoreAspectRatio) return;

    if (fillMode == Qt::KeepAspectRatioByExpanding) {
        const qreal itemAspect = w / h;
        const qreal imgAspect = iw / ih;
        if (imgAspect > itemAspect) {
            const qreal sw = ih * itemAspect;
            *sourceRect = QRectF((iw - sw) * 0.5, 0.0, sw, ih);
        } else {
            const qreal sh = iw / itemAspect;
            *sourceRect = QRectF(0.0, (ih - sh) * 0.5, iw, sh);
        }
        return;
    }

    const qreal scale = qMin(w / iw, h / ih);
    const qreal tw = iw * scale;
    const qreal th = ih * scale;
    *targetRect = QRectF((w - tw) * 0.5, (h - th) * 0.5, tw, th);
}

QSGNode* SoftwareVideoItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    QImage frameCopy;
    bool dirty = false;
    int mode = Qt::KeepAspectRatio;
    {
        QMutexLocker lock(&m_mutex);
        frameCopy = m_image;
        dirty = m_dirty;
        mode = m_fillMode;
        m_dirty = false;
    }
    if (frameCopy.isNull() || width() <= 0.0 || height() <= 0.0 || !window()) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<QSGImageNode*>(oldNode);
    if (!node) {
        node = window()->createImageNode();
        if (!node) return nullptr;
        node->setFiltering(QSGTexture::Linear);
        node->setOwnsTexture(true);
        dirty = true;
    }
    if (dirty || !node->texture()) {
        QSGTexture* tex = window()->createTextureFromImage(frameCopy, QQuickWindow::TextureIsOpaque);
        if (!tex) {
            delete node;
            return nullptr;
        }
        node->setTexture(tex);
    }

    QRectF targetRect;
    QRectF sourceRect;
    computeRects(size(), frameCopy.size(), mode, &targetRect, &sourceRect);
    node->setRect(targetRect);
    node->setSourceRect(sourceRect);
    return node;
}

}
