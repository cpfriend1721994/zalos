#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace Zalos::Core::Network {

class ZaloSession;

class StickerStore : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged FINAL)
    Q_PROPERTY(int currentCategory READ currentCategory NOTIFY stickersChanged FINAL)
    Q_PROPERTY(QVariantList stickers READ stickers NOTIFY stickersChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY stickersChanged FINAL)
    Q_PROPERTY(QVariantList birthdayStickers READ birthdayStickers NOTIFY birthdayStickersChanged FINAL)

public:
    static constexpr int kRecentId = -1;
    static constexpr int kMaxRecent = 24;
    static constexpr int kMaxSearch = 24;
    static constexpr int kBirthdaySuggestions = 8;
    static constexpr int kExtWebp = 2;

    struct Hooks {
        std::function<ZaloSession*()> session;
        std::function<void(const QString&, std::function<void(const QByteArray&, const QString&)>)> fetchBytes;
        std::function<void(qint64, qint64, int)> send;
        std::function<QString()> account;
    };

    static QVariantMap describe(const QJsonObject& detail);
    static QJsonArray stickerArray(const QJsonValue& data);

    explicit StickerStore(Hooks hooks, QObject* parent = nullptr);
    ~StickerStore() override;

    QVariantList categories() const;
    int currentCategory() const { return m_current; }
    QVariantList stickers() const { return m_stickers; }
    bool loading() const { return m_loading > 0; }
    QString searchQuery() const { return m_query; }
    QVariantList birthdayStickers() const { return m_birthday; }

    QVariantMap info(const QString& stickerId) const { return m_info.value(stickerId); }
    void remember(const QVariantMap& info);
    void setCacheFile(const QString& path);

    Q_INVOKABLE void load();
    Q_INVOKABLE void selectCategory(int cateId);
    Q_INVOKABLE void search(const QString& keyword);
    Q_INVOKABLE void send(const QVariantMap& sticker);
    Q_INVOKABLE void loadBirthdayStickers();
    void lookup(const QString& keyword, int limit, std::function<void(const QVariantList&)> done);
    void reset();

    void applyPersonalForTesting(const QJsonObject& data) { applyPersonal(data); }
    void applyCatalogForTesting(const QJsonArray& cates) { applyCatalog(cates); }
    void applyCategoryForTesting(int cateId, const QJsonValue& data) { applyCategory(cateId, data); }

signals:
    void categoriesChanged();
    void stickersChanged();
    void loadingChanged();
    void birthdayStickersChanged();

private:
    ZaloSession* session() const { return m_hooks.session ? m_hooks.session() : nullptr; }
    void applyPersonal(const QJsonObject& data);
    void applyCatalog(const QJsonArray& cates);
    void applyCategory(int cateId, const QJsonValue& data);
    void setStickers(int cateId, const QVariantList& list, const QString& query = QString());
    void busy(int delta);
    QVariantList recent() const;
    void saveCache();
    void loadCache();

    Hooks m_hooks;
    QList<int> m_personal;
    QHash<int, QVariantMap> m_catalog;
    QHash<int, QVariantList> m_byCategory;
    QHash<QString, QVariantMap> m_info;
    QVariantList m_stickers;
    int m_current{kRecentId};
    QString m_query;
    int m_loading{0};
    bool m_loaded{false};
    QString m_cacheFile;
    QTimer m_saveTimer;
    quint64 m_searchSeq{0};
    QVariantList m_birthday;
    bool m_birthdayLoading{false};
};

}
