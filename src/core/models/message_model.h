#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QHash>

namespace Zalos::Core::Models {

struct MessageMention {
    QString uid; // "-1" = @All
    int pos{0};  // UTF-16 offset into MessageItem::text
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
    QString messageType{"text"}; // "text", "image", "table", "file"
    QString mediaUrl;
    qint64 ts{0};           // Epoch milliseconds.
    QString senderId;
    QString senderAvatar;
    QString status;         // Own messages: "sending", "sent", "seen"; empty for incoming.
    bool isRecalled{false};
    QVariantMap pollData;
    QString cliMsgId;       // Client id; bridges a local echo to the server copy of the same message.
    QString quoteMsgId;     // Replied-to message (quote reply), empty otherwise.
    QString quoteSender;
    QString quoteText;
    QString href;           // original media / file / video link (mediaUrl is the thumbnail)
    qint64 fileSize{0};     // bytes, files only
    QVector<MessageMention> mentions; // @mentions inside text (group messages)
    // Per-type details: link {desc, domain}, voice {durationMs}, call {video, missed, outgoing, seconds}.
    QVariantMap extra;
};

class MessageModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS // typed for the QML compiler (reached through ZaloClient properties)

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
        // Presentation helpers computed from neighbours (Zalo groups consecutive messages):
        FirstInGroupRole,   // starts a run by the same sender within 30 minutes (show avatar/name)
        LastInGroupRole,    // ends that run (show time)
        DateSeparatorRole,  // first message of a calendar day (show the date pill)
        ShowStatusRole,     // newest own message: the only one with Đang gửi/Đã gửi/Đã xem
        RichTextRole,       // HTML-escaped text with clickable links
        ExtraRole,          // MessageItem::extra
        UnreadMarkerRole,   // first unread incoming message when the thread was opened ("Tin nhắn chưa đọc" bar)
        // Consecutive caption-less photos / stickers of one sender (<= kMediaRunGapMs apart) render side by side:
        // the first row of such a run carries all tiles [{msgId, messageType, mediaUrl, href, animatedUrl}],
        // the following rows are folded into it (InMediaRunRole: zero height in the view).
        MediaRunRole,
        InMediaRunRole
    };
    static constexpr qint64 kMediaRunGapMs = 5 * 60 * 1000;

    explicit MessageModel(QObject* parent = nullptr);
    // Media runs depend on neighbours: re-evaluates the rows around a changed one.
    void refreshRunsAround(int row);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addMessage(const QString& text, bool isMine, const QString& sender = "Tôi");
    void addAttachmentMessage(const QString& type, const QString& text, const QString& url, bool isMine);
    void addMessageDirect(const QString& convId, const QString& id, const QString& senderName, const QString& text, const QString& timeStr, bool isMine, bool isE2EE, const QString& reaction = "", const QString& messageType = "text", const QString& mediaUrl = "");
    // Merges messages into a thread (deduplicated by id, sorted by ts).
    void mergeMessages(const QString& convId, const QVector<MessageItem>& items);
    // Replaces a locally echoed message id with the server id once delivery is confirmed.
    void confirmMessage(const QString& convId, const QString& localId, const QString& serverId);
    void removeMessage(const QString& convId, const QString& msgId);
    void setReaction(const QString& convId, const QString& msgId, const QString& summary);
    void markRecalled(const QString& convId, const QString& msgId, const QString& placeholder);
    void setMedia(const QString& convId, const QString& msgId, const QString& mediaUrl);
    // Sticker render info resolved later (StickerStore::describe): still url + extra.sticker.
    void setSticker(const QString& convId, const QString& msgId, const QVariantMap& info);
    void setStatus(const QString& convId, const QString& msgId, const QString& status);
    // Marks own messages up to (and including) msgId as seen.
    void markSeenUpTo(const QString& convId, const QString& msgId);
    bool contains(const QString& convId, const QString& msgId) const;
    int threadSize(const QString& convId) const { return m_store.value(convId).size(); }
    QString oldestMessageId(const QString& convId) const;
    QString latestMessageId(const QString& convId) const;
    // Stored copy of one message (empty item when unknown).
    MessageItem message(const QString& convId, const QString& msgId) const;
    // Incoming messages with a numeric id above `readPoint` (unread after another device's read point).
    int countIncomingAfter(const QString& convId, const QString& readPoint) const;
    void clearThread(const QString& convId);
    // Removes server messages with a numeric id <= `lastMsgId` (conversation cleared on another device).
    void removeMessagesUpTo(const QString& convId, const QString& lastMsgId);
    // True while a local echo of an unsent message exists (such a thread must stay in memory).
    bool hasLocalEcho(const QString& convId) const;
    void loadConversation(const QString& conversationId);
    // Marks the first of the newest `unreadCount` incoming messages of `convId` (the open thread). History that
    // arrives later recomputes it until it can be placed. 0 clears it.
    void setUnreadMarker(const QString& convId, int unreadCount);
    void clearUnreadMarker();
    Q_INVOKABLE int unreadMarkerRow() const;
    void clearAll();
    QString currentConversationId() const { return m_currentConvId; }

    Q_INVOKABLE QVariantMap getMessageById(const QString& msgId) const;
    // Row of a loaded message (global id or client id), -1 when not loaded.
    Q_INVOKABLE int rowOfMessage(const QString& msgId) const;
    // Photos of the shown thread for the image viewer: [{msgId, url, thumb, senderName, ts}], oldest first.
    Q_INVOKABLE QVariantList mediaItems() const;
    // HTML-escaped text with URLs turned into links (public for tests).
    static QString linkify(const QString& text);
    // Same, with @mention ranges rendered as zalos-mention:<uid> links.
    static QString linkify(const QString& text, const QVector<MessageMention>& mentions);
    // Zalo rich text (textProperties styles [{start, len, st}]: b i u s c_xxxxxx f_13 f_18 lst_1 lst_2) on top of
    // links and mentions.
    static QString styledHtml(const QString& text, const QVector<MessageMention>& mentions, const QVariantList& styles);
    // Replaces the detail of a poll in every loaded thread (from /api/poll/detail).
    void setPollData(qint64 pollId, const QVariantMap& data);
    // Shared content of the shown thread, newest first: kind "media" (photos/videos), "file" or "link".
    // Each entry: {msgId, url, thumb, title, senderName, ts, fileSize}.
    Q_INVOKABLE QVariantList sharedItems(const QString& kind, int limit = 60) const;

    // Rows of the shown thread whose text matches `query`, ignoring case and Vietnamese diacritics.
    Q_INVOKABLE QList<int> searchMessages(const QString& query) const;
    Q_INVOKABLE void updatePollVote(const QString& convId, qint64 pollId, int optionId, bool voted);

signals:
    // New incoming messages appended at the end of the open thread (count for the "jump down" badge).
    void incomingAppended(int count);
    void unreadMarkerChanged(int row);

private:
    void placeUnreadMarker();
    bool isRunTile(int row) const;
    bool joinsPrevious(int row) const; // tile that continues the run of row - 1

    QVector<MessageItem> m_items;
    QHash<QString, QVector<MessageItem>> m_store;
    QString m_currentConvId;
    QString m_unreadMarkerId;     // message id carrying the unread bar
    int m_pendingUnread{0};       // unread count still to place (history not loaded yet)
};

} // namespace Zalos::Core::Models


