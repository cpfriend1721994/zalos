#include "wheel_boost.h"

#include <QCoreApplication>
#include <QStyleHints>
#include <QWheelEvent>

namespace Zalos::Core::Platform {

WheelBoost::WheelBoost(QObject* parent) : QObject(parent) {}

void WheelBoost::applyWheelScrollLines(QStyleHints* hints) {
    if (hints && hints->wheelScrollLines() < kMinWheelScrollLines) hints->setWheelScrollLines(kMinWheelScrollLines);
}

bool WheelBoost::eventFilter(QObject* watched, QEvent* event) {
    if (m_forwarding || event->type() != QEvent::Wheel) return QObject::eventFilter(watched, event);
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (wheel->phase() == Qt::ScrollBegin) m_remainder = {};
    if (wheel->pixelDelta().isNull() || wheel->phase() == Qt::NoScrollPhase) {
        return QObject::eventFilter(watched, event);
    }

    const QPointF exact = QPointF(wheel->pixelDelta()) * kTouchpadFactor + m_remainder;
    const QPoint pixel = exact.toPoint();
    m_remainder = exact - QPointF(pixel);
    const QPoint angle = (QPointF(wheel->angleDelta()) * kTouchpadFactor).toPoint();

    QWheelEvent boosted(wheel->position(), wheel->globalPosition(), pixel, angle, wheel->buttons(),
                        wheel->modifiers(), wheel->phase(), wheel->inverted(), wheel->source(),
                        wheel->pointingDevice());
    boosted.setTimestamp(wheel->timestamp());
    m_forwarding = true;
    QCoreApplication::sendEvent(watched, &boosted);
    m_forwarding = false;
    event->setAccepted(boosted.isAccepted());
    return true;
}

}
