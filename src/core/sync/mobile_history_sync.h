#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <QTimer>
#include <QVector>

#include <optional>

#include "crypto/db_cross.h"
#include "sync/v2/sync_v2_crypto.h"
#include "sync/v2/sync_v2_frames.h"

class QNetworkReply;

namespace Zalos::Core::Network {
class ZaloSession;
}

namespace Zalos::Core::Sync {

class MobileHistorySync : public QObject {
    Q_OBJECT

public:
    using SequenceStep = std::function<void(int index, std::function<void()> next)>;
    static void runInSequence(int count, SequenceStep step);
    enum State { Idle = 0, WaitingConfirm = 1, PhonePreparing = 2, Importing = 3, Done = 4, Failed = 5 };

    struct KeyPair {
        QString publicKeyBase64;
        QByteArray privateKeyPem;
    };

    struct BackupRow {
        QString globalMsgId;
        QString cliMsgId;
        int msgType{0};
        qint64 ts{0};
        QString senderId;
        QString text;
        QByteArray binNet;
        Crypto::BinNetMessage parsedBin;
        bool hasParsedBin{false};
        qint64 ttl{0};
        std::optional<V2::ParsedSyncV2Quote> v2Quote;
        QVector<V2::ParsedSyncV2Mention> v2Mentions;
    };

    struct BackupConversation {
        QString plainId;
        bool isGroup{false};
        QVector<BackupRow> rows;
        int sourceRows{0};
    };

    explicit MobileHistorySync(Network::ZaloSession* session, QObject* parent = nullptr);
    ~MobileHistorySync() override;

    void setLanguage(const QString& lang) { m_lang = lang; }
    int state() const { return m_state; }

    void start(const QString& myNoisedUid, const QString& pcName);
    void cancel();
    void handleControl(const QJsonObject& content);

    void recordStoreDelta(int inserted, int updated, int deduped, int failed);
    void recordIngestDelta(const QHash<QString, int>& drops);

    static KeyPair generateKeyPair();
    static QByteArray rsaDecrypt(const QByteArray& privateKeyPem, const QString& cipherBase64, bool* ok);
    static QString backupKeyText(const QByteArray& plainKey, int format);
    static QVector<BackupConversation> readBackupDirectory(const QString& dir, QString* error);
    static QJsonArray toMessages(const BackupConversation& conv, const QHash<QString, QString>& ids,
                                 const QString& myPlainUid, const QString& myNoisedUid, int* skipped = nullptr,
                                 int startRow = 0, int maxRows = -1, QHash<QString, int>* reasons = nullptr);
    enum class TransferErrorKind { Ignore, PhoneIdle, PhoneActive, Failed };
    static TransferErrorKind classifyTransferError(const QJsonObject& data);
    static int downloadRetryDelayMs(int httpStatus, int networkError, int attempt, const QByteArray& retryAfter,
                                    const QDateTime& now);

signals:
    void stateChanged(int state, const QString& status, int progress);
    void messagesImported(const QJsonArray& messages, bool isGroup);

private:
    QString L(const char* vi, const char* en) const;
    void setState(int state, const QString& status, int progress);
    void log(const QString& line) const;
    void fail(const QString& reason);
    void startV2Fallback(const QString& why);
    void sendV2SessionRequest(const QString& type, int priority, const QJsonArray& queries, int ussidx);
    void sendNextV2MessageSession();
    void handleV2Control(const QString& act, const QJsonObject& data);
    void processNextV2Download();
    bool handleV2BatchBytes(int batchType, const QByteArray& bytes, QString* error);
    void maybeFinishV2();
    V2::KeyPair loadOrCreateV2IdentityKey() const;
    void onBackupReady(const QJsonObject& data);
    void download();
    void decryptAndRead();
    void resolveIdsAndImport();
    void importNextChunk();
    void cleanup();

    QPointer<Network::ZaloSession> m_session;
    QString m_lang{"vi"};
    int m_state{Idle};
    int m_lastProgress{-1};
    QString m_lastStatus;
    QString m_pcName;
    QString m_myNoisedUid;
    KeyPair m_keys;
    QTimer m_timeout;
    QPointer<QNetworkReply> m_download;
    QString m_workDir;

    QString m_myPlainUid;
    QString m_url;
    QString m_fileName;
    QString m_encryptedKey;
    QString m_checksum;
    qint64 m_expectedSize{0};
    bool m_writeFailed{false};
    bool m_sawSyncV2{false};
    QString m_mobileTempKey;
    int m_downloadAttempt{0};
    int m_format{0};
    int m_expectedMessages{0};

    QVector<BackupConversation> m_conversations;
    QHash<QString, QString> m_ids;
    int m_importIndex{0};
    int m_importRowOffset{0};
    bool m_currentConvImported{false};
    int m_importedMessages{0};
    int m_importedConversations{0};
    int m_skipped{0};
    QHash<QString, int> m_skipReasons;
    int m_storeInserted{0};
    int m_storeUpdated{0};
    int m_storeDeduped{0};
    int m_storeFailed{0};
    QHash<QString, int> m_ingestReasons;
    quint64 m_generation{0};

    enum class V2Phase { Conversation, Message };
    struct V2BatchTask {
        int batchType{0};
        QString url;
    };
    bool m_v2Active{false};
    V2Phase m_v2Phase{V2Phase::Conversation};
    QString m_v2SyncId;
    V2::KeyPair m_v2Ik;
    V2::KeyPair m_v2Ek;
    V2::SessionRecord m_v2Session;
    bool m_v2HasSession{false};
    bool m_v2SawLastBatch{false};
    bool m_v2InFlightDownload{false};
    bool m_v2WakeUpSent{false};
    int m_v2ExpectedBatches{-1};
    int m_v2ConvCursor{0};
    int m_v2UssIdx{0};
    QSet<int> m_v2SeenBatchIdxs;
    QSet<QString> m_v2SeenUrls;
    QVector<V2BatchTask> m_v2DownloadQueue;
    QVector<V2::ParsedSyncV2Conversation> m_v2PendingConvs;
    QSet<QString> m_v2PendingConvKeys;
    QVector<BackupConversation> m_v2Conversations;
};

}
