#include "swipe_back.h"

#include <QEvent>
#include <QPointingDevice>
#include <QWheelEvent>
#include <QtMath>

namespace Zalos::Core::Platform {

SwipeBack::SwipeBack(QObject* parent) : QObject(parent) {}

bool SwipeBack::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::Wheel) return QObject::eventFilter(watched, event);
    auto* wheel = static_cast<QWheelEvent*>(event);
    const QPointingDevice* device = wheel->pointingDevice();
    if (!device || device->type() != QInputDevice::DeviceType::TouchPad || wheel->pixelDelta().isNull())
        return QObject::eventFilter(watched, event);
    if (wheel->phase() == Qt::ScrollBegin) {
        m_dx = 0;
        m_dy = 0;
        m_fired = false;
    }
    const qreal dx = wheel->inverted() ? wheel->pixelDelta().x() : -wheel->pixelDelta().x();
    m_dx += dx;
    m_dy += qAbs(wheel->pixelDelta().y());
    if (!m_fired && m_dx > kTriggerPixels && m_dx > kHorizontalRatio * m_dy) {
        m_fired = true;
        emit swipeBack();
    }
    if (wheel->phase() == Qt::ScrollEnd) {
        m_dx = 0;
        m_dy = 0;
        m_fired = false;
    }
    return QObject::eventFilter(watched, event);
}

}
