#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace Zalos::Core::Platform {

// Local passcode lock (tdesktop "Lock"): covers the window until the passcode is typed, locks on start when a
// passcode is set, on Ctrl+L and after `autoLockMinutes` without keyboard/mouse input. Only the UI is locked:
// sessions, caches and offline.db on disk are not encrypted by it (the Settings text says so).
// Stored in QSettings "lock/*" as PBKDF2-HMAC-SHA256 (random salt, kIterations rounds), never the passcode.
class AppLock : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool hasPasscode READ hasPasscode NOTIFY passcodeChanged FINAL)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged FINAL)
    Q_PROPERTY(int autoLockMinutes READ autoLockMinutes WRITE setAutoLockMinutes NOTIFY autoLockChanged FINAL)
    Q_PROPERTY(int retryAfterSeconds READ retryAfterSeconds NOTIFY retryChanged FINAL)

public:
    static constexpr int kIterations = 120000;
    static constexpr int kMinLength = 4;
    static constexpr int kFreeAttempts = 5;  // then a 30 s wait between tries
    static constexpr int kPenaltySeconds = 30;

    explicit AppLock(QObject* parent = nullptr);

    bool hasPasscode() const { return !m_hash.isEmpty(); }
    bool locked() const { return m_locked; }
    int autoLockMinutes() const { return m_autoLockMinutes; }
    void setAutoLockMinutes(int minutes);
    int retryAfterSeconds() const;

    // New passcode (>= kMinLength chars); `current` must match when one is already set.
    Q_INVOKABLE bool setPasscode(const QString& current, const QString& passcode);
    Q_INVOKABLE bool removePasscode(const QString& current);
    Q_INVOKABLE void lock();
    Q_INVOKABLE bool unlock(const QString& passcode);

    // Any user input: restarts the auto-lock countdown.
    void noteActivity();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void passcodeChanged();
    void lockedChanged();
    void autoLockChanged();
    void retryChanged();

private:
    bool verify(const QString& passcode) const;
    void restartIdle();

    QByteArray m_salt;
    QByteArray m_hash;
    int m_autoLockMinutes{0};
    bool m_locked{false};
    int m_failures{0};
    qint64 m_blockedUntil{0};
    QTimer m_idle;
    QTimer m_retryTick; // 1 s countdown of retryAfterSeconds while blocked
};

} // namespace Zalos::Core::Platform
