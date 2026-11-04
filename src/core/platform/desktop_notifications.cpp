#include "desktop_notifications.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

namespace Zalos::Core::Platform {

namespace {

constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kPath = "/org/freedesktop/Notifications";
constexpr auto kInterface = "org.freedesktop.Notifications";

}

DesktopNotifications::DesktopNotifications(QObject* parent) : QObject(parent) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    bus.connect(QString(), kPath, kInterface, "ActionInvoked", this, SLOT(onActionInvoked(uint, QString)));
    bus.connect(QString(), kPath, kInterface, "NotificationReplied", this, SLOT(onReplied(uint, QString)));
    bus.connect(QString(), kPath, kInterface, "ActivationToken", this, SLOT(onActivationToken(uint, QString)));
    bus.connect(QString(), kPath, kInterface, "NotificationClosed", this, SLOT(onClosed(uint, uint)));

    auto* watcher = new QDBusPendingCallWatcher(
        bus.asyncCall(QDBusMessage::createMethodCall(kService, kPath, kInterface, "GetCapabilities")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        const QDBusPendingReply<QStringList> reply = *w;
        if (reply.isValid()) m_capabilities = reply.value();
        w->deleteLater();
    });
}

QStringList DesktopNotifications::actionList(const Request& request) const {
    QStringList actions;
    for (const auto& action : request.actions) actions << action.first << action.second;
    if (request.inlineReply && supportsInlineReply()) {
        actions << QStringLiteral("inline-reply") << request.replyPlaceholder;
    }
    return actions;
}

void DesktopNotifications::show(const QString& key, const Request& request) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kInterface, "Notify");
    QVariantMap hints;
    if (!request.category.isEmpty()) hints["category"] = request.category;
    hints["desktop-entry"] = QStringLiteral("zalos");
    hints["urgency"] = static_cast<uchar>(request.urgency);
    if (request.inlineReply && !request.replyPlaceholder.isEmpty()) {
        hints["x-kde-reply-placeholder-text"] = request.replyPlaceholder;
    }
    const uint replaces = m_ids.value(key, 0);
    msg << QStringLiteral("Zalos") << replaces << request.icon << request.title << request.body << actionList(request)
        << hints << request.timeoutMs;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher* w) {
        const QDBusPendingReply<uint> reply = *w;
        if (reply.isValid()) remember(key, reply.value());
        w->deleteLater();
    });
}

void DesktopNotifications::close(const QString& key) {
    const uint id = m_ids.value(key, 0);
    if (id == 0) return;
    forget(id);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kInterface, "CloseNotification");
    msg << id;
    bus.asyncCall(msg);
}

void DesktopNotifications::closeAll() {
    const QStringList keys = m_ids.keys();
    for (const QString& key : keys) close(key);
}

void DesktopNotifications::remember(const QString& key, uint id) {
    const uint old = m_ids.value(key, 0);
    if (old != 0 && old != id) m_keys.remove(old);
    m_ids.insert(key, id);
    m_keys.insert(id, key);
}

void DesktopNotifications::forget(uint id) {
    const QString key = m_keys.take(id);
    if (!key.isEmpty() && m_ids.value(key) == id) m_ids.remove(key);
    m_tokens.remove(id);
}

void DesktopNotifications::onActionInvoked(uint id, const QString& action) {
    const QString key = m_keys.value(id);
    if (key.isEmpty() || action == QLatin1String("inline-reply")) return;
    const QString token = m_tokens.take(id);
    emit actionInvoked(key, action, token);
}

void DesktopNotifications::onReplied(uint id, const QString& text) {
    const QString key = m_keys.value(id);
    if (key.isEmpty() || text.trimmed().isEmpty()) return;
    emit replied(key, text);
}

void DesktopNotifications::onActivationToken(uint id, const QString& token) {
    if (m_keys.contains(id)) m_tokens.insert(id, token);
}

void DesktopNotifications::onClosed(uint id, uint reason) {
    Q_UNUSED(reason);
    forget(id);
}

}
