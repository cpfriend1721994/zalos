#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <functional>

class QTimer;

namespace Zalos::Core::Network {

// Scheduled sends (local: this app delivers them while it is running and signed in). Owns the queue, its
// per-account persistence (QSettings "scheduler/<uid>") and the 15 s check; the actual send is injected so it
// knows nothing about the session or the UI. Split out of ZaloClient (2026-11-03).
class MessageScheduler : public QObject {
    Q_OBJECT
public:
    // Sends `text` to `convId`; must call `done(ok, friendlyError)` exactly once.
    using Sender = std::function<void(const QString& convId, bool isGroup, const QString& text,
                                      std::function<void(bool ok, const QString& error)> done)>;

    explicit MessageScheduler(QObject* parent = nullptr);

    void setSender(Sender sender) { m_sender = std::move(sender); }
    // Loads the queue of `uid` (empty: nothing is stored or loaded).
    void setAccount(const QString& uid);
    // Whether due messages may be sent now (signed in); checked on every run.
    void setReadyCheck(std::function<bool()> ready) { m_ready = std::move(ready); }
    void clear();

    // False when the text is empty or `sendAtMs` is not at least 30 s ahead (`error` says which).
    bool schedule(const QString& convId, bool isGroup, const QString& text, qint64 sendAtMs, QString* error = nullptr);
    void cancel(const QString& id);
    // Pending/sending/failed entries of `convId`: [{id, text, sendAt, state}].
    QVariantList entriesFor(const QString& convId) const;
    // Sends every pending entry whose time has come (called by the timer; public for tests).
    void runDue(qint64 nowMs);

signals:
    void changed();
    void sendFailed(const QString& error);

private:
    struct Entry {
        QString id;
        QString convId;
        bool isGroup{false};
        QString text;
        qint64 sendAt{0};
        QString state; // "pending", "sending", "sent", "failed", "cancelled"
    };

    void save() const;

    QVector<Entry> m_entries;
    QString m_uid;
    Sender m_sender;
    QTimer* m_timer{nullptr};
    std::function<bool()> m_ready;
    int m_seq{0};
};

} // namespace Zalos::Core::Network
