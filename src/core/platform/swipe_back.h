#pragma once

#include <QObject>

namespace Zalos::Core::Platform {

class SwipeBack : public QObject {
    Q_OBJECT

public:
    static constexpr qreal kTriggerPixels = 200.0;
    static constexpr qreal kHorizontalRatio = 3.0;

    explicit SwipeBack(QObject* parent = nullptr);

signals:
    void swipeBack();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    qreal m_dx{0};
    qreal m_dy{0};
    bool m_fired{false};
};

}
