#include "mobile_history_sync.h"

#include "crypto/db_cross.h"
#include "network/error_catalog.h"
#include "network/zalo_session.h"
#include "sync/v2/sync_v2_crypto.h"
#include "sync/v2/sync_v2_fallback.h"
#include "sync/v2/sync_v2_frames.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>

namespace Zalos::Core::Sync {

namespace {

constexpr int kConfirmTimeoutMs = 3 * 60 * 1000;
constexpr int kBackupTimeoutMs = 30 * 60 * 1000;
constexpr int kNoiseBatch = 100;
constexpr int kDownloadStallMs = 2 * 60 * 1000;
constexpr int kMaxDownloadAttempts = 5;
constexpr int kDownloadRetryBaseMs = 1000;
constexpr int kDownloadRetryMaxMs = 8000;
constexpr int kRetryAfterMaxMs = 30000;
constexpr qint64 kMaxV2BatchBytes = 64LL * 1024 * 1024;
constexpr qint64 kMaxSyncLogBytes = 2LL * 1024 * 1024;

QString typeName(int t) {
    switch (t) {
    case 2: return QStringLiteral("chat.doodle");
    case 3:
    case 4:
    case 31: return QStringLiteral("chat.photo");
    case 6: return QStringLiteral("chat.voice");
    case 10: return QStringLiteral("chat.sticker");
    case 12: return QStringLiteral("chat.recommended");
    case 15: return QStringLiteral("chat.list.action");
    case 18: return QStringLiteral("chat.location.new");
    case 19: return QStringLiteral("chat.video.msg");
    case 22: return QStringLiteral("share.file");
    case 23: return QStringLiteral("chat.gif");
    case 24: return QStringLiteral("chat.webcontent");
    case 25: return QStringLiteral("chat.video.live.msg");
    case 26: return QStringLiteral("group.poll");
    case 33: return QStringLiteral("chat.delete");
    case 36: return QStringLiteral("chat.undo");
    case 53: return QStringLiteral("chat.webcontent.v2");
    case 0:
    case 20:
    default: return QStringLiteral("webchat");
    }
}

bool isSkippedType(int t) {
    static const QList<int> skipped{20, 21, 25, 26, 29, 32, 33, 34, 35, 36, 45, 51, 52};
    return skipped.contains(t);
}

QString str(const std::string& s) {
    return QString::fromStdString(s);
}

bool isPlaceholderCaption(const QString& s) {
    static const QRegularExpression re(QStringLiteral("^\\[[^\\]]{1,24}\\]$"));
    return re.match(s.trimmed()).hasMatch();
}

bool isDigits(const QString& s) {
    if (s.isEmpty()) return false;
    for (const QChar c : s) {
        if (!c.isDigit()) return false;
    }
    return true;
}

QString firstColumn(const QSqlRecord& rec, const QSqlQuery& q, std::initializer_list<const char*> names) {
    for (const char* n : names) {
        const int i = rec.indexOf(QLatin1String(n));
        if (i >= 0 && !q.value(i).isNull()) return q.value(i).toString();
    }
    return {};
}

}

MobileHistorySync::MobileHistorySync(Network::ZaloSession* session, QObject* parent)
    : QObject(parent), m_session(session) {
    QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("sync")))
        .removeRecursively();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_state == WaitingConfirm
            && V2::decideFallback(0, true, m_sawSyncV2) == V2::FallbackTrigger::ConfirmTimeoutAfterV2) {
            startV2Fallback(QStringLiteral("confirm timeout after transfer_sync2"));
            return;
        } else if (m_state == WaitingConfirm) {
            fail(L("Điện thoại chưa xác nhận yêu cầu đồng bộ. Lưu ý: đồng bộ tin nhắn từ điện thoại (đặc biệt trên iPhone / Sync v2) đang ở mức thử nghiệm (beta).",
                   "The phone did not confirm the sync request. Note: phone history sync (especially on iPhone / Sync v2) is currently in beta."));
        } else if (m_state == PhonePreparing) {
            fail(L("Điện thoại chưa gửi bản sao lưu sau 30 phút.", "The phone did not send a backup within 30 minutes."));
        } else if (m_state == Importing && m_download) {
            fail(L("Tải bản sao lưu bị gián đoạn do mất kết nối mạng quá lâu.",
                   "Backup download timed out due to network inactivity."));
        }
    });
}

MobileHistorySync::~MobileHistorySync() {
    cleanup();
}

QString MobileHistorySync::L(const char* vi, const char* en) const {
    return QString::fromUtf8(m_lang == "en" ? en : vi);
}

void MobileHistorySync::log(const QString& line) const {
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("logs");
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(QStringLiteral("mobile-sync.log")));
    const QIODevice::OpenMode mode = (f.exists() && f.size() > kMaxSyncLogBytes)
        ? (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
        : (QIODevice::Append | QIODevice::Text);
    if (!f.open(mode)) return;
    f.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + line + "\n").toUtf8());
}

namespace {
QString shapeOf(const QJsonValue& v) {
    if (v.isArray()) return QStringLiteral("array[%1]").arg(v.toArray().size());
    if (!v.isObject()) return v.isString() ? "string" : v.isDouble() ? "number" : v.isBool() ? "bool" : "null";
    QStringList parts;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) parts << it.key() + ":" + shapeOf(it.value());
    return "{" + parts.join(",") + "}";
}
}

void MobileHistorySync::setState(int state, const QString& status, int progress) {
    log(QStringLiteral("state %1 progress %2").arg(state).arg(progress));
    m_state = state;
    emit stateChanged(state, status, progress);
}

void MobileHistorySync::fail(const QString& reason) {
    m_timeout.stop();
    ++m_generation;
    if (m_download) m_download->abort();
    if (m_v2Active && m_session && !m_v2SyncId.isEmpty()) {
        m_session->sendSyncV2Release(m_v2SyncId, 2);
    }
    m_v2Active = false;
    m_v2SyncId.clear();
    m_v2HasSession = false;
    m_v2SawLastBatch = false;
    m_v2InFlightDownload = false;
    m_v2SeenUrls.clear();
    m_v2DownloadQueue.clear();
    m_v2Conversations.clear();
    cleanup();
    log("failed: " + reason);
    setState(Failed, reason, 0);
}

MobileHistorySync::KeyPair MobileHistorySync::generateKeyPair() {
    KeyPair out;
    EVP_PKEY* pkey = EVP_RSA_gen(2048);
    if (!pkey) return out;
    unsigned char* der = nullptr;
    const int derLen = i2d_PUBKEY(pkey, &der);
    if (derLen > 0) {
        out.publicKeyBase64 = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(der), derLen).toBase64());
        OPENSSL_free(der);
    }
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio && PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1) {
        char* data = nullptr;
        const long len = BIO_get_mem_data(bio, &data);
        out.privateKeyPem = QByteArray(data, static_cast<int>(len));
    }
    BIO_free(bio);
    EVP_PKEY_free(pkey);
    return out;
}

QByteArray MobileHistorySync::rsaDecrypt(const QByteArray& privateKeyPem, const QString& cipherBase64, bool* ok) {
    if (ok) *ok = false;
    const QByteArray cipher = QByteArray::fromBase64(cipherBase64.toLatin1());
    if (privateKeyPem.isEmpty() || cipher.isEmpty()) return {};
    BIO* bio = BIO_new_mem_buf(privateKeyPem.constData(), privateKeyPem.size());
    EVP_PKEY* pkey = bio ? PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr) : nullptr;
    BIO_free(bio);
    if (!pkey) return {};
    QByteArray plain;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    size_t outLen = 0;
    const auto* in = reinterpret_cast<const unsigned char*>(cipher.constData());
    if (ctx && EVP_PKEY_decrypt_init(ctx) == 1 && EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) == 1
        && EVP_PKEY_decrypt(ctx, nullptr, &outLen, in, static_cast<size_t>(cipher.size())) == 1) {
        plain.resize(static_cast<int>(outLen));
        if (EVP_PKEY_decrypt(ctx, reinterpret_cast<unsigned char*>(plain.data()), &outLen, in,
                             static_cast<size_t>(cipher.size())) == 1) {
            plain.resize(static_cast<int>(outLen));
            if (ok) *ok = true;
        } else {
            plain.clear();
        }
    }
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return plain;
}

QString MobileHistorySync::backupKeyText(const QByteArray& plainKey, int format) {
    return format ? QString::fromLatin1(plainKey.toHex()) : QString::fromLatin1(plainKey.toBase64());
}

QVector<MobileHistorySync::BackupConversation> MobileHistorySync::readBackupDirectory(const QString& dir,
                                                                                    QString* error) {
    QVector<BackupConversation> out;
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.db")}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        QString id = file.chopped(3);
        bool isGroup = false;
        if (id.startsWith(QLatin1String("group_"))) {
            id = id.mid(6);
            isGroup = true;
        } else if (id.startsWith('g') && isDigits(id.mid(1))) {
            id = id.mid(1);
            isGroup = true;
        }
        if (!isDigits(id)) continue;

        BackupConversation conv;
        conv.plainId = id;
        conv.isGroup = isGroup;
        const QString connName = QStringLiteral("zalos_backup_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            db.setDatabaseName(QDir(dir).filePath(file));
            if (db.open() && db.tables().contains(QStringLiteral("ChatContent"))) {
                QSqlQuery q(db);
                if (!q.exec(QStringLiteral("SELECT * FROM ChatContent WHERE MsgStatus > 0 AND CliMsgId NOT NULL "
                                           "ORDER BY TimeStamp ASC"))) {
                    q.exec(QStringLiteral("SELECT * FROM ChatContent"));
                }
                const QSqlRecord rec = q.record();
                while (q.next()) {
                    BackupRow row;
                    row.globalMsgId = firstColumn(rec, q, {"GlbMsgId", "GlobalMsgId", "MsgId"});
                    row.cliMsgId = firstColumn(rec, q, {"CliMsgId", "cliMsgId"});
                    row.msgType = firstColumn(rec, q, {"MsgType"}).toInt();
                    row.ts = firstColumn(rec, q, {"TimeStamp", "Ts"}).toLongLong();
                    row.senderId = firstColumn(rec, q, {"SenderId", "FromId", "FromUid"});
                    row.text = firstColumn(rec, q, {"MsgContent", "Message"});
                    row.ttl = firstColumn(rec, q, {"TTL"}).toLongLong();
                    const int bin = rec.indexOf(QStringLiteral("BinNet"));
                    if (bin >= 0) {
                        const QByteArray rawBin = q.value(bin).toByteArray();
                        if (!rawBin.isEmpty()) {
                            row.parsedBin = Crypto::DbCross::parseBinNet(
                                reinterpret_cast<const uint8_t*>(rawBin.constData()),
                                static_cast<size_t>(rawBin.size()));
                            row.hasParsedBin = true;
                        }
                    }
                    if (row.globalMsgId.isEmpty() && row.cliMsgId.isEmpty()) continue;
                    conv.rows.append(std::move(row));
                }
            } else if (error && error->isEmpty() && !db.isOpen()) {
                *error = db.lastError().text();
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connName);
        if (!conv.rows.isEmpty()) out.append(conv);
    }
    return out;
}

QJsonArray MobileHistorySync::toMessages(const BackupConversation& conv, const QHash<QString, QString>& ids,
                                         const QString& myPlainUid, const QString& myNoisedUid, int* skipped,
                                         int startRow, int maxRows) {
    QJsonArray out;
    int skip = 0;
    const QString thread = ids.value(conv.plainId);
    const int totalRows = static_cast<int>(conv.rows.size());
    const int begin = qMax(0, startRow);
    const int end = (maxRows < 0) ? totalRows : qMin(totalRows, begin + maxRows);
    if (thread.isEmpty()) {
        if (skipped) *skipped += qMax(0, end - begin);
        return out;
    }
    auto noised = [&](const QString& plain) -> QString {
        if (plain.isEmpty()) return {};
        if (plain == myPlainUid) return myNoisedUid;
        return ids.value(plain);
    };

    for (int rowIdx = begin; rowIdx < end; ++rowIdx) {
        const BackupRow& row = conv.rows.at(rowIdx);
        if (isSkippedType(row.msgType)) {
            ++skip;
            continue;
        }
        const bool mine = row.senderId == QLatin1String("0")
                          || (!myPlainUid.isEmpty() && row.senderId == myPlainUid);
        QString uidFrom;
        QString idTo;
        if (conv.isGroup) {
            if (mine) {
                uidFrom = QStringLiteral("0");
            } else {
                uidFrom = noised(row.senderId);
                if (uidFrom.isEmpty()) uidFrom = row.senderId;
            }
            idTo = thread;
        } else {
            uidFrom = mine ? QStringLiteral("0") : thread;
            idTo = mine ? thread : myNoisedUid;
        }
        if (uidFrom.isEmpty() || idTo.isEmpty()) {
            ++skip;
            continue;
        }

        Crypto::BinNetMessage fallbackBin;
        if (!row.hasParsedBin && !row.binNet.isEmpty()) {
            fallbackBin = Crypto::DbCross::parseBinNet(reinterpret_cast<const uint8_t*>(row.binNet.constData()),
                                                       static_cast<size_t>(row.binNet.size()));
        }
        const Crypto::BinNetMessage& bin = row.hasParsedBin ? row.parsedBin : fallbackBin;
        const QString type = typeName(row.msgType);
        QJsonValue content;
        if (type == QLatin1String("webchat") || bin.attachs.empty()) {
            const QString text = row.text.isEmpty() ? str(bin.text) : row.text;
            if ((text.isEmpty() && bin.attachs.empty()) || (type != QLatin1String("webchat") && isPlaceholderCaption(text))) {
                ++skip;
                continue;
            }
            content = text;
        } else {
            const auto& a = bin.attachs.front();
            QJsonObject obj{{"title", str(a.title)}, {"href", str(a.href)}, {"thumb", str(a.thumb)},
                            {"description", str(a.description)}, {"action", str(a.action)},
                            {"params", str(a.params)}, {"type", str(a.type)}};
            if (type == QLatin1String("chat.sticker")) {
                obj["id"] = a.id;
                obj["catId"] = a.catId;
            }
            if (obj.value("title").toString().isEmpty() && !isPlaceholderCaption(row.text)) obj["title"] = row.text;
            content = obj;
        }

        QJsonObject msg{
            {"msgId", row.globalMsgId.isEmpty() || row.globalMsgId == "0" ? row.cliMsgId : row.globalMsgId},
            {"cliMsgId", row.cliMsgId},
            {"msgType", type},
            {"uidFrom", uidFrom},
            {"idTo", idTo},
            {"ts", QString::number(row.ts)},
            {"content", content},
            {"ttl", row.ttl},
        };
        if (bin.quote.globalMsgId > 0 || bin.quote.cliMsgId > 0 || !bin.quote.msg.empty()) {
            const QString owner = bin.quote.ownerId > 0 ? QString::number(bin.quote.ownerId) : QString();
            const QString noisedOwner = noised(owner);
            msg["quote"] = QJsonObject{{"ownerId", noisedOwner.isEmpty() ? owner : noisedOwner},
                                       {"globalMsgId", QString::number(bin.quote.globalMsgId)},
                                       {"cliMsgId", QString::number(bin.quote.cliMsgId)},
                                       {"msg", str(bin.quote.msg)},
                                       {"attach", str(bin.quote.attach)},
                                       {"fromD", str(bin.quote.fromD)},
                                       {"ts", QString::number(bin.quote.ts)}};
        } else if (row.v2Quote.has_value()) {
            const auto& vq = *row.v2Quote;
            if (vq.globalMsgId > 0 || vq.cliMsgId > 0 || !vq.msg.isEmpty()) {
                const QString noisedOwner = noised(vq.ownerId);
                msg["quote"] = QJsonObject{{"ownerId", noisedOwner.isEmpty() ? vq.ownerId : noisedOwner},
                                           {"globalMsgId", QString::number(vq.globalMsgId)},
                                           {"cliMsgId", QString::number(vq.cliMsgId)},
                                           {"msg", vq.msg},
                                           {"attach", vq.attach},
                                           {"fromD", vq.fromD},
                                           {"ts", QString::number(vq.ts)}};
            }
        }
        if (!bin.mentions.empty()) {
            QJsonArray mentionsArr;
            for (const auto& m : bin.mentions) {
                QString mUid;
                if (m.uid < 0) {
                    mUid = QStringLiteral("-1");
                } else if (m.uid > 0) {
                    const QString plain = QString::number(m.uid);
                    mUid = noised(plain);
                    if (mUid.isEmpty()) mUid = plain;
                }
                if (mUid.isEmpty()) continue;
                mentionsArr.append(QJsonObject{{"uid", mUid}, {"pos", m.pos}, {"len", m.len}, {"type", m.type}});
            }
            if (!mentionsArr.isEmpty()) msg["mentions"] = mentionsArr;
        } else if (!row.v2Mentions.isEmpty()) {
            QJsonArray mentionsArr;
            for (const auto& m : row.v2Mentions) {
                QString mUid = m.uid;
                if (mUid != QLatin1String("-1") && !mUid.isEmpty()) {
                    const QString resolved = noised(mUid);
                    if (!resolved.isEmpty()) mUid = resolved;
                }
                if (mUid.isEmpty()) continue;
                mentionsArr.append(QJsonObject{{"uid", mUid}, {"pos", m.pos}, {"len", m.len}, {"type", m.type}});
            }
            if (!mentionsArr.isEmpty()) msg["mentions"] = mentionsArr;
        }
        out.append(msg);
    }
    if (skipped) *skipped += skip;
    return out;
}

void MobileHistorySync::start(const QString& myNoisedUid, const QString& pcName) {
    if (!m_session) return;
    cancel();
    m_myNoisedUid = myNoisedUid;
    m_pcName = pcName;
    m_sawSyncV2 = false;
    const bool forceV2 = qEnvironmentVariableIntValue("ZALOS_FORCE_SYNC_V2") == 1
        || (QCoreApplication::instance() && QCoreApplication::arguments().contains(QLatin1String("--force-sync-v2")));
    if (forceV2) {
        startV2Fallback(QStringLiteral("debug force_sync_v2"));
        return;
    }
    m_keys = generateKeyPair();
    if (m_keys.publicKeyBase64.isEmpty() || m_keys.privateKeyPem.isEmpty()) {
        fail(L("Không tạo được khoá mã hoá cho phiên đồng bộ.", "Couldn't create the encryption key for this sync."));
        return;
    }
    const quint64 gen = ++m_generation;
    setState(WaitingConfirm, L("Đã gửi yêu cầu. Mở Zalo trên điện thoại và bấm Đồng ý để chuyển tin nhắn sang máy tính.",
                               "Request sent. Open Zalo on your phone and tap Allow to transfer messages."), 5);
    m_timeout.start(kConfirmTimeoutMs);
    m_session->pullMobileMessages(m_pcName, m_keys.publicKeyBase64, 0, 0, false,
        [this, gen](bool ok, const QJsonValue&, const QString& error, int code) {
            log(QStringLiteral("pull_mobile_msg ok=%1 code=%2").arg(ok).arg(code));
            if (gen != m_generation || ok) return;
            if (V2::decideFallback(code, false, false) == V2::FallbackTrigger::PullRejected211) {
                startV2Fallback(QStringLiteral("pull_mobile_msg 211 client not supported"));
                return;
            }
            fail(L("Máy chủ từ chối yêu cầu đồng bộ: ", "The server refused the sync request: ")
                 + Network::ErrorCatalog::render(code, error, m_lang == QLatin1String("en")));
        }, m_mobileTempKey);
}

V2::KeyPair MobileHistorySync::loadOrCreateV2IdentityKey() const {
    QSettings settings;
    const QString scopedPrefix = m_myNoisedUid.isEmpty()
                                     ? QStringLiteral("sync_v2/")
                                     : QStringLiteral("sync_v2/%1/").arg(m_myNoisedUid);
    QByteArray pub = QByteArray::fromHex(settings.value(scopedPrefix + QLatin1String("ik_pub")).toByteArray());
    QByteArray priv = QByteArray::fromHex(settings.value(scopedPrefix + QLatin1String("ik_priv")).toByteArray());
    if (pub.size() == 33 && priv.size() == 32) return {pub, priv};
    if (!m_myNoisedUid.isEmpty()) {
        pub = QByteArray::fromHex(settings.value("sync_v2/ik_pub").toByteArray());
        priv = QByteArray::fromHex(settings.value("sync_v2/ik_priv").toByteArray());
        if (pub.size() == 33 && priv.size() == 32) {
            settings.setValue(scopedPrefix + QLatin1String("ik_pub"), pub.toHex());
            settings.setValue(scopedPrefix + QLatin1String("ik_priv"), priv.toHex());
            settings.remove("sync_v2/ik_pub");
            settings.remove("sync_v2/ik_priv");
            return {pub, priv};
        }
    }
    const V2::KeyPair kp = V2::generateKeyPair();
    if (kp.pub.size() == 33 && kp.priv.size() == 32) {
        settings.setValue(scopedPrefix + QLatin1String("ik_pub"), kp.pub.toHex());
        settings.setValue(scopedPrefix + QLatin1String("ik_priv"), kp.priv.toHex());
    }
    return kp;
}

void MobileHistorySync::startV2Fallback(const QString& why) {
    if (!m_session || m_v2Active) return;
    log(QStringLiteral("starting sync v2 fallback: %1").arg(why));

    if (!m_keys.publicKeyBase64.isEmpty()) {
        m_session->cancelPullMobileMessages(m_pcName, m_keys.publicKeyBase64,
                                            [](bool, const QJsonValue&, const QString&, int) {});
        m_keys = {};
    }

    m_v2Ik = loadOrCreateV2IdentityKey();
    m_v2Ek = V2::generateKeyPair();
    if (m_v2Ik.pub.size() != 33 || m_v2Ik.priv.size() != 32 ||
        m_v2Ek.pub.size() != 33 || m_v2Ek.priv.size() != 32) {
        fail(L("Không tạo được khóa Sync v2.", "Couldn't create Sync v2 keys."));
        return;
    }

    m_v2Active = true;
    m_v2Phase = V2Phase::Conversation;
    m_v2ConvCursor = 0;
    m_v2UssIdx = 0;
    m_v2PendingConvs.clear();
    m_v2PendingConvKeys.clear();
    m_v2Conversations.clear();
    m_ids.clear();

    setState(WaitingConfirm,
             L("Đã gửi yêu cầu (Sync v2). Mở Zalo trên điện thoại và bấm Đồng ý để chuyển tin nhắn.",
               "Request sent (Sync v2). Open Zalo on your phone and tap Allow to transfer messages."),
             10);

    const QJsonArray queries{QJsonObject{
        {"partition", QStringLiteral("")},
        {"from", 0},
        {"to", qint64(9007199254740991LL)},
        {"limit", qint64(2147483647LL)},
    }};
    sendV2SessionRequest(QStringLiteral("conversation"), 0, queries, 0);
}

void MobileHistorySync::sendV2SessionRequest(const QString& type, int priority, const QJsonArray& queries,
                                             int ussidx) {
    if (!m_session) return;
    static constexpr char kBase62[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    m_v2SyncId.clear();
    m_v2SyncId.reserve(32);
    for (int i = 0; i < 32; ++i) {
        m_v2SyncId.append(QLatin1Char(kBase62[QRandomGenerator::global()->bounded(62)]));
    }
    m_v2Session = {};
    m_v2HasSession = false;
    m_v2SawLastBatch = false;
    m_v2InFlightDownload = false;
    m_v2WakeUpSent = false;
    m_v2ExpectedBatches = -1;
    m_v2SeenBatchIdxs.clear();
    m_v2SeenUrls.clear();
    m_v2DownloadQueue.clear();

    const QJsonObject syncData{
        {"syncId", m_v2SyncId},
        {"syncType", 0},
        {"ek", QString::fromLatin1(m_v2Ek.pub.toBase64())},
        {"ik", QString::fromLatin1(m_v2Ik.pub.toBase64())},
        {"toDevice", 0},
        {"tempKey", m_mobileTempKey},
        {"deviceName", m_pcName},
        {"req", QJsonObject{
                    {"type", type},
                    {"priority", priority},
                    {"queries", queries},
                    {"batchSize", 2000},
                }},
        {"ver", 1},
        {"ussidx", ussidx},
    };

    ++m_generation;
    m_timeout.start(kConfirmTimeoutMs);
    const bool wsSent = m_session->sendSyncV2Request(m_v2SyncId, syncData);
    log(QStringLiteral("v2 590 sent type=%1 ussidx=%2 queries=%3 sync=%4 ws=%5")
            .arg(type).arg(ussidx).arg(queries.size()).arg(m_v2SyncId.left(8)).arg(wsSent));
    if (!wsSent) {
        fail(L("Chưa kết nối được máy chủ realtime để gửi yêu cầu Sync v2.",
               "Realtime connection is not ready to send the Sync v2 request."));
    }
}

void MobileHistorySync::sendNextV2MessageSession() {
    constexpr int kMaxQueriesPerRequest = 30;
    if (!m_v2Active || !m_session || m_v2ConvCursor >= m_v2PendingConvs.size()) return;

    m_v2Ek = V2::generateKeyPair();
    if (m_v2Ek.pub.size() != 33 || m_v2Ek.priv.size() != 32) {
        fail(L("Không tạo được khóa Sync v2.", "Couldn't create Sync v2 keys."));
        return;
    }

    QJsonArray queries;
    const int end = qMin(static_cast<int>(m_v2PendingConvs.size()), m_v2ConvCursor + kMaxQueriesPerRequest);
    for (int i = m_v2ConvCursor; i < end; ++i) {
        const auto& conv = m_v2PendingConvs.at(i);
        const QString prefix = conv.convType == 2 ? QStringLiteral("group/") : QStringLiteral("oneone/");
        const qint64 toTs = conv.lastTs > 0 ? static_cast<qint64>(conv.lastTs) : qint64(9007199254740991LL);
        queries.append(QJsonObject{
            {"partition", prefix + conv.convId},
            {"from", 0},
            {"to", toTs},
            {"limit", qint64(2147483647LL)},
        });
    }
    m_v2ConvCursor = end;
    ++m_v2UssIdx;

    const int progress = 50 + (18 * m_v2ConvCursor) / qMax(1, static_cast<int>(m_v2PendingConvs.size()));
    setState(PhonePreparing,
             L("Đang tải tin nhắn từng hội thoại Sync v2 (%1/%2)...",
               "Fetching Sync v2 conversation messages (%1/%2)...")
                 .arg(m_v2ConvCursor).arg(m_v2PendingConvs.size()),
             progress);

    sendV2SessionRequest(QStringLiteral("message"), 2, queries, m_v2UssIdx);
}

void MobileHistorySync::cancel() {
    const bool active = m_state == WaitingConfirm || m_state == PhonePreparing || m_state == Importing;
    m_timeout.stop();
    ++m_generation;
    if (m_download) m_download->abort();
    if (m_v2Active && m_session && !m_v2SyncId.isEmpty()) {
        m_session->sendSyncV2Release(m_v2SyncId, 0);
    }
    m_v2Active = false;
    m_v2Phase = V2Phase::Conversation;
    m_v2SyncId.clear();
    m_v2HasSession = false;
    m_v2SawLastBatch = false;
    m_v2InFlightDownload = false;
    m_v2WakeUpSent = false;
    m_v2ExpectedBatches = -1;
    m_v2ConvCursor = 0;
    m_v2UssIdx = 0;
    m_v2SeenBatchIdxs.clear();
    m_v2SeenUrls.clear();
    m_v2DownloadQueue.clear();
    m_v2PendingConvs.clear();
    m_v2PendingConvKeys.clear();
    m_v2Conversations.clear();
    if (active && m_session && !m_keys.publicKeyBase64.isEmpty()) {
        m_session->cancelPullMobileMessages(m_pcName, m_keys.publicKeyBase64, [](bool, const QJsonValue&, const QString&, int) {});
    }
    cleanup();
    m_conversations.clear();
    m_ids.clear();
    m_importIndex = 0;
    m_importRowOffset = 0;
    m_currentConvImported = false;
    if (m_state != Idle) setState(Idle, QString(), 0);
}

void MobileHistorySync::handleControl(const QJsonObject& content) {
    const QString actType = content.value("act_type").toString();
    const QString act = content.value("act").toString();
    const QJsonValue raw = content.value("data");
    const QJsonObject data = raw.isString() ? QJsonDocument::fromJson(raw.toString().toUtf8()).object() : raw.toObject();
    const bool ours = !m_keys.publicKeyBase64.isEmpty() && data.value("public_key").toString() == m_keys.publicKeyBase64;
    const bool active = m_state == WaitingConfirm || m_state == PhonePreparing;
    log(QStringLiteral("event act_type=%1 act=%2 state=%3 ours=%4 user_action=%5 status=%6 error_code=%7 sync_id=%8 batch=%9")
            .arg(actType, act).arg(m_state).arg(ours).arg(data.value("user_action").toInt(-1))
            .arg(data.value("status").toInt(-1)).arg(data.value("error_code").toInt(0))
            .arg(data.contains("syncId")).arg(data.value("batchType").toInt(-1)));
    if (actType != QLatin1String("syncmsgmb")) {
        if (actType == QLatin1String("transfer_after_login")) {
            const QString key = data.value("tempKey").toString(data.value("temp_key").toString()).trimmed();
            if (!key.isEmpty()) m_mobileTempKey = key;
        }
        if (actType == QLatin1String("transfer_sync2")) {
            if (m_v2Active) {
                handleV2Control(act, data);
                return;
            }
            if (active) m_sawSyncV2 = true;
        }
        if (active) {
            Network::ErrorCatalog::trace("phone-sync", QStringLiteral("unhandled event act_type=%1 act=%2 status=%3 while waiting")
                                                           .arg(actType, act).arg(data.value("status").toInt(-1)));
        }
        return;
    }
    if (!active || !ours) return;
    const bool samePc = !data.contains("pc_name") || data.value("pc_name").toString() == m_pcName;
    log(QStringLiteral("control act=%1 samePc=%2 user_action=%3 status=%4 error_code=%5")
            .arg(act).arg(samePc).arg(data.value("user_action").toInt(-1)).arg(data.value("status").toInt(-1))
            .arg(data.value("error_code").toInt(0)));

    if (act == "user_confirm" && samePc) {
        const int action = data.value("user_action").toInt(-1);
        if (action == 1 || action == 3) {
            setState(PhonePreparing, L("Điện thoại đã đồng ý. Đang chờ điện thoại đóng gói và gửi tin nhắn...",
                                       "The phone agreed. Waiting for it to pack and send the messages..."), 15);
            m_timeout.start(kBackupTimeoutMs);
        } else if (action == 0) {
            fail(L("Yêu cầu đồng bộ đã bị từ chối trên điện thoại.", "The sync request was declined on the phone."));
        } else if (action == 2) {
            fail(L("Điện thoại đang khôi phục dữ liệu nên chưa thể gửi tin nhắn. Hãy thử lại sau.",
                   "The phone is restoring data and can't send messages now. Try again later."));
        }
    } else if (act == "transfer_error" && samePc) {
        switch (classifyTransferError(data)) {
        case TransferErrorKind::PhoneIdle:
            setState(PhonePreparing, L("Hãy mở Zalo trên điện thoại và giữ màn hình sáng để tiếp tục sao lưu.",
                                       "Keep Zalo open on your phone so the backup can continue."), 15);
            break;
        case TransferErrorKind::PhoneActive:
            setState(PhonePreparing, L("Điện thoại đang sao lưu tin nhắn...", "The phone is backing up messages..."), 20);
            m_timeout.start(kBackupTimeoutMs);
            break;
        case TransferErrorKind::Failed: {
            const int phoneCode = data.value("error_code").toInt();
            Network::ErrorCatalog::trace("phone-sync", QStringLiteral("transfer_error code=%1 name=%2 status=%3")
                                                  .arg(phoneCode).arg(Network::ErrorCatalog::phoneSyncName(phoneCode))
                                                  .arg(data.value("status").toInt(-1)));
            const bool en = m_lang == QLatin1String("en");
            const bool unmapped = Network::ErrorCatalog::phoneSyncName(phoneCode) == QLatin1String("UNMAPPED");
            fail(Network::ErrorCatalog::pick(Network::ErrorCatalog::phoneSyncError(phoneCode), en)
                 + (en ? QStringLiteral(" (code %1)") : QStringLiteral(" (mã %1)")).arg(phoneCode)
                 + (unmapped ? L(" Nếu bạn dùng iPhone, Zalos có thể chưa hỗ trợ cách đồng bộ của Zalo trên iPhone.",
                                 " If you use an iPhone, Zalos may not support the way Zalo on iPhone syncs yet.")
                             : QString()));
            break;
        }
        case TransferErrorKind::Ignore:
            break;
        }
    } else if (act == "syncmsg_info") {
        onBackupReady(data);
    }
}

MobileHistorySync::TransferErrorKind MobileHistorySync::classifyTransferError(const QJsonObject& data) {
    const int status = data.value("status").toInt(-1);
    if (status == 2) return TransferErrorKind::PhoneIdle;
    if (status == 1) return TransferErrorKind::PhoneActive;
    return data.value("error_code").toInt() != 0 ? TransferErrorKind::Failed : TransferErrorKind::Ignore;
}

void MobileHistorySync::onBackupReady(const QJsonObject& data) {
    m_timeout.stop();
    const QJsonObject info = QJsonDocument::fromJson(data.value("db_info").toString().toUtf8()).object();
    const QJsonObject counts = info.contains("origin_db") ? info.value("origin_db").toObject()
                                                          : info.value("backup_db").toObject();
    m_expectedMessages = counts.value("msg_total").toInt();
    m_format = info.value("db_format").toInt(0);
    m_myPlainUid = data.value("uid").toVariant().toString();
    m_url = data.value("url").toString();
    m_fileName = data.value("file_name").toString();
    m_encryptedKey = data.value("encrypted_key").toString();
    m_checksum = data.value("checksum_code").toString(data.value("checksum").toString());
    m_expectedSize = data.value("file_size").toVariant().toLongLong();

    log(QStringLiteral("backup info: format=%1 msgs=%2 file=%3 host=%4 keys=%5 db_info=%6")
            .arg(m_format).arg(m_expectedMessages).arg(!m_fileName.isEmpty()).arg(QUrl(m_url).host()).arg(shapeOf(data)).arg(shapeOf(info)));
    if (info.isEmpty()) {
        fail(L("Thông tin bản sao lưu không hợp lệ.", "The backup description is invalid."));
        return;
    }
    if (m_expectedMessages == 0) {
        setState(Done, L("Điện thoại không có tin nhắn nào để chuyển.", "The phone had no messages to transfer."), 100);
        return;
    }
    if (m_format != 1) {
        fail(L("Bản sao lưu dùng định dạng cũ (0) mà Zalos chưa hỗ trợ.",
               "The backup uses the old format (0), which Zalos doesn't support yet."));
        return;
    }
    if (m_url.isEmpty() || m_encryptedKey.isEmpty()) {
        fail(L("Bản sao lưu thiếu đường dẫn tải hoặc khoá.", "The backup has no download link or key."));
        return;
    }
    m_downloadAttempt = 0;
    download();
}

void MobileHistorySync::download() {
    if (!m_session) return;
    ++m_downloadAttempt;
    const QUrl url(m_url);
    log(QStringLiteral("download starting attempt=%1 scheme=%2 host=%3 query=%4")
            .arg(m_downloadAttempt).arg(url.scheme(), url.host()).arg(int(url.hasQuery())));
    if (m_workDir.isEmpty()) {
        m_workDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                        .filePath(QStringLiteral("sync/%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
        QDir().mkpath(m_workDir);
        QFile::setPermissions(m_workDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    auto file = std::make_shared<QFile>(QDir(m_workDir).filePath(QStringLiteral("backup.zdb")));
    if (!file->open(QIODevice::WriteOnly)) {
        fail(L("Không ghi được bản sao lưu vào máy.", "Couldn't save the backup on this computer."));
        return;
    }
    setState(Importing, L("Đang tải bản sao lưu từ máy chủ Zalo...", "Downloading the backup from Zalo..."), 20);
    const quint64 gen = m_generation;
    m_writeFailed = false;
    auto md5Hasher = std::make_shared<QCryptographicHash>(QCryptographicHash::Md5);
    m_timeout.start(kDownloadStallMs);
    QPointer<QNetworkReply> reply = m_session->getWithCookies(url);
    m_download = reply;
    if (!reply) {
        fail(L("URL bản sao lưu không hợp lệ.", "Invalid backup URL."));
        return;
    }
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, file, md5Hasher, gen]() {
        if (!reply || gen != m_generation) return;
        m_timeout.start(kDownloadStallMs);
        const QByteArray chunk = reply->readAll();
        if (!m_writeFailed && !chunk.isEmpty()) {
            if (file->write(chunk) != chunk.size()) {
                m_writeFailed = true;
                reply->abort();
            } else {
                md5Hasher->addData(chunk);
            }
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply, gen](qint64 got, qint64 total) {
        if (!reply || gen != m_generation || total <= 0) return;
        m_timeout.start(kDownloadStallMs);
        setState(Importing, L("Đang tải bản sao lưu từ máy chủ Zalo...", "Downloading the backup from Zalo..."),
                 20 + int(30.0 * double(got) / double(total)));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, md5Hasher, gen]() {
        m_timeout.stop();
        if (!reply) return;
        reply->deleteLater();
        if (m_download == reply) m_download.clear();
        if (gen != m_generation) return;
        const QByteArray tail = reply->readAll();
        if (!m_writeFailed && !tail.isEmpty()) {
            if (file->write(tail) != tail.size()) {
                m_writeFailed = true;
            } else {
                md5Hasher->addData(tail);
            }
        }
        file->close();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        log(QStringLiteral("download attempt=%1 http=%2 err=%3 bytes=%4")
                .arg(m_downloadAttempt).arg(http).arg(int(reply->error())).arg(file->size()));
        if (m_writeFailed) {
            fail(L("Không ghi được bản sao lưu vào máy (ổ đĩa đầy hoặc không có quyền ghi).",
                   "Couldn't write the backup to disk (disk full or not writable)."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const int delayMs = downloadRetryDelayMs(http, int(reply->error()), m_downloadAttempt,
                                                     reply->rawHeader("Retry-After"), QDateTime::currentDateTimeUtc());
            if (delayMs >= 0) {
                log(QStringLiteral("download retrying attempt=%1 in %2 ms").arg(m_downloadAttempt + 1).arg(delayMs));
                setState(Importing, L("Đang chờ máy chủ Zalo chuẩn bị bản sao lưu...",
                                      "Waiting for Zalo's server to prepare the backup..."), 20);
                QTimer::singleShot(delayMs, this, [this, gen]() {
                    if (gen == m_generation) download();
                });
                return;
            }
            const bool en = m_lang == QLatin1String("en");
            const QString detail = http > 0
                ? Network::ErrorCatalog::render(http, QStringLiteral("HTTP %1").arg(http), en)
                : Network::ErrorCatalog::render(0, reply->errorString(), en);
            fail(L("Tải bản sao lưu thất bại: ", "Downloading the backup failed: ") + detail);
            return;
        }
        const QByteArray encoding = reply->rawHeader("Content-Encoding").trimmed().toLower();
        const qint64 contentLength = (encoding.isEmpty() || encoding == "identity")
                                         ? reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()
                                         : 0;
        if (file->size() <= 0 || (contentLength > 0 && file->size() != contentLength)) {
            fail(L("Bản sao lưu tải về không đầy đủ (%1/%2 byte). Hãy thử đồng bộ lại.",
                   "The backup download is incomplete (%1/%2 bytes). Please try again.")
                     .arg(file->size()).arg(contentLength));
            return;
        }
        const QString md5 = QString::fromLatin1(md5Hasher->result().toHex());
        log(QStringLiteral("download integrity: content_length=%1 file_size=%2 expected_size=%3 md5=%4 checksum=%5")
                .arg(contentLength).arg(file->size()).arg(m_expectedSize).arg(md5, m_checksum));
        decryptAndRead();
    });
}

int MobileHistorySync::downloadRetryDelayMs(int httpStatus, int networkError, int attempt, const QByteArray& retryAfter,
                                            const QDateTime& now) {
    if (attempt < 1 || attempt >= kMaxDownloadAttempts) return -1;
    bool retryable = false;
    if (httpStatus > 0) {
        retryable = httpStatus == 400 || httpStatus == 404 || httpStatus == 408 || httpStatus == 429 || httpStatus >= 500;
    } else {
        switch (networkError) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyTimeoutError:
        case QNetworkReply::UnknownNetworkError:
            retryable = true;
            break;
        default:
            break;
        }
    }
    if (!retryable) return -1;
    const int backoff = qMin(kDownloadRetryMaxMs, kDownloadRetryBaseMs << (attempt - 1));
    const QByteArray hint = retryAfter.trimmed();
    if ((httpStatus != 429 && httpStatus != 503) || hint.isEmpty()) return backoff;
    bool isSeconds = false;
    const qint64 seconds = hint.toLongLong(&isSeconds);
    qint64 waitMs = -1;
    if (isSeconds) {
        waitMs = qBound(qint64(0), seconds, qint64(kRetryAfterMaxMs / 1000)) * 1000;
    } else {
        QString text = QString::fromLatin1(hint);
        if (text.endsWith(QLatin1String(" GMT"))) text = text.chopped(4) + QStringLiteral(" +0000");
        const QDateTime at = QDateTime::fromString(text, Qt::RFC2822Date);
        if (at.isValid()) waitMs = now.msecsTo(at);
    }
    if (waitMs < 0) return backoff;
    return int(qBound(qint64(backoff), waitMs, qint64(kRetryAfterMaxMs)));
}

void MobileHistorySync::decryptAndRead() {
    bool ok = false;
    const QByteArray plainKey = rsaDecrypt(m_keys.privateKeyPem, m_encryptedKey, &ok);
    if (!ok || plainKey.isEmpty()) {
        fail(L("Không giải mã được khoá của bản sao lưu.", "Couldn't decrypt the backup key."));
        return;
    }
    const QString keyText = backupKeyText(plainKey, m_format);
    setState(Importing, L("Đang giải mã bản sao lưu...", "Decrypting the backup..."), 55);

    struct Result {
        QString error;
        QVector<BackupConversation> conversations;
    };
    auto result = std::make_shared<Result>();
    const QString workDir = m_workDir;
    m_workDir.clear();
    const QString input = QDir(workDir).filePath(QStringLiteral("backup.zdb"));
    const QString output = QDir(workDir).filePath(QStringLiteral("db"));
    const quint64 gen = m_generation;
    QThread* worker = QThread::create([result, input, output, keyText]() {
        std::string err;
        if (!Crypto::DbCross::decompressAndDecryptZdb4(input.toStdString(), output.toStdString(), keyText.toStdString(),
                                                      &err)) {
            result->error = QString::fromStdString(err);
            return;
        }
        QFile::remove(input);
        result->conversations = readBackupDirectory(output, &result->error);
    });
    connect(worker, &QThread::finished, worker, [worker, workDir]() {
        QDir(workDir).removeRecursively();
        worker->deleteLater();
    });
    connect(worker, &QThread::finished, this, [this, result, gen]() {
        if (gen != m_generation) return;
        int rows = 0;
        for (const auto& c : std::as_const(result->conversations)) rows += c.rows.size();
        log(QStringLiteral("decrypt/read: conversations=%1 rows=%2 error=%3")
                .arg(result->conversations.size()).arg(rows).arg(result->error));
        if (result->conversations.isEmpty()) {
            if (!result->error.isEmpty()) Network::ErrorCatalog::trace("phone-sync", "backup read failed raw=" + result->error);
            fail(L("Không đọc được tin nhắn trong bản sao lưu: ", "Couldn't read messages from the backup: ")
                 + (result->error.isEmpty() ? L("bản sao lưu trống", "the backup is empty")
                                            : L("dữ liệu bị lỗi hoặc sai định dạng", "the data is damaged or in an unexpected format")));
            return;
        }
        m_conversations = std::move(result->conversations);
        cleanup();
        if (m_session && !m_keys.publicKeyBase64.isEmpty()) {
            m_session->cleanMobileSync(m_keys.publicKeyBase64, [](bool, const QJsonValue&, const QString&, int) {});
        }
        resolveIdsAndImport();
    });
    worker->start();
}

void MobileHistorySync::resolveIdsAndImport() {
    QSet<QString> users;
    QSet<QString> groups;
    for (const auto& conv : std::as_const(m_conversations)) {
        (conv.isGroup ? groups : users).insert(conv.plainId);
        for (const auto& row : conv.rows) {
            if (!row.senderId.isEmpty() && row.senderId != QLatin1String("0") && row.senderId != m_myPlainUid
                && isDigits(row.senderId)) {
                users.insert(row.senderId);
            }
            const auto collectBinRefs = [&](const Crypto::BinNetMessage& bin) {
                if (bin.quote.ownerId > 0) {
                    const QString qOwner = QString::number(bin.quote.ownerId);
                    if (qOwner != m_myPlainUid) users.insert(qOwner);
                }
                for (const auto& m : bin.mentions) {
                    if (m.uid > 0) {
                        const QString mUid = QString::number(m.uid);
                        if (mUid != m_myPlainUid) users.insert(mUid);
                    }
                }
            };
            if (row.hasParsedBin) {
                collectBinRefs(row.parsedBin);
            } else if (!row.binNet.isEmpty()) {
                collectBinRefs(Crypto::DbCross::parseBinNet(
                    reinterpret_cast<const uint8_t*>(row.binNet.constData()),
                    static_cast<size_t>(row.binNet.size())));
            }
        }
    }
    users.remove(m_myPlainUid);
    setState(Importing, L("Đang đối chiếu danh bạ...", "Matching contacts..."), 65);

    struct Pending {
        int outstanding{0};
        bool failed{false};
    };
    auto pending = std::make_shared<Pending>();
    const QStringList userList(users.cbegin(), users.cend());
    const QStringList groupList(groups.cbegin(), groups.cend());
    const quint64 gen = m_generation;
    m_ids.clear();

    auto onReply = [this, pending, gen](const QStringList& askedUsers, const QStringList& askedGroups, bool ok,
                                        const QJsonValue& data) {
        if (gen != m_generation) return;
        const QJsonObject obj = data.toObject();
        log(QStringLiteral("znoise ok=%1 asked users=%2 groups=%3 reply=%4")
                .arg(ok).arg(askedUsers.size()).arg(askedGroups.size()).arg(shapeOf(data)));
        auto apply = [this](const QStringList& asked, const QJsonValue& answer) {
            if (answer.isArray()) {
                const QJsonArray arr = answer.toArray();
                if (arr.size() != asked.size()) return;
                for (int i = 0; i < arr.size(); ++i) {
                    const QString noised = arr.at(i).toVariant().toString();
                    if (!noised.isEmpty() && noised != "0") m_ids.insert(asked.at(i), noised);
                }
            } else if (answer.isObject()) {
                const QJsonObject map = answer.toObject();
                for (const QString& plain : asked) {
                    const QString noised = map.value(plain).toVariant().toString();
                    if (!noised.isEmpty()) m_ids.insert(plain, noised);
                }
            }
        };
        if (ok) {
            apply(askedUsers, obj.value("fids"));
            apply(askedGroups, obj.value("gids"));
        } else {
            pending->failed = true;
        }
        if (--pending->outstanding > 0) return;
        if (m_ids.isEmpty()) {
            fail(L("Không đối chiếu được danh bạ với máy chủ Zalo.", "Couldn't match contacts with Zalo's servers."));
            return;
        }
        m_importIndex = 0;
        m_importRowOffset = 0;
        m_currentConvImported = false;
        m_importedMessages = 0;
        m_importedConversations = 0;
        m_skipped = 0;
        importNextChunk();
    };

    for (int i = 0; i < qMax(userList.size(), groupList.size()); i += kNoiseBatch) {
        const QStringList u = userList.mid(i, kNoiseBatch);
        const QStringList g = groupList.mid(i, kNoiseBatch);
        if (u.isEmpty() && g.isEmpty()) continue;
        pending->outstanding++;
        m_session->fetchNoisedIds(u, g, [onReply, u, g](bool ok, const QJsonValue& data, const QString&, int) {
            onReply(u, g, ok, data);
        });
    }
    if (pending->outstanding == 0) fail(L("Bản sao lưu không có hội thoại hợp lệ.", "The backup has no valid conversations."));
}

void MobileHistorySync::importNextChunk() {
    constexpr int kRowsPerTick = 300;
    if (m_importIndex >= m_conversations.size()) {
        cleanup();
        m_conversations.clear();
        QString status = L("Đã chuyển %1 tin nhắn của %2 hội thoại từ điện thoại.",
                           "Transferred %1 messages from %2 conversations.")
                             .arg(m_importedMessages).arg(m_importedConversations);
        if (m_skipped > 0) status += L(" Bỏ qua %1 tin không hiển thị được.", " Skipped %1 messages that can't be shown.").arg(m_skipped);
        setState(Done, status, 100);
        return;
    }
    BackupConversation& conv = m_conversations[m_importIndex];
    const int totalRows = static_cast<int>(conv.rows.size());
    const QJsonArray messages = toMessages(conv, m_ids, m_myPlainUid, m_myNoisedUid, &m_skipped,
                                           m_importRowOffset, kRowsPerTick);
    if (!messages.isEmpty()) {
        emit messagesImported(messages, conv.isGroup);
        m_importedMessages += messages.size();
        if (!m_currentConvImported) {
            m_importedConversations++;
            m_currentConvImported = true;
        }
    }
    m_importRowOffset = qMin(totalRows, m_importRowOffset + kRowsPerTick);
    const double convFrac = totalRows > 0 ? double(m_importRowOffset) / double(totalRows) : 1.0;
    const int displayConv = qMin<int>(m_importIndex + 1, m_conversations.size());
    if (m_importRowOffset >= totalRows) {
        conv.rows.clear();
        conv.rows.squeeze();
        m_importIndex++;
        m_importRowOffset = 0;
        m_currentConvImported = false;
    }
    const double doneRatio = (double(m_importIndex) + (m_importRowOffset > 0 ? convFrac : 0.0))
                             / double(qMax(1, int(m_conversations.size())));
    const int progress = 70 + int(29.0 * doneRatio);
    setState(Importing, L("Đang nhập tin nhắn (%1/%2 hội thoại)...", "Importing messages (%1/%2 conversations)...")
                            .arg(displayConv).arg(m_conversations.size()), progress);
    const quint64 gen = m_generation;
    const int delayMs = (messages.size() > 100) ? 10 : 2;
    QTimer::singleShot(delayMs, this, [this, gen]() {
        if (gen == m_generation) importNextChunk();
    });
}

void MobileHistorySync::cleanup() {
    if (!m_workDir.isEmpty()) {
        QDir(m_workDir).removeRecursively();
        m_workDir.clear();
    }
}

void MobileHistorySync::handleV2Control(const QString& act, const QJsonObject& data) {
    if (!m_v2Active || !m_session) return;
    const QString syncId = data.value("syncId").toString();
    if (!syncId.isEmpty() && syncId != m_v2SyncId) {
        if (act == QLatin1String("upload_batch")) m_session->sendSyncV2Release(syncId, 3);
        return;
    }

    if (act == QLatin1String("transfer_status")) {
        const int status = data.value("status").toVariant().toInt();
        if (status == 3) {
            setState(WaitingConfirm,
                     L("Đã gửi yêu cầu (Sync v2). Mở Zalo trên điện thoại và bấm Đồng ý để chuyển tin nhắn.",
                       "Request sent (Sync v2). Open Zalo on your phone and tap Allow to transfer messages."),
                     10);
        } else if (status == 1 || status == 4 || status == 5) {
            setState(PhonePreparing,
                     L("Điện thoại đã đồng ý. Đang chờ điện thoại đóng gói và gửi tin nhắn (Sync v2)...",
                       "The phone agreed. Waiting for it to pack and send messages (Sync v2)..."),
                     20);
            m_timeout.start(kBackupTimeoutMs);
        } else if (status == 6 || status == 7) {
            m_session->sendSyncV2Release(m_v2SyncId, 0);
            m_v2Active = false;
            fail(L("Yêu cầu đồng bộ đã bị từ chối trên điện thoại.", "The sync request was declined on the phone."));
        } else if (status == 8) {
            m_session->sendSyncV2Release(m_v2SyncId, 2);
            m_v2Active = false;
            fail(L("Điện thoại đang khôi phục dữ liệu nên chưa thể gửi tin nhắn. Hãy thử lại sau.",
                   "The phone is restoring data and can't send messages now. Try again later."));
        } else if (status == 2) {
            setState(PhonePreparing,
                     L("Hãy mở Zalo trên điện thoại và giữ màn hình sáng để tiếp tục sao lưu.",
                       "Keep Zalo open on your phone so the backup can continue."),
                     20);
            if (!m_v2WakeUpSent) {
                m_v2WakeUpSent = true;
                m_session->sendSyncV2WakeUp(m_v2SyncId);
            }
            m_timeout.start(kConfirmTimeoutMs);
        } else if (status == 9 || status == 10) {
            m_session->sendSyncV2Release(m_v2SyncId, 2);
            m_v2Active = false;
            fail(L("Điện thoại đã hủy hoặc gặp lỗi khi đồng bộ (Sync v2).",
                   "The phone cancelled or failed during Sync v2."));
        }
        return;
    }

    if (act == QLatin1String("transfer_error")) {
        const int err = data.value("errorCode").toInt(data.value("err").toInt(0));
        m_session->sendSyncV2Release(m_v2SyncId, 2);
        m_v2Active = false;
        fail(L("Lỗi đồng bộ từ điện thoại (Sync v2, mã %1).", "Phone sync error (Sync v2, code %1).").arg(err));
        return;
    }

    if (act == QLatin1String("upload_batch")) {
        const int err = data.value("err").toVariant().toInt();
        if (err != 0) {
            m_session->sendSyncV2Release(m_v2SyncId, 2);
            m_v2Active = false;
            fail(L("Lỗi gói đồng bộ Sync v2 (mã %1).", "Sync v2 batch error (code %1).").arg(err));
            return;
        }
        const QString msgUrl = data.value("msgUrl").toString(data.value("url").toString());
        const int batchType = data.value("batchType").toVariant().toInt();
        bool idxOk = false;
        const int idxParsed = data.value("idx").toVariant().toInt(&idxOk);
        const int batchIdx = (data.contains("idx") && idxOk && idxParsed >= 0) ? idxParsed : -1;
        const bool isLast = data.value("isLast").toVariant().toInt() == 1 || data.value("isLast").toBool();
        if (batchIdx >= 0) m_v2SeenBatchIdxs.insert(batchIdx);
        if (isLast) {
            m_v2SawLastBatch = true;
            if (batchIdx >= 0) m_v2ExpectedBatches = batchIdx + 1;
        }
        if (msgUrl.isEmpty()) {
            if (isLast) maybeFinishV2();
            return;
        }
        const QString seenKey = QStringLiteral("%1:%2").arg(batchType).arg(msgUrl);
        if (m_v2SeenUrls.contains(seenKey)) {
            if (isLast) maybeFinishV2();
            return;
        }
        m_v2SeenUrls.insert(seenKey);

        if (batchType == 2) m_v2DownloadQueue.prepend({batchType, msgUrl});
        else m_v2DownloadQueue.append({batchType, msgUrl});
        processNextV2Download();
    }
}

    struct V2MessageParseResult {
        enum ErrorKind { None, InvalidFraming, DecryptFailed, ZstdFailed, ParseFailed };
        ErrorKind errorKind{None};
        V2::CryptoStatus cryptoStatus{V2::CryptoStatus::Ok};
        QVector<MobileHistorySync::BackupConversation> conversations;
        QVector<V2::ParsedSyncV2Conversation> metaConversations;
        QStringList discoveredIds;
    };

    QString stripV2PartitionPrefix(QString id) {
        if (id.startsWith(QLatin1String("oneone/"))) return id.mid(7);
        if (id.startsWith(QLatin1String("group/"))) return id.mid(6);
        return id;
    }

    void decodeV2PlaintextChunk(const QByteArray& compressed, bool tryConversationChunk, const QString& myNoisedUid,
                                V2MessageParseResult* out) {
        QByteArray proto = compressed;
        if (compressed.size() >= 4 &&
            static_cast<unsigned char>(compressed.at(0)) == 0x28 &&
            static_cast<unsigned char>(compressed.at(1)) == 0xb5 &&
            static_cast<unsigned char>(compressed.at(2)) == 0x2f &&
            static_cast<unsigned char>(compressed.at(3)) == 0xfd) {
            QByteArray zstdErr;
            const V2::ZstdStatus zs = V2::zstdDecompress(compressed, &proto, &zstdErr);
            if (zs != V2::ZstdStatus::Ok) {
                out->errorKind = V2MessageParseResult::ZstdFailed;
                return;
            }
        }
        if (tryConversationChunk) {
            QVector<V2::ParsedSyncV2Conversation> convs;
            if (V2::parseSyncV2ConversationChunk(proto, &convs) && !convs.isEmpty()) {
                out->metaConversations.append(std::move(convs));
                return;
            }
        }
        QVector<V2::ParsedSyncV2Partition> parts;
        if (!V2::parseSyncV2Chunk(proto, &parts)) {
            out->errorKind = V2MessageParseResult::ParseFailed;
            return;
        }
        for (const V2::ParsedSyncV2Partition& part : parts) {
            MobileHistorySync::BackupConversation conv;
            if (part.id.startsWith(QLatin1String("group/"))) {
                conv.isGroup = true;
                conv.plainId = part.id.mid(6);
            } else if (part.id.startsWith(QLatin1String("oneone/"))) {
                conv.isGroup = false;
                conv.plainId = part.id.mid(7);
            } else {
                conv.plainId = part.id;
            }
            if (conv.plainId.isEmpty()) continue;
            out->discoveredIds.append(conv.plainId);

            conv.rows.reserve(part.messages.size());
            for (const V2::ParsedSyncV2Message& pm : part.messages) {
                MobileHistorySync::BackupRow row;
                row.globalMsgId = pm.globalId;
                row.cliMsgId = QString::number(pm.clientId);
                row.msgType = pm.msgType;
                row.ts = static_cast<qint64>(pm.timestamp);
                row.ttl = static_cast<qint64>(pm.ttl);
                const QString sender = stripV2PartitionPrefix(pm.senderId);
                if (!myNoisedUid.isEmpty() && sender == myNoisedUid) {
                    row.senderId = QStringLiteral("0");
                } else {
                    row.senderId = sender;
                    if (!sender.isEmpty()) out->discoveredIds.append(sender);
                }
                row.text = pm.content;
                if (!pm.attaches.isEmpty()) {
                    row.hasParsedBin = true;
                    for (const V2::ParsedSyncV2Attach& pa : pm.attaches) {
                        decltype(row.parsedBin.attachs)::value_type ba;
                        ba.type = pa.attachType.toUtf8().toStdString();
                        ba.action = pa.action.toUtf8().toStdString();
                        ba.params = pa.params.toUtf8().toStdString();
                        ba.title = pa.title.toUtf8().toStdString();
                        ba.href = pa.href.toUtf8().toStdString();
                        ba.thumb = pa.thumb.toUtf8().toStdString();
                        ba.description = pa.description.toUtf8().toStdString();
                        ba.catId = pa.catId;
                        ba.id = pa.id;
                        row.parsedBin.attachs.push_back(std::move(ba));
                    }
                }
                if (pm.quote.has_value()) {
                    V2::ParsedSyncV2Quote q = *pm.quote;
                    q.ownerId = stripV2PartitionPrefix(q.ownerId);
                    if (!q.ownerId.isEmpty() && q.ownerId != myNoisedUid && q.ownerId != QLatin1String("-1")) {
                        out->discoveredIds.append(q.ownerId);
                    }
                    row.v2Quote = std::move(q);
                }
                if (!pm.mentions.isEmpty()) {
                    row.v2Mentions.reserve(pm.mentions.size());
                    for (const V2::ParsedSyncV2Mention& pmMention : pm.mentions) {
                        V2::ParsedSyncV2Mention m = pmMention;
                        m.uid = stripV2PartitionPrefix(m.uid);
                        if (!m.uid.isEmpty() && m.uid != myNoisedUid && m.uid != QLatin1String("-1")) {
                            out->discoveredIds.append(m.uid);
                        }
                        row.v2Mentions.append(std::move(m));
                    }
                }
                conv.rows.append(std::move(row));
            }
            if (!conv.rows.isEmpty()) out->conversations.append(std::move(conv));
        }
    }

    V2MessageParseResult parseV2MessageBatch(const QByteArray& bytes, const V2::SessionRecord& session,
                                             const QString& myNoisedUid, bool tryConversationChunk) {
        V2MessageParseResult out;
        QVector<QByteArray> frames;
        if (!V2::splitFrames(bytes, &frames) || frames.isEmpty()) {
            out.errorKind = V2MessageParseResult::InvalidFraming;
            return out;
        }
        for (const QByteArray& frame : frames) {
            QByteArray compressed;
            const V2::CryptoStatus st = V2::openMessage(session, frame, &compressed);
            if (st != V2::CryptoStatus::Ok) {
                out.errorKind = V2MessageParseResult::DecryptFailed;
                out.cryptoStatus = st;
                return out;
            }
            decodeV2PlaintextChunk(compressed, tryConversationChunk, myNoisedUid, &out);
            if (out.errorKind != V2MessageParseResult::None) return out;
        }
        return out;
    }

void MobileHistorySync::processNextV2Download() {
    if (!m_v2Active || !m_session || m_v2InFlightDownload) return;
    if (m_v2DownloadQueue.isEmpty()) {
        maybeFinishV2();
        if (m_v2Active && !m_v2SawLastBatch) m_timeout.start(kBackupTimeoutMs);
        return;
    }
    int nextIdx = -1;
    for (int i = 0; i < m_v2DownloadQueue.size(); ++i) {
        if (m_v2DownloadQueue.at(i).batchType == 2 || m_v2HasSession) {
            nextIdx = i;
            break;
        }
    }
    if (nextIdx < 0) return;

    const V2BatchTask task = m_v2DownloadQueue.takeAt(nextIdx);
    m_v2InFlightDownload = true;
    setState(PhonePreparing,
             task.batchType == 2
                 ? L("Đang tải và giải mã thông tin phiên Sync v2...", "Downloading and decrypting Sync v2 session...")
                 : L("Đang tải và giải mã gói tin nhắn Sync v2...", "Downloading and decrypting Sync v2 messages..."),
             task.batchType == 2 ? 30 : 50);
    m_timeout.start(kDownloadStallMs);

    const quint64 gen = m_generation;
    QNetworkReply* reply = m_session->getWithCookies(QUrl(task.url));
    reply->setReadBufferSize(kMaxV2BatchBytes + 1);
    m_download = reply;
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply, gen, tooLarge](qint64 got, qint64 total) {
        if (gen != m_generation || !m_v2Active) return;
        m_timeout.start(kDownloadStallMs);
        if (got > kMaxV2BatchBytes || total > kMaxV2BatchBytes) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, task, tooLarge]() {
        reply->deleteLater();
        if (m_download == reply) m_download.clear();
        if (gen != m_generation || !m_v2Active) return;

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (*tooLarge) {
            m_v2InFlightDownload = false;
            m_session->sendSyncV2Release(m_v2SyncId, 2);
            m_v2Active = false;
            fail(L("Gói Sync v2 vượt quá giới hạn 64 MB.", "Sync v2 batch exceeds the 64 MB limit."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
            m_v2InFlightDownload = false;
            m_session->sendSyncV2Release(m_v2SyncId, 2);
            m_v2Active = false;
            fail(L("Không tải được gói Sync v2 (HTTP %1).", "Failed to download Sync v2 batch (HTTP %1).").arg(httpStatus));
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.size() > kMaxV2BatchBytes) {
            m_v2InFlightDownload = false;
            m_session->sendSyncV2Release(m_v2SyncId, 2);
            m_v2Active = false;
            fail(L("Gói Sync v2 vượt quá giới hạn 64 MB.", "Sync v2 batch exceeds the 64 MB limit."));
            return;
        }
        if (task.batchType == 1) {
            if (!m_v2HasSession) {
                m_v2InFlightDownload = false;
                m_session->sendSyncV2Release(m_v2SyncId, 2);
                m_v2Active = false;
                fail(L("Thiếu khóa phiên Sync v2.", "Missing Sync v2 session key."));
                return;
            }
            const V2::SessionRecord session = m_v2Session;
            const QString myNoisedUid = m_myNoisedUid;
            const bool tryConv = m_v2Phase == V2Phase::Conversation;
            const QPointer<MobileHistorySync> self(this);
            QThreadPool::globalInstance()->start([self, gen, bytes, session, myNoisedUid, tryConv]() {
                V2MessageParseResult res = parseV2MessageBatch(bytes, session, myNoisedUid, tryConv);
                if (!self) return;
                QMetaObject::invokeMethod(self, [self, gen, res = std::move(res)]() mutable {
                    if (!self || gen != self->m_generation || !self->m_v2Active) return;
                    self->m_v2InFlightDownload = false;
                    if (res.errorKind != V2MessageParseResult::None) {
                        QString err;
                        switch (res.errorKind) {
                        case V2MessageParseResult::InvalidFraming:
                            err = self->L("Gói dữ liệu Sync v2 không hợp lệ.", "Invalid Sync v2 batch framing.");
                            break;
                        case V2MessageParseResult::DecryptFailed:
                            err = self->L("Không giải mã được gói tin nhắn Sync v2 (%1).",
                                          "Failed to decrypt Sync v2 message batch (%1).")
                                      .arg(V2::cryptoStatusName(res.cryptoStatus));
                            break;
                        case V2MessageParseResult::ZstdFailed:
                            err = self->L("Không giải nén được gói tin nhắn Sync v2 (zstd).",
                                          "Failed to decompress Sync v2 message batch (zstd).");
                            break;
                        case V2MessageParseResult::ParseFailed:
                        case V2MessageParseResult::None:
                            err = self->L("Không phân tích được dữ liệu tin nhắn Sync v2.", "Failed to parse Sync v2 messages.");
                            break;
                        }
                        if (self->m_session) self->m_session->sendSyncV2Release(self->m_v2SyncId, 2);
                        self->m_v2Active = false;
                        self->fail(err);
                        return;
                    }
                    for (const QString& id : std::as_const(res.discoveredIds)) {
                        self->m_ids.insert(id, id);
                    }
                    for (const auto& mc : std::as_const(res.metaConversations)) {
                        const QString key = QStringLiteral("%1:%2").arg(mc.convType).arg(mc.convId);
                        if (!self->m_v2PendingConvKeys.contains(key)) {
                            self->m_v2PendingConvKeys.insert(key);
                            self->m_v2PendingConvs.append(mc);
                        }
                    }
                    for (auto& conv : res.conversations) {
                        self->m_v2Conversations.append(std::move(conv));
                    }
                    self->processNextV2Download();
                }, Qt::QueuedConnection);
            });
            return;
        }
        m_v2InFlightDownload = false;
        QString err;
        if (!handleV2BatchBytes(task.batchType, bytes, &err)) {
            m_session->sendSyncV2Release(m_v2SyncId, 2);
            m_v2Active = false;
            fail(err);
            return;
        }
        processNextV2Download();
    });
}

bool MobileHistorySync::handleV2BatchBytes(int batchType, const QByteArray& bytes, QString* error) {
    if (batchType == 2) {
        QVector<QByteArray> frames;
        if (!V2::splitFrames(bytes, &frames) || frames.isEmpty()) {
            if (error) *error = L("Gói dữ liệu Sync v2 không hợp lệ.", "Invalid Sync v2 batch framing.");
            return false;
        }
        for (const QByteArray& frame : frames) {
            V2::MetadataPlain metaOut;
            const V2::CryptoStatus st = V2::openMetadata(frame, m_v2Ik.pub, m_v2Ik.priv, m_v2Ek.priv, &metaOut);
            if (st != V2::CryptoStatus::Ok) {
                if (error) {
                    *error = L("Không giải mã được thông tin phiên Sync v2 (%1).",
                               "Failed to decrypt Sync v2 session metadata (%1).")
                                 .arg(V2::cryptoStatusName(st));
                }
                return false;
            }
            m_v2Session = metaOut.session;
            m_v2HasSession = true;
            if (!metaOut.plaintext.isEmpty()) {
                V2MessageParseResult metaRes;
                decodeV2PlaintextChunk(metaOut.plaintext, m_v2Phase == V2Phase::Conversation, m_myNoisedUid, &metaRes);
                if (metaRes.errorKind == V2MessageParseResult::None) {
                    for (const QString& id : std::as_const(metaRes.discoveredIds)) {
                        m_ids.insert(id, id);
                    }
                    for (const auto& mc : std::as_const(metaRes.metaConversations)) {
                        const QString key = QStringLiteral("%1:%2").arg(mc.convType).arg(mc.convId);
                        if (!m_v2PendingConvKeys.contains(key)) {
                            m_v2PendingConvKeys.insert(key);
                            m_v2PendingConvs.append(mc);
                        }
                    }
                    for (auto& conv : metaRes.conversations) {
                        m_v2Conversations.append(std::move(conv));
                    }
                }
            }
        }
        return true;
    }

    if (batchType == 1) {
        if (!m_v2HasSession) {
            if (error) *error = L("Thiếu khóa phiên Sync v2.", "Missing Sync v2 session key.");
            return false;
        }
        V2MessageParseResult res = parseV2MessageBatch(bytes, m_v2Session, m_myNoisedUid, m_v2Phase == V2Phase::Conversation);
        if (res.errorKind != V2MessageParseResult::None) {
            if (error) {
                switch (res.errorKind) {
                case V2MessageParseResult::InvalidFraming:
                    *error = L("Gói dữ liệu Sync v2 không hợp lệ.", "Invalid Sync v2 batch framing.");
                    break;
                case V2MessageParseResult::DecryptFailed:
                    *error = L("Không giải mã được gói tin nhắn Sync v2 (%1).",
                               "Failed to decrypt Sync v2 message batch (%1).")
                                 .arg(V2::cryptoStatusName(res.cryptoStatus));
                    break;
                case V2MessageParseResult::ZstdFailed:
                    *error = L("Không giải nén được gói tin nhắn Sync v2 (zstd).",
                               "Failed to decompress Sync v2 message batch (zstd).");
                    break;
                case V2MessageParseResult::ParseFailed:
                case V2MessageParseResult::None:
                    *error = L("Không phân tích được dữ liệu tin nhắn Sync v2.", "Failed to parse Sync v2 messages.");
                    break;
                }
            }
            return false;
        }
        for (const QString& id : std::as_const(res.discoveredIds)) {
            m_ids.insert(id, id);
        }
        for (const auto& mc : std::as_const(res.metaConversations)) {
            const QString key = QStringLiteral("%1:%2").arg(mc.convType).arg(mc.convId);
            if (!m_v2PendingConvKeys.contains(key)) {
                m_v2PendingConvKeys.insert(key);
                m_v2PendingConvs.append(mc);
            }
        }
        for (auto& conv : res.conversations) {
            m_v2Conversations.append(std::move(conv));
        }
        return true;
    }
    return true;
}

void MobileHistorySync::maybeFinishV2() {
    if (!m_v2Active || !m_v2SawLastBatch || m_v2InFlightDownload || !m_v2DownloadQueue.isEmpty()) return;
    if (m_v2ExpectedBatches >= 0 && m_v2SeenBatchIdxs.size() < m_v2ExpectedBatches) return;
    m_timeout.stop();
    if (m_session) m_session->sendSyncV2Release(m_v2SyncId, 1);
    if (m_v2Phase == V2Phase::Conversation && m_v2Conversations.isEmpty() && !m_v2PendingConvs.isEmpty()) {
        m_v2Phase = V2Phase::Message;
        m_v2ConvCursor = 0;
        sendNextV2MessageSession();
        return;
    }
    if (m_v2Phase == V2Phase::Message && m_v2ConvCursor < m_v2PendingConvs.size()) {
        sendNextV2MessageSession();
        return;
    }
    m_v2Active = false;
    m_conversations = std::move(m_v2Conversations);
    m_v2Conversations.clear();
    if (m_conversations.isEmpty()) {
        setState(Done, L("Đồng bộ hoàn tất (không có tin nhắn mới).", "Sync complete (no new messages)."), 100);
        return;
    }
    m_myPlainUid = m_myNoisedUid;
    m_importIndex = 0;
    m_importRowOffset = 0;
    m_currentConvImported = false;
    m_importedMessages = 0;
    m_importedConversations = 0;
    m_skipped = 0;
    setState(Importing, L("Đang nhập tin nhắn từ Sync v2...", "Importing Sync v2 messages..."), 70);
    importNextChunk();
}

}
