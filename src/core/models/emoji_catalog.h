// SPDX-License-Identifier: MIT
#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace Zalos::Core::Models {

class EmojiCatalog : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList categories READ categories CONSTANT FINAL)
    Q_PROPERTY(QString currentCategory READ currentCategory NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QString currentCategoryNameVi READ currentCategoryNameVi NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QString currentCategoryNameEn READ currentCategoryNameEn NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged FINAL)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY itemsChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY itemsChanged FINAL)

public:
    static constexpr int kMaxRecent = 28;
    static constexpr int kMaxSearch = 120;

    explicit EmojiCatalog(QObject* parent = nullptr);

    QVariantList categories() const;
    QString currentCategory() const { return m_currentCategory; }
    QString currentCategoryNameVi() const;
    QString currentCategoryNameEn() const;
    QString searchQuery() const { return m_searchQuery; }
    QVariantList items() const;
    QVariantList sections() const;
    int revision() const { return m_revision; }

    Q_INVOKABLE void selectCategory(const QString& categoryId);
    Q_INVOKABLE void search(const QString& query);
    Q_INVOKABLE void recordRecent(const QString& glyph);
    Q_INVOKABLE QVariantList flatRows(int columns, int revision = 0) const;

    static int totalCount();
    static int categoryCount();

signals:
    void itemsChanged();

private:
    void ensureRecentLoaded() const;
    void saveRecent() const;

    QString m_currentCategory{QStringLiteral("zalo")};
    QString m_searchQuery;
    mutable bool m_recentLoaded{false};
    mutable QStringList m_recent;
    int m_revision{0};
};

}
