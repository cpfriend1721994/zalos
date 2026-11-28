#include "message_scheduler.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

namespace Zalos::Core::Network {

namespace {

constexpr int kCheckIntervalMs = 15000;
constexpr qint64 kMinLeadMs = 30000;

QString settingsKey(const QString& uid) { return QStringLiteral("scheduler/") + uid; }

}

MessageScheduler::MessageScheduler(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(kCheckIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this]() { runDue(QDateTime::currentMSecsSinceEpoch()); });
}

void MessageScheduler::updateTimer() {
    bool hasPending = false;
    for (const auto& e : m_entries) {
        if (e.state == QLatin1String("pending")) {
            hasPending = true;
            break;
        }
    }
    if (hasPending) {
        if (!m_timer->isActive()) m_timer->start();
    } else if (m_timer->isActive()) {
        m_timer->stop();
    }
}

void MessageScheduler::setAccount(const QString& uid) {
    m_uid = uid;
    m_entries.clear();
    if (!m_uid.isEmpty()) {
        QSettings settings;
        const QJsonArray arr = QJsonDocument::fromJson(settings.value(settingsKey(m_uid)).toString().toUtf8()).array();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            Entry e;
            e.id = o.value("id").toString();
            e.convId = o.value("convId").toString();
            e.isGroup = o.value("isGroup").toBool();
            e.text = o.value("text").toString();
            e.sendAt = o.value("sendAt").toVariant().toLongLong();
            e.state = o.value("state").toString("pending");
            if (!e.id.isEmpty() && !e.convId.isEmpty() && !e.text.isEmpty()) m_entries.append(e);
        }
    }
    updateTimer();
    emit changed();
}

void MessageScheduler::clear() {
    m_entries.clear();
    updateTimer();
    emit changed();
}

bool MessageScheduler::schedule(const QString& convId, bool isGroup, const QString& text, qint64 sendAtMs,
                                QString* error) {
    const QString clean = text.trimmed();
    if (clean.isEmpty() || convId.isEmpty()) {
        if (error) error->clear();
        return false;
    }
    if (sendAtMs <= QDateTime::currentMSecsSinceEpoch() + kMinLeadMs) {
        if (error) *error = QStringLiteral("past");
        return false;
    }
    Entry e;
    e.id = QStringLiteral("sched-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(++m_seq);
    e.convId = convId;
    e.isGroup = isGroup;
    e.text = clean;
    e.sendAt = sendAtMs;
    e.state = QStringLiteral("pending");
    m_entries.append(e);
    save();
    updateTimer();
    emit changed();
    return true;
}

void MessageScheduler::cancel(const QString& id) {
    for (auto& e : m_entries) {
        if (e.id == id && (e.state == "pending" || e.state == "failed")) {
            e.state = QStringLiteral("cancelled");
            save();
            updateTimer();
            emit changed();
            return;
        }
    }
}

QVariantList MessageScheduler::entriesFor(const QString& convId) const {
    QVariantList out;
    for (const auto& e : m_entries) {
        if (e.convId != convId || e.state == "sent" || e.state == "cancelled") continue;
        out.append(QVariantMap{{"id", e.id}, {"text", e.text}, {"sendAt", e.sendAt}, {"state", e.state}});
    }
    return out;
}

void MessageScheduler::runDue(qint64 nowMs) {
    if (!m_sender || (m_ready && !m_ready())) return;
    struct DueItem {
        QString id;
        QString convId;
        bool isGroup{false};
        QString text;
    };
    QVector<DueItem> due;
    for (auto& e : m_entries) {
        if (e.state != "pending" || e.sendAt > nowMs) continue;
        e.state = QStringLiteral("sending");
        due.append({e.id, e.convId, e.isGroup, e.text});
    }
    if (due.isEmpty()) {
        updateTimer();
        return;
    }
    QPointer<MessageScheduler> self(this);
    for (const auto& item : due) {
        if (!self) return;
        const QString id = item.id;
        m_sender(item.convId, item.isGroup, item.text, [self, id](bool ok, const QString& error) {
            if (!self) return;
            for (auto& entry : self->m_entries) {
                if (entry.id != id) continue;
                entry.state = ok ? QStringLiteral("sent") : QStringLiteral("failed");
                if (!ok) emit self->sendFailed(error);
            }
            self->save();
            self->updateTimer();
            emit self->changed();
        });
    }
    if (!self) return;
    updateTimer();
    emit changed();
}

void MessageScheduler::save() const {
    if (m_uid.isEmpty()) return;
    QJsonArray arr;
    for (const auto& e : m_entries) {
        if (e.state == "sent" || e.state == "cancelled") continue;
        arr.append(QJsonObject{{"id", e.id}, {"convId", e.convId}, {"isGroup", e.isGroup}, {"text", e.text},
                               {"sendAt", e.sendAt}, {"state", e.state == "sending" ? "pending" : e.state}});
    }
    QSettings settings;
    settings.setValue(settingsKey(m_uid), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

}
