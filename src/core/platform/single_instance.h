#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QLocalServer;
class QLockFile;

namespace Zalos::Core::Platform {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString& id, const QString& runtimeDir = QString(), QObject* parent = nullptr);
    ~SingleInstance() override;

    bool start();
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

}
