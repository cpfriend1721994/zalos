#include "app_updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace Zalos::Core::Platform {

namespace {

QString appImageArchTag(const QString& cpuArch) {
    const QString a = cpuArch.trimmed().toLower();
    if (a == QLatin1String("arm64") || a == QLatin1String("aarch64")) return QStringLiteral("aarch64");
    if (a == QLatin1String("x86_64") || a == QLatin1String("amd64")) return QStringLiteral("x86_64");
    return a;
}

QString debArchTag(const QString& cpuArch) {
    const QString a = cpuArch.trimmed().toLower();
    if (a == QLatin1String("arm64") || a == QLatin1String("aarch64")) return QStringLiteral("arm64");
    if (a == QLatin1String("x86_64") || a == QLatin1String("amd64")) return QStringLiteral("amd64");
    return a;
}

bool probeInstalledDpkg() {
    if (QStandardPaths::findExecutable(QStringLiteral("dpkg-query")).isEmpty()) return false;
    QProcess p;
    p.start(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${Status}"), QStringLiteral("zalos")});
    if (!p.waitForFinished(1500)) return false;
    return p.exitCode() == 0 && QString::fromUtf8(p.readAllStandardOutput()).contains(QLatin1String("install ok installed"));
}

bool probeInstalledRpm() {
    if (QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty()) return false;
    QProcess p;
    p.start(QStringLiteral("rpm"), {QStringLiteral("-q"), QStringLiteral("zalos")});
    if (!p.waitForFinished(1500)) return false;
    return p.exitCode() == 0;
}

}

PackageKind detectPackageKind(const QString& appImageEnv,
                              const QString& exePath,
                              bool hasDpkg,
                              bool hasRpm) {
    if (!appImageEnv.trimmed().isEmpty()) return PackageKind::AppImage;
    if (exePath.startsWith(QLatin1String("/nix/store/"))) return PackageKind::Nix;
    if (hasDpkg) return PackageKind::Deb;
    if (hasRpm) return PackageKind::Rpm;
    return PackageKind::Unknown;
}

PackageKind detectRuntimePackageKind() {
    static const PackageKind cached = []() {
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        const QString exe = QCoreApplication::applicationFilePath();
        if (!appImage.trimmed().isEmpty()) return PackageKind::AppImage;
        if (exe.startsWith(QLatin1String("/nix/store/"))) return PackageKind::Nix;
        return detectPackageKind(appImage, exe, probeInstalledDpkg(), probeInstalledRpm());
    }();
    return cached;
}

QString packageKindKey(PackageKind kind) {
    switch (kind) {
    case PackageKind::AppImage: return QStringLiteral("appimage");
    case PackageKind::Deb: return QStringLiteral("deb");
    case PackageKind::Rpm: return QStringLiteral("rpm");
    case PackageKind::Nix: return QStringLiteral("nix");
    case PackageKind::Unknown:
    default: return QStringLiteral("unknown");
    }
}

SelectedAssets selectAssetsForKind(PackageKind kind,
                                   const QString& cpuArch,
                                   const QJsonArray& assets) {
    SelectedAssets out;
    const QString arch64 = appImageArchTag(cpuArch);
    const QString debArch = debArchTag(cpuArch);

    for (const QJsonValue& v : assets) {
        const QJsonObject obj = v.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        const QString url = obj.value(QStringLiteral("browser_download_url")).toString().trimmed();
        const qint64 size = obj.value(QStringLiteral("size")).toInteger();
        if (name.isEmpty() || url.isEmpty()) continue;

        if (name == QLatin1String("SHA256SUMS")) {
            out.sha256Sums = {name, url, size};
            continue;
        }

        if (out.package.url.isEmpty()) {
            if (kind == PackageKind::AppImage
                && name.endsWith(QStringLiteral("-%1.AppImage").arg(arch64), Qt::CaseSensitive)
                && !name.endsWith(QLatin1String(".zsync"))) {
                out.package = {name, url, size};
            } else if (kind == PackageKind::Deb
                       && name.endsWith(QStringLiteral("_%1.deb").arg(debArch), Qt::CaseSensitive)) {
                out.package = {name, url, size};
            } else if (kind == PackageKind::Rpm
                       && name.endsWith(QStringLiteral(".%1.rpm").arg(arch64), Qt::CaseSensitive)) {
                out.package = {name, url, size};
            }
        }
    }
    return out;
}

QString parseSha256ForFile(const QByteArray& sha256SumsContent, const QString& fileName) {
    if (fileName.isEmpty()) return QString();
    const QString text = QString::fromUtf8(sha256SumsContent);
    const QStringList lines = text.split(QLatin1Char('\n'));
    static const QRegularExpression lineRe(QStringLiteral("^([0-9a-fA-F]{64})\\s+[\\* ]?(.+)$"));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        const auto m = lineRe.match(line);
        if (!m.hasMatch()) continue;
        const QString entryName = QFileInfo(m.captured(2).trimmed()).fileName();
        if (entryName == fileName) {
            return m.captured(1).toLower();
        }
    }
    return QString();
}

bool verifyFileSha256(const QString& filePath, const QString& expectedHex) {
    if (expectedHex.size() != 64) return false;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buf;
    buf.resize(65536);
    while (!f.atEnd()) {
        const qint64 n = f.read(buf.data(), buf.size());
        if (n < 0) return false;
        if (n > 0) hash.addData(QByteArrayView(buf.constData(), n));
    }
    return QString::fromLatin1(hash.result().toHex()).compare(expectedHex, Qt::CaseInsensitive) == 0;
}

bool replaceAppImageFile(const QString& targetPath, const QString& downloadedPath, QString* errorOut) {
    const QFileInfo targetInfo(targetPath);
    const QString stagedPath = targetPath + QStringLiteral(".new");
    QFile::remove(stagedPath);

    QFile src(downloadedPath);
    if (!src.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = src.errorString();
        return false;
    }
    QFile dst(stagedPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = dst.errorString();
        return false;
    }

    QByteArray buf;
    buf.resize(65536);
    while (!src.atEnd()) {
        const qint64 n = src.read(buf.data(), buf.size());
        if (n < 0) {
            if (errorOut) *errorOut = src.errorString();
            dst.close();
            QFile::remove(stagedPath);
            return false;
        }
        if (n > 0 && dst.write(buf.constData(), n) != n) {
            if (errorOut) *errorOut = dst.errorString();
            dst.close();
            QFile::remove(stagedPath);
            return false;
        }
    }
    if (!dst.flush() || dst.error() != QFileDevice::NoError) {
        if (errorOut) *errorOut = dst.errorString();
        dst.close();
        QFile::remove(stagedPath);
        return false;
    }
    dst.close();
    src.close();

    QFileDevice::Permissions perms = targetInfo.exists()
        ? targetInfo.permissions()
        : (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
           | QFileDevice::ReadGroup | QFileDevice::ExeGroup
           | QFileDevice::ReadOther | QFileDevice::ExeOther);
    perms |= QFileDevice::ExeOwner | QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    QFile::setPermissions(stagedPath, perms);

    if (std::rename(QFile::encodeName(stagedPath).constData(),
                    QFile::encodeName(targetPath).constData()) != 0) {
        if (errorOut) *errorOut = QString::fromLocal8Bit(std::strerror(errno));
        QFile::remove(stagedPath);
        return false;
    }
    return true;
}

QString prepareUpdateDirectory(const QString& baseDir) {
    if (baseDir.isEmpty()) return {};
    const QString dir = QDir(baseDir).filePath(QStringLiteral("updates"));
    if (QFileInfo(dir).isSymLink() || !QDir().mkpath(dir)) return {};
    const QFileInfo info(dir);
    if (info.isSymLink() || !info.isDir()) return {};
    if (!QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return {};
    return info.absoluteFilePath();
}

QString updateFilePath(const QString& dir, const QString& assetName) {
    const QString name = QFileInfo(assetName.trimmed()).fileName();
    if (dir.isEmpty() || name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) return {};
    return QDir(dir).filePath(name);
}

}
