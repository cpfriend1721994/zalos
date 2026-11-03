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

} // namespace

MessageScheduler::MessageScheduler(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(kCheckIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this]() { runDue(QDateTime::currentMSecsSinceEpoch()); });
    m_timer->start();
}

void MessageScheduler::setAccount(const QString& uid) {
    m_uid = uid;
    m_entries.clear();
    if (!m_uid.isEmpty()) {
        QSettings settings("Zalos", "ZalosApp");
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
    emit changed();
}

void MessageScheduler::clear() {
    m_entries.clear();
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
    emit changed();
    return true;
}

void MessageScheduler::cancel(const QString& id) {
    for (auto& e : m_entries) {
        if (e.id == id && (e.state == "pending" || e.state == "failed")) {
            e.state = QStringLiteral("cancelled");
            save();
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
    bool started = false;
    for (auto& e : m_entries) {
        if (e.state != "pending" || e.sendAt > nowMs) continue;
        started = true;
        e.state = QStringLiteral("sending");
        const QString id = e.id;
        // The sender may answer later; `this` outlives it only while the client lives, so guard by pointer.
        QPointer<MessageScheduler> self(this);
        m_sender(e.convId, e.isGroup, e.text, [self, id](bool ok, const QString& error) {
            if (!self) return;
            for (auto& item : self->m_entries) {
                if (item.id != id) continue;
                item.state = ok ? QStringLiteral("sent") : QStringLiteral("failed");
                if (!ok) emit self->sendFailed(error);
            }
            self->save();
            emit self->changed();
        });
    }
    if (started) emit changed();
}

void MessageScheduler::save() const {
    if (m_uid.isEmpty()) return; // stored per account
    QJsonArray arr;
    for (const auto& e : m_entries) {
        if (e.state == "sent" || e.state == "cancelled") continue;
        // "sending" is persisted as pending so a crash mid-send retries instead of losing it.
        arr.append(QJsonObject{{"id", e.id}, {"convId", e.convId}, {"isGroup", e.isGroup}, {"text", e.text},
                               {"sendAt", e.sendAt}, {"state", e.state == "sending" ? "pending" : e.state}});
    }
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue(settingsKey(m_uid), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

} // namespace Zalos::Core::Network
