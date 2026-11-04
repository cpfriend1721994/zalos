#pragma once

#include <QtQml/qqmlregistration.h>

#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace Zalos::Core::Platform {

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
    static constexpr int kFreeAttempts = 5;
    static constexpr int kPenaltySeconds = 30;

    explicit AppLock(QObject* parent = nullptr);

    bool hasPasscode() const { return !m_hash.isEmpty(); }
    bool locked() const { return m_locked; }
    int autoLockMinutes() const { return m_autoLockMinutes; }
    void setAutoLockMinutes(int minutes);
    int retryAfterSeconds() const;

    Q_INVOKABLE bool setPasscode(const QString& current, const QString& passcode);
    Q_INVOKABLE bool removePasscode(const QString& current);
    Q_INVOKABLE void lock();
    Q_INVOKABLE bool unlock(const QString& passcode);
    Q_INVOKABLE bool verifyPasscode(const QString& passcode) const { return verify(passcode); }

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
    QTimer m_retryTick;
};

}
