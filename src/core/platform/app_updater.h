#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QString>

namespace Zalos::Core::Platform {

enum class PackageKind {
    Unknown,
    AppImage,
    Deb,
    Rpm,
    Nix
};

struct ReleaseAsset {
    QString name;
    QString url;
    qint64 size{0};
};

struct SelectedAssets {
    ReleaseAsset package;
    ReleaseAsset sha256Sums;
};

PackageKind detectPackageKind(const QString& appImageEnv,
                              const QString& exePath,
                              bool hasDpkg,
                              bool hasRpm);
PackageKind detectRuntimePackageKind();
QString packageKindKey(PackageKind kind);

SelectedAssets selectAssetsForKind(PackageKind kind,
                                   const QString& cpuArch,
                                   const QJsonArray& assets);

QString parseSha256ForFile(const QByteArray& sha256SumsContent, const QString& fileName);
bool verifyFileSha256(const QString& filePath, const QString& expectedHex);
bool replaceAppImageFile(const QString& targetPath, const QString& downloadedPath, QString* errorOut = nullptr);
QString prepareUpdateDirectory(const QString& baseDir);
QString updateFilePath(const QString& dir, const QString& assetName);

}
