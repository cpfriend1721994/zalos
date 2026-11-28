#include "mobile_history_sync.h"

#include "crypto/db_cross.h"
#include "network/error_catalog.h"
#include "network/zalo_session.h"

#include <QElapsedTimer>
#include "sync/v2/sync_v2_crypto.h"
#include "sync/v2/sync_v2_fallback.h"
#include "sync/v2/sync_v2_frames.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPointer>
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
constexpr int kReleaseAckMs = 2000;
constexpr int kBatchIdleMs = 20 * 1000;
constexpr int kFirstBatchMs = 60 * 1000;
constexpr int kMaxSessionRetries = 1;
const QString kMetaPhoneSynced = QStringLiteral("phone_history_synced");
const QString kMetaIncrementalFailed = QStringLiteral("phone_incremental_failed");
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
    static const QList<int> skipped{21, 25, 29, 32, 33, 34, 35, 45, 51, 52};
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

QVector<int> resolveColumns(const QSqlRecord& rec, std::initializer_list<const char*> names) {
    QVector<int> indexes;
    indexes.reserve(static_cast<qsizetype>(names.size()));
    for (const char* n : names) indexes.append(rec.indexOf(QLatin1String(n)));
    return indexes;
}

QString firstColumn(const QSqlQuery& q, const QVector<int>& indexes) {
    for (const int i : indexes) {
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
        if (m_v2Active && (m_v2Importing || m_v2Parsing) && !m_v2InFlightDownload) return;
        if (m_v2Active && m_timeout.interval() == kBatchIdleMs && !m_v2InFlightDownload
            && m_v2DownloadQueue.isEmpty()) {
            log(QStringLiteral("v2 session idle: seen=%1 expected=%2 last=%3")
                    .arg(m_v2SeenBatchIdxs.size()).arg(m_v2ExpectedBatches).arg(m_v2SawLastBatch));
            ++m_v2PartialSessions;
            m_v2SessionPartial = true;
            m_v2SawLastBatch = true;
            m_v2ExpectedBatches = -1;
            maybeFinishV2();
            return;
        }
        if (m_v2Active && m_timeout.interval() == kFirstBatchMs && !v2BatchSeen(int(m_v2SeenBatchIdxs.size()), int(m_v2SeenUrls.size()))) {
            if (m_v2Phase == V2Phase::Message && v2CanRetrySession(m_v2SessionRetries)) {
                log(QStringLiteral("v2 session retry %1: no batch after the phone confirmed").arg(m_v2SessionRetries + 1));
                ++m_v2SessionRetries;
                m_v2ConvCursor = m_v2SessionStart;
                sendNextV2MessageSession();
            } else {
                endV2OnStall(L("Điện thoại không gửi tin nhắn dù đã đồng ý. Hãy mở Zalo trên điện thoại rồi thử lại.",
                               "The phone agreed but sent no messages. Open Zalo on your phone and try again."));
            }
            return;
        }
        if (m_state == WaitingConfirm && !m_v2Active
            && V2::decideFallback(0, true, m_sawSyncV2) == V2::FallbackTrigger::ConfirmTimeoutAfterV2) {
            startV2Fallback(QStringLiteral("confirm timeout after transfer_sync2"));
            return;
        } else if (m_state == WaitingConfirm) {
            fail(L("Điện thoại chưa xác nhận yêu cầu đồng bộ. Lưu ý: đồng bộ tin nhắn từ điện thoại (nhất là iPhone) vẫn đang thử nghiệm.",
                   "The phone did not confirm the sync request. Note: syncing messages from the phone (especially iPhone) is still experimental."));
        } else if (m_state == PhonePreparing && m_v2Active && m_timeout.interval() == kConfirmTimeoutMs
                   && m_v2Phase == V2Phase::Message && v2CanRetrySession(m_v2SessionRetries)) {
            log(QStringLiteral("v2 session retry %1: no answer, waking the phone").arg(m_v2SessionRetries + 1));
            if (m_session) m_session->sendSyncV2WakeUp(m_v2SyncId);
            ++m_v2SessionRetries;
            m_v2ConvCursor = m_v2SessionStart;
            sendNextV2MessageSession();
        } else if (m_state == PhonePreparing && m_v2Active && m_timeout.interval() == kConfirmTimeoutMs) {
            endV2OnStall(L("Điện thoại không phản hồi yêu cầu đồng bộ sau 3 phút. Hãy mở Zalo trên điện thoại rồi thử lại.",
                           "The phone did not answer the sync request within 3 minutes. Open Zalo on your phone and try again."));
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
    if (m_v2Active && state != Failed && state != Done && state != Idle) progress = qMax(progress, m_lastProgress);
    m_lastProgress = progress;
    log(QStringLiteral("state %1 progress %2").arg(state).arg(progress));
    m_state = state;
    emit stateChanged(state, status, progress);
}

void MobileHistorySync::fail(const QString& reason) {
    if (m_v2Incremental && m_metaSet && v2IncrementalUnsupported(m_v2Phase == V2Phase::Message)) {
        m_metaSet(kMetaIncrementalFailed, QStringLiteral("1"));
    }
    m_v2Incremental = false;
    m_timeout.stop();
    ++m_generation;
    if (m_download) m_download->abort();
    if (m_v2Active) releaseV2Sessions(2);
    m_v2OpenSyncIds.clear();
    m_v2ConversationSyncId.clear();
    m_v2Importing = false;
    m_v2Parsing = false;
    m_v2Spooled.clear();
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
                                                                                    QString* error,
                                                                                    BackupDirStats* stats) {
    QVector<BackupConversation> out;
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.db")}, QDir::Files, QDir::Name);
    if (stats) stats->files = int(files.size());
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
        if (!isDigits(id)) {
            if (stats) {
                ++stats->badName;
                const QString base = file.chopped(3);
                const QChar first = base.isEmpty() ? QLatin1Char('_') : base.at(0);
                const QString kind = first.isDigit() ? QStringLiteral("d") : first.isLetter() ? QString(first.toLower()) : QStringLiteral("_");
                ++stats->badNameShapes[kind + QString::number(base.size())];
            }
            continue;
        }

        BackupConversation conv;
        conv.plainId = id;
        conv.isGroup = isGroup;
        const QString connName = QStringLiteral("zalos_backup_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            db.setDatabaseName(QDir(dir).filePath(file));
            if (db.open() && db.tables().contains(QStringLiteral("ChatContent"))) {
                QSqlQuery countQ(db);
                if (countQ.exec(QStringLiteral("SELECT COUNT(*) FROM ChatContent")) && countQ.next()) {
                    conv.sourceRows += countQ.value(0).toInt();
                }
                QSqlQuery q(db);
                q.setForwardOnly(true);
                if (!q.exec(QStringLiteral("SELECT * FROM ChatContent WHERE MsgStatus > 0 AND CliMsgId NOT NULL "
                                           "ORDER BY TimeStamp ASC"))) {
                    q.exec(QStringLiteral("SELECT * FROM ChatContent"));
                }
                const QSqlRecord rec = q.record();
                const QVector<int> globalMsgIdCols = resolveColumns(rec, {"GlbMsgId", "GlobalMsgId", "MsgId"});
                const QVector<int> cliMsgIdCols = resolveColumns(rec, {"CliMsgId", "cliMsgId"});
                const QVector<int> msgTypeCols = resolveColumns(rec, {"MsgType"});
                const QVector<int> tsCols = resolveColumns(rec, {"TimeStamp", "Ts"});
                const QVector<int> senderCols = resolveColumns(rec, {"SenderId", "FromId", "FromUid"});
                const QVector<int> textCols = resolveColumns(rec, {"MsgContent", "Message"});
                const QVector<int> ttlCols = resolveColumns(rec, {"TTL"});
                const int bin = rec.indexOf(QStringLiteral("BinNet"));
                while (q.next()) {
                    BackupRow row;
                    row.globalMsgId = firstColumn(q, globalMsgIdCols);
                    row.cliMsgId = firstColumn(q, cliMsgIdCols);
                    row.msgType = firstColumn(q, msgTypeCols).toInt();
                    row.ts = firstColumn(q, tsCols).toLongLong();
                    row.senderId = firstColumn(q, senderCols);
                    row.text = firstColumn(q, textCols);
                    row.ttl = firstColumn(q, ttlCols).toLongLong();
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
        else if (stats) ++stats->noRows;
    }
    return out;
}

QString MobileHistorySync::phoneLocalId(const QString& cliMsgId, const QString& sender, const QString& thread) {
    return cliMsgId + QLatin1Char('_') + sender + QLatin1Char('_') + thread;
}

bool MobileHistorySync::isPhoneLocalId(const QString& msgId) {
    return msgId.contains(QLatin1Char('_'));
}

QJsonArray MobileHistorySync::toMessages(const BackupConversation& conv, const QHash<QString, QString>& ids,
                                         const QString& myPlainUid, const QString& myNoisedUid, int* skipped,
                                         int startRow, int maxRows, QHash<QString, int>* reasons, bool v2Rows) {
    QJsonArray out;
    int skip = 0;
    auto skipRow = [&skip, reasons](const QString& reason) {
        ++skip;
        if (reasons) (*reasons)[reason] += 1;
    };
    const QString thread = ids.value(conv.plainId);
    const int totalRows = static_cast<int>(conv.rows.size());
    const int begin = qMax(0, startRow);
    const int end = (maxRows < 0) ? totalRows : qMin(totalRows, begin + maxRows);
    if (thread.isEmpty()) {
        if (skipped) *skipped += qMax(0, end - begin);
        if (reasons) (*reasons)[QStringLiteral("no_thread")] += qMax(0, end - begin);
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
            skipRow(QStringLiteral("type_%1").arg(row.msgType));
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
                if (uidFrom.isEmpty() && !v2Rows) uidFrom = row.senderId;
            }
            idTo = thread;
        } else {
            uidFrom = mine ? QStringLiteral("0") : thread;
            idTo = mine ? thread : myNoisedUid;
        }
        if (uidFrom.isEmpty() || idTo.isEmpty()) {
            skipRow(QStringLiteral("no_sender"));
            continue;
        }

        Crypto::BinNetMessage fallbackBin;
        if (!row.hasParsedBin && !row.binNet.isEmpty()) {
            fallbackBin = Crypto::DbCross::parseBinNet(reinterpret_cast<const uint8_t*>(row.binNet.constData()),
                                                       static_cast<size_t>(row.binNet.size()));
        }
        const Crypto::BinNetMessage& bin = row.hasParsedBin ? row.parsedBin : fallbackBin;
        const QString type = typeName(row.msgType);
        const bool noGlobalId = row.globalMsgId.isEmpty() || row.globalMsgId == "0";
        const bool noCliId = row.cliMsgId.isEmpty() || row.cliMsgId == QLatin1String("0");
        if (v2Rows && noGlobalId && noCliId) {
            skipRow(QStringLiteral("no_global_id"));
            continue;
        }
        const QString rowMsgId = !noGlobalId ? row.globalMsgId
                                 : v2Rows   ? phoneLocalId(row.cliMsgId, uidFrom, thread)
                                            : row.cliMsgId;
        auto jsonObjectOf = [](const QString& raw) {
            const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
            return doc.isObject() ? doc.object() : QJsonObject();
        };
        auto attachParams = [&]() {
            return bin.attachs.empty() ? QJsonObject() : jsonObjectOf(str(bin.attachs.front().params));
        };
        auto pickId = [](const QJsonObject& o, std::initializer_list<const char*> keys) {
            for (const char* key : keys) {
                const QString v = o.value(QLatin1String(key)).toVariant().toString();
                if (!v.isEmpty() && v != QLatin1String("0")) return v;
            }
            return QString();
        };
        if (row.msgType == 36) {
            const QJsonObject src = jsonObjectOf(row.text);
            const QString explicitId = pickId(src, {"globalMsgId", "msgId"});
            const QString targetId = explicitId.isEmpty() ? rowMsgId : explicitId;
            if (targetId.isEmpty()) {
                skipRow(QStringLiteral("undo_unparsed{no_id}"));
                continue;
            }
            out.append(QJsonObject{
                {"msgId", targetId},
                {"cliMsgId", row.cliMsgId},
                {"msgType", QStringLiteral("webchat")},
                {"uidFrom", uidFrom},
                {"idTo", idTo},
                {"ts", QString::number(row.ts)},
                {"content", QString()},
                {"ttl", 0},
            });
            out.append(QJsonObject{
                {"msgId", QStringLiteral("undo:%1").arg(targetId)},
                {"cliMsgId", QString()},
                {"msgType", QStringLiteral("chat.undo")},
                {"uidFrom", uidFrom},
                {"idTo", idTo},
                {"ts", QString::number(row.ts)},
                {"content", QJsonObject{{"globalMsgId", targetId}, {"cliMsgId", row.cliMsgId}, {"deleteMsg", 1}}},
            });
            continue;
        }
        if (row.msgType == 26) {
            QJsonObject poll = jsonObjectOf(row.text);
            if (pickId(poll, {"poll_id", "pollId"}).isEmpty() && pickId(poll.value("params").toObject(), {"pollId", "poll_id"}).isEmpty()) {
                QJsonObject params = attachParams();
                poll = QJsonObject{{"action", bin.attachs.empty() ? QString() : str(bin.attachs.front().action)}, {"params", params}};
                if (pickId(params, {"pollId", "poll_id"}).isEmpty()) {
                    skipRow(QStringLiteral("poll_unparsed"));
                    continue;
                }
            }
            out.append(QJsonObject{
                {"msgId", rowMsgId},
                {"cliMsgId", row.cliMsgId},
                {"msgType", type},
                {"uidFrom", uidFrom},
                {"idTo", idTo},
                {"ts", QString::number(row.ts)},
                {"content", poll},
                {"ttl", row.ttl},
            });
            continue;
        }
        QJsonValue content;
        if (type == QLatin1String("webchat") || bin.attachs.empty()) {
            const QString text = row.text.isEmpty() ? str(bin.text) : row.text;
            if ((text.isEmpty() && bin.attachs.empty()) || (type != QLatin1String("webchat") && isPlaceholderCaption(text))) {
                skipRow(QStringLiteral("empty_or_placeholder"));
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

        if (rowMsgId.isEmpty()) {
            skipRow(QStringLiteral("no_id"));
            continue;
        }
        QJsonObject msg{
            {"msgId", rowMsgId},
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
                msg["quote"] = QJsonObject{{"ownerId", noisedOwner.isEmpty() && !v2Rows ? vq.ownerId : noisedOwner},
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
                    if (!resolved.isEmpty() || v2Rows) mUid = resolved;
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
    m_v2Incremental = false;
    if (forceV2) {
        startV2Fallback(QStringLiteral("debug force_sync_v2"));
        return;
    }
    if (m_metaGet && v2PreferIncremental(m_metaGet(kMetaPhoneSynced), m_metaGet(kMetaIncrementalFailed))) {
        m_v2Incremental = true;
        startV2Fallback(QStringLiteral("incremental"));
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
        fail(L("Không chuẩn bị được kết nối an toàn với điện thoại. Vui lòng thử lại.", "Couldn't prepare a secure connection to the phone. Please try again."));
        return;
    }

    m_v2Active = true;
    m_v2Phase = V2Phase::Conversation;
    m_v2ConvCursor = 0;
    m_v2UssIdx = 0;
    m_v2PendingConvs.clear();
    m_v2PendingConvKeys.clear();
    m_v2Checkpoints.clear();
    m_v2SessionPartial = false;
    m_v2StopNote.clear();
    m_v2SessionStart = 0;
    m_v2Conversations.clear();
    m_ids.clear();
    m_v2OpenSyncIds.clear();
    m_v2ConversationSyncId.clear();
    m_v2UnresolvedIds.clear();
    m_v2OwnGlobalId.clear();
    m_v2PartialSessions = 0;
    m_v2SessionRetries = 0;
    m_v2Parsing = false;
    m_v2Spooled.clear();
    m_v2SpoolSeq = 0;
    m_sessionTicks0 = 0;
    m_sessionConvert0 = m_sessionSave0 = 0;
    m_sessionFailed0 = 0;
    m_v2Importing = false;
    m_importV2Rows = true;
    m_rowsPerTick = 50;
    m_importTicks = 0;
    m_importConvertMs = m_importSaveMs = 0;
    m_lastProgress = 0;
    m_conversations.clear();
    m_importedMessages = 0;
    m_importedConversations = 0;
    m_importedConvKeys.clear();
    m_skipped = 0;
    m_skipReasons.clear();
    m_storeInserted = m_storeUpdated = m_storeDeduped = m_storeFailed = 0;
    m_ingestReasons.clear();

    setState(WaitingConfirm,
             L("Đã gửi yêu cầu. Mở Zalo trên điện thoại và bấm Đồng ý để chuyển tin nhắn.",
               "Request sent. Open Zalo on your phone and tap Allow to transfer messages."),
             10);

    const quint64 gen = m_generation;
    m_session->encryptToGlobalIds({m_myNoisedUid}, [this, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (gen != m_generation || !m_v2Active) return;
        m_v2OwnGlobalId = ok ? Network::ZaloSession::globalIdMapOf(data).value(m_myNoisedUid) : QString();
        if (m_v2OwnGlobalId.isEmpty()) {
            fail(L("Không đối chiếu được tài khoản với máy chủ Zalo.", "Couldn't match your account with Zalo's servers."));
            return;
        }
        m_myPlainUid = m_v2OwnGlobalId;
        sendV2ConversationRequest();
    });
}

void MobileHistorySync::sendV2ConversationRequest() {
    const QJsonArray queries{QJsonObject{
        {"partition", QStringLiteral("")},
        {"from", 0},
        {"to", qint64(9007199254740991LL)},
        {"limit", qint64(2147483647LL)},
    }};
    sendV2SessionRequest(QStringLiteral("conversation"), 0, queries, 0, true);
}

void MobileHistorySync::releaseV2Sessions(int reason) {
    if (!m_v2OpenSyncIds.isEmpty()) {
        log(QStringLiteral("v2 release sessions=%1 reason=%2").arg(m_v2OpenSyncIds.size()).arg(reason));
    }
    const QStringList ids = std::exchange(m_v2OpenSyncIds, {});
    m_v2ConversationSyncId.clear();
    if (!m_session || ids.isEmpty()) return;
    const QPointer<Network::ZaloSession> session = m_session;
    runInSequence(int(ids.size()), [session, ids, reason](int k, std::function<void()> next) {
        if (!session) return;
        auto conn = std::make_shared<QMetaObject::Connection>();
        auto done = std::make_shared<bool>(false);
        auto proceed = [conn, done, next]() {
            if (*done) return;
            *done = true;
            QObject::disconnect(*conn);
            next();
        };
        auto expected = std::make_shared<QString>();
        *conn = QObject::connect(session, &Network::ZaloSession::syncV2ReleaseAcked, session,
                                 [expected, proceed](const QString& reqId) {
                                     if (!expected->isEmpty() && reqId == *expected) proceed();
                                 });
        QTimer::singleShot(kReleaseAckMs, session, proceed);
        session->sendSyncV2Release(ids.at(k), reason);
        *expected = session->lastSyncV2ReleaseReqId();
    });
}

void MobileHistorySync::releaseV2ConversationSession() {
    if (m_v2ConversationSyncId.isEmpty()) return;
    if (m_session) m_session->sendSyncV2Release(m_v2ConversationSyncId, 1);
    m_v2OpenSyncIds.removeAll(m_v2ConversationSyncId);
    m_v2ConversationSyncId.clear();
}

void MobileHistorySync::sendV2SessionRequest(const QString& type, int priority, const QJsonArray& queries,
                                             int ussidx, bool firstOfPhase) {
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
    m_v2AwaitingAck = false;
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
        {"tempKey", firstOfPhase ? m_mobileTempKey : QString()},
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
    m_v2OpenSyncIds.append(m_v2SyncId);
    if (type == QLatin1String("conversation")) m_v2ConversationSyncId = m_v2SyncId;
    m_v2AwaitingAck = true;
    const bool wsSent = m_session->sendSyncV2Request(m_v2SyncId, syncData);
    log(QStringLiteral("v2 590 sent type=%1 ussidx=%2 queries=%3 sync=%4 ws=%5")
            .arg(type).arg(ussidx).arg(queries.size()).arg(m_v2SyncId.left(8)).arg(wsSent));
    if (!wsSent) {
        fail(L("Chưa kết nối được máy chủ Zalo để gửi yêu cầu đồng bộ. Vui lòng thử lại.",
               "Not connected to Zalo's servers yet, so the sync request couldn't be sent. Please try again."));
        m_v2AwaitingAck = false;
        return;
    }
}

void MobileHistorySync::onV2RequestSent() {
    if (!m_v2Active || !m_v2AwaitingAck) return;
    const quint64 gen = m_generation;
    QTimer::singleShot(v2AckTimeoutMs(), this, [this, gen]() {
        if (gen != m_generation || !m_v2Active || !m_v2AwaitingAck) return;
        m_v2AwaitingAck = false;
        log(QStringLiteral("v2 590 no ack within %1 ms").arg(v2AckTimeoutMs()));
        endV2OnStall(L("Máy chủ Zalo không nhận yêu cầu đồng bộ. Hãy thử lại sau.",
                       "Zalo's server did not accept the sync request. Try again later."));
    });
}

void MobileHistorySync::sendNextV2MessageSession() {
    constexpr int kMaxQueriesPerRequest = 30;
    if (!m_v2Active || !m_session || m_v2ConvCursor >= m_v2PendingConvs.size()) return;

    m_v2Ek = V2::generateKeyPair();
    if (m_v2Ek.pub.size() != 33 || m_v2Ek.priv.size() != 32) {
        fail(L("Không chuẩn bị được kết nối an toàn với điện thoại. Vui lòng thử lại.", "Couldn't prepare a secure connection to the phone. Please try again."));
        return;
    }

    const bool firstMessageSession = m_v2ConvCursor == 0;
    m_v2SessionStart = m_v2ConvCursor;
    m_v2SessionPartial = false;
    QJsonArray queries;
    const int end = qMin(static_cast<int>(m_v2PendingConvs.size()), m_v2ConvCursor + kMaxQueriesPerRequest);
    for (int i = m_v2ConvCursor; i < end; ++i) {
        const auto& conv = m_v2PendingConvs.at(i);
        const QString partition = v2PartitionOf(conv);
        qint64 fromTs = 0;
        qint64 toTs = 0;
        v2QueryRange(conv.lastTs, m_v2Checkpoints.value(partition), &fromTs, &toTs);
        queries.append(QJsonObject{
            {"partition", partition},
            {"from", fromTs},
            {"to", toTs},
            {"limit", qint64(2147483647LL)},
        });
    }
    m_v2ConvCursor = end;
    ++m_v2UssIdx;

    const int progress = v2MessageSessionProgress(m_v2ConvCursor - static_cast<int>(queries.size()),
                                                        static_cast<int>(m_v2PendingConvs.size()));
    setState(PhonePreparing,
             L("Đang tải tin nhắn từng hội thoại (%1/%2)...",
               "Fetching messages for each conversation (%1/%2)...")
                 .arg(m_v2ConvCursor).arg(m_v2PendingConvs.size()),
             progress);

    sendV2SessionRequest(QStringLiteral("message"), 2, queries, m_v2UssIdx, firstMessageSession);
}

void MobileHistorySync::cancel() {
    const bool active = m_state == WaitingConfirm || m_state == PhonePreparing || m_state == Importing;
    m_timeout.stop();
    ++m_generation;
    if (m_download) m_download->abort();
    if (m_v2Active) releaseV2Sessions(0);
    m_v2OpenSyncIds.clear();
    m_v2ConversationSyncId.clear();
    m_v2Importing = false;
    m_v2Parsing = false;
    m_v2Spooled.clear();
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
    m_v2Checkpoints.clear();
    m_v2SessionPartial = false;
    m_v2StopNote.clear();
    m_v2SessionStart = 0;
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
        BackupDirStats stats;
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
        result->conversations = readBackupDirectory(output, &result->error, &result->stats);
    });
    connect(worker, &QThread::finished, worker, [worker, workDir]() {
        QDir(workDir).removeRecursively();
        worker->deleteLater();
    });
    connect(worker, &QThread::finished, this, [this, result, gen]() {
        if (gen != m_generation) return;
        int rows = 0;
        int sourceRows = 0;
        for (const auto& c : std::as_const(result->conversations)) {
            rows += c.rows.size();
            sourceRows += c.sourceRows;
        }
        log(QStringLiteral("decrypt/read: conversations=%1 source_rows=%2 rows=%3 error=%4")
                .arg(result->conversations.size()).arg(sourceRows).arg(rows).arg(result->error));
        QStringList shapes;
        for (auto it = result->stats.badNameShapes.cbegin(); it != result->stats.badNameShapes.cend(); ++it) {
            shapes << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
        }
        log(QStringLiteral("backup files: total=%1 bad_name=%2 no_rows=%3 bad_shapes=[%4]")
                .arg(result->stats.files).arg(result->stats.badName).arg(result->stats.noRows)
                .arg(shapes.join(QStringLiteral(", "))));
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
        m_importedConvKeys.clear();
        m_importV2Rows = false;
        m_rowsPerTick = 50;
        m_importTicks = 0;
        m_importConvertMs = m_importSaveMs = 0;
        m_skipped = 0;
        m_skipReasons.clear();
        m_storeInserted = m_storeUpdated = m_storeDeduped = m_storeFailed = 0;
        m_ingestReasons.clear();
        importNextChunk();
    };

    QList<QPair<QStringList, QStringList>> batches;
    for (int i = 0; i < qMax(userList.size(), groupList.size()); i += kNoiseBatch) {
        const QStringList u = userList.mid(i, kNoiseBatch);
        const QStringList g = groupList.mid(i, kNoiseBatch);
        if (!u.isEmpty() || !g.isEmpty()) batches.append({u, g});
    }
    pending->outstanding = int(batches.size());
    runInSequence(int(batches.size()), [this, batches, onReply, gen](int k, std::function<void()> next) {
        if (gen != m_generation) return;
        const QStringList u = batches.at(k).first;
        const QStringList g = batches.at(k).second;
        m_session->fetchNoisedIds(u, g, [this, onReply, u, g, gen, next](bool ok, const QJsonValue& data, const QString&, int) {
            onReply(u, g, ok, data);
            if (gen == m_generation) next();
        });
    });
    if (pending->outstanding == 0) fail(L("Bản sao lưu không có hội thoại hợp lệ.", "The backup has no valid conversations."));
}

void MobileHistorySync::runInSequence(int count, SequenceStep step) {
    auto run = std::make_shared<std::function<void(int)>>();
    *run = [count, step = std::move(step), weak = std::weak_ptr<std::function<void(int)>>(run)](int k) {
        if (k >= count) return;
        auto self = weak.lock();
        if (!self) return;
        step(k, [self, k] { (*self)(k + 1); });
    };
    (*run)(0);
}

void MobileHistorySync::recordIngestDelta(const QHash<QString, int>& drops) {
    for (auto it = drops.constBegin(); it != drops.constEnd(); ++it) m_ingestReasons[it.key()] += it.value();
}

void MobileHistorySync::recordStoreDelta(int inserted, int updated, int deduped, int failed) {
    m_storeInserted += inserted;
    m_storeUpdated += updated;
    m_storeDeduped += deduped;
    m_storeFailed += failed;
}

void MobileHistorySync::importNextChunk() {
    if (m_importIndex >= m_conversations.size()) {
        m_conversations.clear();
        if (m_v2Active && m_v2Importing) {
            m_v2Importing = false;
            pumpV2();
            return;
        }
        finishImport();
        return;
    }
    importRows();
}

void MobileHistorySync::finishImport() {
    const QString stopNote = std::exchange(m_v2StopNote, QString());
    cleanup();
    QString status = L("Đã chuyển %1 tin nhắn của %2 hội thoại từ điện thoại.",
                       "Transferred %1 messages from %2 conversations.")
                         .arg(m_importedMessages).arg(m_importedConversations);
    if (!stopNote.isEmpty()) status += QLatin1Char(' ') + stopNote;
    if (m_metaSet) {
        m_metaSet(kMetaPhoneSynced, QStringLiteral("1"));
        m_metaSet(kMetaIncrementalFailed, QString());
    }
    m_v2Incremental = false;
    if (m_skipped > 0) status += L(" Bỏ qua %1 tin không hiển thị được.", " Skipped %1 messages that can't be shown.").arg(m_skipped);
    QStringList reasons;
    for (auto it = m_skipReasons.constBegin(); it != m_skipReasons.constEnd(); ++it) {
        reasons << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
    }
    std::sort(reasons.begin(), reasons.end());
    log(QStringLiteral("import done: transferred=%1 conversations=%2 skipped=%3 reasons=[%4]")
            .arg(m_importedMessages).arg(m_importedConversations).arg(m_skipped).arg(reasons.join(QStringLiteral(", "))));
    log(QStringLiteral("import time: ticks=%1 convert_ms=%2 save_ms=%3")
            .arg(m_importTicks).arg(m_importConvertMs).arg(m_importSaveMs));
    log(QStringLiteral("store: inserted=%1 updated=%2 deduped=%3 failed=%4")
            .arg(m_storeInserted).arg(m_storeUpdated).arg(m_storeDeduped).arg(m_storeFailed));
    QStringList ingest;
    for (auto it = m_ingestReasons.constBegin(); it != m_ingestReasons.constEnd(); ++it) {
        ingest << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
    }
    std::sort(ingest.begin(), ingest.end());
    log(QStringLiteral("ingest drops: [%1]").arg(ingest.join(QStringLiteral(", "))));
    setState(Done, status, 100);
}

void MobileHistorySync::importRows() {
    const int rowsThisTick = m_rowsPerTick;
    QElapsedTimer tick;
    tick.start();
    BackupConversation& conv = m_conversations[m_importIndex];
    const int totalRows = static_cast<int>(conv.rows.size());
    const QJsonArray messages = toMessages(conv, m_ids, m_myPlainUid, m_myNoisedUid, &m_skipped,
                                           m_importRowOffset, rowsThisTick, &m_skipReasons, m_importV2Rows);
    const qint64 convertMs = tick.elapsed();
    if (!messages.isEmpty()) {
        emit messagesImported(messages, conv.isGroup);
        m_importedMessages += messages.size();
        if (!m_currentConvImported) {
            const QString key = (conv.isGroup ? QStringLiteral("g") : QStringLiteral("u")) + conv.plainId;
            if (!m_importedConvKeys.contains(key)) {
                m_importedConvKeys.insert(key);
                m_importedConversations++;
            }
            m_currentConvImported = true;
        }
    }
    const qint64 tickMs = tick.elapsed();
    m_importConvertMs += convertMs;
    m_importSaveMs += tickMs - convertMs;
    ++m_importTicks;
    m_rowsPerTick = nextRowsPerTick(rowsThisTick, tickMs);
    m_importRowOffset = qMin(totalRows, m_importRowOffset + rowsThisTick);
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
    const int progress = m_v2Active ? m_lastProgress : 70 + int(29.0 * doneRatio);
    setState(Importing, L("Đang nhập tin nhắn (%1/%2 hội thoại)...", "Importing messages (%1/%2 conversations)...")
                            .arg(displayConv).arg(m_conversations.size()), progress);
    const quint64 gen = m_generation;
    QTimer::singleShot(10, this, [this, gen]() {
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
        if (act == QLatin1String("upload_batch") && !m_v2OpenSyncIds.contains(syncId)) m_session->sendSyncV2Release(syncId, 3);
        return;
    }

    if (act == QLatin1String("transfer_status")) {
        const int status = data.value("status").toVariant().toInt();
        if (status == 3) {
            setState(WaitingConfirm,
                     L("Đã gửi yêu cầu. Mở Zalo trên điện thoại và bấm Đồng ý để chuyển tin nhắn.",
                       "Request sent. Open Zalo on your phone and tap Allow to transfer messages."),
                     10);
        } else if (status == 1 || status == 4 || status == 5) {
            if (m_v2Phase == V2Phase::Message) releaseV2ConversationSession();
            setState(PhonePreparing,
                     L("Điện thoại đã đồng ý. Đang chờ điện thoại đóng gói và gửi tin nhắn...",
                       "The phone agreed. Waiting for it to pack and send messages..."),
                     20);
            const bool drained = !m_v2InFlightDownload && m_v2DownloadQueue.isEmpty() && !m_v2Importing;
            if (drained) m_timeout.start(v2StatusTimeoutMs(status, v2BatchSeen(int(m_v2SeenBatchIdxs.size()), int(m_v2SeenUrls.size()))));
        } else if (status == 6 || status == 7) {
            releaseV2Sessions(0);
            m_v2Active = false;
            fail(L("Yêu cầu đồng bộ đã bị từ chối trên điện thoại.", "The sync request was declined on the phone."));
        } else if (status == 8) {
            releaseV2Sessions(2);
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
            if (!m_v2Importing) m_timeout.start(v2StatusTimeoutMs(status, v2BatchSeen(int(m_v2SeenBatchIdxs.size()), int(m_v2SeenUrls.size()))));
        } else if (status == 9 || status == 10) {
            releaseV2Sessions(2);
            m_v2Active = false;
            fail(L("Điện thoại đã hủy hoặc gặp lỗi khi đồng bộ.",
                   "The phone cancelled the sync or ran into an error."));
        }
        return;
    }

    if (act == QLatin1String("transfer_error")) {
        const int err = data.value("errorCode").toInt(data.value("err").toInt(0));
        releaseV2Sessions(2);
        m_v2Active = false;
        Network::ErrorCatalog::trace("phone-sync", QStringLiteral("v2 transfer_error code=%1 name=%2")
                                                       .arg(err).arg(Network::ErrorCatalog::phoneSyncName(err)));
        fail(Network::ErrorCatalog::pick(Network::ErrorCatalog::phoneSyncError(err), m_lang == QLatin1String("en")));
        return;
    }

    if (act == QLatin1String("upload_batch")) {
        const int err = data.value("err").toVariant().toInt();
        if (err != 0) {
            releaseV2Sessions(2);
            m_v2Active = false;
            Network::ErrorCatalog::trace("phone-sync", QStringLiteral("v2 upload_batch err=%1").arg(err));
            fail(L("Điện thoại gặp lỗi khi gửi tin nhắn. Vui lòng thử lại.",
                   "The phone ran into an error while sending messages. Please try again."));
            return;
        }
        const QString msgUrl = data.value("msgUrl").toString(data.value("url").toString());
        const int batchType = data.value("batchType").toVariant().toInt();
        bool idxOk = false;
        const int idxParsed = data.value("idx").toVariant().toInt(&idxOk);
        const int batchIdx = (data.contains("idx") && idxOk && idxParsed >= 0) ? idxParsed : -1;
        const bool isLast = data.value("isLast").toVariant().toInt() == 1 || data.value("isLast").toBool();
        log(QStringLiteral("v2 batch idx=%1 last=%2 type=%3 url=%4 seen=%5")
                .arg(batchIdx).arg(isLast).arg(batchType).arg(!msgUrl.isEmpty()).arg(m_v2SeenBatchIdxs.size()));
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
                    row.v2Quote = std::move(q);
                }
                if (!pm.mentions.isEmpty()) {
                    row.v2Mentions.reserve(pm.mentions.size());
                    for (const V2::ParsedSyncV2Mention& pmMention : pm.mentions) {
                        V2::ParsedSyncV2Mention m = pmMention;
                        m.uid = stripV2PartitionPrefix(m.uid);
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
        QVector<QByteArrayView> frames;
        if (!V2::splitFrames(bytes, &frames) || frames.isEmpty()) {
            out.errorKind = V2MessageParseResult::InvalidFraming;
            return out;
        }
        for (const QByteArrayView frame : frames) {
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

QStringList MobileHistorySync::v2IdsToResolve(const QVector<BackupConversation>& convs,
                                              const QHash<QString, QString>& known, const QSet<QString>& unresolved,
                                              const QString& ownGlobalId) {
    QSet<QString> ids;
    auto want = [&](const QString& id) {
        if (id.isEmpty() || id == QLatin1String("0") || id == QLatin1String("-1") || id == ownGlobalId) return;
        if (known.contains(id) || unresolved.contains(id)) return;
        ids.insert(id);
    };
    for (const auto& conv : convs) {
        want(conv.plainId);
        for (const auto& row : conv.rows) {
            want(row.senderId);
            if (row.v2Quote.has_value()) want(row.v2Quote->ownerId);
            for (const auto& m : row.v2Mentions) want(m.uid);
        }
    }
    return QStringList(ids.cbegin(), ids.cend());
}

int MobileHistorySync::drainedQueueTimeoutMs(bool v2Active, bool fulfilled, bool startedNextSession,
                                             bool seenAnyBatch) {
    if (!v2Active || fulfilled || startedNextSession) return 0;
    return seenAnyBatch ? kBatchIdleMs : kBackupTimeoutMs;
}

bool MobileHistorySync::spoolV2Batch(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    if (!ok) QFile::remove(path);
    return ok;
}

QByteArray MobileHistorySync::takeV2Batch(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = f.readAll();
    f.close();
    QFile::remove(path);
    return bytes;
}

bool MobileHistorySync::v2SessionDrained(bool queueEmpty, bool downloading, int spooled, bool parsing, bool importing) {
    return queueEmpty && !downloading && spooled == 0 && !parsing && !importing;
}

QString MobileHistorySync::v2PartitionOf(const V2::ParsedSyncV2Conversation& conv) {
    return (conv.convType == 2 ? QStringLiteral("group/") : QStringLiteral("oneone/")) + conv.convId;
}

bool MobileHistorySync::v2QueryRange(quint64 phoneLastTs, qint64 checkpoint, qint64* from, qint64* to) {
    const bool known = phoneLastTs > 0;
    if (known && checkpoint > 0 && quint64(checkpoint) >= phoneLastTs) return false;
    if (from) *from = known && checkpoint > 0 ? checkpoint : 0;
    if (to) *to = known ? qint64(phoneLastTs) : qint64(9007199254740991LL);
    return true;
}

QVector<V2::ParsedSyncV2Conversation> MobileHistorySync::v2ConversationsToFetch(
    const QVector<V2::ParsedSyncV2Conversation>& convs, const QHash<QString, qint64>& checkpoints,
    const QSet<QString>& storedLastIds, int* upToDate) {
    QVector<V2::ParsedSyncV2Conversation> out;
    out.reserve(convs.size());
    int skipped = 0;
    for (const auto& conv : convs) {
        const bool lastStored = conv.lastGlobalId > 0 && storedLastIds.contains(QString::number(conv.lastGlobalId));
        if (!lastStored && v2QueryRange(conv.lastTs, checkpoints.value(v2PartitionOf(conv)), nullptr, nullptr)) out.append(conv);
        else ++skipped;
    }
    if (upToDate) *upToDate = skipped;
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.lastTs > b.lastTs; });
    return out;
}

QHash<QString, qint64> MobileHistorySync::v2SessionCheckpoints(const QVector<V2::ParsedSyncV2Conversation>& convs,
                                                               int begin, int end, bool partial,
                                                               const QSet<QString>& unresolved, int storeFailures) {
    QHash<QString, qint64> out;
    if (partial || storeFailures > 0) return out;
    for (int i = qMax(0, begin); i < qMin(int(convs.size()), end); ++i) {
        const auto& conv = convs.at(i);
        if (conv.lastTs > 0 && !unresolved.contains(conv.convId)) out.insert(v2PartitionOf(conv), qint64(conv.lastTs));
    }
    return out;
}

int MobileHistorySync::v2StatusTimeoutMs(int status, bool seenAnyBatch) {
    if (status == 2) return kBackupTimeoutMs;
    return seenAnyBatch ? kBatchIdleMs : kFirstBatchMs;
}

bool MobileHistorySync::v2BatchSeen(int indexedBatches, int batchUrls) {
    return indexedBatches > 0 || batchUrls > 0;
}

int MobileHistorySync::v2AckTimeoutMs() {
    return 10 * 1000;
}

void MobileHistorySync::onV2RequestAcked(int err) {
    if (!m_v2Active || !m_v2AwaitingAck) return;
    m_v2AwaitingAck = false;
    log(QStringLiteral("v2 590 ack err=%1").arg(err));
    if (err != 0) {
        endV2OnStall(L("Máy chủ Zalo từ chối yêu cầu đồng bộ. Hãy thử lại sau.",
                       "Zalo's server refused the sync request. Try again later."));
    }
}

bool MobileHistorySync::v2CanRetrySession(int retries) {
    return retries < kMaxSessionRetries;
}

bool MobileHistorySync::v2PreferIncremental(const QString& syncedMarker, const QString& failedMarker) {
    return syncedMarker == QLatin1String("1") && failedMarker != QLatin1String("1");
}

bool MobileHistorySync::v2IncrementalUnsupported(bool conversationListReceived) {
    return !conversationListReceived;
}

bool MobileHistorySync::v2StallEndsPartially(int conversationsDone) {
    return conversationsDone > 0;
}

QString MobileHistorySync::v2PartialStatus(bool english, int conversationsDone, int conversationsTotal) {
    return (english ? QStringLiteral("The phone stopped answering after %1 of %2 conversations. Sync again to continue.")
                    : QStringLiteral("Điện thoại ngừng phản hồi sau %1/%2 hội thoại. Bấm Đồng bộ lại để lấy tiếp."))
        .arg(conversationsDone)
        .arg(conversationsTotal);
}

void MobileHistorySync::endV2OnStall(const QString& failure) {
    if (m_v2Phase != V2Phase::Message || !v2StallEndsPartially(m_v2SessionStart)) {
        fail(failure);
        return;
    }
    log(QStringLiteral("v2 stopped early: no answer, conversations done=%1 of %2")
            .arg(m_v2SessionStart).arg(m_v2PendingConvs.size()));
    m_timeout.stop();
    m_v2StopNote = v2PartialStatus(m_lang == "en", m_v2SessionStart, int(m_v2PendingConvs.size()));
    releaseV2Sessions(1);
    m_v2Active = false;
    m_conversations.clear();
    finishImport();
}

int MobileHistorySync::v2MessageSessionProgress(int conversationsDone, int conversationsTotal) {
    if (conversationsTotal <= 0) return 30;
    return 30 + (68 * qBound(0, conversationsDone, conversationsTotal)) / conversationsTotal;
}

int MobileHistorySync::v2DownloadProgress(bool v2Active, int batchType, int lastProgress) {
    if (v2Active) return lastProgress;
    return batchType == 2 ? 30 : 50;
}

int MobileHistorySync::nextRowsPerTick(int current, qint64 elapsedMs) {
    constexpr int kMinRows = 10;
    constexpr int kMaxRows = 300;
    if (elapsedMs > 60) return qMax(kMinRows, current / 2);
    if (elapsedMs < 20) return qMin(kMaxRows, current * 2);
    return current;
}

QStringList MobileHistorySync::messageIdsOf(const QJsonArray& messages) {
    QStringList ids;
    ids.reserve(messages.size());
    for (const QJsonValue& v : messages) ids << v.toObject().value("msgId").toVariant().toString();
    return ids;
}

QJsonArray MobileHistorySync::withoutSavedMessages(const QJsonArray& messages,
                                                   const QHash<QString, QString>& savedSenders, bool sendersTrusted,
                                                   int* dropped, QHash<QString, QString>* senderFixes) {
    if (savedSenders.isEmpty()) return messages;
    QJsonArray out;
    for (const QJsonValue& v : messages) {
        const QJsonObject msg = v.toObject();
        const QString id = msg.value("msgId").toVariant().toString();
        const auto saved = savedSenders.constFind(id);
        if (saved == savedSenders.cend()) {
            out.append(v);
            continue;
        }
        if (dropped) ++*dropped;
        const QString sender = msg.value("uidFrom").toVariant().toString();
        if (sendersTrusted && senderFixes && !isPhoneLocalId(id) && !sender.isEmpty() && sender != QLatin1String("0") && sender != saved.value()) {
            senderFixes->insert(id, sender);
        }
    }
    return out;
}

bool MobileHistorySync::v2SessionFulfilled(bool sawLastBatch, int expectedBatches, int seenBatches) {
    return sawLastBatch && (expectedBatches < 0 || seenBatches >= expectedBatches);
}

void MobileHistorySync::processNextV2Download() {
    if (!m_v2Active || !m_session || m_v2InFlightDownload) return;
    if (m_v2DownloadQueue.isEmpty()) {
        if (!v2SessionDrained(true, false, int(m_v2Spooled.size()), m_v2Parsing, m_v2Importing)) return;
        const quint64 generationBefore = m_generation;
        maybeFinishV2();
        const int ms = drainedQueueTimeoutMs(
            m_v2Active, v2SessionFulfilled(m_v2SawLastBatch, m_v2ExpectedBatches, int(m_v2SeenBatchIdxs.size())),
            m_generation != generationBefore, v2BatchSeen(int(m_v2SeenBatchIdxs.size()), int(m_v2SeenUrls.size())));
        if (ms > 0) m_timeout.start(ms);
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
                 ? L("Đang nhận thông tin đồng bộ từ điện thoại...", "Receiving sync details from the phone...")
                 : L("Đang nhận tin nhắn từ điện thoại...", "Receiving messages from the phone..."),
             v2DownloadProgress(m_v2Active, task.batchType, m_lastProgress));
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
            releaseV2Sessions(2);
            m_v2Active = false;
            fail(L("Dữ liệu điện thoại gửi quá lớn (trên 64 MB) nên chưa nhận được.", "The data sent by the phone is too large (over 64 MB) to receive."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
            m_v2InFlightDownload = false;
            releaseV2Sessions(2);
            m_v2Active = false;
            const bool en = m_lang == QLatin1String("en");
            const QString detail = httpStatus > 0
                ? Network::ErrorCatalog::render(httpStatus, QStringLiteral("HTTP %1").arg(httpStatus), en)
                : Network::ErrorCatalog::render(0, reply->errorString(), en);
            fail(L("Không nhận được tin nhắn từ điện thoại: ", "Couldn't receive messages from the phone: ") + detail);
            return;
        }
        const QByteArray bytes = reply->readAll();
        if (bytes.size() > kMaxV2BatchBytes) {
            m_v2InFlightDownload = false;
            releaseV2Sessions(2);
            m_v2Active = false;
            fail(L("Dữ liệu điện thoại gửi quá lớn (trên 64 MB) nên chưa nhận được.", "The data sent by the phone is too large (over 64 MB) to receive."));
            return;
        }
        if (task.batchType == 1) {
            if (!m_v2HasSession) {
                m_v2InFlightDownload = false;
                releaseV2Sessions(2);
                m_v2Active = false;
                fail(L("Thiếu thông tin đồng bộ từ điện thoại. Vui lòng thử lại.", "Sync details from the phone are missing. Please try again."));
                return;
            }
            const QString path = QDir(v2WorkDir()).filePath(QStringLiteral("v2-%1.bin").arg(++m_v2SpoolSeq));
            if (!spoolV2Batch(path, bytes)) {
                m_v2InFlightDownload = false;
                releaseV2Sessions(2);
                m_v2Active = false;
                fail(L("Không lưu tạm được tin nhắn điện thoại gửi. Hãy kiểm tra dung lượng trống rồi thử lại.",
                       "Couldn't save the messages sent by the phone. Check free disk space and try again."));
                return;
            }
            m_v2Spooled.append(path);
            m_v2InFlightDownload = false;
            pumpV2();
            return;
        }
        m_v2InFlightDownload = false;
        QString err;
        if (!handleV2BatchBytes(task.batchType, bytes, &err)) {
            releaseV2Sessions(2);
            m_v2Active = false;
            fail(err);
            return;
        }
        pumpV2();
    });
}

QString MobileHistorySync::v2WorkDir() {
    if (m_workDir.isEmpty()) {
        m_workDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                        .filePath(QStringLiteral("sync/%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
        QDir().mkpath(m_workDir);
        QFile::setPermissions(m_workDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    return m_workDir;
}

void MobileHistorySync::pumpV2() {
    if (!m_v2Active) return;
    if (!m_v2Importing && !m_v2Parsing) {
        if (!m_v2Conversations.isEmpty()) importV2Batch();
        else if (!m_v2Spooled.isEmpty()) startV2Parse();
    }
    processNextV2Download();
}

void MobileHistorySync::startV2Parse() {
    const QByteArray bytes = takeV2Batch(m_v2Spooled.takeFirst());
    if (bytes.isEmpty()) {
        releaseV2Sessions(2);
        m_v2Active = false;
        fail(L("Không đọc lại được tin nhắn đã lưu tạm. Vui lòng thử lại.",
               "Couldn't read back the saved messages. Please try again."));
        return;
    }
    m_v2Parsing = true;
    const quint64 gen = m_generation;
    const V2::SessionRecord session = m_v2Session;
    const QString myNoisedUid = m_v2OwnGlobalId;
    const bool tryConv = m_v2Phase == V2Phase::Conversation;
    const QPointer<MobileHistorySync> self(this);
    QThreadPool::globalInstance()->start([self, gen, bytes, session, myNoisedUid, tryConv]() {
        V2MessageParseResult res = parseV2MessageBatch(bytes, session, myNoisedUid, tryConv);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, res = std::move(res)]() mutable {
            if (!self || gen != self->m_generation || !self->m_v2Active) return;
            self->m_v2Parsing = false;
            if (res.errorKind != V2MessageParseResult::None) {
                QString err;
                switch (res.errorKind) {
                case V2MessageParseResult::InvalidFraming:
                    err = self->L("Dữ liệu điện thoại gửi không hợp lệ. Vui lòng thử lại.", "The data sent by the phone is invalid. Please try again.");
                    break;
                case V2MessageParseResult::DecryptFailed:
                    Network::ErrorCatalog::trace("phone-sync", QStringLiteral("v2 message decrypt failed status=%1")
                                                                   .arg(V2::cryptoStatusName(res.cryptoStatus)));
                    err = self->L("Không đọc được tin nhắn điện thoại gửi. Vui lòng thử lại.",
                                  "Couldn't read the messages sent by the phone. Please try again.");
                    break;
                case V2MessageParseResult::ZstdFailed:
                    err = self->L("Không đọc được tin nhắn điện thoại gửi. Vui lòng thử lại.",
                                  "Couldn't read the messages sent by the phone. Please try again.");
                    break;
                case V2MessageParseResult::ParseFailed:
                case V2MessageParseResult::None:
                    err = self->L("Không đọc được tin nhắn điện thoại gửi. Vui lòng thử lại.", "Couldn't read the messages sent by the phone. Please try again.");
                    break;
                }
                self->releaseV2Sessions(2);
                self->m_v2Active = false;
                self->fail(err);
                return;
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
            self->pumpV2();
        }, Qt::QueuedConnection);
    });
}

bool MobileHistorySync::handleV2BatchBytes(int batchType, const QByteArray& bytes, QString* error) {
    if (batchType == 2) {
        QVector<QByteArrayView> frames;
        if (!V2::splitFrames(bytes, &frames) || frames.isEmpty()) {
            if (error) *error = L("Dữ liệu điện thoại gửi không hợp lệ. Vui lòng thử lại.", "The data sent by the phone is invalid. Please try again.");
            return false;
        }
        for (const QByteArrayView frame : frames) {
            V2::MetadataPlain metaOut;
            const V2::CryptoStatus st = V2::openMetadata(frame, m_v2Ik.pub, m_v2Ik.priv, m_v2Ek.priv, &metaOut);
            if (st != V2::CryptoStatus::Ok) {
                if (error) {
                    Network::ErrorCatalog::trace("phone-sync", QStringLiteral("v2 metadata decrypt failed status=%1")
                                                                   .arg(V2::cryptoStatusName(st)));
                    *error = L("Không đọc được thông tin đồng bộ từ điện thoại. Vui lòng thử lại.",
                               "Couldn't read the sync details from the phone. Please try again.");
                }
                return false;
            }
            m_v2Session = metaOut.session;
            m_v2HasSession = true;
            if (!metaOut.plaintext.isEmpty()) {
                V2MessageParseResult metaRes;
                decodeV2PlaintextChunk(metaOut.plaintext, m_v2Phase == V2Phase::Conversation, m_v2OwnGlobalId, &metaRes);
                if (metaRes.errorKind == V2MessageParseResult::None) {
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
            if (error) *error = L("Thiếu thông tin đồng bộ từ điện thoại. Vui lòng thử lại.", "Sync details from the phone are missing. Please try again.");
            return false;
        }
        V2MessageParseResult res = parseV2MessageBatch(bytes, m_v2Session, m_v2OwnGlobalId, m_v2Phase == V2Phase::Conversation);
        if (res.errorKind != V2MessageParseResult::None) {
            if (error) {
                switch (res.errorKind) {
                case V2MessageParseResult::InvalidFraming:
                    *error = L("Dữ liệu điện thoại gửi không hợp lệ. Vui lòng thử lại.", "The data sent by the phone is invalid. Please try again.");
                    break;
                case V2MessageParseResult::DecryptFailed:
                    Network::ErrorCatalog::trace("phone-sync", QStringLiteral("v2 message decrypt failed status=%1")
                                                                   .arg(V2::cryptoStatusName(res.cryptoStatus)));
                    *error = L("Không đọc được tin nhắn điện thoại gửi. Vui lòng thử lại.",
                               "Couldn't read the messages sent by the phone. Please try again.");
                    break;
                case V2MessageParseResult::ZstdFailed:
                    *error = L("Không đọc được tin nhắn điện thoại gửi. Vui lòng thử lại.",
                               "Couldn't read the messages sent by the phone. Please try again.");
                    break;
                case V2MessageParseResult::ParseFailed:
                case V2MessageParseResult::None:
                    *error = L("Không đọc được tin nhắn điện thoại gửi. Vui lòng thử lại.", "Couldn't read the messages sent by the phone. Please try again.");
                    break;
                }
            }
            return false;
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
    if (!m_v2Active || !v2SessionDrained(m_v2DownloadQueue.isEmpty(), m_v2InFlightDownload, int(m_v2Spooled.size()),
                                         m_v2Parsing, m_v2Importing)
        || !m_v2SawLastBatch) {
        return;
    }
    if (!v2SessionFulfilled(m_v2SawLastBatch, m_v2ExpectedBatches, int(m_v2SeenBatchIdxs.size()))) {
        log(QStringLiteral("v2 waiting for batches: seen=%1 expected=%2")
                .arg(m_v2SeenBatchIdxs.size()).arg(m_v2ExpectedBatches));
        return;
    }
    m_timeout.stop();
    log(QStringLiteral("v2 session done ussidx=%1 retries=%2 import_ticks=%3 convert_ms=%4 save_ms=%5")
            .arg(m_v2UssIdx).arg(m_v2SessionRetries).arg(m_importTicks - m_sessionTicks0)
            .arg(m_importConvertMs - m_sessionConvert0).arg(m_importSaveMs - m_sessionSave0));
    m_sessionTicks0 = m_importTicks;
    m_sessionConvert0 = m_importConvertMs;
    m_sessionSave0 = m_importSaveMs;
    m_v2SessionRetries = 0;
    const int sessionStoreFailures = m_storeFailed - m_sessionFailed0;
    m_sessionFailed0 = m_storeFailed;
    if (m_v2Phase == V2Phase::Message && m_saveCheckpoints) {
        const auto marks = v2SessionCheckpoints(m_v2PendingConvs, m_v2SessionStart, m_v2ConvCursor, m_v2SessionPartial,
                                                  m_v2UnresolvedIds, sessionStoreFailures);
        if (!marks.isEmpty()) m_saveCheckpoints(marks);
    }
    if (m_v2Phase == V2Phase::Conversation && !m_v2PendingConvs.isEmpty()) {
        m_v2Checkpoints = m_loadCheckpoints ? m_loadCheckpoints() : QHash<QString, qint64>{};
        QStringList lastIds;
        for (const auto& conv : std::as_const(m_v2PendingConvs)) {
            if (conv.lastGlobalId > 0) lastIds.append(QString::number(conv.lastGlobalId));
        }
        const QSet<QString> storedLastIds = m_storedIds ? m_storedIds(lastIds) : QSet<QString>{};
        int upToDate = 0;
        const int listed = int(m_v2PendingConvs.size());
        m_v2PendingConvs = v2ConversationsToFetch(m_v2PendingConvs, m_v2Checkpoints, storedLastIds, &upToDate);
        log(QStringLiteral("v2 conversations: listed=%1 to_fetch=%2 up_to_date=%3")
                .arg(listed).arg(m_v2PendingConvs.size()).arg(upToDate));
        m_v2Phase = V2Phase::Message;
        m_v2ConvCursor = 0;
        if (!m_v2PendingConvs.isEmpty()) {
            sendNextV2MessageSession();
            return;
        }
    }
    if (m_v2Phase == V2Phase::Message && m_v2ConvCursor < m_v2PendingConvs.size()) {
        sendNextV2MessageSession();
        return;
    }
    releaseV2Sessions(1);
    m_v2Active = false;
    m_conversations.clear();
    log(QStringLiteral("v2 ids: resolved=%1 unresolved=%2 partial_sessions=%3")
            .arg(m_ids.size()).arg(m_v2UnresolvedIds.size()).arg(m_v2PartialSessions));
    finishImport();
}

void MobileHistorySync::importV2Batch() {
    if (!m_v2Active || m_v2Conversations.isEmpty()) return;
    m_v2Importing = true;
    m_conversations = std::move(m_v2Conversations);
    m_v2Conversations.clear();
    resolveV2Ids(v2IdsToResolve(m_conversations, m_ids, m_v2UnresolvedIds, m_v2OwnGlobalId), 0);
}

void MobileHistorySync::resolveV2Ids(const QStringList& ids, int attempt) {
    constexpr int kDecryptBatch = 50;
    constexpr int kDecryptRetries = 2;
    if (ids.isEmpty()) {
        m_importIndex = 0;
        m_importRowOffset = 0;
        m_currentConvImported = false;
        setState(Importing, L("Đang nhập tin nhắn từ điện thoại...", "Importing messages from the phone..."), m_lastProgress);
        importNextChunk();
        return;
    }
    struct Pending {
        int outstanding{0};
        bool transportFailed{false};
    };
    auto pending = std::make_shared<Pending>();
    const quint64 gen = m_generation;
    for (int i = 0; i < ids.size(); i += kDecryptBatch) {
        const QStringList chunk = ids.mid(i, kDecryptBatch);
        pending->outstanding++;
        m_session->decryptGlobalIds(chunk, [this, pending, gen, ids, attempt](bool ok, const QJsonValue& data,
                                                                             const QString&, int) {
            if (gen != m_generation || !m_v2Active) return;
            if (ok) {
                const QHash<QString, QString> map = Network::ZaloSession::globalIdMapOf(data);
                for (auto it = map.constBegin(); it != map.constEnd(); ++it) m_ids.insert(it.key(), it.value());
            } else {
                pending->transportFailed = true;
            }
            if (--pending->outstanding > 0) return;
            QStringList missing;
            for (const QString& id : ids) {
                if (!m_ids.contains(id)) missing.append(id);
            }
            if (!missing.isEmpty() && attempt < kDecryptRetries) {
                QTimer::singleShot(300, this, [this, gen, missing, attempt]() {
                    if (gen == m_generation && m_v2Active) resolveV2Ids(missing, attempt + 1);
                });
                return;
            }
            if (pending->transportFailed) {
                fail(L("Không đối chiếu được danh bạ với máy chủ Zalo.", "Couldn't match contacts with Zalo's servers."));
                return;
            }
            for (const QString& id : std::as_const(missing)) m_v2UnresolvedIds.insert(id);
            resolveV2Ids({}, 0);
        });
    }
}

}
