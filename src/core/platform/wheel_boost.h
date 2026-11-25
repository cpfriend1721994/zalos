#pragma once

#include <QObject>
#include <QPointF>

class QStyleHints;

namespace Zalos::Core::Platform {

class WheelBoost : public QObject {
    Q_OBJECT
public:
    static constexpr int kMinWheelScrollLines = 5;
    static constexpr qreal kTouchpadFactor = 5.0 / 3.0;

    explicit WheelBoost(QObject* parent = nullptr);

    static void applyWheelScrollLines(QStyleHints* hints);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointF m_remainder;
    bool m_forwarding = false;
};

}
