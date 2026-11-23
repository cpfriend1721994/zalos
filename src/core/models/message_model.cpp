#include "message_model.h"

#include <QScopeGuard>
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

QVector<MessageItem>& MessageModel::threadFor(const QString& convId) {
    if (!convId.isEmpty() && convId == m_currentConvId) return m_items;
    return m_store[convId];
}

const QVector<MessageItem>& MessageModel::threadOf(const QString& convId) const {
    static const QVector<MessageItem> kEmpty;
    if (!convId.isEmpty() && convId == m_currentConvId) return m_items;
    const auto it = m_store.constFind(convId);
    return it != m_store.constEnd() ? it.value() : kEmpty;
}

namespace {
constexpr qint64 kGroupBreakMs = 30 * 60 * 1000;

qint64 localDay(const MessageItem& m) {
    if (m.dayCache < 0)
        m.dayCache = QDateTime::fromMSecsSinceEpoch(m.ts).date().toJulianDay();
    return m.dayCache;
}
}

void MessageModel::refreshSenders() {
    if (m_items.isEmpty()) return;
    emit dataChanged(index(0), index(int(m_items.size()) - 1), {SenderNameRole, SenderAvatarRole});
}

QVariant MessageModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const auto& item = m_items.at(index.row());
    switch (role) {
        case IdRole: return item.id;
        case SenderNameRole:
            if (item.senderName.isEmpty() && !item.isMine && m_senderResolver && !item.senderId.isEmpty()
                && item.senderId != QLatin1String("system"))
                return m_senderResolver(item.senderId).first;
            return item.senderName;
        case TextRole: return item.text;
        case TimeRole: return item.timeStr;
        case IsMineRole: return item.isMine;
        case IsE2EERole: return item.isE2EE;
        case IsForwardedRole: return item.isForwarded;
        case ExpireOnRole: return item.expireOn;
        case ReactionRole: return item.reaction;
        case MessageTypeRole: return item.messageType;
        case MediaUrlRole: return item.mediaUrl;
        case TimestampRole: return item.ts;
        case SenderIdRole: return item.senderId;
        case SenderAvatarRole:
            if (item.senderAvatar.isEmpty() && !item.isMine && m_senderResolver && !item.senderId.isEmpty()
                && item.senderId != QLatin1String("system"))
                return m_senderResolver(item.senderId).second;
            return item.senderAvatar;
        case StatusRole: return item.status;
        case IsRecalledRole: return item.isRecalled;
        case PollDataRole: return item.pollData;
        case PollCardRole: return item.pollCard;
        case QuoteMsgIdRole: return item.quoteMsgId;
        case QuoteSenderRole: return item.quoteSender;
        case QuoteTextRole: return item.quoteText;
        case HrefRole: return item.href;
        case FileSizeRole: return item.fileSize;
        case FirstInGroupRole: {
            if (index.row() == 0) return true;
            const auto& prev = m_items.at(index.row() - 1);
            return prev.senderId != item.senderId || prev.isMine != item.isMine || item.ts - prev.ts > kGroupBreakMs
                   || localDay(prev) != localDay(item);
        }
        case LastInGroupRole: {
            int last = index.row();
            while (last + 1 < m_items.size() && joinsPrevious(last + 1)) ++last;
            if (last + 1 >= m_items.size()) return true;
            const auto& next = m_items.at(last + 1);
            return next.senderId != item.senderId || next.isMine != item.isMine || next.ts - item.ts > kGroupBreakMs
                   || localDay(next) != localDay(item);
        }
        case DateSeparatorRole:
            return item.ts > 0 && (index.row() == 0
                   || localDay(m_items.at(index.row() - 1)) != localDay(item));
        case UnreadMarkerRole: return !m_unreadMarkerId.isEmpty() && item.id == m_unreadMarkerId;
        case ShowStatusRole:
            return item.isMine && index.row() == m_lastMineRow;
        case RichTextRole: {
            const QString key = item.id + QLatin1Char(':') + QString::number(m_richTextRev) + QLatin1Char(':')
                              + QString::number(item.text.size());
            if (const QString* cached = m_richTextLru.object(key)) {
                return *cached;
            }
            const QVariantList styles = item.extra.value("styles").toList();
            const QString html = !styles.isEmpty()
                ? styledHtml(item.text, item.mentions, styles)
                : (item.mentions.isEmpty() ? linkify(item.text) : linkify(item.text, item.mentions));
            m_richTextLru.insert(key, new QString(html));
            return html;
        }
        case ExtraRole: return item.extra;
        case SeenUidsRole: return item.seenUids;
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
        {IsForwardedRole, "isForwarded"},
        {ExpireOnRole, "expireOn"},
        {ReactionRole, "reaction"},
        {MessageTypeRole, "messageType"},
        {MediaUrlRole, "mediaUrl"},
        {TimestampRole, "ts"},
        {SenderIdRole, "senderId"},
        {SenderAvatarRole, "senderAvatar"},
        {StatusRole, "status"},
        {IsRecalledRole, "isRecalled"},
        {PollDataRole, "pollData"},
        {PollCardRole, "pollCard"},
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
        {SeenUidsRole, "seenUids"},
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
           && localDay(a) == localDay(b);
}

void MessageModel::refreshRunsAround(int row) {
    if (m_items.isEmpty()) return;
    const int from = qBound(0, row - 12, m_items.size() - 1);
    const int to = qBound(0, row + 12, m_items.size() - 1);
    emit dataChanged(index(from), index(to), {MediaRunRole, InMediaRunRole, LastInGroupRole});
}

void MessageModel::recomputeLastMineRow() {
    m_lastMineRow = -1;
    for (int r = int(m_items.size()) - 1; r >= 0; --r) {
        if (m_items.at(r).isMine) {
            m_lastMineRow = r;
            break;
        }
    }
}

void MessageModel::rebuildActiveIndex() {
    m_activeRowById.clear();
    m_activeRowByCliId.clear();
    m_activeRowById.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        const MessageItem& m = m_items.at(i);
        if (!m.id.isEmpty()) m_activeRowById.insert(m.id, i);
        if (!m.cliMsgId.isEmpty() && m.cliMsgId != QLatin1String("0")) {
            m_activeRowByCliId.insert(m.cliMsgId, i);
        }
    }
}

int MessageModel::activeRowOf(const QString& msgId) const {
    if (msgId.isEmpty()) return -1;
    const auto it = m_activeRowById.constFind(msgId);
    if (it != m_activeRowById.cend()) return it.value();
    return m_activeRowByCliId.value(msgId, -1);
}

void MessageModel::addMessage(const QString& text, bool isMine, const QString& sender) {
    const int prevMine = isMine ? m_lastMineRow : -1;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    MessageItem item;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    item.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_synthSeq);
    item.senderName = sender;
    item.text = text;
    item.timeStr = QDateTime::currentDateTime().toString("HH:mm");
    item.ts = now;
    item.isMine = isMine;
    item.isE2EE = false;
    item.reaction = "";
    item.messageType = "text";
    m_items.append(item);
    const int row = int(m_items.size()) - 1;
    m_activeRowById.insert(item.id, row);
    if (isMine) m_lastMineRow = row;
    endInsertRows();
    if (prevMine >= 0) emit dataChanged(index(prevMine), index(prevMine), {ShowStatusRole});
}

void MessageModel::addAttachmentMessage(const QString& type, const QString& text, const QString& url, bool isMine) {
    const int prevMine = isMine ? m_lastMineRow : -1;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    MessageItem item;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    item.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_synthSeq);
    item.ts = now;
    item.text = text;
    item.timeStr = QDateTime::currentDateTime().toString("HH:mm");
    item.isMine = isMine;
    item.isE2EE = false;
    item.reaction = "";
    item.messageType = type;
    item.mediaUrl = url;
    m_items.append(item);
    const int row = int(m_items.size()) - 1;
    m_activeRowById.insert(item.id, row);
    if (isMine) m_lastMineRow = row;
    endInsertRows();
    if (prevMine >= 0) emit dataChanged(index(prevMine), index(prevMine), {ShowStatusRole});
}

void MessageModel::addMessageDirect(const QString& convId, const QString& id, const QString& senderName, const QString& text, const QString& timeStr, bool isMine, bool isE2EE, const QString& reaction, const QString& messageType, const QString& mediaUrl) {
    MessageItem item;
    item.id = id.isEmpty() ? QStringLiteral("local-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(++m_synthSeq) : id;
    item.senderName = senderName;
    item.text = text;
    item.timeStr = timeStr;
    item.isMine = isMine;
    item.isE2EE = isE2EE;
    item.reaction = reaction;
    item.messageType = messageType;
    item.mediaUrl = mediaUrl;

    if (m_currentConvId == convId) {
        const int prevMine = isMine ? m_lastMineRow : -1;
        beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
        m_items.append(item);
        const int row = int(m_items.size()) - 1;
        m_activeRowById.insert(item.id, row);
        if (isMine) m_lastMineRow = row;
        endInsertRows();
        if (prevMine >= 0) emit dataChanged(index(prevMine), index(prevMine), {ShowStatusRole});
    } else {
        m_store[convId].append(item);
    }
}

void MessageModel::mergeMessages(const QString& convId, const QVector<MessageItem>& items) {
    if (items.isEmpty()) return;
    const bool hasPoll = std::any_of(items.cbegin(), items.cend(),
                                     [](const MessageItem& m) { return m.messageType == QLatin1String("poll"); });
    const auto pollCards = qScopeGuard([this, hasPoll, &convId]() {
        if (hasPoll) refreshPollCards(convId);
    });
    const bool isActive = !convId.isEmpty() && m_currentConvId == convId;
    QVector<MessageItem>& base = threadFor(convId);
    const int baseSize = base.size();
    QVector<MessageItem> appended;
    auto at = [&](int i) -> MessageItem& { return i < baseSize ? base[i] : appended[i - baseSize]; };
    QHash<QString, int> byId;
    byId.reserve(baseSize + items.size());
    QHash<QString, int> byCliId;
    for (int i = 0; i < baseSize; ++i) {
        const MessageItem& m = std::as_const(base)[i];
        byId.insert(m.id, i);
        if (!m.cliMsgId.isEmpty() && m.cliMsgId != QLatin1String("0")) byCliId.insert(m.cliMsgId, i);
    }
    bool needSort = false;
    for (const auto& m : items) {
        if (m.id.isEmpty()) continue;
        if (const auto idIt = byId.constFind(m.id); idIt != byId.cend()) {
            const QString incomingAnim = m.extra.value(QStringLiteral("animatedUrl")).toString();
            if (!incomingAnim.isEmpty()) {
                const int idx = idIt.value();
                MessageItem& existing = at(idx);
                if (existing.extra.value(QStringLiteral("animatedUrl")).toString().isEmpty()) {
                    existing.extra.insert(QStringLiteral("animatedUrl"), incomingAnim);
                    if (isActive && idx < baseSize) {
                        emit dataChanged(index(idx), index(idx), {ExtraRole, MediaRunRole});
                        refreshRunsAround(idx);
                    }
                }
            }
            continue;
        }
        const int existingIdx = (!m.cliMsgId.isEmpty() && m.cliMsgId != QLatin1String("0") && byCliId.contains(m.cliMsgId))
                                ? byCliId.value(m.cliMsgId) : -1;
        if (existingIdx >= 0 && (at(existingIdx).id.startsWith(QLatin1String("local-")) || at(existingIdx).senderId == m.senderId)) {
            MessageItem& existing = at(existingIdx);
            const QString oldId = existing.id;
            MessageItem merged = m;
            if (merged.status.isEmpty()) {
                if (existing.id.startsWith(QLatin1String("local-"))) {
                    merged.status = QStringLiteral("sent");
                } else {
                    merged.status = existing.status.isEmpty() ? QStringLiteral("sent") : existing.status;
                }
            } else if (existing.status == QStringLiteral("seen") && merged.status == QStringLiteral("sent")) {
                merged.status = QStringLiteral("seen");
            } else if (existing.status == QStringLiteral("delivered") && merged.status == QStringLiteral("sent")) {
                merged.status = QStringLiteral("delivered");
            }
            if (merged.reaction.isEmpty()) merged.reaction = existing.reaction;
            for (const QString& u : existing.seenUids) {
                if (!merged.seenUids.contains(u)) merged.seenUids.append(u);
            }
            if (existing.isRecalled) {
                merged.isRecalled = true;
                merged.text = existing.text;
            }
            if (merged.extra.value(QStringLiteral("animatedUrl")).toString().isEmpty()) {
                const QString oldAnim = existing.extra.value(QStringLiteral("animatedUrl")).toString();
                if (!oldAnim.isEmpty()) merged.extra.insert(QStringLiteral("animatedUrl"), oldAnim);
            }
            byId.remove(oldId);
            byId.insert(merged.id, existingIdx);
            const QString oldCli = existing.cliMsgId;
            const qint64 oldTs = existing.ts;
            const bool textOrStylesChanged = merged.text != existing.text
                || merged.extra.value(QStringLiteral("styles")) != existing.extra.value(QStringLiteral("styles"));
            existing = std::move(merged);
            if (isActive && existingIdx < baseSize) {
                m_activeRowById.remove(oldId);
                m_activeRowById.insert(existing.id, existingIdx);
                if (!oldCli.isEmpty() && oldCli != QLatin1String("0")) m_activeRowByCliId.remove(oldCli);
                if (!existing.cliMsgId.isEmpty() && existing.cliMsgId != QLatin1String("0")) {
                    m_activeRowByCliId.insert(existing.cliMsgId, existingIdx);
                }
            }
            if (textOrStylesChanged) {
                ++m_richTextRev;
                m_richTextLru.clear();
            }
            if (existing.ts != oldTs) {
                if ((existingIdx > 0 && existing.ts < at(existingIdx - 1).ts)
                    || (existingIdx + 1 < baseSize && existing.ts > at(existingIdx + 1).ts)) {
                    needSort = true;
                }
            }
            if (isActive && existingIdx < baseSize) {
                emit dataChanged(index(existingIdx), index(existingIdx));
                refreshRunsAround(existingIdx);
            }
            continue;
        }
        byId.insert(m.id, baseSize + appended.size());
        if (!m.cliMsgId.isEmpty() && m.cliMsgId != QLatin1String("0")) {
            byCliId.insert(m.cliMsgId, baseSize + appended.size());
        }
        const MessageItem* last = !appended.isEmpty() ? &appended.last() : (baseSize > 0 ? &std::as_const(base).last() : nullptr);
        if (last && m.ts < last->ts) needSort = true;
        appended.append(m);
    }
    if (appended.isEmpty() && !needSort) return;

    if (!isActive) {
        base.append(std::move(appended));
        if (needSort) {
            std::stable_sort(base.begin(), base.end(), [](const MessageItem& a, const MessageItem& b) { return a.ts < b.ts; });
        }
        return;
    }
    bool pureAppend = !needSort;
    QVector<MessageItem> sorted;
    if (needSort) {
        sorted.reserve(baseSize + appended.size());
        sorted = m_items;
        sorted.append(appended);
        std::stable_sort(sorted.begin(), sorted.end(), [](const MessageItem& a, const MessageItem& b) { return a.ts < b.ts; });
        pureAppend = std::equal(m_items.cbegin(), m_items.cend(), sorted.cbegin(),
                                [](const MessageItem& a, const MessageItem& b) { return a.id == b.id; });
    }
    const int prepended = sorted.size() - m_items.size();
    if (!pureAppend && needSort && prepended > 0
        && std::equal(m_items.cbegin(), m_items.cend(), sorted.cbegin() + prepended,
                      [](const MessageItem& a, const MessageItem& b) { return a.id == b.id; })) {
        beginInsertRows(QModelIndex(), 0, prepended - 1);
        m_items = std::move(sorted);
        recomputeLastMineRow();
        rebuildActiveIndex();
        endInsertRows();
        if (prepended < m_items.size()) {
            emit dataChanged(index(prepended), index(prepended));
            refreshRunsAround(prepended);
        }
        if (m_pendingUnread > 0) placeUnreadMarker();
        return;
    }
    if (pureAppend) {
        if (appended.isEmpty()) return;
        const int before = m_items.size();
        const int prevMine = m_lastMineRow;
        const int count = appended.size();
        beginInsertRows(QModelIndex(), before, before + count - 1);
        if (needSort) {
            m_items = std::move(sorted);
            rebuildActiveIndex();
        } else {
            m_items.append(std::move(appended));
            for (int idx = before; idx < m_items.size(); ++idx) {
                const MessageItem& added = m_items.at(idx);
                if (!added.id.isEmpty()) m_activeRowById.insert(added.id, idx);
                if (!added.cliMsgId.isEmpty() && added.cliMsgId != QLatin1String("0")) {
                    m_activeRowByCliId.insert(added.cliMsgId, idx);
                }
            }
        }
        recomputeLastMineRow();
        endInsertRows();
        if (before > 0) {
            emit dataChanged(index(before - 1), index(before - 1), {LastInGroupRole});
            if (prevMine >= 0 && prevMine != m_lastMineRow && prevMine < m_items.size()) {
                emit dataChanged(index(prevMine), index(prevMine), {ShowStatusRole});
            }
            refreshRunsAround(before);
        }
        int incoming = 0;
        for (int row = before; row < m_items.size(); ++row) {
            if (!m_items.at(row).isMine && m_items.at(row).messageType != QLatin1String("system")) ++incoming;
        }
        if (incoming > 0) emit incomingAppended(incoming);
    } else {
        beginResetModel();
        m_items = std::move(sorted);
        recomputeLastMineRow();
        rebuildActiveIndex();
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
    const int row = m_activeRowById.value(old, -1);
    if (row >= 0 && row < m_items.size()) {
        emit dataChanged(index(row), index(row), {UnreadMarkerRole});
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
    const int row = m_activeRowById.value(m_unreadMarkerId, -1);
    return (row >= 0 && row < m_items.size()) ? row : -1;
}

void MessageModel::confirmMessage(const QString& convId, const QString& localId, const QString& serverId) {
    const bool active = m_currentConvId == convId;
    const bool alreadyPresent = active
        ? m_activeRowById.contains(serverId)
        : std::any_of(threadOf(convId).cbegin(), threadOf(convId).cend(),
                      [&](const MessageItem& m) { return m.id == serverId; });
    if (alreadyPresent) {
        removeMessage(convId, localId);
        return;
    }
    if (!active) {
        for (auto& m : threadFor(convId)) {
            if (m.id == localId) { m.id = serverId; break; }
        }
        return;
    }
    const int row = m_activeRowById.value(localId, -1);
    if (row >= 0 && row < m_items.size()) {
        m_items[row].id = serverId;
        m_activeRowById.remove(localId);
        m_activeRowById.insert(serverId, row);
        emit dataChanged(index(row), index(row), {IdRole});
        refreshRunsAround(row);
    }
}

void MessageModel::removeMessage(const QString& convId, const QString& msgId) {
    if (m_currentConvId != convId) {
        auto it = m_store.find(convId);
        if (it != m_store.end()) {
            it->erase(std::remove_if(it->begin(), it->end(), [&](const MessageItem& m) { return m.id == msgId; }),
                      it->end());
        }
        return;
    }
    const int row = m_activeRowById.value(msgId, -1);
    if (row >= 0 && row < m_items.size()) {
        const bool wasUnreadMarker = !m_unreadMarkerId.isEmpty() && msgId == m_unreadMarkerId;
        const bool wasMine = m_items[row].isMine;
        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        if (wasMine || row < m_lastMineRow) recomputeLastMineRow();
        rebuildActiveIndex();
        endRemoveRows();
        if (wasUnreadMarker) {
            m_unreadMarkerId.clear();
            emit unreadMarkerChanged(-1);
        }
        if (row < m_items.size()) {
            emit dataChanged(index(row), index(row), {FirstInGroupRole, DateSeparatorRole, UnreadMarkerRole});
        }
        if (row > 0 && row - 1 < m_items.size()) {
            emit dataChanged(index(row - 1), index(row - 1), {LastInGroupRole, ShowStatusRole});
        }
        if (wasMine && m_lastMineRow >= 0 && m_lastMineRow != row - 1) {
            emit dataChanged(index(m_lastMineRow), index(m_lastMineRow), {ShowStatusRole});
        }
        refreshRunsAround(qMin(row, int(m_items.size()) - 1));
    }
}

namespace {
template <typename Fn>
void updateItem(QHash<QString, QVector<MessageItem>>& store, QVector<MessageItem>& visible,
                const QHash<QString, int>& activeRowById, const QString& currentConv,
                const QString& convId, const QString& msgId, Fn fn, MessageModel* model, const QList<int>& roles) {
    if (currentConv != convId) {
        auto it = store.find(convId);
        if (it == store.end()) return;
        for (auto& m : *it) {
            if (m.id == msgId) { fn(m); break; }
        }
        return;
    }
    const int row = activeRowById.value(msgId, -1);
    if (row >= 0 && row < visible.size()) {
        fn(visible[row]);
        emit model->dataChanged(model->index(row), model->index(row), roles);
        if (roles.contains(MessageModel::MediaUrlRole) || roles.contains(MessageModel::IsRecalledRole))
            model->refreshRunsAround(row);
    }
}
}

void MessageModel::setReaction(const QString& convId, const QString& msgId, const QString& summary) {
    updateItem(m_store, m_items, m_activeRowById, m_currentConvId, convId, msgId,
               [&](MessageItem& m) { m.reaction = summary; }, this, {ReactionRole});
}

void MessageModel::markRecalled(const QString& convId, const QString& msgId, const QString& placeholder) {
    ++m_richTextRev;
    m_richTextLru.clear();
    updateItem(m_store, m_items, m_activeRowById, m_currentConvId, convId, msgId, [&](MessageItem& m) {
        m.isRecalled = true;
        m.text = placeholder;
        m.messageType = "text";
        m.mediaUrl.clear();
        m.reaction.clear();
    }, this, {IsRecalledRole, TextRole, RichTextRole, MessageTypeRole, MediaUrlRole, ReactionRole});
}

void MessageModel::setMedia(const QString& convId, const QString& msgId, const QString& mediaUrl) {
    updateItem(m_store, m_items, m_activeRowById, m_currentConvId, convId, msgId,
               [&](MessageItem& m) { m.mediaUrl = mediaUrl; }, this, {MediaUrlRole});
}

void MessageModel::setSticker(const QString& convId, const QString& msgId, const QVariantMap& info) {
    updateItem(m_store, m_items, m_activeRowById, m_currentConvId, convId, msgId, [&](MessageItem& m) {
        m.mediaUrl = info.value("url").toString();
        m.extra.insert("sticker", info);
    }, this, {MediaUrlRole, ExtraRole});
}

void MessageModel::setMessageExtra(const QString& convId, const QString& msgId, const QString& text, const QVariantMap& extra) {
    ++m_richTextRev;
    m_richTextLru.clear();
    updateItem(m_store, m_items, m_activeRowById, m_currentConvId, convId, msgId, [&](MessageItem& m) {
        if (!text.isEmpty()) m.text = text;
        for (auto it = extra.cbegin(); it != extra.cend(); ++it) {
            m.extra.insert(it.key(), it.value());
        }
    }, this, {TextRole, RichTextRole, ExtraRole});
}

void MessageModel::setStatus(const QString& convId, const QString& msgId, const QString& status) {
    updateItem(m_store, m_items, m_activeRowById, m_currentConvId, convId, msgId,
               [&](MessageItem& m) { m.status = status; }, this, {StatusRole});
}

void MessageModel::markSeenUpTo(const QString& convId, const QString& msgId) {
    markOwnUpTo(convId, msgId, QStringLiteral("seen"));
}

void MessageModel::recordGroupSeen(const QString& convId, const QString& msgId, const QString& userId) {
    if (userId.isEmpty()) return;
    updateItem(m_store, m_items, m_activeRowById, m_currentConvId, convId, msgId, [&](MessageItem& m) {
        if (!m.seenUids.contains(userId)) {
            m.seenUids.append(userId);
            m.status = QStringLiteral("seen");
        }
    }, this, {SeenUidsRole, StatusRole});
}

void MessageModel::markDeliveredUpTo(const QString& convId, const QString& msgId) {
    markOwnUpTo(convId, msgId, QStringLiteral("delivered"));
}

void MessageModel::markOwnUpTo(const QString& convId, const QString& msgId, const QString& status) {
    const bool active = !convId.isEmpty() && convId == m_currentConvId;
    if (!active && !m_store.contains(convId)) return;
    QVector<MessageItem>& thread = threadFor(convId);
    qint64 limit = -1;
    for (const auto& m : std::as_const(thread)) {
        if (m.id == msgId) { limit = m.ts; break; }
    }
    if (limit < 0 && !thread.isEmpty()) limit = std::as_const(thread).last().ts;
    const bool delivered = status == QLatin1String("delivered");
    int first = -1;
    int last = -1;
    for (int i = 0; i < thread.size(); ++i) {
        const MessageItem& m = std::as_const(thread)[i];
        if (!m.isMine || m.ts > limit || m.status == QLatin1String("seen") || m.id.startsWith(QLatin1String("local-"))) continue;
        if (delivered && m.status == QLatin1String("delivered")) continue;
        thread[i].status = status;
        if (first < 0) first = i;
        last = i;
    }
    if (active && first >= 0) emit dataChanged(index(first), index(last), {StatusRole});
}

bool MessageModel::contains(const QString& convId, const QString& msgId) const {
    if (!convId.isEmpty() && convId == m_currentConvId) return m_activeRowById.contains(msgId);
    const auto& thread = threadOf(convId);
    return std::any_of(thread.cbegin(), thread.cend(), [&](const MessageItem& m) { return m.id == msgId; });
}

qint64 MessageModel::oldestTimestamp(const QString& convId) const {
    qint64 oldest = 0;
    for (const auto& m : threadOf(convId)) {
        if (m.ts > 0 && (oldest == 0 || m.ts < oldest)) oldest = m.ts;
    }
    return oldest;
}

QString MessageModel::oldestMessageId(const QString& convId) const {
    for (const auto& m : threadOf(convId)) {
        bool numeric = false;
        m.id.toULongLong(&numeric);
        if (numeric) return m.id;
    }
    return {};
}

QString MessageModel::latestMessageId(const QString& convId) const {
    const auto& thread = threadOf(convId);
    for (auto it = thread.crbegin(); it != thread.crend(); ++it) {
        bool numeric = false;
        it->id.toULongLong(&numeric);
        if (numeric) return it->id;
    }
    return {};
}

MessageItem MessageModel::message(const QString& convId, const QString& msgId) const {
    if (!convId.isEmpty() && convId == m_currentConvId) {
        const int row = m_activeRowById.value(msgId, -1);
        return (row >= 0 && row < m_items.size()) ? m_items.at(row) : MessageItem{};
    }
    for (const auto& m : threadOf(convId)) {
        if (m.id == msgId) return m;
    }
    return {};
}

int MessageModel::countIncomingAfter(const QString& convId, const QString& readPoint) const {
    bool ok = false;
    const qulonglong point = readPoint.toULongLong(&ok);
    if (!ok) return 0;
    int count = 0;
    for (const auto& m : threadOf(convId)) {
        bool numeric = false;
        const qulonglong id = m.id.toULongLong(&numeric);
        if (numeric && id > point && !m.isMine && !m.isRecalled) ++count;
    }
    return count;
}

bool MessageModel::hasLocalEcho(const QString& convId) const {
    const auto& thread = threadOf(convId);
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
    if (m_currentConvId != convId) {
        auto it = m_store.find(convId);
        if (it != m_store.end()) {
            it->erase(std::remove_if(it->begin(), it->end(), older), it->end());
        }
        return;
    }
    beginResetModel();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), older), m_items.end());
    recomputeLastMineRow();
    rebuildActiveIndex();
    endResetModel();
}

void MessageModel::clearThread(const QString& convId) {
    m_store.remove(convId);
    if (m_currentConvId != convId) return;
    beginResetModel();
    m_items.clear();
    m_activeRowById.clear();
    m_activeRowByCliId.clear();
    m_lastMineRow = -1;
    m_richTextLru.clear();
    endResetModel();
}

void MessageModel::trimThreadWindow(const QString& convId, int maxItems) {
    if (convId.isEmpty() || maxItems <= 0 || convId == m_currentConvId) return;
    auto it = m_store.find(convId);
    if (it == m_store.end() || it->size() <= maxItems) return;
    const int drop = int(it->size()) - maxItems;
    it->erase(it->begin(), it->begin() + drop);
}

void MessageModel::clearAll() {
    beginResetModel();
    m_items.clear();
    m_activeRowById.clear();
    m_activeRowByCliId.clear();
    m_store.clear();
    m_currentConvId.clear();
    m_unreadMarkerId.clear();
    m_pendingUnread = 0;
    m_lastMineRow = -1;
    ++m_richTextRev;
    m_richTextLru.clear();
    endResetModel();
}

void MessageModel::loadConversation(const QString& conversationId) {
    beginResetModel();
    m_unreadMarkerId.clear();
    m_pendingUnread = 0;
    m_richTextLru.clear();
    if (conversationId != m_currentConvId) {
        if (!m_currentConvId.isEmpty()) {
            if (!m_items.isEmpty()) m_store.insert(m_currentConvId, std::move(m_items));
            else m_store.remove(m_currentConvId);
        }
        m_currentConvId = conversationId;
        m_items = conversationId.isEmpty() ? QVector<MessageItem>{} : m_store.take(conversationId);
    }
    recomputeLastMineRow();
    rebuildActiveIndex();
    endResetModel();
}

QVariantMap MessageModel::getMessageById(const QString& msgId) const {
    auto toMap = [](const MessageItem& item) -> QVariantMap {
        return {
            {"id", item.id},
            {"senderName", item.senderName},
            {"text", item.text},
            {"time", item.timeStr},
            {"isMine", item.isMine},
            {"messageType", item.messageType},
            {"mediaUrl", item.mediaUrl},
            {"href", item.href},
            {"ts", item.ts},
            {"senderId", item.senderId}
        };
    };
    const int activeRow = m_activeRowById.value(msgId, -1);
    if (activeRow >= 0 && activeRow < m_items.size()) {
        return toMap(m_items.at(activeRow));
    }
    for (auto it = m_store.constBegin(); it != m_store.constEnd(); ++it) {
        for (const auto& item : it.value()) {
            if (item.id == msgId) return toMap(item);
        }
    }
    return {};
}

namespace {
bool isEmoticonStartBoundary(QChar c) {
    if (c.isSpace()) return true;
    switch (c.unicode()) {
    case '(': case '[': case '{': case '"': case '\'': case '`': case '>': return true;
    default: return false;
    }
}

bool isEmoticonEndBoundary(QChar c) {
    if (c.isSpace()) return true;
    switch (c.unicode()) {
    case ')': case ']': case '}': case '"': case '\'': case '`':
    case '!': case '?': case '.': case ',': case ';': case ':': case '<': return true;
    default: return false;
    }
}

QString convertEmoticons(const QString& text) {
    if (text.isEmpty()) return text;
    struct EmoticonRule {
        const char* code;
        const char* emoji;
    };
    static constexpr EmoticonRule rules[] = {
        {"/-showlove", "🤟"}, {"/-thanks", "🙏"},   {":handclap", "👏"},
        {"/-coffee", "☕"},   {"/-heart", "❤️"},    {"/-strong", "👍"},
        {"/-break", "💔"},    {"/-punch", "👊"},    {"/-share", "🤝"},
        {"/-loveu", "🫶"},    {"/-rose", "🌹"},     {"/-fade", "🥀"},
        {"/-bome", "💣"},     {"/-cake", "🍰"},     {"/-shit", "💩"},
        {"/-weak", "👎"},     {"/-flag", "🚩"},     {"/-beer", "🍺"},
        {":-bye", "🙋"},      {":-dig", "🤨"},      {":wipe", "😅"},
        {":-((", "😭"},       {":)))", "😆"},       {"=)))", "🤣"},
        {"$ -)", "🤩"},       {"$-)", "🤩"},        {"/--|", "😐"},
        {":--|", "😑"},       {"/-bd", "🎂"},       {"/-li", "⚡"},
        {"/-ok", "👌"},       {"/-no", "🙅"},       {"/-bad", "👎"},
        {"/-jj", "🎉"},       {"_()_", "🙇"},       {"8*)", "🤑"},
        {":-D", "😀"},        {":-P", "😛"},        {":-p", "😛"},
        {":-O", "😮"},        {":-o", "🤯"},        {";-)", "😉"},
        {":-)", "🙂"},        {":-(", "🙁"},        {":-*", "😘"},
        {":-|", "😐"},        {":-/", "😕"},        {":-h", "👋"},
        {":-f", "😪"},        {":-l", "😔"},        {":-r", "😕"},
        {":-<", "🥺"},        {";-d", "🤪"},        {";-/", "😖"},
        {";-x", "🤬"},        {";-s", "🤢"},        {";-a", "🥳"},
        {";-!", "🤕"},        {";xx", "😵"},        {"x-)", "😆"},
        {"8-)", "😎"},        {"B-)", "😎"},        {"b-)", "😎"},
        {"p-(", "😩"},        {"&-(", "😠"},        {">-|", "🙄"},
        {"|-)", "🥱"},        {":v:", "✌️"},        {"=))", "🤣"},
        {":))", "🤣"},        {":((", "😭"},        {":')", "😂"},
        {":D", "😀"},         {":d", "😄"},         {":P", "😛"},
        {":p", "😜"},         {":O", "😮"},         {":o", "😲"},
        {";)", "😉"},         {":)", "😊"},         {":(", "🙁"},
        {":*", "😚"},         {":|", "😐"},         {":/", "😕"},
        {":3", "😗"},         {":b", "😛"},         {";d", "😜"},
        {":~", "🥴"},         {":>", "😏"},         {";p", "😝"},
        {";o", "😮"},         {":$", "😳"},         {":q", "😣"},
        {":z", "😴"},         {":Z", "😴"},         {":l", "😒"},
        {":x", "🤐"},         {":!", "😱"},         {"8*", "🤩"},
        {":t", "😤"},         {";?", "🤔"},         {";g", "😬"},
        {";!", "😰"},         {";f", "🥵"},         {":;", "🥶"},
        {"/-v", "✌️"},        {"<3", "❤️"},         {":v", "ICON_PACMAN_PLACEHOLDER"},
        {":V", "ICON_PACMAN_PLACEHOLDER"},
    };
    struct CompiledRule {
        QLatin1String code;
        QString emoji;
        int len;
    };
    struct LookupTable {
        bool hasTrigger[128]{};
        QVector<CompiledRule> buckets[128];
        LookupTable() {
            for (const auto& r : rules) {
                const auto u = static_cast<unsigned char>(r.code[0]);
                const int len = int(std::strlen(r.code));
                hasTrigger[u] = true;
                buckets[u].append({QLatin1String(r.code, len), QString::fromUtf8(r.emoji), len});
            }
        }
    };
    static const LookupTable table;

    const int n = text.size();
    bool anyTrigger = false;
    for (int i = 0; i < n; ++i) {
        const char16_t u = text.at(i).unicode();
        if (u < 128 && table.hasTrigger[u]) {
            anyTrigger = true;
            break;
        }
    }
    if (!anyTrigger) return text;

    const QStringView view(text);
    QString res;
    res.reserve(n);
    int i = 0;
    while (i < n) {
        bool matched = false;
        const char16_t u = text.at(i).unicode();
        if (u < 128 && table.hasTrigger[u]) {
            const auto& bucket = table.buckets[u];
            for (const auto& r : bucket) {
                if (i + r.len <= n && view.mid(i, r.len).compare(r.code) == 0) {
                    const bool startOk = (i == 0 || isEmoticonStartBoundary(text.at(i - 1)));
                    const bool endOk = (i + r.len == n || isEmoticonEndBoundary(text.at(i + r.len)));
                    if (startOk && endOk) {
                        res += r.emoji;
                        i += r.len;
                        matched = true;
                        break;
                    }
                }
            }
        }
        if (!matched) {
            res += text.at(i);
            ++i;
        }
    }
    return res;
}
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
        out += convertEmoticons(text.mid(last, m.capturedStart() - last)).toHtmlEscaped();
        const QString href = link.startsWith(QLatin1String("www.")) ? "https://" + link : link;
        out += QStringLiteral("<a href=\"%1\">%2</a>").arg(href.toHtmlEscaped(), link.toHtmlEscaped());
        last = m.capturedStart() + link.size();
    }
    out += convertEmoticons(text.mid(last)).toHtmlEscaped();
    out.replace(QStringLiteral("ICON_PACMAN_PLACEHOLDER"),
                QStringLiteral("<img src=\"qrc:/Zalos/assets/icons/pacman.png\" width=\"18\" height=\"18\" style=\"vertical-align:middle;\"/>"));
    return out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

int MessageModel::rowOfMessage(const QString& msgId) const {
    const int row = activeRowOf(msgId);
    return (row >= 0 && row < m_items.size()) ? row : -1;
}

QVariantList MessageModel::mediaItems() const {
    QVariantList out;
    for (const auto& m : m_items) {
        if ((m.messageType != QLatin1String("image") && m.messageType != QLatin1String("video")) || m.isRecalled)
            continue;
        const QString animUrl = m.extra.value(QStringLiteral("animatedUrl")).toString();
        const QString full = !animUrl.isEmpty() ? animUrl : (m.href.isEmpty() ? m.mediaUrl : m.href);
        if (full.isEmpty()) continue;
        QString sName = m.senderName;
        if (sName.isEmpty() && !m.isMine && m_senderResolver && !m.senderId.isEmpty()
            && m.senderId != QLatin1String("0") && m.senderId != QLatin1String("system")) {
            sName = m_senderResolver(m.senderId).first;
        }
        out.append(QVariantMap{{"msgId", m.id}, {"url", full}, {"thumb", m.mediaUrl},
                               {"senderName", sName}, {"isMine", m.isMine}, {"ts", m.ts},
                               {"isVideo", m.messageType == QLatin1String("video")},
                               {"animated", !animUrl.isEmpty()}});
    }
    return out;
}

QList<int> MessageModel::searchMessages(const QString& query) const {
    QList<int> results;
    const QStringList terms = foldTerms(query);
    if (terms.isEmpty()) return results;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).isRecalled) continue;
        if (containsAllTerms(foldForSearch(m_items.at(i).text), terms)) results.append(i);
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
            const QString ind = active(from, QStringLiteral("ind_"));
            if (!ind.isEmpty()) {
                bool ok = false;
                const int n = ind.mid(4).toInt(&ok);
                if (ok && n > 0) {
                    const int level = qBound(1, n >= 10 ? (n + 5) / 10 : n, 5);
                    out += QString(level * 4, QChar(0x00A0));
                }
            }
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
        if (color.size() == 8) {
            const QStringView hex = QStringView(color).sliced(2, 6);
            const bool validHex = std::all_of(hex.cbegin(), hex.cend(), [](QChar c) {
                const ushort u = c.unicode();
                return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
            });
            if (validHex) css += QStringLiteral("color:#%1;").arg(hex);
        }
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

void MessageModel::refreshPollCards(const QString& convId) {
    const bool isActive = !convId.isEmpty() && convId == m_currentConvId;
    QVector<MessageItem>& thread = threadFor(convId);
    QSet<qint64> seen;
    for (int i = int(thread.size()) - 1; i >= 0; --i) {
        MessageItem& m = thread[i];
        if (m.messageType != QLatin1String("poll")) continue;
        const qint64 pollId = m.pollData.value(QStringLiteral("poll_id")).toLongLong();
        const bool card = pollId <= 0 || !seen.contains(pollId);
        if (pollId > 0) seen.insert(pollId);
        if (m.pollCard == card) continue;
        m.pollCard = card;
        if (isActive) emit dataChanged(index(i), index(i), {PollCardRole});
    }
}

void MessageModel::setPollData(qint64 pollId, const QVariantMap& data, const QString& convId) {
    if (pollId <= 0) return;
    auto matches = [pollId](const MessageItem& item) {
        return item.messageType == QLatin1String("poll") && item.pollData.value("poll_id").toLongLong() == pollId;
    };
    auto applyPoll = [&](MessageItem& item) {
        item.pollData = data;
        if (!data.value("question").toString().isEmpty()) {
            item.text = data.value("question").toString();
            ++m_richTextRev;
            m_richTextLru.clear();
        }
    };
    if (convId.isEmpty() || convId == m_currentConvId) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (!matches(m_items[i])) continue;
            applyPoll(m_items[i]);
            emit dataChanged(index(i), index(i), {PollDataRole, TextRole, RichTextRole});
        }
    }
    if (!convId.isEmpty()) {
        if (convId != m_currentConvId) {
            auto it = m_store.find(convId);
            if (it != m_store.end()) {
                for (MessageItem& item : *it) {
                    if (matches(item)) applyPoll(item);
                }
            }
        }
        return;
    }
    for (auto it = m_store.begin(); it != m_store.end(); ++it) {
        for (MessageItem& item : it.value()) {
            if (matches(item)) applyPoll(item);
        }
    }
}

QVariantList MessageModel::sharedItems(const QString& kind, int limit) const {
    static const QRegularExpression url(QStringLiteral(R"(https?://[^\s<>"']+)"));
    QVariantList out;
    for (int i = m_items.size() - 1; i >= 0 && out.size() < limit; --i) {
        const MessageItem& m = m_items.at(i);
        if (m.isRecalled) continue;
        QString sender = m.senderName;
        if (sender.isEmpty() && !m.isMine && m_senderResolver && !m.senderId.isEmpty()
            && m.senderId != QLatin1String("system")) {
            sender = m_senderResolver(m.senderId).first;
        }
        QVariantMap e{{"msgId", m.id}, {"senderName", sender}, {"ts", m.ts}};
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
