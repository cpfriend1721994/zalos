#include "message_model.h"
#include "text_fold.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace Zalos::Core::Models {

MessageModel::MessageModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int MessageModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

namespace {
constexpr qint64 kGroupBreakMs = 30 * 60 * 1000;
}

QVariant MessageModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto& item = m_items.at(index.row());
    switch (role) {
        case IdRole: return item.id;
        case SenderNameRole: return item.senderName;
        case TextRole: return item.text;
        case TimeRole: return item.timeStr;
        case IsMineRole: return item.isMine;
        case IsE2EERole: return item.isE2EE;
        case ReactionRole: return item.reaction;
        case MessageTypeRole: return item.messageType;
        case MediaUrlRole: return item.mediaUrl;
        case TimestampRole: return item.ts;
        case SenderIdRole: return item.senderId;
        case SenderAvatarRole: return item.senderAvatar;
        case StatusRole: return item.status;
        case IsRecalledRole: return item.isRecalled;
        case PollDataRole: return item.pollData;
        case QuoteMsgIdRole: return item.quoteMsgId;
        case QuoteSenderRole: return item.quoteSender;
        case QuoteTextRole: return item.quoteText;
        case HrefRole: return item.href;
        case FileSizeRole: return item.fileSize;
        case FirstInGroupRole: {
            if (index.row() == 0) return true;
            const auto& prev = m_items.at(index.row() - 1);
            return prev.senderId != item.senderId || prev.isMine != item.isMine || item.ts - prev.ts > kGroupBreakMs
                   || QDateTime::fromMSecsSinceEpoch(prev.ts).date() != QDateTime::fromMSecsSinceEpoch(item.ts).date();
        }
        case LastInGroupRole: {
            int last = index.row();
            while (last + 1 < m_items.size() && joinsPrevious(last + 1)) ++last;
            if (last + 1 >= m_items.size()) return true;
            const auto& next = m_items.at(last + 1);
            return next.senderId != item.senderId || next.isMine != item.isMine || next.ts - item.ts > kGroupBreakMs
                   || QDateTime::fromMSecsSinceEpoch(next.ts).date() != QDateTime::fromMSecsSinceEpoch(item.ts).date();
        }
        case DateSeparatorRole:
            return item.ts > 0 && (index.row() == 0
                   || QDateTime::fromMSecsSinceEpoch(m_items.at(index.row() - 1).ts).date()
                          != QDateTime::fromMSecsSinceEpoch(item.ts).date());
        case UnreadMarkerRole: return !m_unreadMarkerId.isEmpty() && item.id == m_unreadMarkerId;
        case ShowStatusRole: {
            if (!item.isMine) return false;
            for (int r = index.row() + 1; r < m_items.size(); ++r) {
                if (m_items.at(r).isMine) return false;
            }
            return true;
        }
        case RichTextRole: {
            const QVariantList styles = item.extra.value("styles").toList();
            if (!styles.isEmpty()) return styledHtml(item.text, item.mentions, styles);
            return item.mentions.isEmpty() ? linkify(item.text) : linkify(item.text, item.mentions);
        }
        case ExtraRole: return item.extra;
        case InMediaRunRole: return joinsPrevious(index.row());
        case MediaRunRole: {
            const int row = index.row();
            if (!isRunTile(row) || joinsPrevious(row) || row + 1 >= m_items.size() || !joinsPrevious(row + 1))
                return QVariantList{};
            QVariantList tiles;
            for (int r = row; r < m_items.size() && (r == row || joinsPrevious(r)); ++r) {
                const auto& t = m_items.at(r);
                tiles.append(QVariantMap{{"msgId", t.id}, {"messageType", t.messageType}, {"mediaUrl", t.mediaUrl},
                                         {"href", t.href}, {"animatedUrl", t.extra.value("animatedUrl")}, {"sticker", t.extra.value("sticker")},
                                         {"isMine", t.isMine}, {"senderName", t.senderName}, {"ts", t.ts}});
            }
            return tiles;
        }
        default: return {};
    }
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    return {
        {IdRole, "msgId"},
        {SenderNameRole, "senderName"},
        {TextRole, "text"},
        {TimeRole, "time"},
        {IsMineRole, "isMine"},
        {IsE2EERole, "isE2EE"},
        {ReactionRole, "reaction"},
        {MessageTypeRole, "messageType"},
        {MediaUrlRole, "mediaUrl"},
        {TimestampRole, "ts"},
        {SenderIdRole, "senderId"},
        {SenderAvatarRole, "senderAvatar"},
        {StatusRole, "status"},
        {IsRecalledRole, "isRecalled"},
        {PollDataRole, "pollData"},
        {QuoteMsgIdRole, "quoteMsgId"},
        {QuoteSenderRole, "quoteSender"},
        {QuoteTextRole, "quoteText"},
        {HrefRole, "href"},
        {FileSizeRole, "fileSize"},
        {FirstInGroupRole, "firstInGroup"},
        {LastInGroupRole, "lastInGroup"},
        {UnreadMarkerRole, "unreadMarker"},
        {DateSeparatorRole, "dateSeparator"},
        {ShowStatusRole, "showStatus"},
        {RichTextRole, "richText"},
        {ExtraRole, "extra"},
        {MediaRunRole, "mediaRun"},
        {InMediaRunRole, "inMediaRun"}
    };
}

bool MessageModel::isRunTile(int row) const {
    if (row < 0 || row >= m_items.size()) return false;
    const auto& m = m_items.at(row);
    if (m.isRecalled || !m.quoteText.isEmpty()) return false;
    if (!m_unreadMarkerId.isEmpty() && m.id == m_unreadMarkerId) return false;
    return m.messageType == QLatin1String("sticker") || (m.messageType == QLatin1String("image") && m.text.isEmpty());
}

bool MessageModel::joinsPrevious(int row) const {
    if (row <= 0 || !isRunTile(row) || !isRunTile(row - 1)) return false;
    const auto& a = m_items.at(row - 1);
    const auto& b = m_items.at(row);
    return a.senderId == b.senderId && a.isMine == b.isMine && b.ts - a.ts <= kMediaRunGapMs && b.ts >= a.ts
           && QDateTime::fromMSecsSinceEpoch(a.ts).date() == QDateTime::fromMSecsSinceEpoch(b.ts).date();
}

void MessageModel::refreshRunsAround(int row) {
    if (m_items.isEmpty()) return;
    const int from = qBound(0, row - 12, m_items.size() - 1);
    const int to = qBound(0, row + 12, m_items.size() - 1);
    emit dataChanged(index(from), index(to), {MediaRunRole, InMediaRunRole, LastInGroupRole});
}

void MessageModel::addMessage(const QString& text, bool isMine, const QString& sender) {
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    MessageItem item;
    item.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    item.senderName = sender;
    item.text = text;
    item.timeStr = QDateTime::currentDateTime().toString("HH:mm");
    item.ts = QDateTime::currentMSecsSinceEpoch();
    item.isMine = isMine;
    item.isE2EE = false;
    item.reaction = "";
    item.messageType = "text";
    m_items.append(item);
    if (!m_currentConvId.isEmpty()) {
        m_store[m_currentConvId] = m_items;
    }
    endInsertRows();
}

void MessageModel::addAttachmentMessage(const QString& type, const QString& text, const QString& url, bool isMine) {
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    MessageItem item;
    item.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    item.ts = QDateTime::currentMSecsSinceEpoch();
    item.text = text;
    item.timeStr = QDateTime::currentDateTime().toString("HH:mm");
    item.isMine = isMine;
    item.isE2EE = false;
    item.reaction = "";
    item.messageType = type;
    item.mediaUrl = url;
    m_items.append(item);
    if (!m_currentConvId.isEmpty()) {
        m_store[m_currentConvId] = m_items;
    }
    endInsertRows();
}

void MessageModel::addMessageDirect(const QString& convId, const QString& id, const QString& senderName, const QString& text, const QString& timeStr, bool isMine, bool isE2EE, const QString& reaction, const QString& messageType, const QString& mediaUrl) {
    MessageItem item;
    item.id = id.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch()) : id;
    item.senderName = senderName;
    item.text = text;
    item.timeStr = timeStr;
    item.isMine = isMine;
    item.isE2EE = isE2EE;
    item.reaction = reaction;
    item.messageType = messageType;
    item.mediaUrl = mediaUrl;

    m_store[convId].append(item);
    if (m_currentConvId == convId) {
        beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }
}

void MessageModel::mergeMessages(const QString& convId, const QVector<MessageItem>& items) {
    if (items.isEmpty()) return;
    auto& thread = m_store[convId];
    QSet<QString> known;
    QHash<QString, int> pendingByCliId;
    for (int i = 0; i < thread.size(); ++i) {
        known.insert(thread[i].id);
        if (thread[i].id.startsWith(QLatin1String("local-")) && !thread[i].cliMsgId.isEmpty()) {
            pendingByCliId.insert(thread[i].cliMsgId, i);
        }
    }
    bool added = false;
    bool needSort = false;
    for (const auto& m : items) {
        if (m.id.isEmpty() || known.contains(m.id)) continue;
        known.insert(m.id);
        const int pending = pendingByCliId.contains(m.cliMsgId) ? pendingByCliId.take(m.cliMsgId) : -1;
        if (pending >= 0) {
            const QString localId = thread[pending].id;
            MessageItem merged = m;
            if (merged.status.isEmpty()) merged.status = QStringLiteral("sent");
            thread[pending] = merged;
            if (m_currentConvId == convId) {
                for (int row = 0; row < m_items.size(); ++row) {
                    if (m_items[row].id != localId) continue;
                    m_items[row] = merged;
                    emit dataChanged(index(row), index(row));
                    break;
                }
            }
            continue;
        }
        if (!thread.isEmpty() && m.ts < thread.last().ts) needSort = true;
        thread.append(m);
        added = true;
    }
    if (!added) return;
    if (needSort) {
        std::stable_sort(thread.begin(), thread.end(), [](const MessageItem& a, const MessageItem& b) { return a.ts < b.ts; });
    }

    if (m_currentConvId != convId) return;
    if (thread.size() > m_items.size() && std::equal(m_items.cbegin(), m_items.cend(), thread.cbegin(),
            [](const MessageItem& a, const MessageItem& b) { return a.id == b.id; })) {
        const int before = m_items.size();
        beginInsertRows(QModelIndex(), m_items.size(), thread.size() - 1);
        m_items = thread;
        endInsertRows();
        if (before > 0) {
            emit dataChanged(index(0), index(before - 1), {LastInGroupRole, ShowStatusRole});
            refreshRunsAround(before);
        }
        int incoming = 0;
        for (int row = before; row < m_items.size(); ++row) {
            if (!m_items.at(row).isMine && m_items.at(row).messageType != QLatin1String("system")) ++incoming;
        }
        if (incoming > 0) emit incomingAppended(incoming);
    } else {
        beginResetModel();
        m_items = thread;
        endResetModel();
    }
    if (m_pendingUnread > 0) placeUnreadMarker();
}

void MessageModel::setUnreadMarker(const QString& convId, int unreadCount) {
    if (convId != m_currentConvId) return;
    clearUnreadMarker();
    m_pendingUnread = qMax(0, unreadCount);
    if (m_pendingUnread > 0) placeUnreadMarker();
}

void MessageModel::clearUnreadMarker() {
    m_pendingUnread = 0;
    if (m_unreadMarkerId.isEmpty()) return;
    const QString old = m_unreadMarkerId;
    m_unreadMarkerId.clear();
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).id != old) continue;
        emit dataChanged(index(row), index(row), {UnreadMarkerRole});
        break;
    }
    emit unreadMarkerChanged(-1);
}

void MessageModel::placeUnreadMarker() {
    int seen = 0;
    for (int row = m_items.size() - 1; row >= 0; --row) {
        const MessageItem& item = m_items.at(row);
        if (item.isMine || item.messageType == QLatin1String("system")) continue;
        if (++seen < m_pendingUnread) continue;
        m_pendingUnread = 0;
        m_unreadMarkerId = item.id;
        emit dataChanged(index(row), index(row), {UnreadMarkerRole});
        emit unreadMarkerChanged(row);
        return;
    }
}

int MessageModel::unreadMarkerRow() const {
    if (m_unreadMarkerId.isEmpty()) return -1;
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).id == m_unreadMarkerId) return row;
    }
    return -1;
}

void MessageModel::confirmMessage(const QString& convId, const QString& localId, const QString& serverId) {
    auto& thread = m_store[convId];
    const bool alreadyPresent = std::any_of(thread.cbegin(), thread.cend(),
                                            [&](const MessageItem& m) { return m.id == serverId; });
    if (alreadyPresent) {
        removeMessage(convId, localId);
        return;
    }
    for (auto& m : thread) {
        if (m.id == localId) { m.id = serverId; break; }
    }
    if (m_currentConvId != convId) return;
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items[row].id == localId) {
            m_items[row].id = serverId;
            emit dataChanged(index(row), index(row), {IdRole});
            refreshRunsAround(row);
            break;
        }
    }
}

void MessageModel::removeMessage(const QString& convId, const QString& msgId) {
    auto& thread = m_store[convId];
    thread.erase(std::remove_if(thread.begin(), thread.end(), [&](const MessageItem& m) { return m.id == msgId; }),
                 thread.end());
    if (m_currentConvId != convId) return;
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items[row].id == msgId) {
            beginRemoveRows(QModelIndex(), row, row);
            m_items.removeAt(row);
            endRemoveRows();
            refreshRunsAround(row);
            break;
        }
    }
}

namespace {
template <typename Fn>
void updateItem(QHash<QString, QVector<MessageItem>>& store, QVector<MessageItem>& visible, const QString& currentConv,
                const QString& convId, const QString& msgId, Fn fn, MessageModel* model, const QList<int>& roles) {
    for (auto& m : store[convId]) {
        if (m.id == msgId) { fn(m); break; }
    }
    if (currentConv != convId) return;
    for (int row = 0; row < visible.size(); ++row) {
        if (visible[row].id == msgId) {
            fn(visible[row]);
            emit model->dataChanged(model->index(row), model->index(row), roles);
            if (roles.contains(MessageModel::MediaUrlRole) || roles.contains(MessageModel::IsRecalledRole))
                model->refreshRunsAround(row);
            break;
        }
    }
}
}

void MessageModel::setReaction(const QString& convId, const QString& msgId, const QString& summary) {
    updateItem(m_store, m_items, m_currentConvId, convId, msgId, [&](MessageItem& m) { m.reaction = summary; },
               this, {ReactionRole});
}

void MessageModel::markRecalled(const QString& convId, const QString& msgId, const QString& placeholder) {
    updateItem(m_store, m_items, m_currentConvId, convId, msgId, [&](MessageItem& m) {
        m.isRecalled = true;
        m.text = placeholder;
        m.messageType = "text";
        m.mediaUrl.clear();
        m.reaction.clear();
    }, this, {IsRecalledRole, TextRole, MessageTypeRole, MediaUrlRole, ReactionRole});
}

void MessageModel::setMedia(const QString& convId, const QString& msgId, const QString& mediaUrl) {
    updateItem(m_store, m_items, m_currentConvId, convId, msgId, [&](MessageItem& m) { m.mediaUrl = mediaUrl; },
               this, {MediaUrlRole});
}

void MessageModel::setSticker(const QString& convId, const QString& msgId, const QVariantMap& info) {
    updateItem(m_store, m_items, m_currentConvId, convId, msgId, [&](MessageItem& m) {
        m.mediaUrl = info.value("url").toString();
        m.extra.insert("sticker", info);
    }, this, {MediaUrlRole, ExtraRole});
}

void MessageModel::setStatus(const QString& convId, const QString& msgId, const QString& status) {
    updateItem(m_store, m_items, m_currentConvId, convId, msgId, [&](MessageItem& m) { m.status = status; },
               this, {StatusRole});
}

void MessageModel::markSeenUpTo(const QString& convId, const QString& msgId) {
    const auto thread = m_store.value(convId);
    qint64 limit = -1;
    for (const auto& m : thread) {
        if (m.id == msgId) { limit = m.ts; break; }
    }
    if (limit < 0 && !thread.isEmpty()) limit = thread.last().ts;
    for (const auto& m : thread) {
        if (m.isMine && m.ts <= limit && m.status != "seen" && !m.id.startsWith("local-")) {
            setStatus(convId, m.id, "seen");
        }
    }
}

bool MessageModel::contains(const QString& convId, const QString& msgId) const {
    const auto thread = m_store.value(convId);
    return std::any_of(thread.cbegin(), thread.cend(), [&](const MessageItem& m) { return m.id == msgId; });
}

qint64 MessageModel::oldestTimestamp(const QString& convId) const {
    qint64 oldest = 0;
    for (const auto& m : m_store.value(convId)) {
        if (m.ts > 0 && (oldest == 0 || m.ts < oldest)) oldest = m.ts;
    }
    return oldest;
}

QString MessageModel::oldestMessageId(const QString& convId) const {
    for (const auto& m : m_store.value(convId)) {
        bool numeric = false;
        m.id.toULongLong(&numeric);
        if (numeric) return m.id;
    }
    return {};
}

QString MessageModel::latestMessageId(const QString& convId) const {
    const auto thread = m_store.value(convId);
    for (auto it = thread.crbegin(); it != thread.crend(); ++it) {
        bool numeric = false;
        it->id.toULongLong(&numeric);
        if (numeric) return it->id;
    }
    return {};
}

MessageItem MessageModel::message(const QString& convId, const QString& msgId) const {
    for (const auto& m : m_store.value(convId)) {
        if (m.id == msgId) return m;
    }
    return {};
}

int MessageModel::countIncomingAfter(const QString& convId, const QString& readPoint) const {
    bool ok = false;
    const qulonglong point = readPoint.toULongLong(&ok);
    if (!ok) return 0;
    int count = 0;
    for (const auto& m : m_store.value(convId)) {
        bool numeric = false;
        const qulonglong id = m.id.toULongLong(&numeric);
        if (numeric && id > point && !m.isMine && !m.isRecalled) ++count;
    }
    return count;
}

bool MessageModel::hasLocalEcho(const QString& convId) const {
    const auto thread = m_store.value(convId);
    return std::any_of(thread.cbegin(), thread.cend(),
                       [](const MessageItem& m) { return m.id.startsWith(QLatin1String("local-")); });
}

void MessageModel::removeMessagesUpTo(const QString& convId, const QString& lastMsgId) {
    bool ok = false;
    const qulonglong last = lastMsgId.toULongLong(&ok);
    if (!ok) return;
    auto older = [last](const MessageItem& m) {
        bool numeric = false;
        const qulonglong id = m.id.toULongLong(&numeric);
        return numeric && id <= last;
    };
    auto& thread = m_store[convId];
    thread.erase(std::remove_if(thread.begin(), thread.end(), older), thread.end());
    if (m_currentConvId != convId) return;
    beginResetModel();
    m_items = thread;
    endResetModel();
}

void MessageModel::clearThread(const QString& convId) {
    m_store.remove(convId);
    if (m_currentConvId != convId) return;
    beginResetModel();
    m_items.clear();
    endResetModel();
}

void MessageModel::clearAll() {
    beginResetModel();
    m_items.clear();
    m_store.clear();
    m_currentConvId.clear();
    endResetModel();
}

void MessageModel::loadConversation(const QString& conversationId) {
    beginResetModel();
    m_unreadMarkerId.clear();
    m_pendingUnread = 0;
    m_currentConvId = conversationId;
    m_items = m_store.value(conversationId);
    endResetModel();
}

QVariantMap MessageModel::getMessageById(const QString& msgId) const {
    for (const auto& item : m_items) {
        if (item.id == msgId) {
            return {
                {"id", item.id},
                {"senderName", item.senderName},
                {"text", item.text},
                {"time", item.timeStr},
                {"isMine", item.isMine},
                {"messageType", item.messageType},
                {"mediaUrl", item.mediaUrl},
                {"ts", item.ts},
                {"senderId", item.senderId}
            };
        }
    }
    for (auto it = m_store.constBegin(); it != m_store.constEnd(); ++it) {
        for (const auto& item : it.value()) {
            if (item.id == msgId) {
                return {
                    {"id", item.id},
                    {"senderName", item.senderName},
                    {"text", item.text},
                    {"time", item.timeStr},
                    {"isMine", item.isMine},
                    {"messageType", item.messageType},
                    {"mediaUrl", item.mediaUrl},
                    {"ts", item.ts},
                    {"senderId", item.senderId}
                };
            }
        }
    }
    return {};
}

QString MessageModel::linkify(const QString& text) {
    static const QRegularExpression url(QStringLiteral(R"((https?://[^\s<>"']+|www\.[^\s<>"']+))"));
    QString out;
    int last = 0;
    auto it = url.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        QString link = m.captured(0);
        while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
        out += text.mid(last, m.capturedStart() - last).toHtmlEscaped();
        const QString href = link.startsWith(QLatin1String("www.")) ? "https://" + link : link;
        out += QStringLiteral("<a href=\"%1\">%2</a>").arg(href.toHtmlEscaped(), link.toHtmlEscaped());
        last = m.capturedStart() + link.size();
    }
    out += text.mid(last).toHtmlEscaped();
    return out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

int MessageModel::rowOfMessage(const QString& msgId) const {
    if (msgId.isEmpty()) return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == msgId || m_items.at(i).cliMsgId == msgId) return i;
    }
    return -1;
}

QVariantList MessageModel::mediaItems() const {
    QVariantList out;
    for (const auto& m : m_items) {
        if (m.messageType != QLatin1String("image") || m.isRecalled) continue;
        const QString full = m.href.isEmpty() ? m.mediaUrl : m.href;
        if (full.isEmpty()) continue;
        out.append(QVariantMap{{"msgId", m.id}, {"url", full}, {"thumb", m.mediaUrl},
                               {"senderName", m.senderName}, {"isMine", m.isMine}, {"ts", m.ts}});
    }
    return out;
}

QList<int> MessageModel::searchMessages(const QString& query) const {
    QList<int> results;
    const QString clean = foldForSearch(query.trimmed());
    if (clean.isEmpty()) return results;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).isRecalled) continue;
        if (foldForSearch(m_items.at(i).text).contains(clean)) {
            results.append(i);
        }
    }
    return results;
}

QString MessageModel::linkify(const QString& text, const QVector<MessageMention>& mentions) {
    QVector<MessageMention> sorted = mentions;
    std::sort(sorted.begin(), sorted.end(), [](const MessageMention& a, const MessageMention& b) { return a.pos < b.pos; });
    QString out;
    int last = 0;
    for (const MessageMention& m : sorted) {
        if (m.pos < last || m.len <= 0 || m.pos + m.len > text.size()) continue;
        out += linkify(text.mid(last, m.pos - last));
        out += QStringLiteral("<a href=\"zalos-mention:%1\"><b>%2</b></a>")
                   .arg(m.uid.toHtmlEscaped(), text.mid(m.pos, m.len).toHtmlEscaped());
        last = m.pos + m.len;
    }
    out += linkify(text.mid(last));
    return out;
}

QString MessageModel::styledHtml(const QString& text, const QVector<MessageMention>& mentions, const QVariantList& styles) {
    struct Range { int start; int end; QString st; };
    QVector<Range> ranges;
    QSet<int> cuts{0, int(text.size())};
    for (const QVariant& v : styles) {
        const QVariantMap m = v.toMap();
        const int start = m.value("start").toInt();
        const int end = qMin(int(text.size()), start + m.value("len").toInt());
        if (start < 0 || end <= start) continue;
        ranges.append({start, end, m.value("st").toString()});
        cuts << start << end;
    }
    for (const MessageMention& m : mentions) {
        if (m.pos < 0 || m.len <= 0 || m.pos + m.len > text.size()) continue;
        cuts << m.pos << m.pos + m.len;
    }
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) cuts << i + 1;
    }
    QList<int> points(cuts.begin(), cuts.end());
    std::sort(points.begin(), points.end());

    auto active = [&ranges](int pos, const QString& prefix) {
        QString hit;
        for (const Range& r : ranges) {
            if (pos >= r.start && pos < r.end && r.st.startsWith(prefix)) hit = r.st;
        }
        return hit;
    };
    auto has = [&ranges](int pos, const QString& st) {
        for (const Range& r : ranges) {
            if (pos >= r.start && pos < r.end && r.st == st) return true;
        }
        return false;
    };

    QString out;
    int listNumber = 0;
    for (int k = 0; k + 1 < points.size(); ++k) {
        const int from = points.at(k);
        const int to = points.at(k + 1);
        if (from >= to) continue;
        const bool lineStart = from == 0 || text.at(from - 1) == QLatin1Char('\n');
        if (lineStart) {
            const QString list = active(from, QStringLiteral("lst_"));
            if (list == QLatin1String("lst_2")) out += QString::number(++listNumber) + QStringLiteral(". ");
            else {
                listNumber = 0;
                if (list == QLatin1String("lst_1")) out += QStringLiteral("\u2022 ");
            }
        }
        const QString piece = text.mid(from, to - from);
        QString html;
        const MessageMention* mention = nullptr;
        for (const MessageMention& m : mentions) {
            if (from >= m.pos && to <= m.pos + m.len) { mention = &m; break; }
        }
        if (mention) {
            html = QStringLiteral("<a href=\"zalos-mention:%1\"><b>%2</b></a>")
                       .arg(mention->uid.toHtmlEscaped(), piece.toHtmlEscaped());
        } else {
            html = linkify(piece);
        }
        QString open, close;
        QString css;
        const QString color = active(from, QStringLiteral("c_"));
        if (color.size() == 8) css += QStringLiteral("color:#%1;").arg(color.mid(2));
        const QString size = active(from, QStringLiteral("f_"));
        if (size == QLatin1String("f_13")) css += QStringLiteral("font-size:12px;");
        else if (size == QLatin1String("f_18")) css += QStringLiteral("font-size:18px;");
        if (!css.isEmpty()) { open += QStringLiteral("<span style=\"%1\">").arg(css); close.prepend(QStringLiteral("</span>")); }
        for (const char* tag : {"b", "i", "u", "s"}) {
            if (!has(from, QLatin1String(tag))) continue;
            open += QStringLiteral("<%1>").arg(QLatin1String(tag));
            close.prepend(QStringLiteral("</%1>").arg(QLatin1String(tag)));
        }
        out += open + html + close;
    }
    return out;
}

void MessageModel::setPollData(qint64 pollId, const QVariantMap& data) {
    if (pollId <= 0) return;
    auto matches = [pollId](const MessageItem& item) {
        return item.messageType == QLatin1String("poll") && item.pollData.value("poll_id").toLongLong() == pollId;
    };
    for (int i = 0; i < m_items.size(); ++i) {
        if (!matches(m_items[i])) continue;
        m_items[i].pollData = data;
        if (!data.value("question").toString().isEmpty()) m_items[i].text = data.value("question").toString();
        emit dataChanged(index(i), index(i), {PollDataRole, TextRole, RichTextRole});
    }
    for (auto it = m_store.begin(); it != m_store.end(); ++it) {
        for (MessageItem& item : it.value()) {
            if (!matches(item)) continue;
            item.pollData = data;
            if (!data.value("question").toString().isEmpty()) item.text = data.value("question").toString();
        }
    }
}

QVariantList MessageModel::sharedItems(const QString& kind, int limit) const {
    static const QRegularExpression url(QStringLiteral(R"(https?://[^\s<>"']+)"));
    QVariantList out;
    for (int i = m_items.size() - 1; i >= 0 && out.size() < limit; --i) {
        const MessageItem& m = m_items.at(i);
        if (m.isRecalled) continue;
        QVariantMap e{{"msgId", m.id}, {"senderName", m.senderName}, {"ts", m.ts}};
        if (kind == QLatin1String("media")) {
            if (m.messageType != QLatin1String("image") && m.messageType != QLatin1String("video")) continue;
            if (m.mediaUrl.isEmpty()) continue;
            e["thumb"] = m.mediaUrl;
            e["url"] = m.href.isEmpty() ? m.mediaUrl : m.href;
            e["isVideo"] = m.messageType == QLatin1String("video");
        } else if (kind == QLatin1String("file")) {
            if (m.messageType != QLatin1String("file")) continue;
            e["title"] = m.text;
            e["url"] = m.href.isEmpty() ? m.mediaUrl : m.href;
            e["fileSize"] = m.fileSize;
        } else if (kind == QLatin1String("link")) {
            if (m.messageType == QLatin1String("link")) {
                e["url"] = m.href;
                e["title"] = m.text.isEmpty() ? m.href : m.text;
                e["thumb"] = m.mediaUrl;
            } else {
                const auto match = url.match(m.text);
                if (!match.hasMatch()) continue;
                QString link = match.captured(0);
                while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
                e["url"] = link;
                e["title"] = link;
            }
        } else {
            return out;
        }
        out.append(e);
    }
    return out;
}

void MessageModel::updatePollVote(const QString& convId, qint64 pollId, int optionId, bool voted) {
    auto updateItem = [pollId, optionId, voted](MessageItem& item) -> bool {
        if (item.messageType != "poll") return false;
        qint64 pId = item.pollData.value("poll_id").toLongLong();
        if (pId != pollId && pollId != 0) return false;
        QVariantList optList = item.pollData.value("options").toList();
        bool changed = false;
        int deltaVote = 0;
        for (int i = 0; i < optList.size(); ++i) {
            QVariantMap opt = optList[i].toMap();
            if (opt.value("option_id").toInt() == optionId) {
                bool oldVoted = opt.value("voted").toBool();
                if (oldVoted != voted) {
                    opt["voted"] = voted;
                    int curVotes = opt.value("votes").toInt();
                    opt["votes"] = voted ? (curVotes + 1) : qMax(0, curVotes - 1);
                    optList[i] = opt;
                    changed = true;
                    deltaVote += (voted ? 1 : -1);
                }
            }
        }
        if (changed) {
            item.pollData["options"] = optList;
            int total = item.pollData.value("num_vote").toInt();
            item.pollData["num_vote"] = qMax(0, total + deltaVote);
            return true;
        }
        return false;
    };

    if (m_currentConvId == convId || convId.isEmpty()) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (updateItem(m_items[i])) {
                emit dataChanged(index(i), index(i), {PollDataRole});
            }
        }
    }

    if (!convId.isEmpty() && m_store.contains(convId)) {
        QVector<MessageItem>& thread = m_store[convId];
        for (MessageItem& it : thread) {
            updateItem(it);
        }
    }
}

}
