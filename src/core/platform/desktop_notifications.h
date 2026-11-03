#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Zalos::Core::Platform {

// Desktop notifications over org.freedesktop.Notifications (D-Bus), with actions: one popup per `key` (a newer
// one replaces it instead of stacking), clicks and buttons come back as actionInvoked(key, action), and an
// inline reply (when the server advertises the "inline-reply" capability, e.g. KDE Plasma) as replied(key, text).
// Without a notification server every call is a no-op.
class DesktopNotifications : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString title;
        QString body;
        QString icon = QStringLiteral("zalos");
        QString category;                       // e.g. "im.received", "call.incoming"
        QList<QPair<QString, QString>> actions; // (id, label); "default" is the click on the popup itself
        bool inlineReply = false;               // add a reply field when the server supports it
        QString replyPlaceholder;
        int urgency = 1;                        // 0 low, 1 normal, 2 critical
        int timeoutMs = -1;                     // -1 server default, 0 never expires
    };

    explicit DesktopNotifications(QObject* parent = nullptr);

    void show(const QString& key, const Request& request);
    void close(const QString& key);
    void closeAll();
    bool supportsInlineReply() const { return m_capabilities.contains(QStringLiteral("inline-reply")); }

    // Builds the D-Bus `actions` argument (id, label pairs; inline reply only when supported).
    QStringList actionList(const Request& request) const;

#ifdef ZALOS_TESTING
    void setCapabilitiesForTesting(const QStringList& caps) { m_capabilities = caps; }
    void registerForTesting(const QString& key, uint id) { remember(key, id); }
    void invokeForTesting(uint id, const QString& action) { onActionInvoked(id, action); }
    void replyForTesting(uint id, const QString& text) { onReplied(id, text); }
    void tokenForTesting(uint id, const QString& token) { onActivationToken(id, token); }
    void closedForTesting(uint id) { onClosed(id, 2); }
    uint idForTesting(const QString& key) const { return m_ids.value(key, 0); }
#endif

signals:
    // `activationToken` (xdg-activation, Wayland) lets the window take focus; may be empty.
    void actionInvoked(const QString& key, const QString& action, const QString& activationToken);
    void replied(const QString& key, const QString& text);

private slots:
    void onActionInvoked(uint id, const QString& action);
    void onReplied(uint id, const QString& text);
    void onActivationToken(uint id, const QString& token);
    void onClosed(uint id, uint reason);

private:
    void remember(const QString& key, uint id);
    void forget(uint id);

    QStringList m_capabilities;
    QHash<QString, uint> m_ids;   // key -> notification id
    QHash<uint, QString> m_keys;  // notification id -> key
    QHash<uint, QString> m_tokens; // activation token received just before ActionInvoked
};

} // namespace Zalos::Core::Platform
