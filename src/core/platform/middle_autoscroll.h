#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>

class QQuickItem;
class QQuickWindow;

namespace Zalos::Core::Platform {

class MiddleAutoScroll : public QObject {
    Q_OBJECT
public:
    static constexpr qreal kDeadZone = 12.0;
    static constexpr qreal kMaxStep = 80.0;
    static constexpr int kTickMs = 16;

    explicit MiddleAutoScroll(QObject* parent = nullptr);
    ~MiddleAutoScroll() override;

    static qreal stepFor(qreal offset);
    static QQuickItem* scrollableAt(QQuickItem* root, const QPointF& scenePos);

    bool active() const { return !m_target.isNull(); }
    QQuickItem* target() const { return m_target; }
    void stop();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void start(QQuickWindow* window, QQuickItem* target, const QPointF& scenePos);
    void tick();

    QPointer<QQuickItem> m_target;
    QPointer<QQuickWindow> m_window;
    QTimer m_timer;
    QPointF m_anchor;
    QPointF m_cursor;
    bool m_held = false;
    bool m_dragged = false;
    bool m_overrideCursor = false;
};

}
