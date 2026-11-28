#include "message_model.h"

#include <QScopeGuard>
#include "text_fold.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
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

QString emoticonHtml(const QString& text, int emojiPx, int pacmanPx);
int pacmanPxForJumbo(int emojiPx);
int itemJumboEmojiSize(const MessageItem& item);

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
            if (index.row() == 0 || !drawsBubble(item)) return true;
            const auto& prev = m_items.at(index.row() - 1);
            return !drawsBubble(prev) || item.id == m_unreadMarkerId || prev.senderId != item.senderId
                   || prev.isMine != item.isMine || item.ts - prev.ts > kGroupBreakMs || localDay(prev) != localDay(item);
        }
        case LastInGroupRole: {
            int last = index.row();
            while (last + 1 < m_items.size() && joinsPrevious(last + 1)) ++last;
            if (last + 1 >= m_items.size() || !drawsBubble(m_items.at(last))) return true;
            const auto& next = m_items.at(last + 1);
            return !drawsBubble(next) || next.id == m_unreadMarkerId || next.senderId != item.senderId
                   || next.isMine != item.isMine || next.ts - item.ts > kGroupBreakMs || localDay(next) != localDay(item);
        }
        case DateSeparatorRole:
            return item.ts > 0 && (index.row() == 0
                   || localDay(m_items.at(index.row() - 1)) != localDay(item));
        case UnreadMarkerRole: return !m_unreadMarkerId.isEmpty() && item.id == m_unreadMarkerId;
        case ShowStatusRole:
            return item.isMine && index.row() == m_lastMineRow;
        case RichTextRole: {
            const int jumboPx = itemJumboEmojiSize(item);
            const QString key = item.id + QLatin1Char(':') + QString::number(m_richTextRev) + QLatin1Char(':')
                              + QString::number(item.text.size()) + QLatin1Char(':') + QString::number(jumboPx);
            if (const QString* cached = m_richTextLru.object(key)) {
                return *cached;
            }
            QString html;
            if (jumboPx > 0) {
                html = emoticonHtml(item.text, jumboPx, pacmanPxForJumbo(jumboPx));
            } else {
                const QVariantList styles = item.extra.value("styles").toList();
                html = !styles.isEmpty()
                    ? styledHtml(item.text, item.mentions, styles)
                    : (item.mentions.isEmpty() ? linkify(item.text) : linkify(item.text, item.mentions));
            }
            m_richTextLru.insert(key, new QString(html));
            return html;
        }
        case JumboEmojiSizeRole: return itemJumboEmojiSize(item);
        case CanReactRole: return (actionsFor(item) & ActionReact) != 0;
        case CanReplyRole: return (actionsFor(item) & ActionReply) != 0;
        case CanForwardRole: return (actionsFor(item) & ActionForward) != 0;
        case CanCopyRole: return (actionsFor(item) & ActionCopy) != 0;
        case HasMenuRole: return (actionsFor(item) & ActionMenu) != 0;
        case ExtraRole: return item.extra;
        case SeenUidsRole: return item.seenUids;
        case InMediaRunRole: return joinsPrevious(index.row());
        case MediaRunRole: {
            const int row = index.row();
            if (!isRunTile(row) || joinsPrevious(row) || row + 1 >= m_items.size() || !joinsPrevious(row + 1))
                return QVariantList{};
            QVariantList tiles;
            for (int r = row; r < m_items.size() && tiles.size() < 16 && (r == row || joinsPrevious(r)); ++r) {
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
        {JumboEmojiSizeRole, "jumboEmojiSize"},
        {MediaRunRole, "mediaRun"},
        {InMediaRunRole, "inMediaRun"},
        {CanReactRole, "canReact"},
        {CanReplyRole, "canReply"},
        {CanForwardRole, "canForward"},
        {CanCopyRole, "canCopy"},
        {HasMenuRole, "hasMenu"}
    };
}

bool MessageModel::isRunTile(int row) const {
    if (row < 0 || row >= m_items.size()) return false;
    const auto& m = m_items.at(row);
    if (m.isRecalled || !m.quoteText.isEmpty()) return false;
    if (!m_unreadMarkerId.isEmpty() && m.id == m_unreadMarkerId) return false;
    return m.messageType == QLatin1String("sticker") || (m.messageType == QLatin1String("image") && m.text.isEmpty());
}

bool MessageModel::runPairsWithPrevious(int idx) const {
    if (idx <= 0 || idx >= m_items.size() || !isRunTile(idx) || !isRunTile(idx - 1)) return false;
    const auto& a = m_items.at(idx - 1);
    const auto& b = m_items.at(idx);
    const QString layoutA = a.extra.value(QStringLiteral("groupLayoutId")).toString();
    const QString layoutB = b.extra.value(QStringLiteral("groupLayoutId")).toString();
    if (!layoutA.isEmpty() && !layoutB.isEmpty()) return layoutA == layoutB && a.senderId == b.senderId;
    return a.senderId == b.senderId && a.isMine == b.isMine && b.ts - a.ts <= kMediaRunGapMs && b.ts >= a.ts
           && localDay(a) == localDay(b);
}

bool MessageModel::drawsBubble(const MessageItem& item) {
    return item.messageType != QLatin1String("system") && item.messageType != QLatin1String("ecard")
           && item.messageType != QLatin1String("poll");
}

bool MessageModel::joinsPrevious(int row) const {
    if (!runPairsWithPrevious(row)) return false;
    return ((row - runStartOf(row)) % 16) != 0;
}

int MessageModel::runStartOf(int row) const {
    if (m_runStarts.size() < m_items.size()) {
        const int old = m_runStarts.size();
        m_runStarts.resize(m_items.size());
        std::fill(m_runStarts.begin() + old, m_runStarts.end(), -1);
    }
    int cur = row;
    int start = cur;
    for (;;) {
        if (m_runStarts[cur] >= 0) {
            start = m_runStarts[cur];
            break;
        }
        if (cur == 0 || !runPairsWithPrevious(cur)) {
            start = cur;
            break;
        }
        --cur;
    }
    for (int r = cur; r <= row; ++r) m_runStarts[r] = start;
    return start;
}

void MessageModel::resetRunStarts(int fromRow) {
    if (fromRow < m_runStarts.size()) m_runStarts.resize(qMax(0, fromRow));
}

void MessageModel::refreshRunsAround(int row) {
    if (m_items.isEmpty()) return;
    const int last = int(m_items.size()) - 1;
    int from = runStartOf(qBound(0, row, last));
    int to = qBound(0, row, last);
    while (to < last && runPairsWithPrevious(to + 1)) ++to;
    from = qMax(0, qMin(from, row - 1));
    to = qMin(last, qMax(to, row + 1));
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
    QHash<QString, int> localById;
    QHash<QString, int> localByCliId;
    if (!isActive) {
        for (int i = 0; i < baseSize; ++i) {
            const MessageItem& m = std::as_const(base)[i];
            localById.insert(m.id, i);
            if (!m.cliMsgId.isEmpty() && m.cliMsgId != QLatin1String("0")) localByCliId.insert(m.cliMsgId, i);
        }
    }
    QHash<QString, int>& byId = isActive ? m_activeRowById : localById;
    QHash<QString, int>& byCliId = isActive ? m_activeRowByCliId : localByCliId;
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
        int existingIdx = (!m.cliMsgId.isEmpty() && m.cliMsgId != QLatin1String("0") && byCliId.contains(m.cliMsgId))
                              ? byCliId.value(m.cliMsgId) : -1;
        if (existingIdx < 0 && m.messageType == QLatin1String("call") && !m.id.startsWith(QLatin1String("local-"))) {
            const int total = baseSize + appended.size();
            for (int i = total - 1; i >= 0; --i) {
                const MessageItem& cand = at(i);
                if (cand.messageType == QLatin1String("call")
                    && cand.id.startsWith(QLatin1String("local-call-"))
                    && qAbs(cand.ts - m.ts) <= 120000) {
                    existingIdx = i;
                    break;
                }
            }
        }
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
            if (isActive) resetRunStarts(existingIdx);
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
        resetRunStarts(0);
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
            resetRunStarts(0);
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
        resetRunStarts(0);
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
    resetRunStarts(0);
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
        resetRunStarts(0);
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
        resetRunStarts(row);
        m_activeRowById.remove(localId);
        m_activeRowById.insert(serverId, row);
        emit dataChanged(index(row), index(row), {IdRole, CanReactRole, CanReplyRole, CanForwardRole});
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
        resetRunStarts(row);
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
            if (m.id == msgId) {
                fn(m);
                m.jumboCache = -1;
                break;
            }
        }
        return;
    }
    const int row = activeRowById.value(msgId, -1);
    if (row >= 0 && row < visible.size()) {
        fn(visible[row]);
        visible[row].jumboCache = -1;
        model->resetRunStarts(row);
        emit model->dataChanged(model->index(row), model->index(row), roles);
        if (roles.contains(MessageModel::TextRole) || roles.contains(MessageModel::MessageTypeRole)
            || roles.contains(MessageModel::MediaUrlRole) || roles.contains(MessageModel::IsRecalledRole))
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
    }, this, {IsRecalledRole, TextRole, RichTextRole, JumboEmojiSizeRole, MessageTypeRole, MediaUrlRole, ReactionRole,
              CanReactRole, CanReplyRole, CanForwardRole, CanCopyRole, HasMenuRole});
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
    }, this, {TextRole, RichTextRole, JumboEmojiSizeRole, ExtraRole});
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
    resetRunStarts(0);
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), older), m_items.end());
    recomputeLastMineRow();
    rebuildActiveIndex();
    endResetModel();
}

void MessageModel::clearThread(const QString& convId) {
    m_store.remove(convId);
    if (m_currentConvId != convId) return;
    beginResetModel();
    resetRunStarts(0);
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
    resetRunStarts(0);
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
    resetRunStarts(0);
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

QString MessageModel::messageIdAt(int row) const {
    return data(index(row, 0), IdRole).toString();
}

int MessageModel::actionsFor(const MessageItem& item) {
    if (item.isRecalled) return 0;
    const QString& type = item.messageType;
    int actions = 0;
    if (type == QLatin1String("text") || type == QLatin1String("image") || type == QLatin1String("video")) {
        actions = ActionReact | ActionReply | ActionForward | ActionCopy | ActionMenu;
    } else if (type == QLatin1String("link")) {
        if (!item.extra.value(QStringLiteral("template")).toBool())
            actions = ActionReact | ActionReply | ActionForward | ActionCopy | ActionMenu;
    } else if (type == QLatin1String("file")) {
        actions = ActionReact | ActionReply | ActionForward | ActionMenu;
    } else if (type == QLatin1String("location")) {
        actions = ActionReply | ActionForward | ActionMenu;
    } else if (type == QLatin1String("sticker") || type == QLatin1String("voice") || type == QLatin1String("card")) {
        actions = ActionReply | ActionMenu;
    }
    if (item.id.isEmpty() || item.id.startsWith(QLatin1String("local-"))) actions &= ActionCopy | ActionMenu;
    return actions;
}

QVariantMap MessageModel::getMessageById(const QString& msgId) const {
    auto toMap = [](const MessageItem& item) -> QVariantMap {
        const int actions = actionsFor(item);
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
            {"senderId", item.senderId},
            {"actions", actions},
            {"canReact", (actions & ActionReact) != 0},
            {"canReply", (actions & ActionReply) != 0},
            {"canForward", (actions & ActionForward) != 0},
            {"canCopy", (actions & ActionCopy) != 0},
            {"hasMenu", (actions & ActionMenu) != 0}
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
bool isEmoticonStartBoundary(QChar c) { return c.isSpace(); }

bool isEmoticonEndBoundary(QChar c) { return c.isSpace(); }

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

QVector<MessageModel::LinkSpan> MessageModel::findLinks(const QString& text) {
    static const QRegularExpression url(
        QStringLiteral(R"((?:https?://|www\.)[^\s<>"']+|(?<![@\w./:-])(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+)"
                       R"((?:com|net|org|vn|me|io|co|tv|app|ly|gl|gg|link|info|biz|edu|gov|xyz|site|online|shop|store|)"
                       R"(dev|page|to|asia|top|ai|cc|so|be|us|uk|jp|kr|cn|watch|news|live|video|blog|club|fun|pro|world|tech|ws)(?![\w-])(?::\d{2,5})?(?:[/?#][^\s<>"']*)?)"),
        QRegularExpression::CaseInsensitiveOption);
    QVector<LinkSpan> out;
    if (!text.contains(QLatin1Char('.'))) return out;
    auto it = url.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        QString link = m.captured(0);
        while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
        if (link.isEmpty()) continue;
        const bool hasScheme = link.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
                               || link.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
        out.append({int(m.capturedStart()), int(link.size()), hasScheme ? link : QStringLiteral("https://") + link});
    }
    return out;
}

namespace {
constexpr int kSmallEmojiPx = 16;
constexpr int kSmallPacmanPx = 14;
constexpr int kNormalEmojiPx = 19;
constexpr int kNormalPacmanPx = 17;
constexpr int kLargeEmojiPx = 23;
constexpr int kLargePacmanPx = 20;

int pacmanPxForJumbo(int emojiPx) {
    if (emojiPx == 44) return 40;
    if (emojiPx == 36) return 33;
    return 29;
}

char32_t readCodePoint(const QString& s, int pos, int* step) {
    const QChar ch = s.at(pos);
    if (ch.isHighSurrogate() && pos + 1 < s.size() && s.at(pos + 1).isLowSurrogate()) {
        *step = 2;
        return QChar::surrogateToUcs4(ch, s.at(pos + 1));
    }
    *step = 1;
    return ch.unicode();
}

bool isEmojiBase(char32_t cp) {
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true;
    if (cp >= 0x1F170 && cp <= 0x1F251) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
    switch (cp) {
        case 0x203C:
        case 0x2049:
        case 0x231A:
        case 0x231B:
        case 0x2328:
        case 0x23CF:
        case 0x25AA:
        case 0x25AB:
        case 0x25B6:
        case 0x25C0:
        case 0x2934:
        case 0x2935:
        case 0x2B1B:
        case 0x2B1C:
        case 0x2B50:
        case 0x2B55:
        case 0x3030:
        case 0x303D:
        case 0x3297:
        case 0x3299:
            return true;
        default:
            break;
    }
    return (cp >= 0x23E9 && cp <= 0x23F3) || (cp >= 0x23F8 && cp <= 0x23FA)
           || (cp >= 0x25FB && cp <= 0x25FE) || (cp >= 0x2B05 && cp <= 0x2B07);
}

bool isRegionalIndicator(char32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

bool isEmojiClusterAt(const QString& s, int pos, int* nextPos) {
    if (pos < 0 || pos >= s.size()) return false;
    const int n = s.size();
    int step = 0;
    const char32_t first = readCodePoint(s, pos, &step);
    int cur = pos + step;

    if ((first >= U'0' && first <= U'9') || first == U'#' || first == U'*') {
        if (cur < n && s.at(cur).unicode() == 0xFE0F) ++cur;
        if (cur < n && s.at(cur).unicode() == 0x20E3) {
            if (nextPos) *nextPos = cur + 1;
            return true;
        }
        return false;
    }

    if (isRegionalIndicator(first)) {
        if (cur < n) {
            int s2 = 0;
            const char32_t second = readCodePoint(s, cur, &s2);
            if (isRegionalIndicator(second)) {
                if (nextPos) *nextPos = cur + s2;
                return true;
            }
        }
        return false;
    }

    if (!isEmojiBase(first)) return false;

    auto consumeModifiers = [&](int& idx) -> bool {
        while (idx < n) {
            int ms = 0;
            const char32_t mcp = readCodePoint(s, idx, &ms);
            if (mcp == 0xFE0E) return false;
            if (mcp == 0xFE0F || mcp == 0x20E3
                || (mcp >= 0x1F3FB && mcp <= 0x1F3FF)
                || (mcp >= 0xE0020 && mcp <= 0xE007F)) {
                idx += ms;
                continue;
            }
            break;
        }
        return true;
    };

    if (!consumeModifiers(cur)) return false;

    while (cur < n && s.at(cur).unicode() == 0x200D) {
        int probe = cur + 1;
        if (probe >= n) break;
        int js = 0;
        const char32_t nextBase = readCodePoint(s, probe, &js);
        if (!isEmojiBase(nextBase)) break;
        probe += js;
        if (!consumeModifiers(probe)) return false;
        cur = probe;
    }

    if (nextPos) *nextPos = cur;
    return true;
}

QString anchorHtml(const QString& href, const QString& label) {
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(href.toHtmlEscaped(), label.toHtmlEscaped());
}

QString emoticonHtml(const QString& text, int emojiPx, int pacmanPx) {
    static const QString kPacmanPlaceholder = QStringLiteral("ICON_PACMAN_PLACEHOLDER");
    const QString converted = convertEmoticons(text);
    const QStringView view(converted);
    const int n = converted.size();
    QString out;
    out.reserve(n + 32);
    int i = 0;
    while (i < n) {
        if (view.sliced(i).startsWith(kPacmanPlaceholder)) {
            out += QStringLiteral("<img src=\"image://icon/pacman/%1\" width=\"%1\" height=\"%1\" style=\"vertical-align:middle;\"/>")
                       .arg(pacmanPx);
            i += kPacmanPlaceholder.size();
            continue;
        }
        int nextEmoji = 0;
        if (isEmojiClusterAt(converted, i, &nextEmoji)) {
            int end = nextEmoji;
            while (end < n && !view.sliced(end).startsWith(kPacmanPlaceholder)
                   && isEmojiClusterAt(converted, end, &nextEmoji)) {
                end = nextEmoji;
            }
            out += QStringLiteral("<span style=\"font-size:%1px;\">%2</span>")
                       .arg(emojiPx)
                       .arg(converted.mid(i, end - i));
            i = end;
            continue;
        }
        const QChar ch = converted.at(i);
        if (ch == QLatin1Char('\n')) out += QStringLiteral("<br>");
        else if (ch == QLatin1Char('&')) out += QStringLiteral("&amp;");
        else if (ch == QLatin1Char('<')) out += QStringLiteral("&lt;");
        else if (ch == QLatin1Char('>')) out += QStringLiteral("&gt;");
        else if (ch == QLatin1Char('"')) out += QStringLiteral("&quot;");
        else out += ch;
        ++i;
    }
    return out;
}

int computeJumboEmojiSize(const MessageItem& item);

int itemJumboEmojiSize(const MessageItem& item) {
    if (item.jumboCache < 0) item.jumboCache = qint16(computeJumboEmojiSize(item));
    return item.jumboCache;
}

int computeJumboEmojiSize(const MessageItem& item) {
    if (item.messageType != QLatin1String("text") || item.isRecalled
        || !item.quoteMsgId.isEmpty() || !item.quoteText.isEmpty()
        || !item.mentions.isEmpty()
        || !item.extra.value(QStringLiteral("styles")).toList().isEmpty()) {
        return 0;
    }
    return MessageModel::jumboEmojiSizeFor(item.text);
}
}

int MessageModel::jumboEmojiSizeFor(const QString& text) {
    if (text.isEmpty()) return 0;
    static const QString kPacmanPlaceholder = QStringLiteral("ICON_PACMAN_PLACEHOLDER");
    const QString converted = convertEmoticons(text);
    const QStringView view(converted);
    const int n = converted.size();
    int count = 0;
    int i = 0;
    while (i < n) {
        if (converted.at(i).isSpace()) {
            ++i;
            continue;
        }
        if (view.sliced(i).startsWith(kPacmanPlaceholder)) {
            ++count;
            i += kPacmanPlaceholder.size();
        } else {
            int nextPos = 0;
            if (!isEmojiClusterAt(converted, i, &nextPos)) return 0;
            ++count;
            i = nextPos;
        }
        if (count > 3) return 0;
    }
    if (count == 1) return 44;
    if (count == 2) return 36;
    if (count == 3) return 32;
    return 0;
}

QString MessageModel::linkifyPlain(const QString& text) {
    QString out;
    int last = 0;
    for (const LinkSpan& span : findLinks(text)) {
        out += text.mid(last, span.start - last).toHtmlEscaped();
        out += anchorHtml(span.href, text.mid(span.start, span.length));
        last = span.start + span.length;
    }
    out += text.mid(last).toHtmlEscaped();
    return out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QString MessageModel::linkify(const QString& text) {
    QString out;
    int last = 0;
    for (const LinkSpan& span : findLinks(text)) {
        out += emoticonHtml(text.mid(last, span.start - last), kNormalEmojiPx, kNormalPacmanPx);
        out += anchorHtml(span.href, text.mid(span.start, span.length));
        last = span.start + span.length;
    }
    out += emoticonHtml(text.mid(last), kNormalEmojiPx, kNormalPacmanPx);
    return out;
}

int MessageModel::rowOfMessage(const QString& msgId) const {
    const int row = activeRowOf(msgId);
    return (row >= 0 && row < m_items.size()) ? row : -1;
}

namespace {

bool isPlayableVideoFile(const MessageItem& m) {
    if (m.messageType != QLatin1String("file")) return false;
    auto hasVideoExt = [](const QString& nameOrPath) {
        static const QStringList kExts{
            QStringLiteral(".mp4"), QStringLiteral(".mov"), QStringLiteral(".webm"),
            QStringLiteral(".mkv"), QStringLiteral(".m4v")
        };
        const QString clean = nameOrPath.trimmed();
        for (const QString& ext : kExts) {
            if (clean.endsWith(ext, Qt::CaseInsensitive)) return true;
        }
        return false;
    };
    if (hasVideoExt(m.text)) return true;
    const QString urlPath = QUrl(m.href.isEmpty() ? m.mediaUrl : m.href).path();
    return hasVideoExt(urlPath);
}

}

QVariantList MessageModel::mediaItems() const {
    QVariantList out;
    for (const auto& m : m_items) {
        const bool isVideoFile = isPlayableVideoFile(m);
        if ((m.messageType != QLatin1String("image") && m.messageType != QLatin1String("video") && !isVideoFile)
            || m.isRecalled) {
            continue;
        }
        const QString animUrl = m.extra.value(QStringLiteral("animatedUrl")).toString();
        const QString full = !animUrl.isEmpty() ? animUrl : (m.href.isEmpty() ? m.mediaUrl : m.href);
        if (full.isEmpty()) continue;
        QString sName = m.senderName;
        if (sName.isEmpty() && !m.isMine && m_senderResolver && !m.senderId.isEmpty()
            && m.senderId != QLatin1String("0") && m.senderId != QLatin1String("system")) {
            sName = m_senderResolver(m.senderId).first;
        }
        out.append(QVariantMap{{"msgId", m.id}, {"url", full}, {"thumb", isVideoFile ? QString() : m.mediaUrl},
                               {"senderName", sName}, {"isMine", m.isMine}, {"ts", m.ts},
                               {"isVideo", m.messageType == QLatin1String("video") || isVideoFile},
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
    const QVector<LinkSpan> links = findLinks(text);
    for (const LinkSpan& span : links) cuts << span.start << span.start + span.length;
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
        const QString size = active(from, QStringLiteral("f_"));
        const int emojiPx = size == QLatin1String("f_13") ? kSmallEmojiPx
                          : (size == QLatin1String("f_18") ? kLargeEmojiPx : kNormalEmojiPx);
        const int pacmanPx = size == QLatin1String("f_13") ? kSmallPacmanPx
                           : (size == QLatin1String("f_18") ? kLargePacmanPx : kNormalPacmanPx);
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
            const LinkSpan* link = nullptr;
            for (const LinkSpan& span : links) {
                if (from >= span.start && to <= span.start + span.length) { link = &span; break; }
            }
            html = link ? anchorHtml(link->href, piece) : emoticonHtml(piece, emojiPx, pacmanPx);
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
            resetRunStarts(i);
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

QString MessageModel::internMessageType(QStringView type) {
    static const QString kText = QStringLiteral("text");
    static const QString kImage = QStringLiteral("image");
    static const QString kFile = QStringLiteral("file");
    static const QString kSticker = QStringLiteral("sticker");
    static const QString kVoice = QStringLiteral("voice");
    static const QString kVideo = QStringLiteral("video");
    static const QString kLocation = QStringLiteral("location");
    static const QString kEcard = QStringLiteral("ecard");
    static const QString kSystem = QStringLiteral("system");
    static const QString kLink = QStringLiteral("link");
    static const QString kBankcard = QStringLiteral("bankcard");
    static const QString kCall = QStringLiteral("call");
    static const QString kCard = QStringLiteral("card");
    static const QString kPoll = QStringLiteral("poll");
    if (type.isEmpty() || type == kText) return kText;
    if (type == kImage) return kImage;
    if (type == kFile) return kFile;
    if (type == kSticker) return kSticker;
    if (type == kVoice) return kVoice;
    if (type == kVideo) return kVideo;
    if (type == kLocation) return kLocation;
    if (type == kEcard) return kEcard;
    if (type == kSystem) return kSystem;
    if (type == kLink) return kLink;
    if (type == kBankcard) return kBankcard;
    if (type == kCall) return kCall;
    if (type == kCard) return kCard;
    if (type == kPoll) return kPoll;
    return type.toString();
}

QString MessageModel::internStatus(QStringView status) {
    if (status.isEmpty()) return {};
    static const QString kSent = QStringLiteral("sent");
    static const QString kDelivered = QStringLiteral("delivered");
    static const QString kSeen = QStringLiteral("seen");
    static const QString kSending = QStringLiteral("sending");
    static const QString kFailed = QStringLiteral("failed");
    if (status == kSent) return kSent;
    if (status == kDelivered) return kDelivered;
    if (status == kSeen) return kSeen;
    if (status == kSending) return kSending;
    if (status == kFailed) return kFailed;
    return status.toString();
}

}
