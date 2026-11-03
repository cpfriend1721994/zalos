#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>

namespace Zalos::Core::Platform {

// Local storage usage and cleanup (tdesktop "Manage local storage"). Categories:
//   avatars  — avatar disk cache; pruned to `avatarLimitMb` and `maxAgeDays` (oldest mtime first; a cache hit
//              touches the file), on start and daily
//   voice    — recorded voice takes (sent ones are kept on the server); older than 1 day are removed
//   sounds   — synthesised notification sounds (regenerated on demand)
//   qmlcache — Qt's compiled QML cache (rebuilt on demand)
//   downloads — files saved from chats (~/Downloads/Zalos): size only, never deleted by Zalos
// Offline message history (offline.db) is not a cache here: it is the account's offline copy.
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

    // [{key, bytes, files, clearable, path}] — computed on call (a directory walk).
    Q_INVOKABLE QVariantList usage() const;
    // Deletes a clearable category; returns the bytes freed.
    Q_INVOKABLE qint64 clear(const QString& key);
    Q_INVOKABLE void openDownloadsFolder() const;
    // Applies the limits now; returns the bytes freed.
    Q_INVOKABLE qint64 prune();

    // Drops the in-memory avatar images too when the avatar category is cleared (set by main.cpp; the provider
    // lives in the QML layer, which the core does not link).
    void setAvatarMemoryCleaner(std::function<void()> cleaner) { m_avatarMemoryCleaner = std::move(cleaner); }

    static QString directory(const QString& key);
    // Removes files of `dir` older than `maxAgeMs`, then the oldest until the total is <= `limitBytes`.
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

} // namespace Zalos::Core::Platform
