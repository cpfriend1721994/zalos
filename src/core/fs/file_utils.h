#pragma once

#include <QString>
#include <QVector>
#include <QDateTime>

namespace Zalos::Core::Fs {

struct DiskUsage {
    double available{0.0};
    double free{0.0};
    double total{0.0};
};

struct DirectorySizeResult {
    QString path;
    double totalSize{0.0};
    double fileCount{0.0};
    double dirCount{0.0};
    double durationMs{0.0};
    int workers{1};
};

struct DirectoryTreeNode {
    QString relativePath;
    int depth{0};
    double totalSize{0.0};
    double fileCount{0.0};
    double dirCount{0.0};
    QVector<DirectoryTreeNode> children;
};

class FileUtils {
public:
    static DiskUsage getDiskUsage(const QString& path);

    static DirectorySizeResult getDirectorySize(const QString& path, int workers = 4);

    static DirectoryTreeNode getDirectoryTree(const QString& path, int maxDepth = -1);

    static qint64 cleanCacheDirectory(const QString& dirPath, qint64 maxAgeSeconds);
};

}
