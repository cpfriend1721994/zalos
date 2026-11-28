#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QCache>
#include <QPair>
#include <functional>
#include <QString>
#include <QStringView>
#include <QVector>
#include <QHash>

namespace Zalos::Core::Models {

struct MessageMention {
    QString uid;
    int pos{0};
    int len{0};
};

struct MessageItem {
    QString id;
    QString senderName;
    QString text;
    QString timeStr;
    qint64 expireOn{0};
    QString reaction;
    QString messageType{QStringLiteral("text")};
    QString mediaUrl;
    qint64 ts{0};
    QString senderId;
    QString senderAvatar;
    QString status;
    QVariantMap pollData;
    QString cliMsgId;
    QString quoteMsgId;
    QString quoteSender;
    QString quoteText;
    QString href;
    qint64 fileSize{0};
    QVector<MessageMention> mentions;
    QVariantMap extra;
    QStringList seenUids;
    mutable qint64 dayCache{-1};
    bool isMine{false};
    bool isE2EE{false};
    bool isForwarded{false};
    bool isRecalled{false};
    bool pollCard{true};
    mutable qint16 jumboCache{-1};
};

class MessageModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        SenderNameRole,
        TextRole,
        TimeRole,
        IsMineRole,
        IsE2EERole,
        ReactionRole,
        MessageTypeRole,
        MediaUrlRole,
        TimestampRole,
        SenderIdRole,
        SenderAvatarRole,
        StatusRole,
        IsRecalledRole,
        PollDataRole,
        PollCardRole,
        QuoteMsgIdRole,
        QuoteSenderRole,
        QuoteTextRole,
        HrefRole,
        FileSizeRole,
        FirstInGroupRole,
        LastInGroupRole,
        DateSeparatorRole,
        ShowStatusRole,
        RichTextRole,
        ExtraRole,
        UnreadMarkerRole,
        MediaRunRole,
        InMediaRunRole,
        SeenUidsRole,
        JumboEmojiSizeRole,
        CanReactRole,
        CanReplyRole,
        CanForwardRole,
        CanCopyRole,
        HasMenuRole
    };
    enum Action {
        ActionReact = 0x1,
        ActionReply = 0x2,
        ActionForward = 0x4,
        ActionCopy = 0x8,
        ActionMenu = 0x10
    };
    static int actionsFor(const MessageItem& item);
    static bool isServerId(const QString& msgId);
    static constexpr qint64 kMediaRunGapMs = 5 * 60 * 1000;

    explicit MessageModel(QObject* parent = nullptr);
    using SenderResolver = std::function<QPair<QString, QString>(const QString& uid)>;
    void setSenderResolver(SenderResolver resolver) { m_senderResolver = std::move(resolver); }
    void refreshSenders();
    void refreshRunsAround(int row);
    void resetRunStarts(int fromRow);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void mergeMessages(const QString& convId, const QVector<MessageItem>& items);
    void confirmMessage(const QString& convId, const QString& localId, const QString& serverId);
    void removeMessage(const QString& convId, const QString& msgId);
    void setReaction(const QString& convId, const QString& msgId, const QString& summary);
    void markRecalled(const QString& convId, const QString& msgId, const QString& placeholder);
    void setMedia(const QString& convId, const QString& msgId, const QString& mediaUrl);
    void setSticker(const QString& convId, const QString& msgId, const QVariantMap& info);
    void setMessageExtra(const QString& convId, const QString& msgId, const QString& text, const QVariantMap& extra);
    void setStatus(const QString& convId, const QString& msgId, const QString& status);
    void setSender(const QString& convId, const QString& msgId, const QString& senderId);
    void markSeenUpTo(const QString& convId, const QString& msgId);
    void recordGroupSeen(const QString& convId, const QString& msgId, const QString& userId);
    void markDeliveredUpTo(const QString& convId, const QString& msgId);
    bool contains(const QString& convId, const QString& msgId) const;
    int threadSize(const QString& convId) const { return int(threadOf(convId).size()); }
    bool hasAnyMessages() const {
        if (!m_items.isEmpty()) return true;
        for (const auto& list : m_store) {
            if (!list.isEmpty()) return true;
        }
        return false;
    }
    bool isActiveThreadDuplicatedForTesting() const { return !m_currentConvId.isEmpty() && m_store.contains(m_currentConvId); }
    QString oldestMessageId(const QString& convId) const;
    qint64 oldestTimestamp(const QString& convId) const;
    QString latestMessageId(const QString& convId) const;
    MessageItem message(const QString& convId, const QString& msgId) const;
    const QVector<MessageItem>& threadOf(const QString& convId) const;
    int countIncomingAfter(const QString& convId, const QString& readPoint) const;
    void clearThread(const QString& convId);
    void trimThreadWindow(const QString& convId, int maxItems);
    void removeMessagesUpTo(const QString& convId, const QString& lastMsgId);
    bool hasLocalEcho(const QString& convId) const;
    void loadConversation(const QString& conversationId);
    void setUnreadMarker(const QString& convId, int unreadCount);
    void clearUnreadMarker();
    Q_INVOKABLE int unreadMarkerRow() const;
    Q_INVOKABLE QString messageIdAt(int row) const;
    void clearAll();
    QString currentConversationId() const { return m_currentConvId; }

    Q_INVOKABLE QVariantMap getMessageById(const QString& msgId) const;
    Q_INVOKABLE int rowOfMessage(const QString& msgId) const;
    Q_INVOKABLE QVariantList mediaItems() const;
    struct LinkSpan {
        int start = 0;
        int length = 0;
        QString href;
    };
    static QVector<LinkSpan> findLinks(const QString& text);
    static QString linkifyPlain(const QString& text);
    static QString linkify(const QString& text);
    static QString linkify(const QString& text, const QVector<MessageMention>& mentions);
    static QString styledHtml(const QString& text, const QVector<MessageMention>& mentions, const QVariantList& styles);
    static int jumboEmojiSizeFor(const QString& text);
    static QString internMessageType(QStringView type);
    static QString internStatus(QStringView status);
    void setPollData(qint64 pollId, const QVariantMap& data, const QString& convId = QString());
    void refreshPollCards(const QString& convId);
    Q_INVOKABLE QVariantList sharedItems(const QString& kind, int limit = 60) const;

    Q_INVOKABLE QList<int> searchMessages(const QString& query) const;

signals:
    void incomingAppended(int count);
    void unreadMarkerChanged(int row);

private:
    QVector<MessageItem>& threadFor(const QString& convId);
    void recomputeLastMineRow();
    void rebuildActiveIndex();
    int activeRowOf(const QString& msgId) const;
    void markOwnUpTo(const QString& convId, const QString& msgId, const QString& status);
    void placeUnreadMarker();
    bool isRunTile(int row) const;
    bool runPairsWithPrevious(int row) const;
    bool joinsPrevious(int row) const;
    int runStartOf(int row) const;
    static bool drawsBubble(const MessageItem& item);

    QVector<MessageItem> m_items;
    mutable QVector<int> m_runStarts;
    QHash<QString, int> m_activeRowById;
    QHash<QString, int> m_activeRowByCliId;
    SenderResolver m_senderResolver;
    QHash<QString, QVector<MessageItem>> m_store;
    QString m_currentConvId;
    QString m_unreadMarkerId;
    int m_pendingUnread{0};
    int m_lastMineRow{-1};
    quint64 m_richTextRev{0};
    mutable QCache<QString, QString> m_richTextLru{64};
};

}
