#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <functional>

class QTimer;

namespace Zalos::Core::Network {

class MessageScheduler : public QObject {
    Q_OBJECT
public:
    using Sender = std::function<void(const QString& convId, bool isGroup, const QString& text,
                                      std::function<void(bool ok, const QString& error)> done)>;

    explicit MessageScheduler(QObject* parent = nullptr);

    void setSender(Sender sender) { m_sender = std::move(sender); }
    void setAccount(const QString& uid);
    void setReadyCheck(std::function<bool()> ready) { m_ready = std::move(ready); }
    void clear();

    bool schedule(const QString& convId, bool isGroup, const QString& text, qint64 sendAtMs, QString* error = nullptr);
    void cancel(const QString& id);
    QVariantList entriesFor(const QString& convId) const;
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
        QString state;
    };

    void save() const;

    QVector<Entry> m_entries;
    QString m_uid;
    Sender m_sender;
    QTimer* m_timer{nullptr};
    std::function<bool()> m_ready;
    int m_seq{0};
};

}
