#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

class QNetworkReply;

namespace Zalos::Core::Network {
class ZaloSession;
}

namespace Zalos::Core::Sync {

// Message history transfer from the user's phone, as Zalo PC does it (SyncMessageController V1,
// reverse-engineered from ../zalo-linux-native/pc-dist; see 01-business-domain §7):
//  1. RSA-2048 key pair; GET file:/api/message/pull_mobile_msg {pc_name, public_key (SPKI DER base64), ...}.
//  2. Socket 601 "syncmsgmb": user_confirm (phone tapped Allow/Deny), transfer_error, syncmsg_info
//     {url, file_name, encrypted_key, db_info, uid, checksum, public_key}.
//  3. Download the backup, RSA-decrypt encrypted_key (PKCS#1 v1.5), decrypt the ZDB4.0 container.
//  4. Read one SQLite file per conversation (ChatContent), map plain ids -> noised ids (profile:/api/znoise)
//     and hand TMessage-shaped objects to the normal message pipeline (display + offline cache).
// Nothing is reported as done until messages were really imported.
class MobileHistorySync : public QObject {
    Q_OBJECT

public:
    enum State { Idle = 0, WaitingConfirm = 1, PhonePreparing = 2, Importing = 3, Done = 4, Failed = 5 };

    struct KeyPair {
        QString publicKeyBase64; // SPKI DER, base64 (what Zalo PC sends)
        QByteArray privateKeyPem;
    };

    // One message row of a backup conversation (ChatContent), already decoded from BinNet.
    struct BackupRow {
        QString globalMsgId;
        QString cliMsgId;
        int msgType{0};
        qint64 ts{0};
        QString senderId;  // plain id
        QString text;      // MsgContent
        QByteArray binNet;
        qint64 ttl{0};
    };

    struct BackupConversation {
        QString plainId;
        bool isGroup{false};
        QVector<BackupRow> rows;
    };

    explicit MobileHistorySync(Network::ZaloSession* session, QObject* parent = nullptr);
    ~MobileHistorySync() override;

    void setLanguage(const QString& lang) { m_lang = lang; }
    int state() const { return m_state; }

    void start(const QString& myNoisedUid, const QString& pcName);
    void cancel();
    // Socket 601 control content with act_type "syncmsgmb".
    void handleControl(const QJsonObject& content);

    // Building blocks (public for unit tests with neutral fixtures).
    static KeyPair generateKeyPair();
    // RSA PKCS#1 v1.5 decrypt of a base64 ciphertext.
    static QByteArray rsaDecrypt(const QByteArray& privateKeyPem, const QString& cipherBase64, bool* ok);
    // The backup key as Zalo PC hands it to the decryptor: hex for format 1, base64 for format 0.
    static QString backupKeyText(const QByteArray& plainKey, int format);
    // Reads every "<id>.db" / "group_<id>.db" of an extracted backup.
    static QVector<BackupConversation> readBackupDirectory(const QString& dir, QString* error);
    // Converts one conversation to TMessage-shaped objects. `ids` maps plain ids to noised ids
    // (users and groups); conversations whose id is unknown produce nothing.
    static QJsonArray toMessages(const BackupConversation& conv, const QHash<QString, QString>& ids,
                                 const QString& myPlainUid, const QString& myNoisedUid, int* skipped = nullptr);

signals:
    void stateChanged(int state, const QString& status, int progress);
    // TMessage objects of one conversation (history: no unread bump, no notification).
    void messagesImported(const QJsonArray& messages, bool isGroup);

private:
    QString L(const char* vi, const char* en) const;
    void setState(int state, const QString& status, int progress);
    // Diagnostics in <AppData>/logs/mobile-sync.log: steps, codes, counts and JSON shapes only —
    // never keys, URLs or message content.
    void log(const QString& line) const;
    void fail(const QString& reason);
    void onBackupReady(const QJsonObject& data);
    void download();
    void decryptAndRead();
    void resolveIdsAndImport();
    void importNextChunk();
    void cleanup();

    QPointer<Network::ZaloSession> m_session;
    QString m_lang{"vi"};
    int m_state{Idle};
    QString m_pcName;
    QString m_myNoisedUid;
    KeyPair m_keys;
    QTimer m_timeout;
    QPointer<QNetworkReply> m_download;
    QString m_workDir;

    // syncmsg_info
    QString m_myPlainUid;
    QString m_url;
    QString m_fileName;
    QString m_encryptedKey;
    int m_format{0};
    int m_expectedMessages{0};

    QVector<BackupConversation> m_conversations;
    QHash<QString, QString> m_ids;
    int m_importIndex{0};
    int m_importedMessages{0};
    int m_importedConversations{0};
    int m_skipped{0};
    quint64 m_generation{0}; // invalidates async work after cancel()
};

} // namespace Zalos::Core::Sync
