#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <QTimer>
#include <QVector>

#include <functional>

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
    void setStoreHooks(std::function<QSet<QString>(const QStringList&)> storedIds,
                       std::function<QString(const QString&)> metaGet,
                       std::function<void(const QString&, const QString&)> metaSet) {
        m_storedIds = std::move(storedIds);
        m_metaGet = std::move(metaGet);
        m_metaSet = std::move(metaSet);
    }
    void setCheckpointStore(std::function<QHash<QString, qint64>()> load,
                            std::function<void(const QHash<QString, qint64>&)> save) {
        m_loadCheckpoints = std::move(load);
        m_saveCheckpoints = std::move(save);
    }
    int state() const { return m_state; }

    void start(const QString& myNoisedUid, const QString& pcName);
    void cancel();
    void handleControl(const QJsonObject& content);

    void recordStoreDelta(int inserted, int updated, int deduped, int failed);
    void recordIngestDelta(const QHash<QString, int>& drops);

    static KeyPair generateKeyPair();
    static QByteArray rsaDecrypt(const QByteArray& privateKeyPem, const QString& cipherBase64, bool* ok);
    static QString backupKeyText(const QByteArray& plainKey, int format);
    struct BackupDirStats {
        int files{0};
        int badName{0};
        int noRows{0};
        QMap<QString, int> badNameShapes;
    };
    static QVector<BackupConversation> readBackupDirectory(const QString& dir, QString* error,
                                                           BackupDirStats* stats = nullptr);
    static QString phoneLocalId(const QString& cliMsgId, const QString& sender, const QString& thread);
    static bool isPhoneLocalId(const QString& msgId);
    static QJsonArray toMessages(const BackupConversation& conv, const QHash<QString, QString>& ids,
                                 const QString& myPlainUid, const QString& myNoisedUid, int* skipped = nullptr,
                                 int startRow = 0, int maxRows = -1, QHash<QString, int>* reasons = nullptr,
                                 bool v2Rows = false);
    enum class TransferErrorKind { Ignore, PhoneIdle, PhoneActive, Failed };
    static TransferErrorKind classifyTransferError(const QJsonObject& data);
    static int downloadRetryDelayMs(int httpStatus, int networkError, int attempt, const QByteArray& retryAfter,
                                    const QDateTime& now);
    static int drainedQueueTimeoutMs(bool v2Active, bool fulfilled, bool startedNextSession, bool seenAnyBatch);
    static bool v2SessionFulfilled(bool sawLastBatch, int expectedBatches, int seenBatches);
    static bool spoolV2Batch(const QString& path, const QByteArray& bytes);
    static QByteArray takeV2Batch(const QString& path);
    static bool v2SessionDrained(bool queueEmpty, bool downloading, int spooled, bool parsing, bool importing);
    static QString v2PartitionOf(const V2::ParsedSyncV2Conversation& conv);
    static bool v2QueryRange(quint64 phoneLastTs, qint64 checkpoint, qint64* from, qint64* to);
    static QVector<V2::ParsedSyncV2Conversation> v2ConversationsToFetch(
        const QVector<V2::ParsedSyncV2Conversation>& convs, const QHash<QString, qint64>& checkpoints,
        const QSet<QString>& storedLastIds, int* upToDate);
    static bool v2PreferIncremental(const QString& syncedMarker, const QString& failedMarker);
    static bool v2IncrementalUnsupported(bool conversationListReceived);
    static QHash<QString, qint64> v2SessionCheckpoints(const QVector<V2::ParsedSyncV2Conversation>& convs, int begin,
                                                       int end, bool partial, const QSet<QString>& unresolved,
                                                       int storeFailures = 0);
    static int v2StatusTimeoutMs(int status, bool seenAnyBatch);
    static bool v2CanRetrySession(int retries);
    static bool v2StallEndsPartially(int conversationsDone);
    static QString v2PartialStatus(bool english, int conversationsDone, int conversationsTotal);
    static int v2MessageSessionProgress(int conversationsDone, int conversationsTotal);
    static int v2DownloadProgress(bool v2Active, int batchType, int lastProgress);
    static int nextRowsPerTick(int current, qint64 elapsedMs);
    static QStringList messageIdsOf(const QJsonArray& messages);
    static QJsonArray withoutSavedMessages(const QJsonArray& messages, const QHash<QString, QString>& savedSenders,
                                           bool sendersTrusted, int* dropped, QHash<QString, QString>* senderFixes);
    bool importingFromV2() const { return m_v2Active; }
    static int v2AckTimeoutMs();
    static bool v2BatchSeen(int indexedBatches, int batchUrls);
    void onV2RequestAcked(int err);
    void onV2RequestSent();
    static QStringList v2IdsToResolve(const QVector<BackupConversation>& convs, const QHash<QString, QString>& known,
                                      const QSet<QString>& unresolved, const QString& ownGlobalId);

signals:
    void stateChanged(int state, const QString& status, int progress);
    void messagesImported(const QJsonArray& messages, bool isGroup);

private:
    QString L(const char* vi, const char* en) const;
    void setState(int state, const QString& status, int progress);
    void log(const QString& line) const;
    void fail(const QString& reason);
    void startV2Fallback(const QString& why);
    void sendV2SessionRequest(const QString& type, int priority, const QJsonArray& queries, int ussidx,
                              bool firstOfPhase);
    void releaseV2Sessions(int reason);
    void releaseV2ConversationSession();
    void sendV2ConversationRequest();
    void importV2Batch();
    void pumpV2();
    void startV2Parse();
    QString v2WorkDir();
    void resolveV2Ids(const QStringList& ids, int attempt);
    void finishImport();
    void importRows();
    void sendNextV2MessageSession();
    void endV2OnStall(const QString& failure);
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
    QStringList m_v2OpenSyncIds;
    QString m_v2ConversationSyncId;
    QString m_v2OwnGlobalId;
    QSet<QString> m_v2UnresolvedIds;
    QSet<QString> m_importedConvKeys;
    bool m_v2Importing{false};
    int m_v2PartialSessions{0};
    int m_v2SessionRetries{0};
    QStringList m_v2Spooled;
    bool m_v2Parsing{false};
    int m_v2SpoolSeq{0};
    int m_v2SessionStart{0};
    int m_sessionTicks0{0};
    qint64 m_sessionConvert0{0};
    qint64 m_sessionSave0{0};
    int m_sessionFailed0{0};
    int m_rowsPerTick{50};
    int m_importTicks{0};
    qint64 m_importConvertMs{0};
    qint64 m_importSaveMs{0};
    bool m_importV2Rows{false};
    int m_lastProgress{0};
    V2::KeyPair m_v2Ik;
    V2::KeyPair m_v2Ek;
    V2::SessionRecord m_v2Session;
    bool m_v2HasSession{false};
    bool m_v2SawLastBatch{false};
    bool m_v2InFlightDownload{false};
    bool m_v2WakeUpSent{false};
    bool m_v2AwaitingAck{false};
    int m_v2ExpectedBatches{-1};
    int m_v2ConvCursor{0};
    int m_v2UssIdx{0};
    QSet<int> m_v2SeenBatchIdxs;
    QSet<QString> m_v2SeenUrls;
    QVector<V2BatchTask> m_v2DownloadQueue;
    QVector<V2::ParsedSyncV2Conversation> m_v2PendingConvs;
    QHash<QString, qint64> m_v2Checkpoints;
    bool m_v2SessionPartial{false};
    QString m_v2StopNote;
    std::function<QHash<QString, qint64>()> m_loadCheckpoints;
    std::function<void(const QHash<QString, qint64>&)> m_saveCheckpoints;
    std::function<QSet<QString>(const QStringList&)> m_storedIds;
    std::function<QString(const QString&)> m_metaGet;
    std::function<void(const QString&, const QString&)> m_metaSet;
    bool m_v2Incremental{false};
    QSet<QString> m_v2PendingConvKeys;
    QVector<BackupConversation> m_v2Conversations;
};

}
