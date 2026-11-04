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

class MobileHistorySync : public QObject {
    Q_OBJECT

public:
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
    void handleControl(const QJsonObject& content);

    static KeyPair generateKeyPair();
    static QByteArray rsaDecrypt(const QByteArray& privateKeyPem, const QString& cipherBase64, bool* ok);
    static QString backupKeyText(const QByteArray& plainKey, int format);
    static QVector<BackupConversation> readBackupDirectory(const QString& dir, QString* error);
    static QJsonArray toMessages(const BackupConversation& conv, const QHash<QString, QString>& ids,
                                 const QString& myPlainUid, const QString& myNoisedUid, int* skipped = nullptr);

signals:
    void stateChanged(int state, const QString& status, int progress);
    void messagesImported(const QJsonArray& messages, bool isGroup);

private:
    QString L(const char* vi, const char* en) const;
    void setState(int state, const QString& status, int progress);
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
    quint64 m_generation{0};
};

}
