#include "conversation_model.h"
#include "text_fold.h"
#include <algorithm>

namespace Zalos::Core::Models {

ConversationModel::ConversationModel(QObject* parent)
    : QAbstractListModel(parent) {
    clearAll();
}

int ConversationModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_filteredItems.size();
}

QVariant ConversationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filteredItems.size()) {
        return {};
    }

    const auto& item = m_filteredItems.at(index.row());
    switch (role) {
        case IdRole: return item.id;
        case NameRole: return item.name;
        case LastMessageRole: return item.lastMessage;
        case TimestampRole: return item.timestampStr;
        case UnreadCountRole: return item.unreadCount;
        case IsOnlineRole: return item.isOnline;
        case IsPinnedRole: return item.isPinned;
        case IsMutedRole: return item.isMuted;
        case AvatarUrlRole: return item.avatarUrl;
        case CategoryRole: return item.category;
        case SenderPrefixRole: return item.senderPrefix;
        case MemberCountRole: return item.memberCount;
        case TagRole: return item.tag;
        case TagColorRole: return item.tagColor;
        case LabelIdRole: return item.labelId;
        case IsGroupRole: return item.isGroup;
        case LastTsRole: return item.lastTs;
        case IsTypingRole: return item.isTyping;
        case DraftRole: return m_drafts.value(item.id);
        case MemberAvatarsRole: return m_memberAvatars.value(item.id);
        case HasUnreadMentionRole: return item.hasUnreadMention;
        case IsHiddenRole: return m_hiddenIds.contains(item.id);
        default: return {};
    }
}

QHash<int, QByteArray> ConversationModel::roleNames() const {
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {LastMessageRole, "lastMessage"},
        {TimestampRole, "timestamp"},
        {UnreadCountRole, "unreadCount"},
        {IsOnlineRole, "isOnline"},
        {IsPinnedRole, "isPinned"},
        {IsMutedRole, "isMuted"},
        {AvatarUrlRole, "avatarUrl"},
        {CategoryRole, "category"},
        {SenderPrefixRole, "senderPrefix"},
        {MemberCountRole, "memberCount"},
        {TagRole, "tag"},
        {TagColorRole, "tagColor"},
        {LabelIdRole, "labelId"},
        {IsGroupRole, "isGroup"},
        {LastTsRole, "lastTs"},
        {IsTypingRole, "isTyping"},
        {DraftRole, "draft"},
        {MemberAvatarsRole, "memberAvatars"},
        {HasUnreadMentionRole, "hasUnreadMention"},
        {IsHiddenRole, "isHidden"}
    };
}

void ConversationModel::setCategoryFilter(const QString& category) {
    if (m_activeCategory != category) {
        m_activeCategory = category;
        applyFilter();
        emit filterChanged();
    }
}

void ConversationModel::setSearchFilter(const QString& query) {
    if (m_searchQuery != query) {
        m_searchQuery = query;
        applyFilter();
        emit filterChanged();
    }
}

void ConversationModel::beginBatch() {
    ++m_batchDepth;
}

void ConversationModel::endBatch() {
    if (m_batchDepth == 0 || --m_batchDepth > 0) return;
    if (m_filterDirty) applyFilter();
    if (m_unreadDirty) emitUnreadChanged();
}

void ConversationModel::emitUnreadChanged() {
    if (m_batchDepth > 0) {
        m_unreadDirty = true;
        return;
    }
    m_unreadDirty = false;
    emit unreadCountChanged(totalUnreadCount(), hasAnyUnreadMention());
}

void ConversationModel::applyFilter() {
    if (m_batchDepth > 0) {
        m_filterDirty = true;
        return;
    }
    m_filterDirty = false;
    beginResetModel();
    m_filteredItems.clear();

    const QStringList terms = foldTerms(m_searchQuery);
    const bool searching = !terms.isEmpty();
    const bool archivedView = m_viewMode == QLatin1String("archived");
    const bool directView = m_viewMode == QLatin1String("direct");
    for (const auto& item : m_allItems) {
        const bool isArchived = m_archivedIds.contains(item.id);
        const bool isHidden = m_hiddenIds.contains(item.id);
        if (isHidden && (!searching || m_hiddenPinReveal)) continue;
        if (archivedView) {
            if (!isArchived) continue;
        } else if (directView) {
            if (isArchived) continue;
            if (item.isGroup && !item.hasUnreadMention) continue;
            if (!searching) {
                if (m_labelFilter > 0 && item.labelId != m_labelFilter) continue;
                if (m_strangerOnly && !strangerId(item.id, item.isGroup)) continue;
            }
        } else if (!searching) {
            if (isArchived) continue;
            if (m_labelFilter > 0 && item.labelId != m_labelFilter) continue;
            if (m_strangerOnly && !strangerId(item.id, item.isGroup)) continue;
            if (!(m_activeCategory.isEmpty() ||
                  (m_activeCategory == "unread" ? item.unreadCount > 0 : item.category == m_activeCategory)))
                continue;
        }
        const QString shownName = !item.name.isEmpty() ? item.name
                                  : (item.isGroup ? QStringLiteral("Nhóm chưa đặt tên Unnamed group")
                                                  : QStringLiteral("Người dùng Zalo Zalo user"));
        if (searching) {
            const QString haystack = isHidden ? foldForSearch(shownName)
                                              : foldForSearch(shownName) + QLatin1Char(' ') + foldForSearch(item.lastMessage);
            if (!containsAllTerms(haystack, terms)) continue;
        }
        m_filteredItems.append(item);
    }

    std::stable_sort(m_filteredItems.begin(), m_filteredItems.end(), [](const ConversationItem& a, const ConversationItem& b) {
        if (a.isPinned != b.isPinned) {
            return a.isPinned > b.isPinned;
        }
        return a.lastTs > b.lastTs;
    });

    endResetModel();
}

void ConversationModel::stampLabel(ConversationItem& item) const {
    const auto it = m_labels.constFind(item.id);
    if (it == m_labels.cend()) return;
    item.labelId = it->value("id").toInt();
    item.tag = it->value("text").toString();
    item.tagColor = it->value("color").toString();
}

void ConversationModel::addConversation(const ConversationItem& item) {
    m_allItems.append(item);
    stampLabel(m_allItems.last());
    applyFilter();
    emitUnreadChanged();
}

void ConversationModel::upsertConversation(const ConversationItem& item) {
    for (auto& existing : m_allItems) {
        if (existing.id == item.id) {
            existing = item;
            stampLabel(existing);
            applyFilter();
            emitUnreadChanged();
            return;
        }
    }
    addConversation(item);
}

void ConversationModel::setConversations(const QVector<ConversationItem>& items) {
    m_allItems = items;
    for (auto& item : m_allItems) stampLabel(item);
    applyFilter();
    emitUnreadChanged();
}

bool ConversationModel::contains(const QString& convId) const {
    return std::any_of(m_allItems.cbegin(), m_allItems.cend(), [&](const ConversationItem& c) { return c.id == convId; });
}

ConversationItem ConversationModel::conversation(const QString& convId) const {
    for (const auto& item : m_allItems) {
        if (item.id == convId) return item;
    }
    return {};
}

void ConversationModel::updateProfile(const QString& convId, const QString& name, const QString& avatarUrl, int memberCount) {
    bool changed = false;
    for (auto& item : m_allItems) {
        if (item.id != convId) continue;
        if (!name.isEmpty() && item.name != name) { item.name = name; changed = true; }
        if (!avatarUrl.isEmpty() && item.avatarUrl != avatarUrl) { item.avatarUrl = avatarUrl; changed = true; }
        if (memberCount >= 0 && item.memberCount != memberCount) { item.memberCount = memberCount; changed = true; }
        break;
    }
    if (!changed) return;
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        if (m_filteredItems[row].id != convId) continue;
        m_filteredItems[row] = conversation(convId);
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {NameRole, AvatarUrlRole, MemberCountRole});
        break;
    }
}

void ConversationModel::recordMessage(const QString& convId, const QString& preview, const QString& senderPrefix,
                                      qint64 ts, bool incrementUnread, bool hasMention) {
    for (auto& item : m_allItems) {
        if (item.id != convId) continue;
        if (ts >= item.lastTs) {
            item.lastMessage = preview;
            item.senderPrefix = senderPrefix;
            item.lastTs = ts;
        }
        if (incrementUnread) {
            item.unreadCount++;
            if (hasMention) item.hasUnreadMention = true;
        }
        applyFilter();
        if (incrementUnread) emitUnreadChanged();
        return;
    }
}

void ConversationModel::setPinned(const QString& convId, bool pinned) {
    for (auto& item : m_allItems) {
        if (item.id == convId && item.isPinned != pinned) {
            item.isPinned = pinned;
            applyFilter();
            return;
        }
    }
}

void ConversationModel::setMuted(const QString& convId, bool muted) {
    for (auto& item : m_allItems) {
        if (item.id == convId && item.isMuted != muted) {
            item.isMuted = muted;
            applyFilter();
            return;
        }
    }
}

namespace {
template <typename Fn>
void patchRows(QVector<ConversationItem>& all, QVector<ConversationItem>& filtered, ConversationModel* model,
               const QList<int>& roles, Fn fn) {
    for (auto& item : all) fn(item);
    for (int row = 0; row < filtered.size(); ++row) {
        ConversationItem before = filtered[row];
        fn(filtered[row]);
        if (before.tag != filtered[row].tag || before.tagColor != filtered[row].tagColor ||
            before.isOnline != filtered[row].isOnline || before.isTyping != filtered[row].isTyping) {
            emit model->dataChanged(model->index(row), model->index(row), roles);
        }
    }
}
}

void ConversationModel::setTag(const QString& convId, const QString& tag, const QString& color) {
    patchRows(m_allItems, m_filteredItems, this, {TagRole, TagColorRole}, [&](ConversationItem& item) {
        if (item.id == convId) { item.tag = tag; item.tagColor = color; }
    });
}

void ConversationModel::setLabelFilter(int labelId) {
    if (labelId == m_labelFilter) return;
    m_labelFilter = labelId;
    applyFilter();
    emit filterChanged();
}

void ConversationModel::setViewMode(const QString& mode) {
    const QString next = mode.isEmpty() ? QStringLiteral("normal") : mode;
    if (next == m_viewMode) return;
    m_viewMode = next;
    applyFilter();
    emit filterChanged();
}

void ConversationModel::setHiddenThreads(const QSet<QString>& ids) {
    if (ids == m_hiddenIds) return;
    m_hiddenIds = ids;
    applyFilter();
    emitUnreadChanged();
    emit filterChanged();
}

void ConversationModel::setHiddenPinReveal(bool reveal) {
    if (reveal == m_hiddenPinReveal) return;
    m_hiddenPinReveal = reveal;
    applyFilter();
}

void ConversationModel::setArchivedThreads(const QSet<QString>& ids) {
    if (ids == m_archivedIds) return;
    m_archivedIds = ids;
    applyFilter();
    emit filterChanged();
}

void ConversationModel::setStrangerOnly(bool on) {
    if (on == m_strangerOnly) return;
    m_strangerOnly = on;
    applyFilter();
    emit filterChanged();
}

void ConversationModel::setFriendContext(const QSet<QString>& friends, const QSet<QString>& officials,
                                         const QString& myUid, bool known) {
    if (friends == m_friendIds && officials == m_officialIds && myUid == m_myUid && known == m_friendsKnown) return;
    m_friendIds = friends;
    m_officialIds = officials;
    m_myUid = myUid;
    m_friendsKnown = known;
    if (m_strangerOnly) applyFilter();
    emit filterChanged();
}

bool ConversationModel::strangerId(const QString& convId, bool isGroup) const {
    return m_friendsKnown && !isGroup && !convId.isEmpty() && !convId.startsWith(QLatin1Char('g'))
           && convId != QLatin1String("0") && convId != m_myUid && !m_friendIds.contains(convId)
           && !m_officialIds.contains(convId);
}

bool ConversationModel::isStranger(const QString& convId) const {
    for (const auto& item : m_allItems)
        if (item.id == convId) return strangerId(convId, item.isGroup);
    return strangerId(convId, false);
}

void ConversationModel::applyLabels(const QHash<QString, QVariantMap>& labels) {
    m_labels = labels;
    patchRows(m_allItems, m_filteredItems, this, {TagRole, TagColorRole, LabelIdRole}, [&](ConversationItem& item) {
        const QVariantMap l = labels.value(item.id);
        item.labelId = l.value("id").toInt();
        item.tag = l.value("text").toString();
        item.tagColor = l.value("color").toString();
    });
    if (m_labelFilter > 0) applyFilter();
}

void ConversationModel::setOnlineIds(const QSet<QString>& ids) {
    patchRows(m_allItems, m_filteredItems, this, {IsOnlineRole}, [&](ConversationItem& item) {
        item.isOnline = !item.isGroup && ids.contains(item.id);
    });
}

void ConversationModel::setTyping(const QString& convId, bool typing) {
    patchRows(m_allItems, m_filteredItems, this, {IsTypingRole}, [&](ConversationItem& item) {
        if (item.id == convId) item.isTyping = typing;
    });
}

QString ConversationModel::adjacentId(const QString& convId, int delta) const {
    if (m_filteredItems.isEmpty()) return {};
    int row = -1;
    for (int i = 0; i < m_filteredItems.size(); ++i) {
        if (m_filteredItems[i].id == convId) {
            row = i;
            break;
        }
    }
    if (row < 0) return m_filteredItems.first().id;
    const int target = row + delta;
    if (target < 0 || target >= m_filteredItems.size()) return {};
    return m_filteredItems[target].id;
}

QStringList ConversationModel::pinnedIds() const {
    QStringList ids;
    for (const auto& item : m_filteredItems) {
        if (item.isPinned) ids << item.id;
    }
    return ids;
}

void ConversationModel::setDraft(const QString& convId, const QString& text) {
    const QString clean = text.trimmed().isEmpty() ? QString() : text;
    if (m_drafts.value(convId) == clean) return;
    if (clean.isEmpty()) m_drafts.remove(convId);
    else m_drafts.insert(convId, clean);
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        if (m_filteredItems[row].id != convId) continue;
        emit dataChanged(index(row), index(row), {DraftRole});
        break;
    }
}

void ConversationModel::setMemberAvatars(const QString& convId, const QVariantList& members) {
    if (m_memberAvatars.value(convId) == members) return;
    if (members.isEmpty()) m_memberAvatars.remove(convId);
    else m_memberAvatars.insert(convId, members);
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        if (m_filteredItems[row].id != convId) continue;
        emit dataChanged(index(row), index(row), {MemberAvatarsRole});
        break;
    }
}

void ConversationModel::clearDrafts() {
    if (m_drafts.isEmpty()) return;
    m_drafts.clear();
    if (!m_filteredItems.isEmpty()) emit dataChanged(index(0), index(m_filteredItems.size() - 1), {DraftRole});
}

void ConversationModel::updateLastMessage(const QString& convId, const QString& message, const QString& timeStr) {
    for (int i = 0; i < m_allItems.size(); ++i) {
        if (m_allItems[i].id == convId) {
            m_allItems[i].lastMessage = message;
            m_allItems[i].timestampStr = timeStr;
            break;
        }
    }
    applyFilter();
}

void ConversationModel::clearUnread(const QString& convId) {
    for (int i = 0; i < m_allItems.size(); ++i) {
        if (m_allItems[i].id == convId && (m_allItems[i].unreadCount > 0 || m_allItems[i].hasUnreadMention)) {
            m_allItems[i].unreadCount = 0;
            m_allItems[i].hasUnreadMention = false;
            break;
        }
    }
    applyFilter();
    emitUnreadChanged();
}

void ConversationModel::setUnreadCount(const QString& convId, int count) {
    for (auto& item : m_allItems) {
        if (item.id != convId) continue;
        if (count == 0) item.hasUnreadMention = false;
        if (item.unreadCount == qMax(0, count)) return;
        item.unreadCount = qMax(0, count);
        applyFilter();
        emitUnreadChanged();
        return;
    }
}

void ConversationModel::removeConversation(const QString& convId) {
    const auto it = std::find_if(m_allItems.begin(), m_allItems.end(),
                                 [&](const ConversationItem& c) { return c.id == convId; });
    if (it == m_allItems.end()) return;
    m_allItems.erase(it);
    applyFilter();
    emitUnreadChanged();
}

int ConversationModel::totalUnreadCount() const {
    int total = 0;
    for (const auto& item : m_allItems) {
        if (!m_hiddenIds.contains(item.id)) total += item.unreadCount;
    }
    return total;
}

bool ConversationModel::hasAnyUnreadMention() const {
    for (const auto& item : m_allItems) {
        if (item.unreadCount > 0 && item.hasUnreadMention && !m_hiddenIds.contains(item.id)) return true;
    }
    return false;
}

void ConversationModel::clearAll() {
    beginResetModel();
    m_allItems.clear();
    m_filteredItems.clear();
    endResetModel();
    emit unreadCountChanged(0, false);
}

}
