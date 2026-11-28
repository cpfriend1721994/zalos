#include "conversation_model.h"
#include "list_diff.h"
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
        case IsOfficialRole: {
            if (item.isGroup || item.id.isEmpty()) return false;
            if (m_officialIds.contains(item.id)) return true;
            const QString key = !item.foldedName.isEmpty() ? item.foldedName : foldForSearch(item.name).simplified();
            return !key.isEmpty() && isOfficialTitle(item, key);
        }
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
        {IsHiddenRole, "isHidden"},
        {IsOfficialRole, "isOfficial"}
    };
}

void ConversationModel::setCategoryFilter(const QString& category) {
    if (m_activeCategory != category) {
        m_activeCategory = category;
        refilter();
        emit filterChanged();
    }
}

void ConversationModel::setSearchFilter(const QString& query) {
    if (m_searchQuery != query) {
        m_searchQuery = query;
        refilter();
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
    QVector<ConversationItem> next;
    next.reserve(m_allItems.size());

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
            if (item.isGroup && (!item.hasUnreadMention || item.unreadCount <= 0)) continue;
            if (!searching) {
                if (m_labelFilter > 0 && item.labelId != m_labelFilter) continue;
                if (m_typeFilter == QLatin1String("group") && !item.isGroup) continue;
                if (m_typeFilter == QLatin1String("direct") && item.isGroup) continue;
                if (m_typeFilter == QLatin1String("friend") && (item.isGroup || !m_friendIds.contains(item.id))) continue;
                if ((m_typeFilter == QLatin1String("stranger") || m_strangerOnly)
                    && (!strangerId(item.id, item.isGroup) || isSystemOrOfficial(item))) continue;
            }
        } else if (!searching) {
            if (isArchived) continue;
            if (m_labelFilter > 0 && item.labelId != m_labelFilter) continue;
            if (m_typeFilter == QLatin1String("group") && !item.isGroup) continue;
            if (m_typeFilter == QLatin1String("direct") && item.isGroup) continue;
            if (m_typeFilter == QLatin1String("friend") && (item.isGroup || !m_friendIds.contains(item.id))) continue;
            if ((m_typeFilter == QLatin1String("stranger") || m_strangerOnly)
                && (!strangerId(item.id, item.isGroup) || isSystemOrOfficial(item))) continue;
            if (!(m_activeCategory.isEmpty() ||
                  (m_activeCategory == "unread" ? item.unreadCount > 0 : item.category == m_activeCategory)))
                continue;
        }
        const QString shownName = !item.name.isEmpty() ? item.name
                                  : (item.isGroup ? QStringLiteral("Nhóm chưa đặt tên Unnamed group")
                                                  : QStringLiteral("Người dùng Zalo Zalo user"));
        if (searching) {
            bool match = containsAllTerms(folded(shownName), terms);
            if (!match && !isHidden) {
                const QString haystack = folded(shownName) + QLatin1Char(' ') + folded(item.lastMessage);
                match = containsAllTerms(haystack, terms);
            }
            if (!match) continue;
        }
        next.append(item);
    }

    std::stable_sort(next.begin(), next.end(), [](const ConversationItem& a, const ConversationItem& b) {
        if (a.isPinned != b.isPinned) {
            return a.isPinned > b.isPinned;
        }
        return a.lastTs > b.lastTs;
    });

    if (m_resetPending) {
        m_resetPending = false;
        beginResetModel();
        m_filteredItems = std::move(next);
        endResetModel();
        return;
    }
    publishFiltered(std::move(next));
}

void ConversationModel::refilter() {
    m_resetPending = true;
    applyFilter();
}

QString ConversationModel::folded(const QString& text) const {
    auto it = m_foldCache.constFind(text);
    if (it != m_foldCache.constEnd()) return it.value();
    if (m_foldCache.size() > 8192) m_foldCache.clear();
    return m_foldCache.insert(text, foldForSearch(text)).value();
}

namespace {
QVector<int> diffConversationRoles(const ConversationItem& a, const ConversationItem& b) {
    QVector<int> roles;
    if (a.id != b.id) roles << ConversationModel::IdRole;
    if (a.name != b.name) roles << ConversationModel::NameRole;
    if (a.lastMessage != b.lastMessage) roles << ConversationModel::LastMessageRole;
    if (a.timestampStr != b.timestampStr) roles << ConversationModel::TimestampRole;
    if (a.unreadCount != b.unreadCount) roles << ConversationModel::UnreadCountRole;
    if (a.isOnline != b.isOnline) roles << ConversationModel::IsOnlineRole;
    if (a.isPinned != b.isPinned) roles << ConversationModel::IsPinnedRole;
    if (a.avatarUrl != b.avatarUrl) roles << ConversationModel::AvatarUrlRole;
    if (a.category != b.category) roles << ConversationModel::CategoryRole;
    if (a.senderPrefix != b.senderPrefix) roles << ConversationModel::SenderPrefixRole;
    if (a.isMuted != b.isMuted) roles << ConversationModel::IsMutedRole;
    if (a.tag != b.tag) roles << ConversationModel::TagRole;
    if (a.tagColor != b.tagColor) roles << ConversationModel::TagColorRole;
    if (a.labelId != b.labelId) roles << ConversationModel::LabelIdRole;
    if (a.memberCount != b.memberCount) roles << ConversationModel::MemberCountRole;
    if (a.isGroup != b.isGroup) roles << ConversationModel::IsGroupRole;
    if (a.lastTs != b.lastTs) roles << ConversationModel::LastTsRole;
    if (a.isTyping != b.isTyping) roles << ConversationModel::IsTypingRole;
    if (a.hasUnreadMention != b.hasUnreadMention) roles << ConversationModel::HasUnreadMentionRole;
    return roles;
}
}

void ConversationModel::publishFiltered(QVector<ConversationItem> next) {
    ListDiffOps<ConversationItem> ops;
    ops.reset = [this](QVector<ConversationItem>&& value) {
        beginResetModel();
        m_filteredItems = std::move(value);
        endResetModel();
    };
    ops.removeRows = [this](int first, int last, const std::function<void()>& mutate) {
        beginRemoveRows(QModelIndex(), first, last);
        mutate();
        endRemoveRows();
    };
    ops.moveRow = [this](int from, int to, const std::function<void()>& mutate) {
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to);
        mutate();
        endMoveRows();
    };
    ops.insertRows = [this](int first, int last, const std::function<void()>& mutate) {
        beginInsertRows(QModelIndex(), first, last);
        mutate();
        endInsertRows();
    };
    ops.changed = [this](int row, const QVector<int>& roles) { emit dataChanged(index(row), index(row), roles); };
    applyKeyedListDiff(m_filteredItems, std::move(next), diffConversationRoles, ops);
}

void ConversationModel::reindex() {
    m_index.clear();
    m_index.reserve(m_allItems.size());
    for (int i = 0; i < m_allItems.size(); ++i) m_index.insert(m_allItems[i].id, i);
}

int ConversationModel::indexOf(const QString& convId) const {
    return m_index.value(convId, -1);
}

void ConversationModel::stampFolded(ConversationItem& item) {
    item.foldedName = foldForSearch(item.name).simplified();
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
    m_index.insert(item.id, m_allItems.size() - 1);
    stampLabel(m_allItems.last());
    stampFolded(m_allItems.last());
    applyFilter();
    emitUnreadChanged();
}

void ConversationModel::upsertConversation(const ConversationItem& item) {
    const int i = indexOf(item.id);
    if (i < 0) {
        addConversation(item);
        return;
    }
    m_allItems[i] = item;
    stampLabel(m_allItems[i]);
    stampFolded(m_allItems[i]);
    applyFilter();
    emitUnreadChanged();
}

void ConversationModel::setConversations(const QVector<ConversationItem>& items) {
    m_allItems = items;
    for (auto& item : m_allItems) {
        stampLabel(item);
        stampFolded(item);
    }
    reindex();
    m_resetPending = true;
    applyFilter();
    emitUnreadChanged();
}

bool ConversationModel::contains(const QString& convId) const {
    return indexOf(convId) >= 0;
}

ConversationItem ConversationModel::conversation(const QString& convId) const {
    const int i = indexOf(convId);
    return i >= 0 ? m_allItems[i] : ConversationItem{};
}

bool ConversationModel::updateProfile(const QString& convId, const QString& name, const QString& avatarUrl, int memberCount) {
    bool changed = false;
    bool nameChanged = false;
    const int i = indexOf(convId);
    if (i >= 0) {
        auto& item = m_allItems[i];
        if (!name.isEmpty() && item.name != name) {
            item.name = name;
            stampFolded(item);
            changed = true;
            nameChanged = true;
        }
        if (!avatarUrl.isEmpty() && item.avatarUrl != avatarUrl) { item.avatarUrl = avatarUrl; changed = true; }
        if (memberCount >= 0 && item.memberCount != memberCount) { item.memberCount = memberCount; changed = true; }
    }
    if (!changed) return false;
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        if (m_filteredItems[row].id != convId) continue;
        m_filteredItems[row] = conversation(convId);
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {NameRole, AvatarUrlRole, MemberCountRole, IsOfficialRole});
        break;
    }
    if (nameChanged && (!m_searchQuery.isEmpty() || m_strangerOnly || m_typeFilter == QLatin1String("stranger")))
        applyFilter();
    return true;
}

void ConversationModel::setSenderPrefix(const QString& convId, const QString& senderPrefix) {
    const int i = indexOf(convId);
    if (i < 0 || m_allItems[i].senderPrefix == senderPrefix) return;
    m_allItems[i].senderPrefix = senderPrefix;
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        if (m_filteredItems[row].id != convId) continue;
        m_filteredItems[row].senderPrefix = senderPrefix;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {SenderPrefixRole});
        break;
    }
}

void ConversationModel::recordMessage(const QString& convId, const QString& preview, const QString& senderPrefix,
                                      qint64 ts, bool incrementUnread, bool hasMention) {
    const int i = indexOf(convId);
    if (i < 0) return;
    auto& item = m_allItems[i];
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
}

void ConversationModel::setPinned(const QString& convId, bool pinned) {
    const int i = indexOf(convId);
    if (i < 0 || m_allItems[i].isPinned == pinned) return;
    m_allItems[i].isPinned = pinned;
    applyFilter();
}

void ConversationModel::setMuted(const QString& convId, bool muted) {
    const int i = indexOf(convId);
    if (i < 0 || m_allItems[i].isMuted == muted) return;
    m_allItems[i].isMuted = muted;
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        if (m_filteredItems[row].id != convId) continue;
        m_filteredItems[row].isMuted = muted;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {IsMutedRole});
        break;
    }
}

namespace {
template <typename Fn>
void patchRows(QVector<ConversationItem>& all, QVector<ConversationItem>& filtered, ConversationModel* model,
               const QList<int>& roles, Fn fn) {
    for (auto& item : all) fn(item);
    for (int row = 0; row < filtered.size(); ++row) {
        ConversationItem& item = filtered[row];
        const QString tag = item.tag;
        const QString tagColor = item.tagColor;
        const int labelId = item.labelId;
        const bool online = item.isOnline;
        const bool typing = item.isTyping;
        fn(item);
        if (tag != item.tag || tagColor != item.tagColor || labelId != item.labelId || online != item.isOnline
            || typing != item.isTyping) {
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
    refilter();
    emit filterChanged();
}

void ConversationModel::setTypeFilter(const QString& filter) {
    const QString next = (filter == QLatin1String("group")
                          || filter == QLatin1String("direct")
                          || filter == QLatin1String("friend")
                          || filter == QLatin1String("stranger"))
                         ? filter : QStringLiteral("all");
    if (next == m_typeFilter) return;
    m_typeFilter = next;
    refilter();
    emit filterChanged();
}

void ConversationModel::setViewMode(const QString& mode) {
    const QString next = mode.isEmpty() ? QStringLiteral("normal") : mode;
    if (next == m_viewMode) return;
    m_viewMode = next;
    refilter();
    emit filterChanged();
}

void ConversationModel::setHiddenThreads(const QSet<QString>& ids) {
    if (ids == m_hiddenIds) return;
    m_hiddenIds = ids;
    refilter();
    emitUnreadChanged();
    emit filterChanged();
}

void ConversationModel::setHiddenPinReveal(bool reveal) {
    if (reveal == m_hiddenPinReveal) return;
    m_hiddenPinReveal = reveal;
    refilter();
}

void ConversationModel::setArchivedThreads(const QSet<QString>& ids) {
    if (ids == m_archivedIds) return;
    m_archivedIds = ids;
    refilter();
    emit filterChanged();
}

void ConversationModel::setStrangerOnly(bool on) {
    if (on == m_strangerOnly) return;
    m_strangerOnly = on;
    refilter();
    emit filterChanged();
}

void ConversationModel::setFriendContext(const QSet<QString>& friends, const QSet<QString>& officials,
                                         const QString& myUid, bool known) {
    if (friends == m_friendIds && officials == m_officialIds && myUid == m_myUid && known == m_friendsKnown) return;
    m_friendIds = friends;
    m_officialIds = officials;
    m_myUid = myUid;
    m_friendsKnown = known;
    if (m_strangerOnly || m_typeFilter == QLatin1String("stranger") || m_typeFilter == QLatin1String("friend")) {
        refilter();
    } else if (!m_filteredItems.isEmpty()) {
        emit dataChanged(index(0), index(m_filteredItems.size() - 1), {IsOfficialRole});
    }
    emit filterChanged();
}

bool ConversationModel::isOfficialTitle(const ConversationItem& item, const QString& key) const {
    if (!m_friendIds.contains(item.id) && (key == QLatin1String("zalo") || key.startsWith(QLatin1String("zalo ")))) {
        return true;
    }
    return key == QLatin1String("zbusiness") || key == QLatin1String("zcloud") || key == QLatin1String("zalopay")
        || key == QLatin1String("thoi tiet") || key.contains(QLatin1String("medlatec"));
}

bool ConversationModel::isSystemOrOfficial(const ConversationItem& item) const {
    if (item.isGroup || item.id.isEmpty()) return false;
    if (m_officialIds.contains(item.id)) return true;
    const QString key = !item.foldedName.isEmpty() ? item.foldedName : foldForSearch(item.name).simplified();
    if (key.isEmpty()) return false;
    if (key == QLatin1String("my documents") || key == QLatin1String("cloud cua toi") || key == QLatin1String("truyen file"))
        return true;
    return isOfficialTitle(item, key);
}

bool ConversationModel::strangerId(const QString& convId, bool isGroup) const {
    return m_friendsKnown && !isGroup && !convId.isEmpty() && !convId.startsWith(QLatin1Char('g'))
           && convId != QLatin1String("0") && convId != m_myUid && !m_friendIds.contains(convId)
           && !m_officialIds.contains(convId);
}

bool ConversationModel::isStranger(const QString& convId) const {
    const int i = indexOf(convId);
    return strangerId(convId, i >= 0 && m_allItems[i].isGroup);
}

void ConversationModel::applyLabels(const QHash<QString, QVariantMap>& labels) {
    m_labels = labels;
    patchRows(m_allItems, m_filteredItems, this, {TagRole, TagColorRole, LabelIdRole}, [&](ConversationItem& item) {
        const QVariantMap l = labels.value(item.id);
        item.labelId = l.value("id").toInt();
        item.tag = l.value("text").toString();
        item.tagColor = l.value("color").toString();
    });
    if (m_labelFilter > 0) refilter();
}

void ConversationModel::setOnlineIds(const QSet<QString>& ids) {
    patchRows(m_allItems, m_filteredItems, this, {IsOnlineRole}, [&](ConversationItem& item) {
        item.isOnline = !item.isGroup && ids.contains(item.id);
    });
}

void ConversationModel::setTyping(const QString& convId, bool typing) {
    const int i = indexOf(convId);
    if (i < 0 || m_allItems[i].isTyping == typing) return;
    m_allItems[i].isTyping = typing;
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        if (m_filteredItems[row].id != convId) continue;
        m_filteredItems[row].isTyping = typing;
        emit dataChanged(index(row), index(row), {IsTypingRole});
        break;
    }
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

QStringList ConversationModel::visibleUnreadIds() const {
    QStringList ids;
    for (const auto& item : m_filteredItems) {
        if (item.unreadCount > 0) ids << item.id;
    }
    return ids;
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

void ConversationModel::updateLastMessage(const QString& convId, const QString& message, const QString& timeStr,
                                          const QString& senderPrefix, bool updatePrefix) {
    const int i = indexOf(convId);
    if (i < 0) return;
    auto& item = m_allItems[i];
    const bool sameMsg = item.lastMessage == message && item.timestampStr == timeStr;
    const bool samePrefix = !updatePrefix || item.senderPrefix == senderPrefix;
    if (sameMsg && samePrefix) return;
    item.lastMessage = message;
    item.timestampStr = timeStr;
    if (updatePrefix) item.senderPrefix = senderPrefix;
    applyFilter();
}

void ConversationModel::clearUnread(const QString& convId) {
    const int i = indexOf(convId);
    if (i < 0 || (m_allItems[i].unreadCount == 0 && !m_allItems[i].hasUnreadMention)) return;
    m_allItems[i].unreadCount = 0;
    m_allItems[i].hasUnreadMention = false;
    applyFilter();
    emitUnreadChanged();
}

void ConversationModel::setUnreadCount(const QString& convId, int count) {
    const int i = indexOf(convId);
    if (i < 0) return;
    auto& item = m_allItems[i];
    const bool mentionCleared = count == 0 && item.hasUnreadMention;
    if (count == 0) item.hasUnreadMention = false;
    if (item.unreadCount == qMax(0, count) && !mentionCleared) return;
    item.unreadCount = qMax(0, count);
    applyFilter();
    emitUnreadChanged();
}

void ConversationModel::removeConversation(const QString& convId) {
    const int i = indexOf(convId);
    if (i < 0) return;
    m_allItems.remove(i);
    reindex();
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
    m_index.clear();
    m_foldCache.clear();
    m_drafts.clear();
    m_memberAvatars.clear();
    m_hiddenIds.clear();
    m_hiddenPinReveal = false;
    m_archivedIds.clear();
    m_strangerOnly = false;
    m_friendsKnown = false;
    m_friendIds.clear();
    m_officialIds.clear();
    m_myUid.clear();
    m_labels.clear();
    m_activeCategory = QStringLiteral("priority");
    m_searchQuery.clear();
    m_labelFilter = 0;
    m_typeFilter = QStringLiteral("all");
    m_viewMode = QStringLiteral("normal");
    m_batchDepth = 0;
    m_filterDirty = false;
    m_resetPending = false;
    m_unreadDirty = false;
    endResetModel();
    emit filterChanged();
    emit unreadCountChanged(0, false);
}

}
