#include "mobile_history_sync.h"

#include "crypto/db_cross.h"
#include "network/zalo_session.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <memory>

namespace Zalos::Core::Sync {

namespace {

constexpr int kConfirmTimeoutMs = 3 * 60 * 1000;   // phone shows the Allow prompt
constexpr int kBackupTimeoutMs = 30 * 60 * 1000;   // phone packs and uploads the backup
constexpr int kNoiseBatch = 100;

// ChatContent.MsgType -> TMessage msgType (Zalo PC shared-worker table `Be`).
QString typeName(int t) {
    static const QHash<int, QString> map{
        {0, "webchat"}, {2, "chat.doodle"}, {3, "chat.photo"}, {4, "chat.photo"}, {6, "chat.voice"},
        {10, "chat.sticker"}, {12, "chat.recommended"}, {15, "chat.list.action"}, {18, "chat.location.new"},
        {19, "chat.video.msg"}, {20, "webchat"}, {22, "share.file"}, {23, "chat.gif"}, {24, "chat.webcontent"},
        {53, "chat.webcontent.v2"}, {25, "chat.video.live.msg"}, {26, "group.poll"}, {31, "chat.photo"},
        {33, "chat.delete"}, {36, "chat.undo"},
    };
    return map.value(t, QStringLiteral("webchat"));
}

// Types Zalo PC itself never imports from a backup (system/ephemeral rows), plus delete/undo markers.
bool isSkippedType(int t) {
    static const QList<int> skipped{20, 21, 25, 26, 29, 32, 33, 34, 35, 36, 45, 51, 52};
    return skipped.contains(t);
}

QString str(const std::string& s) {
    return QString::fromStdString(s);
}

// "[Photo] ", "[Hình ảnh]", "[Video]"... — preview placeholders stored instead of real text.
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

} // namespace

MobileHistorySync::MobileHistorySync(Network::ZaloSession* session, QObject* parent)
    : QObject(parent), m_session(session) {
    // A run killed mid-import leaves decrypted history behind: remove it at startup.
    QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("sync")))
        .removeRecursively();
    // A run interrupted by a crash or kill may have left a download or decrypted databases behind.
    QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("sync")))
        .removeRecursively();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_state == WaitingConfirm) {
            fail(L("Điện thoại chưa xác nhận yêu cầu đồng bộ.", "The phone did not confirm the sync request."));
        } else if (m_state == PhonePreparing) {
            fail(L("Điện thoại chưa gửi bản sao lưu sau 30 phút.", "The phone did not send a backup within 30 minutes."));
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
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " " + line + "\n").toUtf8());
}

namespace {
// Keys and value types of a JSON object, for logs (no values).
QString shapeOf(const QJsonValue& v) {
    if (v.isArray()) return QStringLiteral("array[%1]").arg(v.toArray().size());
    if (!v.isObject()) return v.isString() ? "string" : v.isDouble() ? "number" : v.isBool() ? "bool" : "null";
    QStringList parts;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) parts << it.key() + ":" + shapeOf(it.value());
    return "{" + parts.join(",") + "}";
}
} // namespace

void MobileHistorySync::setState(int state, const QString& status, int progress) {
    log(QStringLiteral("state %1 progress %2").arg(state).arg(progress));
    m_state = state;
    emit stateChanged(state, status, progress);
}

void MobileHistorySync::fail(const QString& reason) {
    m_timeout.stop();
    ++m_generation;
    if (m_download) m_download->abort();
    cleanup();
    log("failed: " + reason);
    setState(Failed, reason, 0);
}

// ---------------------------------------------------------------------------
// Crypto building blocks
// ---------------------------------------------------------------------------

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
    // Zalo PC: privateDecrypt(..., output: format ? "hex" : "base64").
    return format ? QString::fromLatin1(plainKey.toHex()) : QString::fromLatin1(plainKey.toBase64());
}

// ---------------------------------------------------------------------------
// Backup reading and conversion
// ---------------------------------------------------------------------------

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
                // Same filter as Zalo PC (MsgStatus > 0, has a client id); fall back to all rows when a
                // backup variant lacks one of the columns.
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
                    if (bin >= 0) row.binNet = q.value(bin).toByteArray();
                    if (row.globalMsgId.isEmpty() && row.cliMsgId.isEmpty()) continue;
                    conv.rows.append(row);
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
                                         const QString& myPlainUid, const QString& myNoisedUid, int* skipped) {
    QJsonArray out;
    int skip = 0;
    const QString thread = ids.value(conv.plainId);
    if (thread.isEmpty()) {
        if (skipped) *skipped += conv.rows.size();
        return out;
    }
    auto noised = [&](const QString& plain) -> QString {
        if (plain.isEmpty()) return {};
        if (plain == myPlainUid) return myNoisedUid;
        return ids.value(plain);
    };

    for (const BackupRow& row : conv.rows) {
        if (isSkippedType(row.msgType)) {
            ++skip;
            continue;
        }
        const bool mine = !myPlainUid.isEmpty() && row.senderId == myPlainUid;
        QString uidFrom;
        QString idTo;
        if (conv.isGroup) {
            uidFrom = mine ? QStringLiteral("0") : noised(row.senderId);
            idTo = thread;
        } else {
            uidFrom = mine ? QStringLiteral("0") : thread;
            idTo = mine ? thread : myNoisedUid;
        }
        if (uidFrom.isEmpty() || idTo.isEmpty()) {
            ++skip;
            continue;
        }

        const auto bin = Crypto::DbCross::parseBinNet(reinterpret_cast<const uint8_t*>(row.binNet.constData()),
                                                     static_cast<size_t>(row.binNet.size()));
        const QString type = typeName(row.msgType);
        QJsonValue content;
        if (type == QLatin1String("webchat") || bin.attachs.empty()) {
            const QString text = row.text.isEmpty() ? str(bin.text) : row.text;
            // A media row without its attachment is only a placeholder: nothing to show.
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
            // MsgContent of media rows is a placeholder ("[Photo] ", "[Video]"...), never a caption.
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
        if (bin.quote.globalMsgId > 0) {
            const QString owner = QString::number(bin.quote.ownerId);
            msg["quote"] = QJsonObject{{"ownerId", noised(owner)},
                                       {"globalMsgId", QString::number(bin.quote.globalMsgId)},
                                       {"cliMsgId", QString::number(bin.quote.cliMsgId)},
                                       {"msg", str(bin.quote.msg)},
                                       {"attach", str(bin.quote.attach)},
                                       {"fromD", str(bin.quote.fromD)},
                                       {"ts", QString::number(bin.quote.ts)}};
        }
        out.append(msg);
    }
    if (skipped) *skipped += skip;
    return out;
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

void MobileHistorySync::start(const QString& myNoisedUid, const QString& pcName) {
    if (!m_session) return;
    cancel();
    m_myNoisedUid = myNoisedUid;
    m_pcName = pcName;
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
            fail(L("Máy chủ từ chối yêu cầu đồng bộ: ", "The server refused the sync request: ")
                 + (error.isEmpty() ? QString::number(code) : error));
        });
}

void MobileHistorySync::cancel() {
    const bool active = m_state == WaitingConfirm || m_state == PhonePreparing || m_state == Importing;
    m_timeout.stop();
    ++m_generation;
    if (m_download) m_download->abort();
    if (active && m_session && !m_keys.publicKeyBase64.isEmpty()) {
        m_session->cancelPullMobileMessages(m_pcName, m_keys.publicKeyBase64, [](bool, const QJsonValue&, const QString&, int) {});
    }
    cleanup();
    m_conversations.clear();
    m_ids.clear();
    if (m_state != Idle) setState(Idle, QString(), 0);
}

void MobileHistorySync::handleControl(const QJsonObject& content) {
    if (m_state != WaitingConfirm && m_state != PhonePreparing) return;
    const QString act = content.value("act").toString();
    const QJsonValue raw = content.value("data");
    const QJsonObject data = raw.isString() ? QJsonDocument::fromJson(raw.toString().toUtf8()).object() : raw.toObject();
    // Only events for this request (Zalo PC matches pc_name and public_key).
    if (data.value("public_key").toString() != m_keys.publicKeyBase64) return;
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
        if (data.value("error_code").toInt() != 0) {
            fail(L("Điện thoại báo lỗi khi sao lưu tin nhắn (mã %1).", "The phone failed to back up messages (code %1).")
                     .arg(data.value("error_code").toInt()));
        } else if (data.value("status").toInt() == 2) {
            setState(PhonePreparing, L("Hãy mở Zalo trên điện thoại và giữ màn hình sáng để tiếp tục sao lưu.",
                                       "Keep Zalo open on your phone so the backup can continue."), 15);
        } else if (data.value("status").toInt() == 1) {
            setState(PhonePreparing, L("Điện thoại đang sao lưu tin nhắn...", "The phone is backing up messages..."), 20);
        }
    } else if (act == "syncmsg_info") {
        onBackupReady(data);
    }
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

    log(QStringLiteral("backup info: format=%1 msgs=%2 file=%3 keys=%4 db_info=%5")
            .arg(m_format).arg(m_expectedMessages).arg(!m_fileName.isEmpty()).arg(shapeOf(data)).arg(shapeOf(info)));
    if (info.isEmpty()) {
        fail(L("Thông tin bản sao lưu không hợp lệ.", "The backup description is invalid."));
        return;
    }
    if (m_expectedMessages == 0) {
        setState(Done, L("Điện thoại không có tin nhắn nào để chuyển.", "The phone had no messages to transfer."), 100);
        return;
    }
    if (m_format != 1) {
        // Format 0 (older single-database backups) uses another decryptor that is not implemented.
        fail(L("Bản sao lưu dùng định dạng cũ (0) mà Zalos chưa hỗ trợ.",
               "The backup uses the old format (0), which Zalos doesn't support yet."));
        return;
    }
    if (m_url.isEmpty() || m_encryptedKey.isEmpty()) {
        fail(L("Bản sao lưu thiếu đường dẫn tải hoặc khoá.", "The backup has no download link or key."));
        return;
    }
    download();
}

void MobileHistorySync::download() {
    if (!m_session) return;
    m_workDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                    .filePath(QStringLiteral("sync/%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
    QDir().mkpath(m_workDir);
    QFile::setPermissions(m_workDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    auto file = std::make_shared<QFile>(QDir(m_workDir).filePath(QStringLiteral("backup.zdb")));
    if (!file->open(QIODevice::WriteOnly)) {
        fail(L("Không ghi được bản sao lưu vào máy.", "Couldn't save the backup on this computer."));
        return;
    }
    setState(Importing, L("Đang tải bản sao lưu từ máy chủ Zalo...", "Downloading the backup from Zalo..."), 20);
    const quint64 gen = m_generation;
    m_download = m_session->getWithCookies(QUrl(m_url));
    connect(m_download, &QNetworkReply::readyRead, this, [this, file]() {
        if (m_download) file->write(m_download->readAll());
    });
    connect(m_download, &QNetworkReply::downloadProgress, this, [this, gen](qint64 got, qint64 total) {
        if (gen != m_generation || total <= 0) return;
        setState(Importing, L("Đang tải bản sao lưu từ máy chủ Zalo...", "Downloading the backup from Zalo..."),
                 20 + int(30.0 * double(got) / double(total)));
    });
    connect(m_download, &QNetworkReply::finished, this, [this, file, gen]() {
        QNetworkReply* reply = m_download;
        if (!reply) return;
        reply->deleteLater();
        m_download = nullptr;
        if (gen != m_generation) return;
        file->write(reply->readAll());
        file->close();
        log(QStringLiteral("download http=%1 bytes=%2").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                .arg(file->size()));
        if (reply->error() != QNetworkReply::NoError) {
            // errorString() embeds the private download URL: show only the status.
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            fail(L("Tải bản sao lưu thất bại (%1).", "Downloading the backup failed (%1).")
                     .arg(http > 0 ? QStringLiteral("HTTP %1").arg(http) : QString::number(int(reply->error()))));
            return;
        }
        decryptAndRead();
    });
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
    const QString input = QDir(m_workDir).filePath(QStringLiteral("backup.zdb"));
    const QString output = QDir(m_workDir).filePath(QStringLiteral("db"));
    const quint64 gen = m_generation;
    QThread* worker = QThread::create([result, input, output, keyText]() {
        std::string err;
        if (!Crypto::DbCross::decompressAndDecryptZdb4(input.toStdString(), output.toStdString(), keyText.toStdString(),
                                                      &err)) {
            result->error = QString::fromStdString(err);
            return;
        }
        QFile::remove(input); // the encrypted copy is no longer needed
        result->conversations = readBackupDirectory(output, &result->error);
    });
    connect(worker, &QThread::finished, this, [this, worker, result, gen]() {
        worker->deleteLater();
        if (gen != m_generation) return;
        int rows = 0;
        for (const auto& c : std::as_const(result->conversations)) rows += c.rows.size();
        log(QStringLiteral("decrypt/read: conversations=%1 rows=%2 error=%3")
                .arg(result->conversations.size()).arg(rows).arg(result->error));
        if (result->conversations.isEmpty()) {
            fail(L("Không đọc được tin nhắn trong bản sao lưu: ", "Couldn't read messages from the backup: ")
                 + (result->error.isEmpty() ? L("bản sao lưu trống", "the backup is empty") : result->error));
            return;
        }
        m_conversations = result->conversations;
        cleanup(); // rows are in memory now; the decrypted databases must not stay on disk
        resolveIdsAndImport();
    });
    worker->start();
}

void MobileHistorySync::resolveIdsAndImport() {
    // Every plain id in the backup must be turned into the noised id the app uses.
    QSet<QString> users;
    QSet<QString> groups;
    for (const auto& conv : std::as_const(m_conversations)) {
        (conv.isGroup ? groups : users).insert(conv.plainId);
        if (!conv.isGroup) continue;
        for (const auto& row : conv.rows) {
            if (!row.senderId.isEmpty() && row.senderId != m_myPlainUid && isDigits(row.senderId)) users.insert(row.senderId);
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
                if (arr.size() != asked.size()) return; // cannot align safely
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
    const BackupConversation& conv = m_conversations.at(m_importIndex++);
    const QJsonArray messages = toMessages(conv, m_ids, m_myPlainUid, m_myNoisedUid, &m_skipped);
    // One batch per conversation: the receiver re-sorts/persists once per conversation.
    if (!messages.isEmpty()) emit messagesImported(messages, conv.isGroup);
    if (!messages.isEmpty()) {
        m_importedMessages += messages.size();
        m_importedConversations++;
    }
    const int progress = 70 + int(29.0 * double(m_importIndex) / double(qMax(1, int(m_conversations.size()))));
    setState(Importing, L("Đang nhập tin nhắn (%1/%2 hội thoại)...", "Importing messages (%1/%2 conversations)...")
                            .arg(m_importIndex).arg(m_conversations.size()), progress);
    const quint64 gen = m_generation;
    // Yield to the event loop between conversations so the window stays responsive.
    QTimer::singleShot(1, this, [this, gen]() {
        if (gen == m_generation) importNextChunk();
    });
}

void MobileHistorySync::cleanup() {
    // The download and the decrypted databases are private history: never leave them behind.
    if (!m_workDir.isEmpty()) {
        QDir(m_workDir).removeRecursively();
        m_workDir.clear();
    }
}

} // namespace Zalos::Core::Sync
