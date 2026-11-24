#include "dependency_report.h"

#include "call/call_bridge.h"
#include "call/h264_codec.h"
#include "multimedia_support.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLibraryInfo>
#include <QMediaFormat>
#include <QProcess>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QSslSocket>

#include <cstdio>
#ifdef __linux__
#include <dlfcn.h>
#endif

namespace Zalos::Core::Platform::DependencyReport {

namespace {

bool pluginPresent(const QStringList& files) {
    QStringList roots = QCoreApplication::libraryPaths();
    roots << QLibraryInfo::path(QLibraryInfo::PluginsPath);
    for (const QString& root : std::as_const(roots)) {
        for (const QString& file : files) {
            if (QFileInfo::exists(QDir(root).filePath(file))) return true;
        }
    }
    return false;
}

QStringList missingQmlModules() {
    QQmlEngine engine;
    const QStringList required{"QtQuick/Controls", "QtQuick/Layouts", "QtQuick/Templates", "QtQuick/Window",
                               "QtQuick/Shapes", "QtMultimedia", "QtCore"};
    QStringList missing;
    for (const QString& module : required) {
        bool found = false;
        for (const QString& path : engine.importPathList()) {
            if (QFileInfo::exists(QDir(path).filePath(module + QStringLiteral("/qmldir")))) {
                found = true;
                break;
            }
        }
        if (!found) missing << module;
    }
    return missing;
}

QString yesNo(bool v) { return v ? QStringLiteral("yes") : QStringLiteral("missing"); }

}

QList<Line> collect() {
    QList<Line> out;
    const bool bundled = qEnvironmentVariableIntValue("ZALOS_BUNDLED_QT") == 1;
    out.append({"qt", QStringLiteral("%1 (%2)").arg(QString::fromLatin1(qVersion()), bundled ? "bundled" : "system")});

    const bool xcb = pluginPresent({"platforms/libqxcb.so"});
    const bool wayland = pluginPresent({"platforms/libqwayland-generic.so", "platforms/libqwayland.so"});
    out.append({"platform", QStringLiteral("xcb=%1 wayland=%2").arg(yesNo(xcb), yesNo(wayland)), true, xcb || wayland});

    const QStringList qml = missingQmlModules();
    out.append({"qml", qml.isEmpty() ? QStringLiteral("ok") : QStringLiteral("missing ") + qml.join(','), true, qml.isEmpty()});

    const bool ssl = QSslSocket::supportsSsl();
    out.append({"openssl", ssl ? QSslSocket::sslLibraryVersionString() : QStringLiteral("missing"), true, ssl});

    const bool sqlite = QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"));
    out.append({"sqlite", yesNo(sqlite), true, sqlite});

    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    out.append({"images", QStringLiteral("webp=%1 svg=%2 gif=%3")
                              .arg(yesNo(formats.contains("webp")), yesNo(formats.contains("svg")),
                                   yesNo(formats.contains("gif")))});

    if (!MultimediaSupport::available()) {
        out.append({"media-decode", QStringLiteral("missing (") + MultimediaSupport::unavailableReason() + ")"});
    } else {
    QMediaFormat media;
    const auto audio = media.supportedAudioCodecs(QMediaFormat::Decode);
    const auto video = media.supportedVideoCodecs(QMediaFormat::Decode);
    bool hasAac = audio.contains(QMediaFormat::AudioCodec::AAC);
    bool hasMp3 = audio.contains(QMediaFormat::AudioCodec::MP3);
    bool hasH264 = video.contains(QMediaFormat::VideoCodec::H264);
#ifdef __linux__
    for (const char* libName : {"libavcodec.so.61", "libavcodec.so.60", "libavcodec.so.59", "libavcodec.so.58"}) {
        void* handle = dlopen(libName, RTLD_LAZY | RTLD_NOLOAD);
        if (!handle) continue;
        using FindDecFn = const void* (*)(const char*);
        if (auto findDec = reinterpret_cast<FindDecFn>(dlsym(handle, "avcodec_find_decoder_by_name"))) {
            hasAac = hasAac || findDec("aac") != nullptr;
            hasMp3 = hasMp3 || findDec("mp3") != nullptr || findDec("mp3float") != nullptr;
            hasH264 = hasH264 || findDec("h264") != nullptr;
        }
        dlclose(handle);
        break;
    }
#endif
    out.append({"media-decode", (!hasAac && !hasMp3 && !hasH264 && audio.isEmpty() && video.isEmpty())
                                    ? QStringLiteral("missing (no Qt Multimedia backend/codecs)")
                                    : QStringLiteral("aac=%1 mp3=%2 h264=%3")
                                          .arg(yesNo(hasAac), yesNo(hasMp3), yesNo(hasH264))});
    }

    auto& h264 = Call::OpenH264::instance();
    out.append({"openh264", h264.available() ? h264.version() : QStringLiteral("missing (") + h264.unavailableReason() + ")"});

    out.append({"dbus-session", yesNo(QDBusConnection::sessionBus().isConnected())});

    const QString engine = Call::CallBridge::findEngine();
    out.append({"call-engine", engine.isEmpty() ? QStringLiteral("missing") : engine});
    const QString audioEngine = Call::CallBridge::audioEngineFor(engine);
    if (audioEngine.isEmpty()) {
        out.append({"audio-engine", QStringLiteral("missing")});
    } else {
        QProcess proc;
        proc.start(audioEngine, {QStringLiteral("--check-deps")});
        if (proc.waitForFinished(5000)) {
            for (const QString& line : QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
                const int eq = line.indexOf('=');
                out.append({QStringLiteral("audio.") + line.left(eq), line.mid(eq + 1)});
            }
        } else {
            proc.kill();
            out.append({"audio-engine", QStringLiteral("no answer")});
        }
    }
    return out;
}

int run() {
    bool ok = true;
    for (const Line& line : collect()) {
        std::printf("%-18s %s%s\n", qPrintable(line.key), qPrintable(line.value),
                    line.essential && !line.ok ? "  [REQUIRED]" : "");
        if (line.essential && !line.ok) ok = false;
    }
    std::fflush(stdout);
    return ok ? 0 : 1;
}

}
