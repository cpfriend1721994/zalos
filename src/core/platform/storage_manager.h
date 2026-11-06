#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>

namespace Zalos::Core::Platform {

class StorageManager : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int avatarLimitMb READ avatarLimitMb WRITE setAvatarLimitMb NOTIFY settingsChanged FINAL)
    Q_PROPERTY(int maxAgeDays READ maxAgeDays WRITE setMaxAgeDays NOTIFY settingsChanged FINAL)

public:
    explicit StorageManager(QObject* parent = nullptr);

    int avatarLimitMb() const { return m_avatarLimitMb; }
    void setAvatarLimitMb(int mb);
    int maxAgeDays() const { return m_maxAgeDays; }
    void setMaxAgeDays(int days);

    Q_INVOKABLE QVariantList usage() const;
    Q_INVOKABLE qint64 clear(const QString& key);
    Q_INVOKABLE void openDownloadsFolder() const;
    Q_INVOKABLE qint64 prune();

    void setAvatarMemoryCleaner(std::function<void()> cleaner) { m_avatarMemoryCleaner = std::move(cleaner); }
    void cleanAvatarMemory() const { if (m_avatarMemoryCleaner) m_avatarMemoryCleaner(); }

    static QString directory(const QString& key);
    static qint64 pruneDirectory(const QString& dir, qint64 limitBytes, qint64 maxAgeMs);

signals:
    void settingsChanged();
    void usageChanged();

private:
    int m_avatarLimitMb{300};
    int m_maxAgeDays{31};
    QTimer m_daily;
    std::function<void()> m_avatarMemoryCleaner;
};

}
