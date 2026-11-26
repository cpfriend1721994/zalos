#include "bound_loader.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QScopedValueRollback>

namespace Zalos::Core::Platform {

BoundLoader::BoundLoader(QQuickItem* parent) : QQuickItem(parent) {}

BoundLoader::~BoundLoader() {
    m_destroying = true;
    clear();
}

void BoundLoader::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    emit activeChanged();
    if (!isComponentComplete()) return;
    if (m_active) {
        if (!m_item && m_sourceComponent) load();
    } else {
        clear();
    }
}

void BoundLoader::setSourceComponent(QQmlComponent* component) {
    if (m_sourceComponent == component) return;
    clear();
    m_sourceComponent = component;
    emit sourceComponentChanged();
    if (isComponentComplete() && m_active && m_sourceComponent) load();
}

void BoundLoader::componentComplete() {
    QQuickItem::componentComplete();
    if (m_active && m_sourceComponent && !m_item) load();
}

void BoundLoader::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    syncSize();
}

void BoundLoader::syncSize() {
    if (m_updatingSize || m_destroying || !m_item) return;
    QScopedValueRollback<bool> guard(m_updatingSize, true);
    const qreal iw = (m_item->implicitWidth() > 0.0)
                         ? m_item->implicitWidth()
                         : (m_itemHasOwnWidth ? m_item->width() : 0.0);
    const qreal ih = (m_item->implicitHeight() > 0.0)
                         ? m_item->implicitHeight()
                         : (m_itemHasOwnHeight ? m_item->height() : 0.0);
    setImplicitSize(iw, ih);
    if (widthValid() && !m_itemHasOwnWidth) m_item->setWidth(width());
    if (heightValid() && !m_itemHasOwnHeight) m_item->setHeight(height());
}

void BoundLoader::load() {
    if (m_loading || m_destroying || !m_active || !m_sourceComponent || m_item) return;
    QScopedValueRollback<bool> guard(m_loading, true);
    QQmlContext* ctx = m_sourceComponent->creationContext();
    if (!ctx) ctx = qmlContext(this);
    QObject* obj = m_sourceComponent->beginCreate(ctx);
    if (!obj) return;
    auto* child = qobject_cast<QQuickItem*>(obj);
    if (!child) {
        m_sourceComponent->completeCreate();
        obj->deleteLater();
        return;
    }
    child->setParent(this);
    child->setParentItem(this);
    if (widthValid()) child->setWidth(width());
    if (heightValid()) child->setHeight(height());
    m_item = child;
    m_sourceComponent->completeCreate();
    if (m_item != child) return;
    m_itemHasOwnWidth = (!widthValid() && child->implicitWidth() <= 0.0 && child->width() > 0.0);
    m_itemHasOwnHeight = (!heightValid() && child->implicitHeight() <= 0.0 && child->height() > 0.0);
    connect(child, &QQuickItem::implicitWidthChanged, this, &BoundLoader::syncSize);
    connect(child, &QQuickItem::implicitHeightChanged, this, &BoundLoader::syncSize);
    connect(child, &QQuickItem::widthChanged, this, &BoundLoader::syncSize);
    connect(child, &QQuickItem::heightChanged, this, &BoundLoader::syncSize);
    syncSize();
    emit itemChanged();
    emit loaded();
}

void BoundLoader::clear() {
    QQuickItem* old = m_item;
    m_item = nullptr;
    m_itemHasOwnWidth = false;
    m_itemHasOwnHeight = false;
    if (old) {
        disconnect(old, nullptr, this, nullptr);
        old->setParentItem(nullptr);
        old->deleteLater();
    }
    if (!m_destroying) {
        setImplicitSize(0.0, 0.0);
        if (old) emit itemChanged();
    }
}

}
