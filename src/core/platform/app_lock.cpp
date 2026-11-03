#include "app_lock.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEvent>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSettings>

namespace Zalos::Core::Platform {

namespace {

QByteArray derive(const QString& passcode, const QByteArray& salt) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, passcode.toUtf8(), salt,
                                              AppLock::kIterations, 32);
}

// Constant-time comparison (no early exit on the first differing byte).
bool sameBytes(const QByteArray& a, const QByteArray& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}

} // namespace

AppLock::AppLock(QObject* parent) : QObject(parent) {
    QSettings settings;
    m_salt = QByteArray::fromBase64(settings.value("lock/salt").toByteArray());
    m_hash = QByteArray::fromBase64(settings.value("lock/hash").toByteArray());
    if (settings.value("lock/iterations").toInt() != kIterations) m_hash.clear(); // unknown scheme: no lock
    m_autoLockMinutes = qBound(0, settings.value("lock/autoMinutes", 0).toInt(), 24 * 60);
    m_locked = hasPasscode(); // a set passcode locks on start, like tdesktop

    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this]() {
        if (hasPasscode()) lock();
    });
    m_retryTick.setInterval(1000);
    connect(&m_retryTick, &QTimer::timeout, this, [this]() {
        if (retryAfterSeconds() == 0) m_retryTick.stop();
        emit retryChanged();
    });
    if (QCoreApplication::instance()) QCoreApplication::instance()->installEventFilter(this);
    restartIdle();
}

void AppLock::setAutoLockMinutes(int minutes) {
    minutes = qBound(0, minutes, 24 * 60);
    if (minutes == m_autoLockMinutes) return;
    m_autoLockMinutes = minutes;
    QSettings().setValue("lock/autoMinutes", minutes);
    restartIdle();
    emit autoLockChanged();
}

int AppLock::retryAfterSeconds() const {
    const qint64 left = m_blockedUntil - QDateTime::currentMSecsSinceEpoch();
    return left > 0 ? int((left + 999) / 1000) : 0;
}

bool AppLock::verify(const QString& passcode) const {
    return hasPasscode() && sameBytes(derive(passcode, m_salt), m_hash);
}

bool AppLock::setPasscode(const QString& current, const QString& passcode) {
    if (passcode.size() < kMinLength) return false;
    if (hasPasscode() && !verify(current)) return false;
    QByteArray salt(16, '\0');
    QRandomGenerator::system()->generate(reinterpret_cast<quint32*>(salt.data()),
                                         reinterpret_cast<quint32*>(salt.data() + salt.size()));
    m_salt = salt;
    m_hash = derive(passcode, salt);
    QSettings settings;
    settings.setValue("lock/salt", m_salt.toBase64());
    settings.setValue("lock/hash", m_hash.toBase64());
    settings.setValue("lock/iterations", kIterations);
    restartIdle();
    emit passcodeChanged();
    return true;
}

bool AppLock::removePasscode(const QString& current) {
    if (!hasPasscode() || !verify(current)) return false;
    m_salt.clear();
    m_hash.clear();
    QSettings settings;
    settings.remove("lock/salt");
    settings.remove("lock/hash");
    settings.remove("lock/iterations");
    if (m_locked) {
        m_locked = false;
        emit lockedChanged();
    }
    m_idle.stop();
    emit passcodeChanged();
    return true;
}

void AppLock::lock() {
    if (!hasPasscode() || m_locked) return;
    m_locked = true;
    m_idle.stop();
    emit lockedChanged();
}

bool AppLock::unlock(const QString& passcode) {
    if (!m_locked) return true;
    if (retryAfterSeconds() > 0) return false;
    if (!verify(passcode)) {
        if (++m_failures >= kFreeAttempts) {
            m_blockedUntil = QDateTime::currentMSecsSinceEpoch() + kPenaltySeconds * 1000;
            m_retryTick.start();
        }
        emit retryChanged();
        return false;
    }
    m_failures = 0;
    m_blockedUntil = 0;
    m_locked = false;
    restartIdle();
    emit retryChanged();
    emit lockedChanged();
    return true;
}

void AppLock::noteActivity() {
    if (!m_locked && m_idle.isActive()) m_idle.start();
}

void AppLock::restartIdle() {
    if (hasPasscode() && m_autoLockMinutes > 0 && !m_locked) m_idle.start(m_autoLockMinutes * 60 * 1000);
    else m_idle.stop();
}

bool AppLock::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        noteActivity();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Zalos::Core::Platform
