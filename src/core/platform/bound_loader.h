#pragma once

#include <QPointer>
#include <QQmlComponent>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace Zalos::Core::Platform {

class BoundLoader : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(ZBoundLoader)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    Q_PROPERTY(QQmlComponent* sourceComponent READ sourceComponent WRITE setSourceComponent NOTIFY sourceComponentChanged FINAL)
    Q_PROPERTY(QQuickItem* item READ item NOTIFY itemChanged FINAL)

public:
    explicit BoundLoader(QQuickItem* parent = nullptr);
    ~BoundLoader() override;

    bool active() const { return m_active; }
    void setActive(bool active);

    QQmlComponent* sourceComponent() const { return m_sourceComponent.data(); }
    void setSourceComponent(QQmlComponent* component);

    QQuickItem* item() const { return m_item; }

signals:
    void activeChanged();
    void sourceComponentChanged();
    void itemChanged();
    void loaded();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private slots:
    void syncSize();

private:
    void load();
    void clear();

    bool m_active{true};
    bool m_loading{false};
    bool m_updatingSize{false};
    bool m_destroying{false};
    bool m_itemHasOwnWidth{false};
    bool m_itemHasOwnHeight{false};
    QPointer<QQmlComponent> m_sourceComponent;
    QQuickItem* m_item{nullptr};
};

}
