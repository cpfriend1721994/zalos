#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Zalos::Core::Platform {

class DesktopNotifications : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString title;
        QString body;
        QString icon = QStringLiteral("zalos");
        QString category;
        QList<QPair<QString, QString>> actions;
        bool inlineReply = false;
        QString replyPlaceholder;
        int urgency = 1;
        int timeoutMs = -1;
    };

    explicit DesktopNotifications(QObject* parent = nullptr);

    void show(const QString& key, const Request& request);
    void close(const QString& key);
    void closeAll();
    bool supportsInlineReply() const { return m_capabilities.contains(QStringLiteral("inline-reply")); }

    QStringList actionList(const Request& request) const;

#ifdef ZALOS_TESTING
    void setCapabilitiesForTesting(const QStringList& caps) { m_capabilities = caps; }
    void registerForTesting(const QString& key, uint id) { remember(key, id); }
    void invokeForTesting(uint id, const QString& action) { onActionInvoked(id, action); }
    void replyForTesting(uint id, const QString& text) { onReplied(id, text); }
    void tokenForTesting(uint id, const QString& token) { onActivationToken(id, token); }
    void closedForTesting(uint id) { onClosed(id, 2); }
    uint idForTesting(const QString& key) const { return m_ids.value(key, 0); }
    int shownForTesting(const QString& key) const { return m_shownKeys.count(key); }
#endif

signals:
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
    QHash<QString, uint> m_ids;
    QHash<uint, QString> m_keys;
    QHash<uint, QString> m_tokens;
    QStringList m_shownKeys;
};

}
