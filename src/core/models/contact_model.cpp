#include "contact_model.h"
#include <algorithm>

namespace Zalos::Core::Models {

ContactModel::ContactModel(QObject* parent)
    : QAbstractListModel(parent) {
}

int ContactModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_filteredItems.size();
}

QVariant ContactModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filteredItems.size()) {
        return QVariant();
    }

    const auto& item = m_filteredItems.at(index.row());
    switch (role) {
        case IdRole:
            return item.id;
        case NameRole:
            return item.name;
        case AvatarUrlRole:
            return item.avatarUrl;
        case IsOnlineRole:
            return item.isOnline;
        case IsGroupRole:
            return item.isGroup;
        case MemberCountRole:
            return item.memberCount;
        case CategoryRole:
            return item.category;
        case SubtitleRole:
            return item.subtitle;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> ContactModel::roleNames() const {
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {AvatarUrlRole, "avatarUrl"},
        {IsOnlineRole, "isOnline"},
        {IsGroupRole, "isGroup"},
        {MemberCountRole, "memberCount"},
        {CategoryRole, "category"},
        {SubtitleRole, "subtitle"}
    };
}

int ContactModel::friendCount() const {
    int c = 0;
    for (const auto& item : m_allItems) {
        if (!item.isGroup && item.category == "friends") c++;
    }
    return c;
}

int ContactModel::groupCount() const {
    int c = 0;
    for (const auto& item : m_allItems) {
        if (item.isGroup || item.category == "groups") c++;
    }
    return c;
}

int ContactModel::inviteCount() const {
    int c = 0;
    for (const auto& item : m_allItems) {
        if (item.category == "invites") c++;
    }
    return c;
}

void ContactModel::setCategoryFilter(const QString& category) {
    if (m_activeCategory != category) {
        m_activeCategory = category;
        applyFilter();
        emit filterChanged();
    }
}

void ContactModel::setSearchFilter(const QString& query) {
    if (m_searchQuery != query) {
        m_searchQuery = query;
        applyFilter();
        emit filterChanged();
    }
}

void ContactModel::applyFilter() {
    beginResetModel();
    m_filteredItems.clear();

    for (const auto& item : m_allItems) {
        bool matchesCategory = m_activeCategory.isEmpty() || (item.category == m_activeCategory);
        bool matchesSearch = m_searchQuery.isEmpty() ||
                             item.name.contains(m_searchQuery, Qt::CaseInsensitive);

        if (matchesCategory && matchesSearch) {
            m_filteredItems.append(item);
        }
    }

    // Sort alphabetically by name
    std::stable_sort(m_filteredItems.begin(), m_filteredItems.end(), [](const ContactItem& a, const ContactItem& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    endResetModel();
    emit countChanged();
}

void ContactModel::addContact(const ContactItem& item) {
    m_allItems.append(item);
    applyFilter();
}

void ContactModel::setContacts(const QVector<ContactItem>& items) {
    beginResetModel();
    m_allItems = items;
    m_filteredItems.clear();

    for (const auto& item : m_allItems) {
        bool matchesCategory = m_activeCategory.isEmpty() || (item.category == m_activeCategory);
        bool matchesSearch = m_searchQuery.isEmpty() ||
                             item.name.contains(m_searchQuery, Qt::CaseInsensitive);

        if (matchesCategory && matchesSearch) {
            m_filteredItems.append(item);
        }
    }

    std::stable_sort(m_filteredItems.begin(), m_filteredItems.end(), [](const ContactItem& a, const ContactItem& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    endResetModel();
    emit countChanged();
}

void ContactModel::setOnlineIds(const QSet<QString>& ids) {
    for (auto& item : m_allItems) item.isOnline = ids.contains(item.id);
    for (int row = 0; row < m_filteredItems.size(); ++row) {
        const bool online = ids.contains(m_filteredItems[row].id);
        if (m_filteredItems[row].isOnline != online) {
            m_filteredItems[row].isOnline = online;
            emit dataChanged(index(row), index(row), {IsOnlineRole});
        }
    }
}

void ContactModel::clearAll() {
    beginResetModel();
    m_allItems.clear();
    m_filteredItems.clear();
    endResetModel();
    emit countChanged();
}

} // namespace Zalos::Core::Models

