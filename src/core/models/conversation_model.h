#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QString>
#include <QSet>
#include <QVector>
#include <QHash>
#include <QVariantMap>
#include <QDateTime>

namespace Zalos::Core::Models {

struct ConversationItem {
    QString id;
    QString name;
    QString lastMessage;
    QString timestampStr;
    int unreadCount{0};
    bool isOnline{false};
    bool isPinned{false};
    QString avatarUrl;
    QString category{"priority"};
    QString senderPrefix{""};
    bool isMuted{false};
    QString tag{""};
    QString tagColor{""};
    int labelId{0};
    int memberCount{0};
    bool isGroup{false};
    qint64 lastTs{0};
    bool isTyping{false};
    bool hasUnreadMention{false};
};

class ConversationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString activeCategory READ activeCategory WRITE setCategoryFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(int labelFilter READ labelFilter WRITE setLabelFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY filterChanged FINAL)
    Q_PROPERTY(int totalUnreadCount READ totalUnreadCount NOTIFY unreadCountChanged FINAL)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        LastMessageRole,
        TimestampRole,
        UnreadCountRole,
        IsOnlineRole,
        IsPinnedRole,
        IsMutedRole,
        AvatarUrlRole,
        CategoryRole,
        SenderPrefixRole,
        MemberCountRole,
        TagRole,
        TagColorRole,
        LabelIdRole,
        IsGroupRole,
        LastTsRole,
        IsTypingRole,
        DraftRole,
        MemberAvatarsRole,
        HasUnreadMentionRole
    };

    explicit ConversationModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeCategory() const { return m_activeCategory; }
    QString searchQuery() const { return m_searchQuery; }

    Q_INVOKABLE void setCategoryFilter(const QString& category);
    Q_INVOKABLE void setSearchFilter(const QString& query);
    int labelFilter() const { return m_labelFilter; }
    Q_INVOKABLE void setLabelFilter(int labelId);
    QString viewMode() const { return m_viewMode; }
    Q_INVOKABLE void setViewMode(const QString& mode);
    void setHiddenThreads(const QSet<QString>& ids);
    void setArchivedThreads(const QSet<QString>& ids);
    void applyLabels(const QHash<QString, QVariantMap>& labels);
    Q_INVOKABLE int totalUnreadCount() const;
    Q_INVOKABLE bool hasAnyUnreadMention() const;

    void addConversation(const ConversationItem& item);
    void upsertConversation(const ConversationItem& item);
    void setConversations(const QVector<ConversationItem>& items);
    bool contains(const QString& convId) const;
    ConversationItem conversation(const QString& convId) const;
    QString adjacentId(const QString& convId, int delta) const;
    QStringList pinnedIds() const;
    void updateProfile(const QString& convId, const QString& name, const QString& avatarUrl, int memberCount = -1);
    void recordMessage(const QString& convId, const QString& preview, const QString& senderPrefix, qint64 ts, bool incrementUnread, bool hasMention = false);
    void setPinned(const QString& convId, bool pinned);
    void setMuted(const QString& convId, bool muted);
    void setTag(const QString& convId, const QString& tag, const QString& color);
    void setOnlineIds(const QSet<QString>& ids);
    void setTyping(const QString& convId, bool typing);
    void setDraft(const QString& convId, const QString& text);
    QString draft(const QString& convId) const { return m_drafts.value(convId); }
    void clearDrafts();
    void setMemberAvatars(const QString& convId, const QVariantList& members);
    QVariantList memberAvatars(const QString& convId) const { return m_memberAvatars.value(convId); }
    void updateLastMessage(const QString& convId, const QString& message, const QString& timeStr);
    void clearUnread(const QString& convId);
    void setUnreadCount(const QString& convId, int count);
    void removeConversation(const QString& convId);
    void beginBatch();
    void endBatch();
    void clearAll();

signals:
    void unreadCountChanged(int totalUnread, bool hasMentions);
    void filterChanged();

private:
    void applyFilter();
    void emitUnreadChanged();
    int m_batchDepth{0};
    bool m_filterDirty{false};
    bool m_unreadDirty{false};

    QVector<ConversationItem> m_allItems;
    QHash<QString, QString> m_drafts;
    QHash<QString, QVariantList> m_memberAvatars;
    QVector<ConversationItem> m_filteredItems;
    QString m_activeCategory{"priority"};
    QString m_searchQuery;
    int m_labelFilter{0};
    QString m_viewMode{QStringLiteral("normal")};
    QSet<QString> m_hiddenIds;
    QSet<QString> m_archivedIds;
    void stampLabel(ConversationItem& item) const;
    QHash<QString, QVariantMap> m_labels;
};

}
