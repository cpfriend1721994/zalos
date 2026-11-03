#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QLocalServer;
class QLockFile;

namespace Zalos::Core::Platform {

// One Zalos per user and data directory: two instances would share offline.db and log in twice (Zalo then drops
// one session). The first process holds a QLockFile and listens on a local socket; a second launch sends
// "activate <xdg-activation token>" to it and exits, and the first one raises its window.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    // `id` scopes the instance (data directory); `runtimeDir` holds the lock file (XDG_RUNTIME_DIR by default).
    explicit SingleInstance(const QString& id, const QString& runtimeDir = QString(), QObject* parent = nullptr);
    ~SingleInstance() override;

    // True: this process is the primary instance (lock taken, server listening). False: another instance runs
    // and was asked to come to the front (or did not answer: then the stale lock is taken over).
    bool start();
    // Drops the lock and the server, e.g. right before relaunching ourselves.
    void release();
    bool isPrimary() const { return m_primary; }

    static QString defaultId();
    QString serverName() const { return m_serverName; }

signals:
    void activationRequested(const QString& activationToken);

private:
    bool notifyPrimary();
    void listen();

    QString m_serverName;
    QString m_lockPath;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer* m_server{nullptr};
    bool m_primary{false};
};

} // namespace Zalos::Core::Platform
