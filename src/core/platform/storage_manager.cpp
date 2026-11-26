#include "storage_manager.h"
#include "fs/file_utils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace Zalos::Core::Platform {

namespace {

constexpr qint64 kMb = 1024 * 1024;
constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;
const QStringList kKeys = {"avatars", "voice", "sounds", "qmlcache", "downloads"};

struct DirStats {
    qint64 bytes{0};
    int files{0};
};

DirStats stats(const QString& dir) {
    DirStats s;
    if (dir.isEmpty()) return s;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        s.bytes += it.fileInfo().size();
        ++s.files;
    }
    return s;
}

QVariantList collectUsage() {
    QVariantList out;
    for (const QString& key : kKeys) {
        const QString dir = StorageManager::directory(key);
        const DirStats s = stats(dir);
        out.append(QVariantMap{{"key", key}, {"bytes", s.bytes}, {"files", s.files},
                               {"clearable", key != "downloads"}, {"path", dir}});
    }
    return out;
}

}

StorageManager::StorageManager(QObject* parent) : QObject(parent) {
    QSettings settings;
    m_avatarLimitMb = qBound(50, settings.value("storage/avatarLimitMb", 300).toInt(), 10240);
    m_maxAgeDays = qBound(1, settings.value("storage/maxAgeDays", 31).toInt(), 3650);
    QTimer::singleShot(30000, this, [this]() { pruneAsync(); });
    m_daily.setInterval(int(kDayMs));
    connect(&m_daily, &QTimer::timeout, this, [this]() { pruneAsync(); });
    m_daily.start();
}

void StorageManager::setAvatarLimitMb(int mb) {
    mb = qBound(50, mb, 10240);
    if (mb == m_avatarLimitMb) return;
    m_avatarLimitMb = mb;
    QSettings().setValue("storage/avatarLimitMb", mb);
    emit settingsChanged();
    pruneAsync();
}

void StorageManager::setMaxAgeDays(int days) {
    days = qBound(1, days, 3650);
    if (days == m_maxAgeDays) return;
    m_maxAgeDays = days;
    QSettings().setValue("storage/maxAgeDays", days);
    emit settingsChanged();
    pruneAsync();
}

QString StorageManager::directory(const QString& key) {
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (key == "avatars") return cache + "/avatars";
    if (key == "voice") return cache + "/voice";
    if (key == "sounds") return cache + "/sounds";
    if (key == "qmlcache") return cache + "/qmlcache";
    if (key == "downloads") return QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath("Zalos");
    return {};
}

QVariantList StorageManager::usage() const {
    return collectUsage();
}

void StorageManager::refreshUsageAsync() {
    QPointer<StorageManager> self(this);
    QThreadPool::globalInstance()->start([self]() {
        const QVariantList stats = collectUsage();
        QObject* app = QCoreApplication::instance();
        if (app && self) {
            QMetaObject::invokeMethod(app, [self, stats]() {
                if (self) emit self->usageReady(stats);
            }, Qt::QueuedConnection);
        }
    });
}

void StorageManager::clear(const QString& key) {
    if (key == "downloads" || !kKeys.contains(key)) return;
    const bool clearAvatars = key == "avatars";
    QPointer<StorageManager> self(this);
    QThreadPool::globalInstance()->start([self, key, clearAvatars]() {
        const QString dir = directory(key);
        QDir d(dir);
        for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
            if (fi.isDir()) QDir(fi.absoluteFilePath()).removeRecursively();
            else QFile::remove(fi.absoluteFilePath());
        }
        QObject* app = QCoreApplication::instance();
        if (app && self) {
            QMetaObject::invokeMethod(app, [self, clearAvatars]() {
                if (!self) return;
                if (clearAvatars && self->m_avatarMemoryCleaner) self->m_avatarMemoryCleaner();
                emit self->usageChanged();
            }, Qt::QueuedConnection);
        }
    });
}

bool StorageManager::openDownloadsFolder() const {
    const QString dir = directory("downloads");
    QDir().mkpath(dir);
    return Fs::FileUtils::openLocalPath(dir);
}

qint64 StorageManager::pruneDirectory(const QString& dir, qint64 limitBytes, qint64 maxAgeMs) {
    if (dir.isEmpty()) return 0;
    struct Entry {
        QString path;
        qint64 size;
        qint64 mtime;
    };
    QList<Entry> entries;
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        entries.append({fi.absoluteFilePath(), fi.size(), fi.lastModified().toMSecsSinceEpoch()});
        total += fi.size();
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 freed = 0;
    for (const Entry& e : std::as_const(entries)) {
        const bool tooOld = maxAgeMs > 0 && now - e.mtime > maxAgeMs;
        const bool overLimit = limitBytes >= 0 && total > limitBytes;
        if (!tooOld && !overLimit) break;
        if (QFile::remove(e.path)) {
            total -= e.size;
            freed += e.size;
        }
    }
    return freed;
}

qint64 StorageManager::prune() {
    cleanTemporaryFiles();
    qint64 freed = pruneDirectory(directory("avatars"), qint64(m_avatarLimitMb) * kMb, qint64(m_maxAgeDays) * kDayMs);
    freed += pruneDirectory(directory("voice"), -1, kDayMs);
    if (freed > 0) emit usageChanged();
    return freed;
}

void StorageManager::pruneAsync() {
    const int limitMb = m_avatarLimitMb;
    const int maxAgeDays = m_maxAgeDays;
    QPointer<StorageManager> self(this);
    QThreadPool::globalInstance()->start([self, limitMb, maxAgeDays]() {
        cleanTemporaryFiles();
        qint64 freed = pruneDirectory(directory("avatars"), qint64(limitMb) * kMb, qint64(maxAgeDays) * kDayMs);
        freed += pruneDirectory(directory("voice"), -1, kDayMs);
        QObject* app = QCoreApplication::instance();
        if (freed > 0 && app && self) {
            QMetaObject::invokeMethod(app, [self]() {
                if (self) emit self->usageChanged();
            }, Qt::QueuedConnection);
        }
    });
}

void StorageManager::cleanTemporaryFiles() {
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) return;
    QDir dir(tempDir);
    const QStringList filters = {"zalos_paste_*", "zalos_annotated_*", "zalos_print_*"};
    const QFileInfoList list = dir.entryInfoList(filters, QDir::Files | QDir::NoSymLinks);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kMaxAgeMs = 15 * 60 * 1000;
    for (const QFileInfo& fi : list) {
        if (now - fi.lastModified().toMSecsSinceEpoch() > kMaxAgeMs) {
            QFile::remove(fi.absoluteFilePath());
        }
    }
    const QFileInfoList dirs = dir.entryInfoList({QStringLiteral("zalos_fwd_*")},
                                                 QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo& fi : dirs) {
        if (now - fi.lastModified().toMSecsSinceEpoch() > kMaxAgeMs) {
            QDir(fi.absoluteFilePath()).removeRecursively();
        }
    }
}

}
