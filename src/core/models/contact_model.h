#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QString>
#include <QSet>
#include <QVector>

namespace Zalos::Core::Models {

struct ContactItem {
    QString id;
    QString name;
    QString avatarUrl;
    bool isOnline{false};
    bool isGroup{false};
    int memberCount{0};
    QString category{"friends"};
    QString subtitle;
};

class ContactModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString activeCategory READ activeCategory WRITE setCategoryFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchFilter NOTIFY filterChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int friendCount READ friendCount NOTIFY countChanged FINAL)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY countChanged FINAL)
    Q_PROPERTY(int inviteCount READ inviteCount NOTIFY countChanged FINAL)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        AvatarUrlRole,
        IsOnlineRole,
        IsGroupRole,
        MemberCountRole,
        CategoryRole,
        SubtitleRole
    };

    explicit ContactModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeCategory() const { return m_activeCategory; }
    QString searchQuery() const { return m_searchQuery; }
    int count() const { return m_filteredItems.size(); }
    int friendCount() const;
    int groupCount() const;
    int inviteCount() const;

    Q_INVOKABLE void setCategoryFilter(const QString& category);
    Q_INVOKABLE void setSearchFilter(const QString& query);
    Q_INVOKABLE void clearAll();

    void addContact(const ContactItem& item);
    void setContacts(const QVector<ContactItem>& items);
    void setOnlineIds(const QSet<QString>& ids);

signals:
    void filterChanged();
    void countChanged();

private:
    void applyFilter();

    QVector<ContactItem> m_allItems;
    QVector<ContactItem> m_filteredItems;
    QString m_activeCategory{"friends"};
    QString m_searchQuery{""};
};

}
