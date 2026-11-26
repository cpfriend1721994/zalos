#include "file_utils.h"

#include <QDesktopServices>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace Zalos::Core::Fs {

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
    if (out.contains(QStringLiteral("ZALOS_OWN_SCALE_FACTOR"))) {
        out.remove(QStringLiteral("QT_SCALE_FACTOR"));
        out.remove(QStringLiteral("ZALOS_OWN_SCALE_FACTOR"));
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
