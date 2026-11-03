#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QVector>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <memory>

#include "models/conversation_model.h"
#include "models/message_model.h"

namespace Zalos::Core::Offline {

class ZaloOfflineStore : public QObject {
    Q_OBJECT
    QML_ANONYMOUS // typed for the QML compiler (reached through ZaloClient properties)

    Q_PROPERTY(bool isOfflineActive READ isOfflineActive NOTIFY offlineStateChanged FINAL)
    Q_PROPERTY(QString syncState READ syncState NOTIFY syncStateChanged FINAL)

public:
    explicit ZaloOfflineStore(QObject* parent = nullptr);
    ~ZaloOfflineStore();

    bool isOfflineActive() const { return m_isOfflineActive; }
    QString syncState() const { return m_syncState; }

    // Replaces plugins/offline/store.js sessionKey: SHA-256 of zpw_sek cookie
    static QByteArray deriveSessionKey(const QString& zpwSekCookie);

    // Replaces plugins/offline/store.js AES-256-GCM read & write
    static bool writeEncryptedSession(const QString& directory, const QString& zpwSekCookie, const QJsonObject& record);
    static QJsonObject readEncryptedSession(const QString& directory, const QString& zpwSekCookie);
    static void clearEncryptedSession(const QString& directory);

    struct CachedProfile {
        QString id;
        QString name;
        QString avatar;
        bool isGroup{false};
        int memberCount{0};
        bool isFriend{false};
    };

    // SQLite cache of the signed-in account (offline.db, 0600): conversations, messages with their raw
    // TMessage, and profiles. Everything here was received from Zalo; nothing is generated.
    bool initializeDatabase(const QString& customPath = QString());
    bool isDatabaseOpen() const { return m_db.isOpen(); }
    QString connectionNameForTesting() const { return m_db.connectionName(); }
    // The account the cache belongs to. Binding another uid wipes the cache first; returns true if it did.
    QString ownerUid() const;
    bool bindOwner(const QString& uid);
    void clearCache();
    bool saveConversation(const Models::ConversationItem& item);
    QVector<Models::ConversationItem> loadConversations();
    bool saveMessage(const QString& convId, const Models::MessageItem& item, const QJsonObject& raw = {},
                     bool isGroup = false);
    // Newest `limit` messages of a thread, oldest first; `raws` receives the stored TMessage per id.
    QVector<Models::MessageItem> loadMessages(const QString& convId, QHash<QString, QJsonObject>* raws = nullptr,
                                              int limit = 2000);
    // Recall of a message whose thread is not loaded in memory.
    bool markRecalled(const QString& msgId, const QString& placeholder);
    // Deletions made on another device (chat.delete): one message, or a thread up to a server id.
    bool deleteMessage(const QString& msgId);
    bool deleteMessagesUpTo(const QString& convId, const QString& lastMsgId);
    bool saveProfile(const CachedProfile& profile);
    // Value of a PRAGMA on the open database (tests: journal_mode, synchronous).
    QString pragmaForTesting(const QString& name) const;
    // Reaction events (one per tap; event_id dedupes replays) and the message's summary column, also for messages
    // that are only on disk. addReactionEvent returns false when the event was already stored.
    struct ReactionEvent {
        QString uid;
        QString icon;
        QString eventId;
        qint64 ts{0};
    };
    bool addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId, qint64 ts);
    bool removeReactionEvent(const QString& msgId, const QString& eventId);
    bool clearReactions(const QString& msgId, const QString& uid); // a removal: that person's reactions
    QVector<ReactionEvent> reactionEventsOf(const QString& msgId) const;
    QSet<QString> reactedMessageIds() const;
    bool setMessageReaction(const QString& msgId, const QString& summary);
    QVector<CachedProfile> loadProfiles();
    // Groups many writes into one transaction.
    bool beginBatch();
    void endBatch();
    // Drops a conversation and its cached messages (after "delete conversation" on the server).
    bool deleteConversation(const QString& convId);

    // Connection state mirror, driven by ZaloClient from the real realtime socket.
    Q_INVOKABLE void setOfflineActive(bool active);
    Q_INVOKABLE void startSyncRecovery();

signals:
    void offlineStateChanged(bool active);
    void syncStateChanged(const QString& state);
    void recoveryCompleted(bool success);

private:
    bool m_isOfflineActive{false};
    QString m_syncState{"CONNECTED"}; // DISCONNECTED, CHECKING, CONNECTED
    QSqlDatabase m_db;
    std::unique_ptr<QSqlQuery> m_saveMessageQuery; // prepared once; history imports insert tens of thousands
};

} // namespace Zalos::Core::Offline

