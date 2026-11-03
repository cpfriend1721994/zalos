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
    QString category{"priority"}; // "priority" or "other"
    QString senderPrefix{""};
    bool isMuted{false};
    QString tag{""};        // e.g. "Khách hàng", "Công việc"
    QString tagColor{""};   // e.g. "#E11D48", "#2563EB", "#16A34A"
    int labelId{0};         // Zalo label ("Phân loại") id, 0 = none
    int memberCount{0};
    bool isGroup{false};
    qint64 lastTs{0};       // Epoch milliseconds of the latest message, 0 when unknown.
    bool isTyping{false};
};

class ConversationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS // typed for the QML compiler (reached through ZaloClient properties)

    Q_PROPERTY(QString activeCategory READ activeCategory WRITE setCategoryFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchFilter NOTIFY filterChanged FINAL)
    // Only conversations carrying this label (0 = all), combined with the category / search filters.
    Q_PROPERTY(int labelFilter READ labelFilter WRITE setLabelFilter NOTIFY filterChanged FINAL)
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
        DraftRole,          // unsent composer text of the thread ("" when none)
        MemberAvatarsRole   // groups without a photo: up to 4 members [{avatar, name}] for the 2×2 composite
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
    // Replaces every conversation's label: convId -> {id, text, color}; ids not in the map lose theirs.
    void applyLabels(const QHash<QString, QVariantMap>& labels);
    Q_INVOKABLE int totalUnreadCount() const;

    void addConversation(const ConversationItem& item);
    // Inserts or replaces by id while keeping pinned-first, newest-first order.
    void upsertConversation(const ConversationItem& item);
    void setConversations(const QVector<ConversationItem>& items);
    bool contains(const QString& convId) const;
    ConversationItem conversation(const QString& convId) const;
    // Shown order (current filter): the id `delta` rows away from `convId` (first row when not shown), and the
    // pinned ids top to bottom. Used by the chat.next/previous/pinnedN shortcuts.
    QString adjacentId(const QString& convId, int delta) const;
    QStringList pinnedIds() const;
    void updateProfile(const QString& convId, const QString& name, const QString& avatarUrl, int memberCount = -1);
    void recordMessage(const QString& convId, const QString& preview, const QString& senderPrefix, qint64 ts, bool incrementUnread);
    void setPinned(const QString& convId, bool pinned);
    void setMuted(const QString& convId, bool muted);
    void setTag(const QString& convId, const QString& tag, const QString& color);
    void setOnlineIds(const QSet<QString>& ids);
    void setTyping(const QString& convId, bool typing);
    // Kept apart from the items: setConversations() rebuilds them, drafts must survive that.
    void setDraft(const QString& convId, const QString& text);
    QString draft(const QString& convId) const { return m_drafts.value(convId); }
    void clearDrafts();
    void setMemberAvatars(const QString& convId, const QVariantList& members);
    QVariantList memberAvatars(const QString& convId) const { return m_memberAvatars.value(convId); }
    void updateLastMessage(const QString& convId, const QString& message, const QString& timeStr);
    void clearUnread(const QString& convId);
    // Sets the badge directly (read points from other devices, manual "mark as unread").
    void setUnreadCount(const QString& convId, int count);
    void removeConversation(const QString& convId);
    // Bulk updates (history import, socket pages): filtering/sorting and the view reset run once at
    // endBatch() instead of once per message. Nested calls are allowed.
    void beginBatch();
    void endBatch();
    void clearAll();

signals:
    void unreadCountChanged(int totalUnread);
    void filterChanged();

private:
    void applyFilter();
    void emitUnreadChanged();
    int m_batchDepth{0};
    bool m_filterDirty{false};
    bool m_unreadDirty{false};

    QVector<ConversationItem> m_allItems;
    QHash<QString, QString> m_drafts;
    QHash<QString, QVariantList> m_memberAvatars; // group id -> composite members
    QVector<ConversationItem> m_filteredItems;
    QString m_activeCategory{"priority"};
    QString m_searchQuery;
    int m_labelFilter{0};
    void stampLabel(ConversationItem& item) const;
    QHash<QString, QVariantMap> m_labels; // kept for conversations added later
};

} // namespace Zalos::Core::Models

