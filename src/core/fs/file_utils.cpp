#include "file_utils.h"

#include <sys/statvfs.h>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace Zalos::Core::Fs {

DiskUsage FileUtils::getDiskUsage(const QString& path) {
    DiskUsage usage;
    struct statvfs stat;
    QByteArray cPath = QFile::encodeName(path.isEmpty() ? "/" : path);

    if (statvfs(cPath.constData(), &stat) == 0) {
        double factor = (stat.f_frsize > 0) ? static_cast<double>(stat.f_frsize) : static_cast<double>(stat.f_bsize);
        usage.available = static_cast<double>(stat.f_bavail) * factor;
        usage.free = static_cast<double>(stat.f_bfree) * factor;
        usage.total = static_cast<double>(stat.f_blocks) * factor;
    }
    return usage;
}

DirectorySizeResult FileUtils::getDirectorySize(const QString& path, int workers) {
    DirectorySizeResult result;
    result.path = path;
    result.workers = (workers > 0) ? workers : 1;

    QElapsedTimer timer;
    timer.start();

    QDir dir(path);
    if (!dir.exists()) {
        result.durationMs = timer.elapsed();
        return result;
    }

    QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo info = it.fileInfo();
        if (info.isDir()) {
            result.dirCount += 1.0;
        } else {
            result.fileCount += 1.0;
            result.totalSize += static_cast<double>(info.size());
        }
    }

    result.durationMs = static_cast<double>(timer.elapsed());
    return result;
}

static DirectoryTreeNode scanNode(const QString& rootPath, const QString& currentPath, int depth, int maxDepth) {
    DirectoryTreeNode node;
    node.depth = depth;
    node.relativePath = QDir(rootPath).relativeFilePath(currentPath);
    if (node.relativePath.isEmpty()) {
        node.relativePath = ".";
    }

    QDir dir(currentPath);
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks, QDir::Name);

    for (const auto& entry : entries) {
        if (entry.isSymLink()) continue;
        if (entry.isDir()) {
            node.dirCount += 1.0;
            if (maxDepth < 0 || depth < maxDepth) {
                DirectoryTreeNode child = scanNode(rootPath, entry.absoluteFilePath(), depth + 1, maxDepth);
                node.totalSize += child.totalSize;
                node.fileCount += child.fileCount;
                node.dirCount += child.dirCount;
                node.children.append(child);
            }
        } else {
            node.fileCount += 1.0;
            node.totalSize += static_cast<double>(entry.size());
        }
    }
    return node;
}

DirectoryTreeNode FileUtils::getDirectoryTree(const QString& path, int maxDepth) {
    QDir dir(path);
    if (!dir.exists()) {
        DirectoryTreeNode emptyNode;
        emptyNode.relativePath = path;
        return emptyNode;
    }
    return scanNode(path, path, 0, maxDepth);
}

qint64 FileUtils::cleanCacheDirectory(const QString& dirPath, qint64 maxAgeSeconds) {
    qint64 removedBytes = 0;
    QDir dir(dirPath);
    if (!dir.exists()) return 0;

    QDateTime cutoff = QDateTime::currentDateTime().addSecs(-maxAgeSeconds);
    QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        it.next();
        QFileInfo info = it.fileInfo();
        if (info.lastModified() < cutoff) {
            qint64 sz = info.size();
            if (QFile::remove(info.absoluteFilePath())) {
                removedBytes += sz;
            }
        }
    }
    return removedBytes;
}

bool FileUtils::sandboxed() {
    return qEnvironmentVariableIsSet("FLATPAK_ID") || qEnvironmentVariableIsSet("SNAP");
}

QProcessEnvironment FileUtils::launchEnvironment(const QProcessEnvironment& env) {
    QProcessEnvironment out = env;
    const QString appDir = env.value(QStringLiteral("APPDIR"));
    const auto isPrivate = [&appDir](const QString& entry) {
        if (entry.isEmpty()) return true;
        if (!appDir.isEmpty() && (entry == appDir || entry.startsWith(appDir + QLatin1Char('/')))) return true;
        return entry.contains(QLatin1String("/qt6-fallback")) || entry.contains(QLatin1String("/zalos/qt6"))
               || entry.contains(QLatin1String("/zalos/fallback")) || entry.contains(QLatin1String("/xkb-fallback"))
               || entry.startsWith(QLatin1String("/opt/qt6"));
    };
    const auto filterList = [&](const QString& key) {
        if (!out.contains(key)) return;
        QStringList kept;
        for (const QString& entry : out.value(key).split(QLatin1Char(':'))) {
            if (!isPrivate(entry)) kept << entry;
        }
        if (kept.isEmpty()) out.remove(key);
        else out.insert(key, kept.join(QLatin1Char(':')));
    };
    filterList(QStringLiteral("LD_LIBRARY_PATH"));
    filterList(QStringLiteral("PATH"));
    filterList(QStringLiteral("XDG_DATA_DIRS"));
    for (const char* key : {"QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "ZALOS_BUNDLED_QT",
                            "GLIBC_TUNABLES", "QT_QPA_PLATFORMTHEME", "APPDIR", "APPIMAGE", "ARGV0", "OWD"}) {
        out.remove(QString::fromLatin1(key));
    }
    if (!out.contains(QStringLiteral("PATH"))) out.insert(QStringLiteral("PATH"), QStringLiteral("/usr/local/bin:/usr/bin:/bin"));
    return out;
}

QString FileUtils::urlLauncher() {
    const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
    if (!xdgOpen.isEmpty()) return QLatin1Char('"') + xdgOpen + QStringLiteral("\" \"$1\"");
    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (!gio.isEmpty()) return QLatin1Char('"') + gio + QStringLiteral("\" open \"$1\"");
    return QString();
}

bool FileUtils::openUrlDetached(const QString& url, const QString& failureFile) {
    if (url.isEmpty()) return false;
    const QString launcher = urlLauncher();
    const QString sh = QStandardPaths::findExecutable(QStringLiteral("sh"));
    if (launcher.isEmpty() || sh.isEmpty()) return false;
    QString script = launcher;
    if (!failureFile.isEmpty()) script += QStringLiteral(" || echo $? > \"$2\"");
    QProcess proc;
    proc.setProgram(sh);
    proc.setArguments({QStringLiteral("-c"), script, QStringLiteral("zalos-open"), url, failureFile});
    proc.setProcessEnvironment(launchEnvironment(QProcessEnvironment::systemEnvironment()));
    proc.setStandardInputFile(QProcess::nullDevice());
    proc.setStandardOutputFile(QProcess::nullDevice());
    proc.setStandardErrorFile(QProcess::nullDevice());
    return proc.startDetached();
}

bool FileUtils::openLocalPath(const QString& path) {
    if (path.isEmpty()) return false;
    if (!sandboxed()) {
        const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
        if (!xdgOpen.isEmpty()) {
            QProcess proc;
            proc.setProgram(xdgOpen);
            proc.setArguments({path});
            proc.setProcessEnvironment(launchEnvironment(QProcessEnvironment::systemEnvironment()));
            proc.setStandardInputFile(QProcess::nullDevice());
            proc.setStandardOutputFile(QProcess::nullDevice());
            proc.setStandardErrorFile(QProcess::nullDevice());
            if (proc.startDetached()) return true;
        }
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

}
