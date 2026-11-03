#include "file_utils.h"

#include <sys/statvfs.h>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QElapsedTimer>

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

    QDirIterator it(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
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
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);

    for (const auto& entry : entries) {
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
    QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

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

} // namespace Zalos::Core::Fs

