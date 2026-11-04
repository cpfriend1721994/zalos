#include "single_instance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>

#include <unistd.h>

namespace Zalos::Core::Platform {

namespace {

constexpr int kConnectTimeoutMs = 1500;

}

SingleInstance::SingleInstance(const QString& id, const QString& runtimeDir, QObject* parent) : QObject(parent) {
    const QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    m_serverName = QStringLiteral("zalos-%1-%2").arg(getuid()).arg(QString::fromLatin1(hash));
    QString dir = runtimeDir;
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    m_lockPath = dir + QLatin1Char('/') + m_serverName + QStringLiteral(".lock");
}

SingleInstance::~SingleInstance() { release(); }

QString SingleInstance::defaultId() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool SingleInstance::start() {
    m_lock = std::make_unique<QLockFile>(m_lockPath);
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        if (notifyPrimary()) {
            m_lock.reset();
            return false;
        }
        m_lock->removeStaleLockFile();
        if (!m_lock->tryLock(0)) {
            m_lock.reset();
            return false;
        }
    }
    m_primary = true;
    listen();
    return true;
}

void SingleInstance::release() {
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    if (m_lock) {
        m_lock->unlock();
        m_lock.reset();
    }
    m_primary = false;
}

bool SingleInstance::notifyPrimary() {
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(kConnectTimeoutMs)) return false;
    const QByteArray token = qgetenv("XDG_ACTIVATION_TOKEN");
    socket.write("activate " + token + '\n');
    socket.flush();
    socket.waitForBytesWritten(kConnectTimeoutMs);
    socket.waitForReadyRead(kConnectTimeoutMs);
    socket.disconnectFromServer();
    return true;
}

void SingleInstance::listen() {
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_serverName)) {
        QLocalServer::removeServer(m_serverName);
        m_server->listen(m_serverName);
    }
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* client = m_server->nextPendingConnection()) {
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            connect(client, &QLocalSocket::readyRead, client, [this, client]() {
                if (!client->canReadLine()) return;
                const QByteArray line = client->readLine().trimmed();
                if (line == "activate" || line.startsWith("activate ")) {
                    emit activationRequested(QString::fromUtf8(line.mid(9)));
                }
                client->write("ok\n");
                client->flush();
            });
        }
    });
}

}
