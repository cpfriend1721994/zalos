#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QPair>
#include <functional>
#include <QVector>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <memory>

#include "models/conversation_model.h"
#include "models/message_model.h"

namespace Zalos::Core::Offline {

class ZaloOfflineStore : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(bool isOfflineActive READ isOfflineActive NOTIFY offlineStateChanged FINAL)
    Q_PROPERTY(QString syncState READ syncState NOTIFY syncStateChanged FINAL)

public:
    explicit ZaloOfflineStore(QObject* parent = nullptr);
    ~ZaloOfflineStore();

    bool isOfflineActive() const { return m_isOfflineActive; }
    QString syncState() const { return m_syncState; }

    static QByteArray deriveSessionKey(const QString& zpwSekCookie);

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
        bool isOfficial{false};
    };

    bool initializeDatabase(const QString& customPath = QString());
    void migrateStorage();
    bool isDatabaseOpen() const { return m_db.isOpen(); }
    QString connectionNameForTesting() const { return m_db.connectionName(); }
    QString ownerUid() const;
    bool bindOwner(const QString& uid);
    void clearCache();
    bool saveConversation(const Models::ConversationItem& item);
    QVector<Models::ConversationItem> loadConversations();
    bool saveMessage(const QString& convId, const Models::MessageItem& item, const QJsonObject& raw = {},
                     bool isGroup = false);
    bool recordGroupSeen(const QString& msgId, const QString& userId);
    QVector<Models::MessageItem> loadMessages(const QString& convId, QHash<QString, QJsonObject>* raws = nullptr,
                                              int limit = 2000, qint64 beforeTs = 0);
    QJsonObject rawMessage(const QString& msgId) const;

    struct SearchHit {
        QString msgId;
        QString convId;
        QString senderName;
        QString text;
        qint64 ts{0};
    };
    QVector<SearchHit> searchMessages(const QString& query, const QString& convId = QString(), int limit = 200) const;
    static QString ftsQuery(const QString& input);
    bool ftsAvailable() const { return m_ftsAvailable; }
#ifdef ZALOS_TESTING
    void setFtsAvailableForTesting(bool available) { m_ftsAvailable = available; }
#endif
    qint64 messageTs(const QString& msgId, QString* convId = nullptr) const;
    int countMessagesBetween(const QString& convId, qint64 fromTs, qint64 toTs) const;
    static QByteArray encodeRaw(const QJsonObject& raw);
    static QJsonObject decodeRaw(const QVariant& stored);
    void maintenance();
    bool markRecalled(const QString& msgId, const QString& placeholder);
    bool deleteMessage(const QString& msgId);
    static qint64 expiryOf(const QJsonObject& raw, qint64 ts);
    QVector<QPair<QString, QString>> purgeExpired(qint64 now);
    qint64 nextExpiry(qint64 now) const;
    QString metaValue(const QString& key) const;
    void setMetaValue(const QString& key, const QString& value);
    QSet<QString> reparseStored(const std::function<bool(const QJsonObject& raw, bool isGroup, Models::MessageItem* out)>& fn);
    bool deleteMessagesUpTo(const QString& convId, const QString& lastMsgId);
    bool saveProfile(const CachedProfile& profile);
    QString pragmaForTesting(const QString& name) const;
    struct ReactionEvent {
        QString uid;
        QString icon;
        QString eventId;
        qint64 ts{0};
    };
    bool addReactionEvent(const QString& msgId, const QString& uid, const QString& icon, const QString& eventId, qint64 ts);
    bool removeReactionEvent(const QString& msgId, const QString& eventId);
    bool clearReactions(const QString& msgId, const QString& uid);
    QVector<ReactionEvent> reactionEventsOf(const QString& msgId) const;
    QSet<QString> reactedMessageIds() const;
    bool setMessageReaction(const QString& msgId, const QString& summary);
    QVector<CachedProfile> loadProfiles();
    bool beginBatch();
    void endBatch();
    bool deleteConversation(const QString& convId);

    Q_INVOKABLE void setOfflineActive(bool active);
    Q_INVOKABLE void startSyncRecovery();

signals:
    void offlineStateChanged(bool active);
    void syncStateChanged(const QString& state);
    void recoveryCompleted(bool success);

private:
    bool m_isOfflineActive{false};
    QString m_syncState{"CONNECTED"};
    QSqlDatabase m_db;
    std::unique_ptr<QSqlQuery> m_saveMessageQuery;
    bool m_ftsAvailable{false};
    void indexMessage(const QString& msgId, const Models::MessageItem& item);
};

}
