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

// Stickers (zca-js getStickers / searchSticker / getStickersDetail / getStickerCategoryDetail, Zalo PC sticker
// store): the account's sticker packs, their stickers, keyword search, recently sent, and the render info of every
// sticker id seen (cached on disk so history stickers animate without asking the server again).
//
// Render info ("sticker" map, also stored in MessageItem::extra.sticker): {id, cateId, type, url (still image),
// animatedUrl (animated WebP: stickerSpriteUrl when ext == 2, else stickerWebpUrl), spriteUrl + frames + duration
// (horizontal strip of square frames, one every `duration` ms — Zalo PC _playSticker), text}.
// Exposed to QML as ZaloClient.stickers; ZSticker.qml draws one.
class StickerStore : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // [{id, name, iconUrl}]: "recent" (id -1) when not empty, then the account's packs in its order.
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged FINAL)
    Q_PROPERTY(int currentCategory READ currentCategory NOTIFY stickersChanged FINAL)
    // Stickers of the current category or of the last search (render info maps).
    Q_PROPERTY(QVariantList stickers READ stickers NOTIFY stickersChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY stickersChanged FINAL)

public:
    static constexpr int kRecentId = -1;
    static constexpr int kMaxRecent = 24;
    static constexpr int kMaxSearch = 24;
    static constexpr int kExtWebp = 2; // Zalo PC STICKER_EXT {PNG 0, JPG 1, WEBP 2}

    struct Hooks {
        std::function<ZaloSession*()> session;
        // Plain HTTPS download (category list JSON file).
        std::function<void(const QString&, std::function<void(const QByteArray&, const QString&)>)> fetchBytes;
        std::function<void(qint64, qint64, int)> send;   // sticker id, cate id, type → current conversation
        std::function<QString()> account;                // uid for the recent list ("" = signed out)
    };

    static QVariantMap describe(const QJsonObject& detail);
    // Reply of category/sticker_detail: an array, or an object holding one.
    static QJsonArray stickerArray(const QJsonValue& data);

    explicit StickerStore(Hooks hooks, QObject* parent = nullptr);
    ~StickerStore() override;

    QVariantList categories() const;
    int currentCategory() const { return m_current; }
    QVariantList stickers() const { return m_stickers; }
    bool loading() const { return m_loading > 0; }
    QString searchQuery() const { return m_query; }

    // Render info of a sticker id ({} when never seen) and remembering new ones (persisted, debounced).
    QVariantMap info(const QString& stickerId) const { return m_info.value(stickerId); }
    void remember(const QVariantMap& info);
    void setCacheFile(const QString& path); // tests / account switch

    Q_INVOKABLE void load();
    Q_INVOKABLE void selectCategory(int cateId);
    Q_INVOKABLE void search(const QString& keyword);
    Q_INVOKABLE void send(const QVariantMap& sticker);
    void reset();

    // Test seams.
    void applyPersonalForTesting(const QJsonObject& data) { applyPersonal(data); }
    void applyCatalogForTesting(const QJsonArray& cates) { applyCatalog(cates); }
    void applyCategoryForTesting(int cateId, const QJsonValue& data) { applyCategory(cateId, data); }

signals:
    void categoriesChanged();
    void stickersChanged();
    void loadingChanged();

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
    QList<int> m_personal;                 // the account's pack ids, in order
    QHash<int, QVariantMap> m_catalog;     // cate id -> {id, name, iconUrl}
    QHash<int, QVariantList> m_byCategory; // cate id -> stickers
    QHash<QString, QVariantMap> m_info;    // sticker id -> render info
    QVariantList m_stickers;
    int m_current{kRecentId};
    QString m_query;
    int m_loading{0};
    bool m_loaded{false};
    QString m_cacheFile;
    QTimer m_saveTimer;
    quint64 m_searchSeq{0};
};

} // namespace Zalos::Core::Network
