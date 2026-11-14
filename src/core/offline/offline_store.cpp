#include "offline_store.h"

#include "../models/text_fold.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace Zalos::Core::Offline {

ZaloOfflineStore::ZaloOfflineStore(QObject* parent)
    : QObject(parent) {
    initializeDatabase();
}

ZaloOfflineStore::~ZaloOfflineStore() {
    m_saveMessageQuery.reset();
    maintenance();
    QString connName = m_db.connectionName();
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    if (!connName.isEmpty()) {
        QSqlDatabase::removeDatabase(connName);
    }
}

QByteArray ZaloOfflineStore::deriveSessionKey(const QString& zpwSekCookie) {
    if (zpwSekCookie.isEmpty()) return QByteArray();

    unsigned char hash[SHA256_DIGEST_LENGTH];
    QByteArray cookieBytes = zpwSekCookie.toUtf8();
    SHA256(reinterpret_cast<const unsigned char*>(cookieBytes.constData()), cookieBytes.size(), hash);
    return QByteArray(reinterpret_cast<const char*>(hash), SHA256_DIGEST_LENGTH);
}

bool ZaloOfflineStore::writeEncryptedSession(const QString& directory, const QString& zpwSekCookie, const QJsonObject& record) {
    QByteArray key = deriveSessionKey(zpwSekCookie);
    if (key.size() != 32) return false;

    QByteArray plain = QJsonDocument(record).toJson(QJsonDocument::Compact);
    if (plain.isEmpty() || plain.size() > 4 * 1024 * 1024) return false;

    unsigned char iv[12];
    if (RAND_bytes(iv, sizeof(iv)) != 1) return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    QByteArray cipherText(plain.size(), Qt::Uninitialized);
    int outLen = 0;
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(cipherText.data()), &outLen,
                          reinterpret_cast<const unsigned char*>(plain.constData()), plain.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    int finalLen = 0;
    EVP_EncryptFinal_ex(ctx, nullptr, &finalLen);

    unsigned char tag[16];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    EVP_CIPHER_CTX_free(ctx);

    QDir().mkpath(directory);
    QString filePath = QDir(directory).filePath("linux-offline-session.enc");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    file.write(reinterpret_cast<const char*>(iv), sizeof(iv));
    file.write(reinterpret_cast<const char*>(tag), sizeof(tag));
    file.write(cipherText.constData(), outLen);
    file.close();
    return true;
}

QJsonObject ZaloOfflineStore::readEncryptedSession(const QString& directory, const QString& zpwSekCookie) {
    QByteArray key = deriveSessionKey(zpwSekCookie);
    if (key.size() != 32) return QJsonObject();

    QString filePath = QDir(directory).filePath("linux-offline-session.enc");
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QJsonObject();

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 28) return QJsonObject();

    const unsigned char* iv = reinterpret_cast<const unsigned char*>(data.constData());
    const unsigned char* tag = reinterpret_cast<const unsigned char*>(data.constData() + 12);
    const unsigned char* cipher = reinterpret_cast<const unsigned char*>(data.constData() + 28);
    int cipherLen = data.size() - 28;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QJsonObject();

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.constData()), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return QJsonObject();
    }

    QByteArray plain(cipherLen, Qt::Uninitialized);
    int outLen = 0;
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &outLen, cipher, cipherLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return QJsonObject();
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(tag)) != 1 ||
        EVP_DecryptFinal_ex(ctx, nullptr, &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return QJsonObject();
    }
    EVP_CIPHER_CTX_free(ctx);

    QJsonDocument doc = QJsonDocument::fromJson(plain);
    return doc.isObject() ? doc.object() : QJsonObject();
}

void ZaloOfflineStore::clearEncryptedSession(const QString& directory) {
    QString filePath = QDir(directory).filePath("linux-offline-session.enc");
    QFile::remove(filePath);
}

namespace {
void ensureColumn(QSqlDatabase& db, const QString& table, const QString& column, const QString& type) {
    QSqlQuery info(db);
    info.exec(QStringLiteral("PRAGMA table_info(%1);").arg(table));
    while (info.next()) {
        if (info.value(1).toString() == column) return;
    }
    QSqlQuery alter(db);
    alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3;").arg(table, column, type));
}

}

bool ZaloOfflineStore::initializeDatabase(const QString& customPath) {
    QString dbPath = customPath;
    if (dbPath.isEmpty()) {
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(baseDir);
        dbPath = QDir(baseDir).filePath("offline.db");
    }

    QString connName = QString("offline_store_%1").arg(reinterpret_cast<quintptr>(this));
    m_db = QSqlDatabase::addDatabase("QSQLITE", connName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        return false;
    }
    QFile::setPermissions(dbPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL;");
    q.exec("PRAGMA synchronous=NORMAL;");
    q.exec("PRAGMA cache_size=-2048;");
    q.exec("PRAGMA temp_store=MEMORY;");
    q.exec("PRAGMA mmap_size=0;");
    q.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS conversations ("
           "id TEXT PRIMARY KEY, "
           "name TEXT, "
           "last_message TEXT, "
           "timestamp_str TEXT, "
           "unread_count INTEGER, "
           "is_online INTEGER, "
           "is_pinned INTEGER, "
           "avatar_url TEXT, "
           "category TEXT, "
           "sender_prefix TEXT"
           ");");
    for (const auto& [col, type] : {std::pair{"last_ts", "INTEGER DEFAULT 0"}, std::pair{"is_group", "INTEGER DEFAULT 0"},
                                     std::pair{"is_muted", "INTEGER DEFAULT 0"},
                                     std::pair{"member_count", "INTEGER DEFAULT 0"}}) {
        ensureColumn(m_db, "conversations", col, type);
    }

    q.exec("CREATE TABLE IF NOT EXISTS messages ("
           "id TEXT PRIMARY KEY, "
           "conv_id TEXT, "
           "sender_name TEXT, "
           "text TEXT, "
           "time_str TEXT, "
           "is_mine INTEGER, "
           "is_e2ee INTEGER, "
           "reaction TEXT, "
           "message_type TEXT, "
           "media_url TEXT"
           ");");
    for (const auto& [col, type] : {std::pair{"ts", "INTEGER DEFAULT 0"}, std::pair{"sender_id", "TEXT"},
                                     std::pair{"sender_avatar", "TEXT"}, std::pair{"status", "TEXT"},
                                     std::pair{"is_recalled", "INTEGER DEFAULT 0"}, std::pair{"cli_msg_id", "TEXT"},
                                     std::pair{"quote_msg_id", "TEXT"}, std::pair{"quote_sender", "TEXT"},
                                     std::pair{"quote_text", "TEXT"}, std::pair{"is_group", "INTEGER DEFAULT 0"},
                                     std::pair{"raw", "TEXT"}, std::pair{"href", "TEXT"},
                                     std::pair{"file_size", "INTEGER DEFAULT 0"}, std::pair{"expire_on", "INTEGER DEFAULT 0"},
                                     std::pair{"seen_uids", "TEXT DEFAULT ''"}}) {
        ensureColumn(m_db, "messages", col, type);
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_conv_ts ON messages(conv_id, ts);");
    q.exec("CREATE INDEX IF NOT EXISTS idx_messages_expire ON messages(expire_on) WHERE expire_on > 0;");

    q.exec("CREATE TABLE IF NOT EXISTS reactions (msg_id TEXT, uid TEXT, icon TEXT, ts INTEGER DEFAULT 0, "
           "PRIMARY KEY (msg_id, uid));");
    q.exec("CREATE TABLE IF NOT EXISTS reaction_events (msg_id TEXT, uid TEXT, icon TEXT, event_id TEXT, "
           "ts INTEGER DEFAULT 0, PRIMARY KEY (msg_id, event_id));");
    q.exec("INSERT OR IGNORE INTO reaction_events (msg_id, uid, icon, event_id, ts) "
           "SELECT msg_id, uid, icon, 'legacy:' || uid, ts FROM reactions;");
    q.exec("DELETE FROM reactions;");

    q.exec("CREATE TABLE IF NOT EXISTS profiles ("
           "id TEXT PRIMARY KEY, name TEXT, avatar TEXT, is_group INTEGER DEFAULT 0, "
           "member_count INTEGER DEFAULT 0, is_friend INTEGER DEFAULT 0);");
    ensureColumn(m_db, "profiles", "is_oa", "INTEGER DEFAULT 0");

    m_ftsAvailable = q.exec("CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(body, tokenize = 'unicode61 remove_diacritics 2');");
    if (m_ftsAvailable) {
        q.exec("CREATE TRIGGER IF NOT EXISTS messages_fts_delete AFTER DELETE ON messages BEGIN "
               "DELETE FROM messages_fts WHERE rowid = old.rowid; END;");
        q.exec("CREATE TRIGGER IF NOT EXISTS messages_fts_recall AFTER UPDATE OF is_recalled ON messages "
               "WHEN new.is_recalled = 1 BEGIN DELETE FROM messages_fts WHERE rowid = old.rowid; END;");
    } else {
        qWarning() << "SQLite without FTS5: message search covers the loaded history only";
        q.exec("DROP TRIGGER IF EXISTS messages_fts_delete;");
        q.exec("DROP TRIGGER IF EXISTS messages_fts_recall;");
    }

    migrateStorage();
    return true;
}

void ZaloOfflineStore::migrateStorage() {
    if (!m_db.isOpen()) return;
    auto meta = [this](const QString& key) {
        QSqlQuery m(m_db);
        m.prepare("SELECT value FROM meta WHERE key = :k;");
        m.bindValue(":k", key);
        return (m.exec() && m.next()) ? m.value(0).toString() : QString();
    };
    auto setMeta = [this](const QString& key, const QString& value) {
        QSqlQuery m(m_db);
        m.prepare("INSERT INTO meta (key, value) VALUES (:k, :v) ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
        m.bindValue(":k", key);
        m.bindValue(":v", value);
        m.exec();
    };
    if (m_ftsAvailable && meta("fts_version") != QLatin1String("1")) {
        m_db.transaction();
        QSqlQuery w(m_db);
        w.exec("DELETE FROM messages_fts;");
        QSqlQuery r(m_db);
        r.setForwardOnly(true);
        r.exec("SELECT rowid, text FROM messages WHERE is_recalled = 0 AND message_type <> 'system' "
               "AND length(text) > 0;");
        w.prepare("INSERT INTO messages_fts (rowid, body) VALUES (:rowid, :body);");
        while (r.next()) {
            w.bindValue(":rowid", r.value(0));
            w.bindValue(":body", Models::foldForSearch(r.value(1).toString()));
            w.exec();
        }
        setMeta("fts_version", "1");
        m_db.commit();
    }
    if (meta("ttl_version") != QLatin1String("2")) {
        m_db.transaction();
        QSqlQuery r(m_db);
        r.setForwardOnly(true);
        r.exec("SELECT rowid, raw, ts, expire_on FROM messages WHERE raw IS NOT NULL AND length(raw) > 0;");
        QSqlQuery w(m_db);
        w.prepare("UPDATE messages SET expire_on = :e WHERE rowid = :rowid;");
        while (r.next()) {
            const qint64 expireOn = expiryOf(decodeRaw(r.value(1)), r.value(2).toLongLong());
            if (expireOn == r.value(3).toLongLong()) continue;
            w.bindValue(":e", expireOn);
            w.bindValue(":rowid", r.value(0));
            w.exec();
        }
        setMeta("ttl_version", "2");
        m_db.commit();
    }
    if (meta("media_label_cleanup") != QLatin1String("1")) {
        QSqlQuery u(m_db);
        u.exec("UPDATE messages SET text = '' WHERE message_type IN ('image', 'sticker', 'video') "
               "AND trim(text) LIKE '[%]' AND length(trim(text)) <= 26;");
        setMeta("media_label_cleanup", "1");
    }
    if (meta("dedup_cli_version") != QLatin1String("1")) {
        m_db.transaction();
        QSqlQuery d(m_db);
        d.exec("DELETE FROM messages WHERE rowid IN ("
               "SELECT m1.rowid FROM messages m1 "
               "JOIN messages m2 ON m1.conv_id = m2.conv_id "
               "AND m1.cli_msg_id = m2.cli_msg_id "
               "AND m1.cli_msg_id IS NOT NULL AND m1.cli_msg_id <> '' AND m1.cli_msg_id <> '0' "
               "AND (m1.rowid < m2.rowid OR (m1.id LIKE 'local-%' AND m2.id NOT LIKE 'local-%')));");
        setMeta("dedup_cli_version", "1");
        m_db.commit();
    }
    if (meta("raw_codec") != QLatin1String("z1")) {
        m_db.transaction();
        QSqlQuery r(m_db);
        r.setForwardOnly(true);
        r.exec("SELECT rowid, raw FROM messages WHERE typeof(raw) = 'text' AND length(raw) > 0;");
        QSqlQuery w(m_db);
        w.prepare("UPDATE messages SET raw = :raw WHERE rowid = :rowid;");
        while (r.next()) {
            const QJsonObject raw = decodeRaw(r.value(1));
            if (raw.isEmpty()) continue;
            w.bindValue(":raw", encodeRaw(raw));
            w.bindValue(":rowid", r.value(0));
            w.exec();
        }
        setMeta("raw_codec", "z1");
        m_db.commit();
        QSqlQuery v(m_db);
        v.exec("PRAGMA auto_vacuum = INCREMENTAL;");
        v.exec("VACUUM;");
    }
}

QByteArray ZaloOfflineStore::encodeRaw(const QJsonObject& raw) {
    if (raw.isEmpty()) return {};
    return qCompress(QJsonDocument(raw).toJson(QJsonDocument::Compact), 6);
}

QJsonObject ZaloOfflineStore::decodeRaw(const QVariant& stored) {
    if (!stored.isValid() || stored.isNull()) return {};
    QByteArray bytes;
    if (stored.typeId() == QMetaType::QByteArray) {
        bytes = stored.toByteArray();
        if (!bytes.isEmpty() && bytes.at(0) != '{') bytes = qUncompress(bytes);
    } else {
        bytes = stored.toString().toUtf8();
    }
    return bytes.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(bytes).object();
}

void ZaloOfflineStore::maintenance() {
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    q.exec("PRAGMA optimize;");
    q.exec("PRAGMA incremental_vacuum;");
}

void ZaloOfflineStore::indexMessage(const QString& msgId, const Models::MessageItem& item) {
    if (!m_ftsAvailable) return;
    QSqlQuery q(m_db);
    q.prepare("SELECT rowid, is_recalled FROM messages WHERE id = :id;");
    q.bindValue(":id", msgId);
    if (!q.exec() || !q.next()) return;
    const qint64 rowid = q.value(0).toLongLong();
    const bool recalled = item.isRecalled || q.value(1).toInt() == 1;
    q.prepare("DELETE FROM messages_fts WHERE rowid = :rowid;");
    q.bindValue(":rowid", rowid);
    q.exec();
    if (recalled || item.text.trimmed().isEmpty() || item.messageType == QLatin1String("system")) return;
    q.prepare("INSERT INTO messages_fts (rowid, body) VALUES (:rowid, :body);");
    q.bindValue(":rowid", rowid);
    q.bindValue(":body", Models::foldForSearch(item.text));
    q.exec();
}

QString ZaloOfflineStore::ftsQuery(const QString& input) {
    const QString folded = Models::foldForSearch(input);
    QStringList terms;
    QString word;
    auto flush = [&]() {
        if (!word.isEmpty()) terms << QStringLiteral("\"%1\"*").arg(word);
        word.clear();
    };
    for (const QChar c : folded) {
        if (c.isLetterOrNumber()) word.append(c);
        else flush();
    }
    flush();
    return terms.join(QLatin1Char(' '));
}

QVector<ZaloOfflineStore::SearchHit> ZaloOfflineStore::searchMessages(const QString& query, const QString& convId,
                                                                      int limit) const {
    QVector<SearchHit> hits;
    const QString match = ftsQuery(query);
    if (!m_db.isOpen() || match.isEmpty()) return hits;
    if (!m_ftsAvailable) {
        const QStringList terms = Models::foldTerms(query);
        QSqlQuery s(m_db);
        s.setForwardOnly(true);
        s.prepare(QStringLiteral("SELECT id, conv_id, sender_name, text, ts FROM messages WHERE is_recalled = 0 "
                                 "AND message_type <> 'system' AND length(text) > 0 %1 ORDER BY ts DESC LIMIT 20000;")
                      .arg(convId.isEmpty() ? QString() : QStringLiteral("AND conv_id = :conv")));
        if (!convId.isEmpty()) s.bindValue(":conv", convId);
        if (!s.exec()) return hits;
        while (s.next() && hits.size() < limit) {
            const QString text = s.value(3).toString();
            if (!Models::containsAllTerms(Models::foldForSearch(text), terms)) continue;
            hits.append({s.value(0).toString(), s.value(1).toString(), s.value(2).toString(), text,
                         s.value(4).toLongLong()});
        }
        return hits;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT m.id, m.conv_id, m.sender_name, m.text, m.ts FROM messages_fts f "
                             "JOIN messages m ON m.rowid = f.rowid WHERE messages_fts MATCH :match %1 "
                             "ORDER BY m.ts DESC LIMIT :limit;")
                  .arg(convId.isEmpty() ? QString() : QStringLiteral("AND m.conv_id = :conv")));
    q.bindValue(":match", match);
    if (!convId.isEmpty()) q.bindValue(":conv", convId);
    q.bindValue(":limit", limit);
    if (!q.exec()) return hits;
    while (q.next()) {
        hits.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                     q.value(4).toLongLong()});
    }
    return hits;
}

qint64 ZaloOfflineStore::messageTs(const QString& msgId, QString* convId) const {
    if (!m_db.isOpen() || msgId.isEmpty()) return 0;
    QSqlQuery q(m_db);
    q.prepare("SELECT ts, conv_id FROM messages WHERE id = :id OR cli_msg_id = :cli LIMIT 1;");
    q.bindValue(":id", msgId);
    q.bindValue(":cli", msgId);
    if (!q.exec() || !q.next()) return 0;
    if (convId) *convId = q.value(1).toString();
    return q.value(0).toLongLong();
}

int ZaloOfflineStore::countMessagesBetween(const QString& convId, qint64 fromTs, qint64 toTs) const {
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    q.prepare("SELECT count(*) FROM messages WHERE conv_id = :conv AND ts >= :from AND ts <= :to;");
    q.bindValue(":conv", convId);
    q.bindValue(":from", fromTs);
    q.bindValue(":to", toTs);
    return (q.exec() && q.next()) ? q.value(0).toInt() : 0;
}

bool ZaloOfflineStore::addReactionEvent(const QString& msgId, const QString& uid, const QString& icon,
                                         const QString& eventId, qint64 ts) {
    if (!m_db.isOpen() || msgId.isEmpty() || uid.isEmpty() || icon.isEmpty() || eventId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO reaction_events (msg_id, uid, icon, event_id, ts) VALUES (:m, :u, :i, :e, :t);");
    q.bindValue(":m", msgId);
    q.bindValue(":u", uid);
    q.bindValue(":i", icon);
    q.bindValue(":e", eventId);
    q.bindValue(":t", ts);
    return q.exec() && q.numRowsAffected() > 0;
}

bool ZaloOfflineStore::removeReactionEvent(const QString& msgId, const QString& eventId) {
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM reaction_events WHERE msg_id = :m AND event_id = :e;");
    q.bindValue(":m", msgId);
    q.bindValue(":e", eventId);
    return q.exec();
}

bool ZaloOfflineStore::clearReactions(const QString& msgId, const QString& uid) {
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM reaction_events WHERE msg_id = :m AND uid = :u;");
    q.bindValue(":m", msgId);
    q.bindValue(":u", uid);
    return q.exec();
}

QVector<ZaloOfflineStore::ReactionEvent> ZaloOfflineStore::reactionEventsOf(const QString& msgId) const {
    QVector<ReactionEvent> out;
    if (!m_db.isOpen() || msgId.isEmpty()) return out;
    QSqlQuery q(m_db);
    q.prepare("SELECT uid, icon, event_id, ts FROM reaction_events WHERE msg_id = :m ORDER BY ts, rowid;");
    q.bindValue(":m", msgId);
    if (q.exec()) {
        while (q.next()) out.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toLongLong()});
    }
    return out;
}

QSet<QString> ZaloOfflineStore::reactedMessageIds() const {
    QSet<QString> out;
    if (!m_db.isOpen()) return out;
    QSqlQuery q(m_db);
    if (q.exec("SELECT DISTINCT msg_id FROM reaction_events;")) {
        while (q.next()) out.insert(q.value(0).toString());
    }
    return out;
}

bool ZaloOfflineStore::setMessageReaction(const QString& msgId, const QString& summary) {
    if (!m_db.isOpen() || msgId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE messages SET reaction = :r WHERE id = :id;");
    q.bindValue(":r", summary);
    q.bindValue(":id", msgId);
    return q.exec();
}

QString ZaloOfflineStore::pragmaForTesting(const QString& name) const {
    if (!m_db.isOpen()) return {};
    QSqlQuery q(m_db);
    return q.exec(QStringLiteral("PRAGMA %1;").arg(name)) && q.next() ? q.value(0).toString() : QString();
}

QString ZaloOfflineStore::ownerUid() const {
    if (!m_db.isOpen()) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM meta WHERE key = 'owner_uid';");
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

bool ZaloOfflineStore::bindOwner(const QString& uid) {
    if (!m_db.isOpen() || uid.isEmpty()) return false;
    const QString current = ownerUid();
    if (current == uid) return false;
    clearCache();
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO meta (key, value) VALUES ('owner_uid', :uid);");
    q.bindValue(":uid", uid);
    q.exec();
    return !current.isEmpty();
}

void ZaloOfflineStore::clearCache() {
    if (!m_db.isOpen()) return;
    m_saveMessageQuery.reset();
    QSqlQuery q(m_db);
    for (const char* table : {"messages", "reactions", "reaction_events", "conversations", "profiles", "meta"}) {
        q.exec(QStringLiteral("DELETE FROM %1;").arg(QLatin1String(table)));
    }
}

bool ZaloOfflineStore::saveConversation(const Models::ConversationItem& item) {
    if (!m_db.isOpen() || item.id.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO conversations (id, name, last_message, timestamp_str, unread_count, is_online, "
              "is_pinned, avatar_url, category, sender_prefix, last_ts, is_group, is_muted, member_count) "
              "VALUES (:id, :name, :last_message, :timestamp_str, :unread_count, :is_online, :is_pinned, :avatar_url, "
              ":category, :sender_prefix, :last_ts, :is_group, :is_muted, :member_count);");
    q.bindValue(":id", item.id);
    q.bindValue(":name", item.name);
    q.bindValue(":last_message", item.lastMessage);
    q.bindValue(":timestamp_str", item.timestampStr);
    q.bindValue(":unread_count", item.unreadCount);
    q.bindValue(":is_online", 0);
    q.bindValue(":is_pinned", item.isPinned ? 1 : 0);
    q.bindValue(":avatar_url", item.avatarUrl);
    q.bindValue(":category", item.category);
    q.bindValue(":sender_prefix", item.senderPrefix);
    q.bindValue(":last_ts", item.lastTs);
    q.bindValue(":is_group", item.isGroup ? 1 : 0);
    q.bindValue(":is_muted", item.isMuted ? 1 : 0);
    q.bindValue(":member_count", item.memberCount);
    return q.exec();
}

QVector<Models::ConversationItem> ZaloOfflineStore::loadConversations() {
    QVector<Models::ConversationItem> list;
    if (!m_db.isOpen()) return list;

    QSqlQuery q("SELECT id, name, last_message, timestamp_str, unread_count, is_pinned, avatar_url, category, "
                "sender_prefix, last_ts, is_group, is_muted, member_count FROM conversations "
                "ORDER BY is_pinned DESC, last_ts DESC;", m_db);
    while (q.next()) {
        Models::ConversationItem item;
        item.id = q.value(0).toString();
        item.name = q.value(1).toString();
        item.lastMessage = q.value(2).toString();
        item.timestampStr = q.value(3).toString();
        item.unreadCount = q.value(4).toInt();
        item.isPinned = q.value(5).toInt() == 1;
        item.avatarUrl = q.value(6).toString();
        item.category = q.value(7).toString();
        if (item.category.isEmpty()) item.category = QStringLiteral("priority");
        item.senderPrefix = q.value(8).toString();
        item.lastTs = q.value(9).toLongLong();
        item.isGroup = q.value(10).toInt() == 1;
        item.isMuted = q.value(11).toInt() == 1;
        item.memberCount = q.value(12).toInt();
        list.append(item);
    }
    return list;
}

bool ZaloOfflineStore::saveMessage(const QString& convId, const Models::MessageItem& item, const QJsonObject& raw,
                                   bool isGroup) {
    if (!m_db.isOpen() || item.id.isEmpty() || item.id.startsWith(QLatin1String("local-"))) return false;

    if (!item.cliMsgId.isEmpty() && item.cliMsgId != QLatin1String("0")) {
        QSqlQuery chk(m_db);
        chk.prepare("SELECT id, sender_id FROM messages WHERE conv_id = :conv AND cli_msg_id = :cli AND id <> :id;");
        chk.bindValue(":conv", convId);
        chk.bindValue(":cli", item.cliMsgId);
        chk.bindValue(":id", item.id);
        QStringList stale;
        if (chk.exec()) {
            while (chk.next()) {
                const QString existingId = chk.value(0).toString();
                if (existingId.startsWith(QLatin1String("local-")) || chk.value(1).toString() == item.senderId) stale << existingId;
            }
        }
        for (const QString& oldId : stale) {
            QSqlQuery del(m_db);
            del.prepare("DELETE FROM messages WHERE id = :oldId;");
            del.bindValue(":oldId", oldId);
            del.exec();
        }
    }

    if (!m_saveMessageQuery) {
        m_saveMessageQuery = std::make_unique<QSqlQuery>(m_db);
        m_saveMessageQuery->prepare("INSERT INTO messages (id, conv_id, sender_name, text, time_str, is_mine, is_e2ee, reaction, message_type, "
                  "media_url, ts, sender_id, sender_avatar, status, is_recalled, cli_msg_id, quote_msg_id, quote_sender, "
                  "quote_text, is_group, raw, href, file_size, expire_on, seen_uids) VALUES (:id, :conv_id, :sender_name, :text, :time_str, :is_mine, :is_e2ee, "
                  ":reaction, :message_type, :media_url, :ts, :sender_id, :sender_avatar, :status, :is_recalled, "
                  ":cli_msg_id, :quote_msg_id, :quote_sender, :quote_text, :is_group, :raw, :href, :file_size, :expire_on, "
                  ":seen_uids) "
                  "ON CONFLICT(id) DO UPDATE SET conv_id = excluded.conv_id, sender_name = excluded.sender_name, "
                  "text = CASE WHEN messages.is_recalled = 1 THEN messages.text ELSE excluded.text END, "
                  "time_str = excluded.time_str, is_mine = excluded.is_mine, "
                  "is_e2ee = excluded.is_e2ee, reaction = CASE WHEN coalesce(excluded.reaction, '') <> '' "
                  "THEN excluded.reaction ELSE messages.reaction END, "
                  "message_type = CASE WHEN messages.is_recalled = 1 THEN messages.message_type ELSE excluded.message_type END, "
                  "media_url = CASE WHEN messages.is_recalled = 1 THEN '' ELSE excluded.media_url END, ts = excluded.ts, sender_id = excluded.sender_id, "
                  "sender_avatar = excluded.sender_avatar, "
                  "status = CASE WHEN messages.status = 'seen' THEN messages.status ELSE excluded.status END, "
                  "is_recalled = MAX(messages.is_recalled, excluded.is_recalled), "
                  "cli_msg_id = excluded.cli_msg_id, quote_msg_id = excluded.quote_msg_id, "
                  "quote_sender = excluded.quote_sender, quote_text = excluded.quote_text, is_group = excluded.is_group, "
                  "href = excluded.href, file_size = excluded.file_size, "
                  "expire_on = CASE WHEN length(coalesce(excluded.raw, '')) > 0 THEN excluded.expire_on "
                  "ELSE messages.expire_on END, "
                  "raw = COALESCE(NULLIF(excluded.raw, ''), messages.raw), "
                  "seen_uids = CASE WHEN length(coalesce(excluded.seen_uids, '')) > 0 "
                  "THEN excluded.seen_uids ELSE messages.seen_uids END;");
    }
    QSqlQuery& q = *m_saveMessageQuery;
    q.bindValue(":id", item.id);
    q.bindValue(":conv_id", convId);
    q.bindValue(":sender_name", item.senderName);
    q.bindValue(":text", item.text);
    q.bindValue(":time_str", item.timeStr);
    q.bindValue(":is_mine", item.isMine ? 1 : 0);
    q.bindValue(":is_e2ee", item.isE2EE ? 1 : 0);
    q.bindValue(":reaction", item.reaction);
    q.bindValue(":message_type", item.messageType);
    q.bindValue(":media_url", item.mediaUrl);
    q.bindValue(":ts", item.ts);
    q.bindValue(":sender_id", item.senderId);
    q.bindValue(":sender_avatar", item.senderAvatar);
    q.bindValue(":status", item.status);
    q.bindValue(":is_recalled", item.isRecalled ? 1 : 0);
    q.bindValue(":cli_msg_id", item.cliMsgId);
    q.bindValue(":quote_msg_id", item.quoteMsgId);
    q.bindValue(":quote_sender", item.quoteSender);
    q.bindValue(":quote_text", item.quoteText);
    q.bindValue(":is_group", isGroup ? 1 : 0);
    const QByteArray encoded = encodeRaw(raw);
    q.bindValue(":raw", encoded.isEmpty() ? QVariant(QString()) : QVariant(encoded));
    q.bindValue(":href", item.href);
    q.bindValue(":file_size", item.fileSize);
    q.bindValue(":expire_on", expiryOf(raw, item.ts));
    q.bindValue(":seen_uids", item.seenUids.join(QLatin1Char(',')));
    if (!q.exec()) return false;
    indexMessage(item.id, item);
    return true;
}

bool ZaloOfflineStore::deleteConversation(const QString& convId) {
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM messages WHERE conv_id = :conv_id;");
    q.bindValue(":conv_id", convId);
    bool ok = q.exec();
    q.prepare("DELETE FROM conversations WHERE id = :id;");
    q.bindValue(":id", convId);
    return q.exec() && ok;
}

bool ZaloOfflineStore::recordGroupSeen(const QString& msgId, const QString& userId) {
    if (!m_db.isOpen() || msgId.isEmpty() || userId.isEmpty()) return false;
    QSqlQuery r(m_db);
    r.prepare("SELECT seen_uids FROM messages WHERE id = :id LIMIT 1;");
    r.bindValue(":id", msgId);
    QStringList seen;
    if (r.exec() && r.next()) seen = r.value(0).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (seen.contains(userId)) return true;
    seen.append(userId);
    QSqlQuery q(m_db);
    q.prepare("UPDATE messages SET status = 'seen', seen_uids = :seen WHERE id = :id;");
    q.bindValue(":seen", seen.join(QLatin1Char(',')));
    q.bindValue(":id", msgId);
    return q.exec() && q.numRowsAffected() > 0;
}

QJsonObject ZaloOfflineStore::rawMessage(const QString& msgId) const {
    if (!m_db.isOpen() || msgId.isEmpty()) return {};
    QSqlQuery q(m_db);
    q.prepare("SELECT raw FROM messages WHERE id = :id LIMIT 1;");
    q.bindValue(":id", msgId);
    if (!q.exec() || !q.next()) return {};
    return decodeRaw(q.value(0));
}

QVector<Models::MessageItem> ZaloOfflineStore::loadMessages(const QString& convId, QHash<QString, QJsonObject>* raws,
                                                            int limit, qint64 beforeTs) {
    QVector<Models::MessageItem> list;
    if (!m_db.isOpen()) return list;

    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM (SELECT id, sender_name, text, time_str, is_mine, is_e2ee, reaction, message_type, "
              "media_url, ts, sender_id, sender_avatar, status, is_recalled, cli_msg_id, quote_msg_id, quote_sender, "
              "quote_text, raw, href, file_size, rowid AS r, seen_uids FROM messages WHERE conv_id = :conv_id AND (:before <= 0 OR ts <= :before) "
              "AND (expire_on = 0 OR expire_on > :now) ORDER BY ts DESC, r DESC LIMIT :limit) "
              "ORDER BY ts ASC, r ASC;");
    q.bindValue(":conv_id", convId);
    q.bindValue(":limit", limit);
    q.bindValue(":before", beforeTs);
    q.bindValue(":now", QDateTime::currentMSecsSinceEpoch());
    if (q.exec()) {
        while (q.next()) {
            Models::MessageItem item;
            item.id = q.value(0).toString();
            item.senderName = q.value(1).toString();
            item.text = q.value(2).toString();
            item.timeStr = q.value(3).toString();
            item.isMine = q.value(4).toInt() == 1;
            item.isE2EE = q.value(5).toInt() == 1;
            item.reaction = q.value(6).toString();
            item.messageType = q.value(7).toString();
            item.mediaUrl = q.value(8).toString();
            item.ts = q.value(9).toLongLong();
            if (item.ts < 946684800000LL) item.timeStr.clear();
            item.senderId = q.value(10).toString();
            item.senderAvatar = q.value(11).toString();
            item.status = q.value(12).toString();
            item.isRecalled = q.value(13).toInt() == 1;
            item.cliMsgId = q.value(14).toString();
            item.quoteMsgId = q.value(15).toString();
            item.quoteSender = q.value(16).toString();
            item.quoteText = q.value(17).toString();
            item.href = q.value(19).toString();
            item.fileSize = q.value(20).toLongLong();
            const QString seenColumn = q.value(22).toString();
            if (!seenColumn.isEmpty()) item.seenUids = seenColumn.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (raws) {
                const QJsonObject raw = decodeRaw(q.value(18));
                if (!raw.isEmpty()) raws->insert(item.id, raw);
            }
            list.append(item);
        }
    }
    return list;
}

QString ZaloOfflineStore::metaValue(const QString& key) const {
    if (!m_db.isOpen()) return {};
    QSqlQuery m(m_db);
    m.prepare("SELECT value FROM meta WHERE key = :k;");
    m.bindValue(":k", key);
    return (m.exec() && m.next()) ? m.value(0).toString() : QString();
}

void ZaloOfflineStore::setMetaValue(const QString& key, const QString& value) {
    if (!m_db.isOpen()) return;
    QSqlQuery m(m_db);
    m.prepare("INSERT INTO meta (key, value) VALUES (:k, :v) ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
    m.bindValue(":k", key);
    m.bindValue(":v", value);
    m.exec();
}

QSet<QString> ZaloOfflineStore::reparseStored(
    const std::function<bool(const QJsonObject& raw, bool isGroup, Models::MessageItem* out)>& fn) {
    QSet<QString> touched;
    if (!m_db.isOpen()) return touched;
    struct Update { QString id; QString convId; Models::MessageItem item; };
    QVector<Update> updates;
    {
        QSqlQuery r(m_db);
        r.setForwardOnly(true);
        r.exec("SELECT id, conv_id, is_group, raw, text, message_type FROM messages "
               "WHERE is_recalled = 0 AND raw IS NOT NULL AND length(raw) > 0;");
        while (r.next()) {
            const QJsonObject raw = decodeRaw(r.value(3));
            if (raw.isEmpty()) continue;
            Models::MessageItem item;
            if (!fn(raw, r.value(2).toInt() == 1, &item)) continue;
            if (item.text == r.value(4).toString() && item.messageType == r.value(5).toString()) continue;
            updates.append({r.value(0).toString(), r.value(1).toString(), item});
        }
    }
    if (updates.isEmpty()) return touched;
    m_db.transaction();
    QSqlQuery w(m_db);
    w.prepare("UPDATE messages SET text = :text, message_type = :type, href = :href, media_url = :media WHERE id = :id;");
    for (const Update& u : std::as_const(updates)) {
        w.bindValue(":text", u.item.text);
        w.bindValue(":type", u.item.messageType);
        w.bindValue(":href", u.item.href);
        w.bindValue(":media", u.item.mediaUrl);
        w.bindValue(":id", u.id);
        if (!w.exec()) continue;
        indexMessage(u.id, u.item);
        touched.insert(u.convId);
    }
    m_db.commit();
    return touched;
}

qint64 ZaloOfflineStore::expiryOf(const QJsonObject& raw, qint64 ts) {
    static const QSet<QString> broadcast{QStringLiteral("chat.webcontent"), QStringLiteral("chat.webcontent.v2"),
                                         QStringLiteral("chat.list.action"), QStringLiteral("chat.ecard")};
    if (ts <= 0 || broadcast.contains(raw.value("msgType").toString())) return 0;
    const qint64 ttl = raw.value("ttl").toVariant().toLongLong();
    return ttl > 0 ? ts + ttl : 0;
}

QVector<QPair<QString, QString>> ZaloOfflineStore::purgeExpired(qint64 now) {
    QVector<QPair<QString, QString>> gone;
    if (!m_db.isOpen()) return gone;
    QSqlQuery q(m_db);
    q.prepare("SELECT conv_id, id FROM messages WHERE expire_on > 0 AND expire_on <= :now;");
    q.bindValue(":now", now);
    if (q.exec()) {
        while (q.next()) gone.append({q.value(0).toString(), q.value(1).toString()});
    }
    if (gone.isEmpty()) return gone;
    q.prepare("DELETE FROM messages WHERE expire_on > 0 AND expire_on <= :now;");
    q.bindValue(":now", now);
    q.exec();
    return gone;
}

qint64 ZaloOfflineStore::nextExpiry(qint64 now) const {
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    q.prepare("SELECT MIN(expire_on) FROM messages WHERE expire_on > :now;");
    q.bindValue(":now", now);
    return (q.exec() && q.next()) ? q.value(0).toLongLong() : 0;
}

bool ZaloOfflineStore::markRecalled(const QString& msgId, const QString& placeholder) {
    if (!m_db.isOpen() || msgId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE messages SET is_recalled = 1, text = :text, message_type = 'text', media_url = '', "
              "reaction = '' WHERE id = :id;");
    q.bindValue(":text", placeholder);
    q.bindValue(":id", msgId);
    if (!q.exec()) return false;
    const bool touched = q.numRowsAffected() > 0;
    Models::MessageItem gone;
    gone.isRecalled = true;
    if (touched) indexMessage(msgId, gone);
    return touched;
}

bool ZaloOfflineStore::deleteMessage(const QString& msgId) {
    if (!m_db.isOpen() || msgId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM messages WHERE id = :id;");
    q.bindValue(":id", msgId);
    return q.exec();
}

bool ZaloOfflineStore::deleteMessagesUpTo(const QString& convId, const QString& lastMsgId) {
    if (!m_db.isOpen() || convId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM messages WHERE conv_id = :conv AND CAST(id AS INTEGER) <= CAST(:last AS INTEGER);");
    q.bindValue(":conv", convId);
    q.bindValue(":last", lastMsgId);
    return q.exec();
}

bool ZaloOfflineStore::saveProfile(const CachedProfile& p) {
    if (!m_db.isOpen() || p.id.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO profiles (id, name, avatar, is_group, member_count, is_friend, is_oa) "
              "VALUES (:id, :name, :avatar, :is_group, :member_count, :is_friend, :is_oa);");
    q.bindValue(":id", p.id);
    q.bindValue(":name", p.name);
    q.bindValue(":avatar", p.avatar);
    q.bindValue(":is_group", p.isGroup ? 1 : 0);
    q.bindValue(":member_count", p.memberCount);
    q.bindValue(":is_friend", p.isFriend ? 1 : 0);
    q.bindValue(":is_oa", p.isOfficial ? 1 : 0);
    return q.exec();
}

QVector<ZaloOfflineStore::CachedProfile> ZaloOfflineStore::loadProfiles() {
    QVector<CachedProfile> list;
    if (!m_db.isOpen()) return list;
    QSqlQuery q("SELECT id, name, avatar, is_group, member_count, is_friend, is_oa FROM profiles;", m_db);
    while (q.next()) {
        list.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toInt() == 1,
                     q.value(4).toInt(), q.value(5).toInt() == 1, q.value(6).toInt() == 1});
    }
    return list;
}

bool ZaloOfflineStore::beginBatch() {
    return m_db.isOpen() && m_db.transaction();
}

void ZaloOfflineStore::endBatch() {
    if (m_db.isOpen()) m_db.commit();
}

void ZaloOfflineStore::setOfflineActive(bool active) {
    const QString state = active ? QStringLiteral("DISCONNECTED") : QStringLiteral("CONNECTED");
    const bool recovered = !active && m_syncState == "CHECKING";
    if (m_isOfflineActive != active) {
        m_isOfflineActive = active;
        emit offlineStateChanged(m_isOfflineActive);
    }
    if (m_syncState != state) {
        m_syncState = state;
        emit syncStateChanged(m_syncState);
    }
    if (recovered) emit recoveryCompleted(true);
}

void ZaloOfflineStore::startSyncRecovery() {
    m_syncState = "CHECKING";
    emit syncStateChanged(m_syncState);
}

}
