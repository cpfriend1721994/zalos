#include "sticker_store.h"

#include "zalo_session.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace Zalos::Core::Network {

namespace {

QString idText(const QJsonValue& v) {
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(qint64(v.toDouble()));
    return {};
}

}

QVariantMap StickerStore::describe(const QJsonObject& d) {
    const QString id = idText(d.value("id").isUndefined() ? d.value("sticker_id") : d.value("id"));
    if (id.isEmpty() || id == "0") return {};
    const QString still = d.value("stickerUrl").toString();
    const QString sprite = d.value("stickerSpriteUrl").toString();
    const QString webp = d.value("stickerWebpUrl").toString();
    const int ext = d.value("ext").toInt();
    const int frames = d.value("totalFrames").toInt();
    QString animated;
    if (ext == kExtWebp && !sprite.isEmpty()) animated = sprite;
    else if (!webp.isEmpty()) animated = webp;
    const bool useSprite = animated.isEmpty() && frames > 1 && !sprite.isEmpty();
    return {{"id", id},
            {"cateId", d.value("cateId").isUndefined() ? d.value("cate_id").toVariant() : d.value("cateId").toVariant()},
            {"type", d.value("type").toInt()},
            {"url", still.isEmpty() ? (animated.isEmpty() ? sprite : animated) : still},
            {"animatedUrl", animated},
            {"spriteUrl", useSprite ? sprite : QString()},
            {"frames", useSprite ? frames : 1},
            {"duration", qBound(30, d.value("duration").toInt(100), 2000)},
            {"text", d.value("text").toString()}};
}

QJsonArray StickerStore::stickerArray(const QJsonValue& data) {
    if (data.isArray()) return data.toArray();
    const QJsonObject o = data.toObject();
    for (const char* key : {"stickers", "data", "sticker", "items"}) {
        if (o.value(key).isArray()) return o.value(key).toArray();
    }
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.value().isArray()) return it.value().toArray();
    }
    return {};
}

StickerStore::StickerStore(Hooks hooks, QObject* parent) : QObject(parent), m_hooks(std::move(hooks)) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(2000);
    connect(&m_saveTimer, &QTimer::timeout, this, &StickerStore::saveCache);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    setCacheFile(dir.isEmpty() ? QString() : dir + "/sticker-info.json");
}

StickerStore::~StickerStore() {
    if (m_saveTimer.isActive()) saveCache();
}

void StickerStore::setCacheFile(const QString& path) {
    if (m_saveTimer.isActive()) saveCache();
    m_cacheFile = path;
    m_info.clear();
    loadCache();
}

void StickerStore::loadCache() {
    if (m_cacheFile.isEmpty()) return;
    QFile f(m_cacheFile);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) m_info.insert(it.key(), it.value().toObject().toVariantMap());
}

void StickerStore::saveCache() {
    m_saveTimer.stop();
    if (m_cacheFile.isEmpty()) return;
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QJsonObject o;
    for (auto it = m_info.cbegin(); it != m_info.cend(); ++it) o.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
    QSaveFile f(m_cacheFile);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.commit();
}

void StickerStore::remember(const QVariantMap& info) {
    const QString id = info.value("id").toString();
    if (id.isEmpty() || m_info.value(id) == info) return;
    m_info.insert(id, info);
    m_saveTimer.start();
}

void StickerStore::busy(int delta) {
    const bool was = loading();
    m_loading = qMax(0, m_loading + delta);
    if (was != loading()) emit loadingChanged();
}

QVariantList StickerStore::recent() const {
    const QString uid = m_hooks.account ? m_hooks.account() : QString();
    if (uid.isEmpty()) return {};
    const QJsonArray arr = QJsonDocument::fromJson(QSettings().value("stickers/recent/" + uid).toByteArray()).array();
    return arr.toVariantList();
}

QVariantList StickerStore::categories() const {
    QVariantList out;
    if (!recent().isEmpty())
        out.append(QVariantMap{{"id", kRecentId}, {"name", tr("Gần đây")}, {"iconUrl", QString()}});
    for (int id : m_personal) {
        QVariantMap c = m_catalog.value(id);
        if (c.isEmpty()) c = {{"id", id}, {"name", QString()}, {"iconUrl", QString()}};
        if (c.value("iconUrl").toString().isEmpty() && !m_byCategory.value(id).isEmpty())
            c.insert("iconUrl", m_byCategory.value(id).first().toMap().value("url"));
        out.append(c);
    }
    return out;
}

void StickerStore::setStickers(int cateId, const QVariantList& list, const QString& query) {
    m_current = cateId;
    m_stickers = list;
    m_query = query;
    emit stickersChanged();
}

void StickerStore::load() {
    if (m_loaded) {
        if (m_query.isEmpty()) selectCategory(m_current);
        return;
    }
    ZaloSession* s = session();
    if (!s) {
        setStickers(kRecentId, recent());
        return;
    }
    m_loaded = true;
    setStickers(kRecentId, recent());
    QPointer<StickerStore> self(this);
    busy(1);
    s->fetchPersonalStickerList([self](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self) return;
        self->busy(-1);
        if (ok) self->applyPersonal(data.toObject());
    });
    busy(1);
    s->fetchStickerCategories([self](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self) return;
        if (!ok) {
            self->busy(-1);
            return;
        }
        const QJsonObject o = data.toObject();
        const QJsonArray fallback = o.value("data").toArray();
        const QString url = o.value("dataUrl").toString();
        if (url.isEmpty() || !self->m_hooks.fetchBytes) {
            self->applyCatalog(fallback);
            self->busy(-1);
            return;
        }
        self->m_hooks.fetchBytes(url, [self, fallback](const QByteArray& bytes, const QString& error) {
            if (!self) return;
            const QJsonArray cates = error.isEmpty() ? QJsonDocument::fromJson(bytes).array() : QJsonArray();
            self->applyCatalog(cates.isEmpty() ? fallback : cates);
            self->busy(-1);
        });
    });
}

void StickerStore::applyPersonal(const QJsonObject& data) {
    m_personal.clear();
    for (const QJsonValue& v : data.value("sticker_cates").toArray()) {
        const int id = v.toVariant().toInt();
        if (id > 0 && !m_personal.contains(id)) m_personal.append(id);
    }
    emit categoriesChanged();
    if (m_current == kRecentId && m_stickers.isEmpty() && !m_personal.isEmpty()) selectCategory(m_personal.first());
}

void StickerStore::applyCatalog(const QJsonArray& cates) {
    for (const QJsonValue& v : cates) {
        const QJsonObject c = v.toObject();
        const int id = c.value("id").toVariant().toInt();
        if (id <= 0) continue;
        m_catalog.insert(id, {{"id", id}, {"name", c.value("name").toString()}, {"iconUrl", c.value("iconUrl").toString()}});
    }
    emit categoriesChanged();
}

void StickerStore::selectCategory(int cateId) {
    if (cateId == kRecentId) {
        setStickers(kRecentId, recent());
        return;
    }
    if (m_byCategory.contains(cateId)) {
        setStickers(cateId, m_byCategory.value(cateId));
        return;
    }
    setStickers(cateId, {});
    ZaloSession* s = session();
    if (!s) return;
    busy(1);
    QPointer<StickerStore> self(this);
    s->fetchCategoryStickers(cateId, [self, cateId](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self) return;
        self->busy(-1);
        if (ok) self->applyCategory(cateId, data);
    });
}

void StickerStore::applyCategory(int cateId, const QJsonValue& data) {
    QVariantList list;
    for (const QJsonValue& v : stickerArray(data)) {
        QVariantMap info = describe(v.toObject());
        if (info.isEmpty()) continue;
        if (info.value("cateId").toInt() == 0) info.insert("cateId", cateId);
        remember(info);
        list.append(info);
    }
    m_byCategory.insert(cateId, list);
    if (m_current == cateId && m_query.isEmpty()) setStickers(cateId, list);
    emit categoriesChanged();
}

void StickerStore::lookup(const QString& keyword, int limit, std::function<void(const QVariantList&)> done) {
    ZaloSession* s = session();
    if (!s) {
        done({});
        return;
    }
    QPointer<StickerStore> self(this);
    auto finish = [self, done](const QList<qint64>& ids) {
        if (!self) return;
        auto results = std::make_shared<QHash<qint64, QVariantMap>>();
        auto remaining = std::make_shared<int>(0);
        auto publish = [self, ids, results, done]() {
            if (!self) return;
            QVariantList list;
            for (qint64 id : ids) {
                if (results->contains(id)) list.append(results->value(id));
            }
            done(list);
        };
        ZaloSession* session = self->session();
        for (qint64 id : ids) {
            const QVariantMap cached = self->info(QString::number(id));
            if (!cached.isEmpty() || !session) {
                if (!cached.isEmpty()) results->insert(id, cached);
                continue;
            }
            ++*remaining;
            session->fetchStickerDetail(id, [self, id, results, remaining, publish](bool ok, const QJsonValue& d, const QString&, int) {
                if (self && ok) {
                    const QVariantMap info = describe(d.toObject());
                    if (!info.isEmpty()) {
                        self->remember(info);
                        results->insert(id, info);
                    }
                }
                if (--*remaining == 0) publish();
            });
        }
        if (*remaining == 0) publish();
    };
    s->searchStickers(keyword, [self, finish, keyword, limit](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self) return;
        QList<qint64> ids;
        QJsonArray suggested = ok ? data.toObject().value("sugg_sticker").toArray() : QJsonArray();
        if (suggested.isEmpty() && ok)
            suggested = data.toObject().value("suggestions").toObject().value("sugg_sticker").toArray();
        for (const QJsonValue& v : suggested) {
            const qint64 id = v.toObject().value("sticker_id").toVariant().toLongLong();
            if (id > 0 && !ids.contains(id)) ids.append(id);
            if (ids.size() >= limit) break;
        }
        ZaloSession* session = self->session();
        if (!ids.isEmpty() || !session) {
            finish(ids);
            return;
        }
        session->searchStickerIds(keyword, limit, [finish, limit](bool ok2, const QJsonValue& d2, const QString&, int) {
            QList<qint64> found;
            for (const QJsonValue& v : (ok2 ? stickerArray(d2) : QJsonArray())) {
                const qint64 id = v.toObject().value("sticker_id").toVariant().toLongLong();
                if (id > 0 && !found.contains(id)) found.append(id);
                if (found.size() >= limit) break;
            }
            finish(found);
        });
    });
}

void StickerStore::search(const QString& keyword) {
    const QString q = keyword.trimmed();
    if (q.isEmpty()) {
        m_query.clear();
        selectCategory(m_current);
        return;
    }
    const quint64 seq = ++m_searchSeq;
    setStickers(m_current, {}, q);
    if (!session()) return;
    busy(1);
    QPointer<StickerStore> self(this);
    lookup(q, kMaxSearch, [self, seq, q](const QVariantList& list) {
        if (!self || seq != self->m_searchSeq) return;
        self->setStickers(self->m_current, list, q);
        self->busy(-1);
    });
}

void StickerStore::loadBirthdayStickers() {
    if (!m_birthday.isEmpty() || m_birthdayLoading || !session()) return;
    m_birthdayLoading = true;
    QPointer<StickerStore> self(this);
    lookup(QStringLiteral("sinh nhật"), kBirthdaySuggestions, [self](const QVariantList& list) {
        if (!self) return;
        if (!list.isEmpty()) {
            self->m_birthdayLoading = false;
            self->m_birthday = list;
            emit self->birthdayStickersChanged();
            return;
        }
        self->lookup(QStringLiteral("birthday"), kBirthdaySuggestions, [self](const QVariantList& fallback) {
            if (!self) return;
            self->m_birthdayLoading = false;
            self->m_birthday = fallback;
            emit self->birthdayStickersChanged();
        });
    });
}

void StickerStore::send(const QVariantMap& sticker) {
    const qint64 id = sticker.value("id").toLongLong();
    if (id <= 0) return;
    if (m_hooks.send) m_hooks.send(id, sticker.value("cateId").toLongLong(), sticker.value("type").toInt());
    const QString uid = m_hooks.account ? m_hooks.account() : QString();
    if (uid.isEmpty()) return;
    QVariantList list = recent();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).toMap().value("id").toString() == sticker.value("id").toString()) list.removeAt(i);
    }
    list.prepend(sticker);
    while (list.size() > kMaxRecent) list.removeLast();
    QSettings().setValue("stickers/recent/" + uid,
                         QJsonDocument(QJsonArray::fromVariantList(list)).toJson(QJsonDocument::Compact));
    emit categoriesChanged();
    if (m_current == kRecentId && m_query.isEmpty()) setStickers(kRecentId, list);
}

void StickerStore::reset() {
    m_personal.clear();
    m_byCategory.clear();
    m_loaded = false;
    m_query.clear();
    m_birthday.clear();
    m_birthdayLoading = false;
    setStickers(kRecentId, {});
    emit categoriesChanged();
    emit birthdayStickersChanged();
}

}
