#include "middle_autoscroll.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtMath>

namespace Zalos::Core::Platform {

namespace {

bool canScrollVertically(const QQuickItem* item) {
    if (!item->inherits("QQuickFlickable")) return false;
    if (!item->property("interactive").toBool()) return false;
    return item->property("contentHeight").toReal() > item->height() + 1.0;
}

}

MiddleAutoScroll::MiddleAutoScroll(QObject* parent) : QObject(parent) {
    m_timer.setInterval(kTickMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &MiddleAutoScroll::tick);
}

MiddleAutoScroll::~MiddleAutoScroll() { stop(); }

qreal MiddleAutoScroll::stepFor(qreal offset) {
    const qreal distance = qAbs(offset) - kDeadZone;
    if (distance <= 0) return 0;
    const qreal step = qMin(distance * distance / 400.0 + distance / 10.0, kMaxStep);
    return offset < 0 ? -step : step;
}

QQuickItem* MiddleAutoScroll::scrollableAt(QQuickItem* root, const QPointF& scenePos) {
    QQuickItem* found = nullptr;
    QQuickItem* item = root;
    while (item) {
        const QPointF local = item->mapFromScene(scenePos);
        QQuickItem* child = item->childAt(local.x(), local.y());
        if (!child) break;
        if (canScrollVertically(child)) found = child;
        item = child;
    }
    return found;
}

void MiddleAutoScroll::start(QQuickWindow* window, QQuickItem* target, const QPointF& scenePos) {
    m_window = window;
    m_target = target;
    m_anchor = scenePos;
    m_cursor = scenePos;
    m_held = true;
    m_dragged = false;
    if (!m_overrideCursor) {
        QGuiApplication::setOverrideCursor(Qt::SizeVerCursor);
        m_overrideCursor = true;
    }
    m_timer.start();
}

void MiddleAutoScroll::stop() {
    m_timer.stop();
    m_target = nullptr;
    m_window = nullptr;
    m_held = false;
    m_dragged = false;
    if (m_overrideCursor) {
        QGuiApplication::restoreOverrideCursor();
        m_overrideCursor = false;
    }
}

void MiddleAutoScroll::tick() {
    if (!m_target || !m_target->isVisible()) {
        stop();
        return;
    }
    const qreal step = stepFor(m_cursor.y() - m_anchor.y());
    if (step == 0) return;
    const qreal originY = m_target->property("originY").toReal();
    const qreal maxY = qMax(originY, originY + m_target->property("contentHeight").toReal() - m_target->height());
    const qreal current = m_target->property("contentY").toReal();
    const qreal next = qBound(originY, current + step, maxY);
    if (!qFuzzyCompare(next + 1.0, current + 1.0)) m_target->setProperty("contentY", next);
}

bool MiddleAutoScroll::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (active()) {
            stop();
            return true;
        }
        if (mouse->button() != Qt::MiddleButton) break;
        auto* window = qobject_cast<QQuickWindow*>(watched);
        if (!window) break;
        QQuickItem* target = scrollableAt(window->contentItem(), mouse->scenePosition());
        if (!target) break;
        start(window, target, mouse->scenePosition());
        return true;
    }
    case QEvent::MouseMove:
        if (active()) {
            m_cursor = static_cast<QMouseEvent*>(event)->scenePosition();
            if (m_held && qAbs(m_cursor.y() - m_anchor.y()) > kDeadZone) m_dragged = true;
        }
        break;
    case QEvent::MouseButtonRelease:
        if (active() && m_held && static_cast<QMouseEvent*>(event)->button() == Qt::MiddleButton) {
            m_held = false;
            if (m_dragged) stop();
            return true;
        }
        break;
    case QEvent::KeyPress:
        if (active() && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            stop();
            return true;
        }
        break;
    case QEvent::Wheel:
    case QEvent::FocusOut:
    case QEvent::Hide:
    case QEvent::WindowDeactivate:
        if (active()) stop();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

}
