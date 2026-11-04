#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QString>
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
    bool isMine{false};
    bool isE2EE{false};
    QString reaction;
    QString messageType{"text"};
    QString mediaUrl;
    qint64 ts{0};
    QString senderId;
    QString senderAvatar;
    QString status;
    bool isRecalled{false};
    QVariantMap pollData;
    QString cliMsgId;
    QString quoteMsgId;
    QString quoteSender;
    QString quoteText;
    QString href;
    qint64 fileSize{0};
    QVector<MessageMention> mentions;
    QVariantMap extra;
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
        InMediaRunRole
    };
    static constexpr qint64 kMediaRunGapMs = 5 * 60 * 1000;

    explicit MessageModel(QObject* parent = nullptr);
    void refreshRunsAround(int row);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addMessage(const QString& text, bool isMine, const QString& sender = "Tôi");
    void addAttachmentMessage(const QString& type, const QString& text, const QString& url, bool isMine);
    void addMessageDirect(const QString& convId, const QString& id, const QString& senderName, const QString& text, const QString& timeStr, bool isMine, bool isE2EE, const QString& reaction = "", const QString& messageType = "text", const QString& mediaUrl = "");
    void mergeMessages(const QString& convId, const QVector<MessageItem>& items);
    void confirmMessage(const QString& convId, const QString& localId, const QString& serverId);
    void removeMessage(const QString& convId, const QString& msgId);
    void setReaction(const QString& convId, const QString& msgId, const QString& summary);
    void markRecalled(const QString& convId, const QString& msgId, const QString& placeholder);
    void setMedia(const QString& convId, const QString& msgId, const QString& mediaUrl);
    void setSticker(const QString& convId, const QString& msgId, const QVariantMap& info);
    void setStatus(const QString& convId, const QString& msgId, const QString& status);
    void markSeenUpTo(const QString& convId, const QString& msgId);
    bool contains(const QString& convId, const QString& msgId) const;
    int threadSize(const QString& convId) const { return m_store.value(convId).size(); }
    QString oldestMessageId(const QString& convId) const;
    qint64 oldestTimestamp(const QString& convId) const;
    QString latestMessageId(const QString& convId) const;
    MessageItem message(const QString& convId, const QString& msgId) const;
    int countIncomingAfter(const QString& convId, const QString& readPoint) const;
    void clearThread(const QString& convId);
    void removeMessagesUpTo(const QString& convId, const QString& lastMsgId);
    bool hasLocalEcho(const QString& convId) const;
    void loadConversation(const QString& conversationId);
    void setUnreadMarker(const QString& convId, int unreadCount);
    void clearUnreadMarker();
    Q_INVOKABLE int unreadMarkerRow() const;
    void clearAll();
    QString currentConversationId() const { return m_currentConvId; }

    Q_INVOKABLE QVariantMap getMessageById(const QString& msgId) const;
    Q_INVOKABLE int rowOfMessage(const QString& msgId) const;
    Q_INVOKABLE QVariantList mediaItems() const;
    static QString linkify(const QString& text);
    static QString linkify(const QString& text, const QVector<MessageMention>& mentions);
    static QString styledHtml(const QString& text, const QVector<MessageMention>& mentions, const QVariantList& styles);
    void setPollData(qint64 pollId, const QVariantMap& data);
    Q_INVOKABLE QVariantList sharedItems(const QString& kind, int limit = 60) const;

    Q_INVOKABLE QList<int> searchMessages(const QString& query) const;
    Q_INVOKABLE void updatePollVote(const QString& convId, qint64 pollId, int optionId, bool voted);

signals:
    void incomingAppended(int count);
    void unreadMarkerChanged(int row);

private:
    void placeUnreadMarker();
    bool isRunTile(int row) const;
    bool joinsPrevious(int row) const;

    QVector<MessageItem> m_items;
    QHash<QString, QVector<MessageItem>> m_store;
    QString m_currentConvId;
    QString m_unreadMarkerId;
    int m_pendingUnread{0};
};

}
