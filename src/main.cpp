#include <QGuiApplication>
#include <QImageReader>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickStyle>
#include <QProcessEnvironment>
#include <QCommandLineParser>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QPixmapCache>
#include <QQuickWindow>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QSet>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "core/network/zalo_client.h"
#include "core/platform/theme_manager.h"
#include "core/platform/system_tray.h"
#include "core/crypto/db_cross.h"
#include "core/platform/avatar_image_provider.h"
#include "core/platform/icon_image_provider.h"
#include "core/platform/render_profile.h"
#include "core/platform/wheel_boost.h"
#include "core/platform/middle_autoscroll.h"
#include "core/platform/dependency_report.h"
#include "core/platform/single_instance.h"

#ifdef __GLIBC__
#include <malloc.h>
#endif
#ifdef __linux__
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace {

void scanRunningInputMethods(bool& fcitxRunning, bool& ibusRunning) {
    const QDir procDir("/proc");
    const QStringList pids = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& pid : pids) {
        bool isNumeric = false;
        pid.toInt(&isNumeric);
        if (!isNumeric) continue;
        QFile commFile("/proc/" + pid + "/comm");
        if (!commFile.open(QIODevice::ReadOnly)) continue;
        const QByteArray comm = commFile.readAll().trimmed();
        if (comm == "fcitx5" || comm == "fcitx") fcitxRunning = true;
        else if (comm == "ibus-daemon") ibusRunning = true;
        if (fcitxRunning && ibusRunning) break;
    }
}

bool hasInputContextPlugin(const QString& key) {
    QStringList dirs;
    dirs << QLibraryInfo::path(QLibraryInfo::PluginsPath);
    const QString extra = qEnvironmentVariable("QT_PLUGIN_PATH");
    if (!extra.isEmpty()) {
        dirs << extra.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    }
    for (const QString& dir : dirs) {
        const QDir icDir(dir + "/platforminputcontexts");
        if (!icDir.entryList({"*" + key + "*platforminputcontext*"}, QDir::Files).isEmpty()) {
            return true;
        }
    }
    return false;
}

const QByteArray& sessionBusOwnedNames() {
    static const QByteArray names = [] {
        QProcess proc;
        proc.start("dbus-send", {"--session", "--print-reply", "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus",
                                 "org.freedesktop.DBus.ListNames"});
        if (!proc.waitForFinished(1500) || proc.exitCode() != 0) return QByteArray{};
        return proc.readAllStandardOutput();
    }();
    return names;
}

bool sessionBusHasName(const QString& name) {
    const QByteArray& reply = sessionBusOwnedNames();
    if (reply.isEmpty()) return false;
    return reply.contains('"' + name.toUtf8() + '"');
}

void configureInputMethod() {
    const QString forced = qEnvironmentVariable("ZALOS_IM_MODULE");
    if (!forced.isEmpty()) {
        if (forced == "native" || forced == "none") {
            qunsetenv("QT_IM_MODULE");
        } else {
            qputenv("QT_IM_MODULE", forced.toUtf8());
        }
        return;
    }

    const QString current = qEnvironmentVariable("QT_IM_MODULE").toLower();
    bool procFcitx = false;
    bool procIbus = false;
    scanRunningInputMethods(procFcitx, procIbus);
    const bool fcitxRunning = procFcitx
                              || sessionBusHasName("org.fcitx.Fcitx5") || sessionBusHasName("org.freedesktop.portal.Fcitx");
    const bool ibusRunning = procIbus || sessionBusHasName("org.freedesktop.IBus")
                             || sessionBusHasName("org.freedesktop.portal.IBus");
    const bool hasFcitxPlugin = hasInputContextPlugin("fcitx");
    const bool hasIbusPlugin = hasInputContextPlugin("ibus");

    if ((current == "ibus" && ibusRunning && hasIbusPlugin) ||
        ((current == "fcitx" || current == "fcitx5") && fcitxRunning && hasFcitxPlugin) ||
        (!current.isEmpty() && current != "ibus" && current != "fcitx" && current != "fcitx5" && current != "wayland")) {
        return;
    }

    QString chosen;
    if (fcitxRunning && hasFcitxPlugin) {
        chosen = "fcitx";
    } else if ((ibusRunning || fcitxRunning) && hasIbusPlugin) {
        chosen = "ibus";
    }

    if (chosen.isEmpty()) {
        if (!current.isEmpty() && current != "wayland") qunsetenv("QT_IM_MODULE");
        return;
    }

    qputenv("QT_IM_MODULE", chosen.toUtf8());
    qputenv("XMODIFIERS", QByteArray("@im=") + (fcitxRunning ? "fcitx" : "ibus"));
    if (chosen == "ibus" && fcitxRunning && (!ibusRunning || sessionBusHasName("org.freedesktop.portal.IBus"))) {
        qputenv("IBUS_USE_PORTAL", "1");
    }
    if (QFile::exists("/.flatpak-info") || QFile::exists("/run/.containerenv") || QFile::exists("/.dockerenv")) {
        qputenv("IBUS_USE_PORTAL", "1");
    }
}

void ensureSymlink(const QString& target, const QString& linkPath) {
    const QFileInfo info(linkPath);
    if (info.isSymLink()) {
        if (QFileInfo::exists(info.symLinkTarget())) return;
        QFile::remove(linkPath);
    } else if (info.exists()) {
        return;
    }
    QFile::link(target, linkPath);
}

void setupMinimalPluginPath() {
    if (qEnvironmentVariableIsSet("ZALOS_BUNDLED_QT")) {
        const QString bundledPlugins = qEnvironmentVariable("QT_PLUGIN_PATH");
        if (!bundledPlugins.isEmpty()) {
            QCoreApplication::setLibraryPaths(bundledPlugins.split(QDir::listSeparator(), Qt::SkipEmptyParts));
        }
        return;
    }

    QStringList srcDirs;
    const QString systemPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!systemPlugins.isEmpty() && QFileInfo::exists(systemPlugins)) {
        srcDirs << systemPlugins;
    }
    const QString extraPlugins = qEnvironmentVariable("QT_PLUGIN_PATH");
    if (!extraPlugins.isEmpty()) {
        for (const QString& dir : extraPlugins.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
            if (QFileInfo::exists(dir) && !srcDirs.contains(dir)) srcDirs << dir;
        }
    }
    if (srcDirs.isEmpty()) return;

    QString runtimeBase = qEnvironmentVariable("XDG_RUNTIME_DIR");
    bool usingTempFallback = false;
    if (runtimeBase.isEmpty() || !QFileInfo::exists(runtimeBase)) {
        runtimeBase = QDir::tempPath();
        usingTempFallback = true;
    }
#ifdef __linux__
    const quint64 selfUid = quint64(::getuid());
    const QString uidStr = QString::number(selfUid);
#else
    const QString uidStr = QStringLiteral("0");
#endif
    const QString runtimeDir = runtimeBase + QStringLiteral("/zalos-qt-plugins-%1-%2")
                                                 .arg(uidStr, QString::fromLatin1(qVersion()));
    QDir root(runtimeDir);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) return;
    if (usingTempFallback) {
#ifdef __linux__
        const QFileInfo rtInfo(runtimeDir);
        if (!rtInfo.isDir() || rtInfo.isSymLink() || quint64(rtInfo.ownerId()) != selfUid) return;
#endif
        QFile::setPermissions(runtimeDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    const QSet<QString> allowedImagePlugins{
        QStringLiteral("libqjpeg.so"),
        QStringLiteral("libqgif.so"),
        QStringLiteral("libqwebp.so"),
        QStringLiteral("libqsvg.so"),
        QStringLiteral("libqico.so"),
        QStringLiteral("kimg_jxl.so"),
        QStringLiteral("libqjxl.so")
    };

    auto syncFilteredDir = [&](const QString& subName, const QSet<QString>& allowed) {
        QDir subDir(runtimeDir + QLatin1Char('/') + subName);
        if (!subDir.exists() && !subDir.mkpath(QStringLiteral("."))) return;
        for (const QFileInfo& entry : subDir.entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
            if (!allowed.contains(entry.fileName()) || (entry.isSymLink() && !QFileInfo::exists(entry.symLinkTarget()))) {
                QFile::remove(entry.absoluteFilePath());
            }
        }
        for (const QString& base : srcDirs) {
            const QDir srcSub(base + QLatin1Char('/') + subName);
            if (!srcSub.exists()) continue;
            for (const QFileInfo& f : srcSub.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
                if (allowed.contains(f.fileName())) {
                    ensureSymlink(f.absoluteFilePath(), subDir.filePath(f.fileName()));
                }
            }
        }
    };

    syncFilteredDir(QStringLiteral("imageformats"), allowedImagePlugins);

    for (const QFileInfo& entry : root.entryInfoList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        if (entry.isSymLink() && !QFileInfo::exists(entry.symLinkTarget())) {
            QFile::remove(entry.absoluteFilePath());
        }
    }

    for (const QString& base : srcDirs) {
        const QDir src(base);
        for (const QFileInfo& sub : src.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString name = sub.fileName();
            if (name == QLatin1String("styles") || name == QLatin1String("imageformats")
                || name == QLatin1String("platformthemes")) {
                continue;
            }
            ensureSymlink(sub.absoluteFilePath(), root.filePath(name));
        }
    }

    qputenv("QT_PLUGIN_PATH", runtimeDir.toUtf8());
    QCoreApplication::setLibraryPaths({runtimeDir});
}

void markColdAnonymousMappings() {
#ifdef __linux__
    constexpr int kMadvCold = 20;
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const size_t sp1 = line.find(' ');
        if (sp1 == std::string::npos) continue;
        const size_t sp2 = line.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) continue;
        if (sp2 - sp1 - 1 != 4 || line.compare(sp1 + 1, 4, "rw-p") != 0) continue;

        const size_t dash = line.find('-');
        if (dash == std::string::npos || dash >= sp1) continue;

        size_t pos = sp2 + 1;
        for (int field = 0; field < 3 && pos != std::string::npos; ++field) {
            while (pos < line.size() && line[pos] == ' ') ++pos;
            if (field == 2) break;
            pos = line.find(' ', pos);
        }
        if (pos == std::string::npos) continue;
        if (line[pos] != '0' || (pos + 1 < line.size() && line[pos + 1] != ' ')) continue;
        pos = line.find_first_not_of(' ', pos + 1);
        const std::string path = (pos != std::string::npos) ? line.substr(pos) : std::string();
        if (!path.empty() && path != "[heap]") continue;

        unsigned long start = 0;
        unsigned long end = 0;
        try {
            start = std::stoul(line.substr(0, dash), nullptr, 16);
            end = std::stoul(line.substr(dash + 1, sp1 - dash - 1), nullptr, 16);
        } catch (...) {
            continue;
        }
        if (end > start && end - start >= 65536UL) {
            ::madvise(reinterpret_cast<void*>(start), size_t(end - start), kMadvCold);
        }
    }
#endif
}

}

void configureBundledQtEnv() {
    const QString exeDir = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalPath();
    if (exeDir.isEmpty()) return;
    const QStringList candidates{
        QDir::cleanPath(exeDir + QStringLiteral("/../lib/zalos/qt6")),
        QDir::cleanPath(exeDir + QStringLiteral("/../lib64/zalos/qt6")),
        QDir::cleanPath(exeDir + QStringLiteral("/../lib/qt6-fallback")),
    };
    for (const QString& cand : candidates) {
        if (!QFileInfo::exists(cand + QStringLiteral("/plugins/platforms"))) continue;
        qputenv("ZALOS_BUNDLED_QT", "1");
        qputenv("QT_PLUGIN_PATH", (cand + QStringLiteral("/plugins")).toUtf8());
        const QString qmlDir = cand + QStringLiteral("/qml");
        qputenv("QML_IMPORT_PATH", qmlDir.toUtf8());
        qputenv("QML2_IMPORT_PATH", qmlDir.toUtf8());
        QCoreApplication::setLibraryPaths({cand + QStringLiteral("/plugins")});
        return;
    }
}

int main(int argc, char* argv[]) {
#ifdef __GLIBC__
    mallopt(M_ARENA_MAX, 2);
    mallopt(M_MMAP_THRESHOLD, 256 * 1024);
    mallopt(M_TRIM_THRESHOLD, 512 * 1024);
#endif
#ifdef __linux__
    constexpr int kPrSetMemoryMerge = 67;
    ::prctl(kPrSetMemoryMerge, 1, 0, 0, 0);
#endif

    configureBundledQtEnv();
    QCoreApplication::setOrganizationName("Zalos");
    QCoreApplication::setApplicationName("Zalos");
    const bool ownScale = qEnvironmentVariableIsSet("ZALOS_OWN_SCALE_FACTOR");
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR") || ownScale) {
        const int uiScale = QSettings("Zalos", "ZalosApp").value("appearance/interfaceScale", 100).toInt();
        if (uiScale == 80 || uiScale == 90 || uiScale == 125 || uiScale == 150) {
            qputenv("QT_SCALE_FACTOR", QByteArray::number(uiScale / 100.0, 'f', 2));
            qputenv("ZALOS_OWN_SCALE_FACTOR", "1");
        } else if (ownScale) {
            qunsetenv("QT_SCALE_FACTOR");
            qunsetenv("ZALOS_OWN_SCALE_FACTOR");
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) != "--check-deps") continue;
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication probe(argc, argv);
        return Zalos::Core::Platform::DependencyReport::run();
    }
    bool foregroundRun = qEnvironmentVariableIsSet("ZALOS_DEBUG");
    for (int i = 1; i < argc && !foregroundRun; ++i) {
        const QByteArray a(argv[i]);
        foregroundRun = a == "--debug" || a == "--test-gui" || a.startsWith("--screenshot")
                        || a == "-h" || a == "--help" || a == "--version" || a == "-v";
    }
    if (!foregroundRun) {
        const QString logDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("logs");
        QDir().mkpath(logDir);
        const QString logFile = QDir(logDir).filePath("zalos.log");
        if (QFileInfo(logFile).size() > 2 * 1024 * 1024) {
            const QString backup = logFile + QStringLiteral(".1");
            QFile::remove(backup);
            QFile::rename(logFile, backup);
        }
        const QByteArray logPath = logFile.toLocal8Bit();
        if (std::freopen(logPath.constData(), "a", stdout)) std::setvbuf(stdout, nullptr, _IOLBF, 0);
        if (std::freopen(logPath.constData(), "a", stderr)) std::setvbuf(stderr, nullptr, _IOLBF, 0);
        QFile::setPermissions(logFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    if (qEnvironmentVariableIsEmpty("QT_MESSAGE_PATTERN")) {
        qputenv("QT_MESSAGE_PATTERN", "[%{time hh:mm:ss.zzz}] %{if-category}%{category}: %{endif}%{message}");
    }

    using Zalos::Core::Platform::RenderProfile;
    const RenderProfile renderProfile = RenderProfile::decide(RenderProfile::probeSystem());
    renderProfile.apply();
    std::cerr << "[zalos] renderer: " << RenderProfile::backendName(renderProfile.backend).toStdString()
              << " (" << renderProfile.reason.toStdString() << ")" << std::endl;

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        const auto env = QProcessEnvironment::systemEnvironment();
        if (!env.value("WAYLAND_DISPLAY").isEmpty() || env.value("XDG_SESSION_TYPE").compare("wayland", Qt::CaseInsensitive) == 0) {
            qputenv("QT_QPA_PLATFORM", "wayland;xcb");
        } else if (!env.value("DISPLAY").isEmpty()) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    }

    configureInputMethod();
    setupMinimalPluginPath();

    QGuiApplication app(argc, argv);
    QImageReader::setAllocationLimit(64);
    app.setApplicationName("Zalos");
    app.setOrganizationName("Zalos");
    app.setApplicationVersion(QStringLiteral(ZALOS_APP_VERSION));
    app.setDesktopFileName(QFile::exists(QStringLiteral("/.flatpak-info"))
                               ? QStringLiteral("io.github.cpfriend1721994.zalos")
                               : QStringLiteral("zalos"));
    app.setWindowIcon(QIcon(":/Zalos/assets/icons/zalos.svg"));
    QPixmapCache::setCacheLimit(4096);
    Zalos::Core::Platform::WheelBoost::applyWheelScrollLines(app.styleHints());
    Zalos::Core::Platform::WheelBoost wheelBoost;
    Zalos::Core::Platform::MiddleAutoScroll middleAutoScroll;

    QCommandLineParser parser;
    parser.setApplicationDescription("Zalos - Native Qt6 (C++/QML) messaging client for Zalo on Linux");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption testGuiOption("test-gui", "Run headless GUI sanity check and exit automatically.");
    parser.addOption(testGuiOption);
    QCommandLineOption tabOption("tab", "Select default navigation tab (messages, contacts, cloud).", "name");
    parser.addOption(tabOption);
    QCommandLineOption screenshotOption("screenshot", "Render the main window to an image file and exit (headless verification).", "path");
    parser.addOption(screenshotOption);
    QCommandLineOption screenshotDelayOption("screenshot-delay", "Delay in milliseconds before capturing the screenshot (default 2500).", "ms", "2500");
    parser.addOption(screenshotDelayOption);
    QCommandLineOption hiddenOption("hidden", "Start hidden in the system tray.");
    parser.addOption(hiddenOption);
    QCommandLineOption autostartOption("autostart", "Launched by the desktop's autostart entry (honours \"start minimized\").");
    parser.addOption(autostartOption);
    QCommandLineOption debugOption("debug", "Keep logs on the terminal and let Ctrl+C quit (default: detach, log to file).");
    parser.addOption(debugOption);
    QCommandLineOption forceSyncV2Option("force-sync-v2", "Debug: use phone history Sync v2 directly (same as ZALOS_FORCE_SYNC_V2=1).");
    parser.addOption(forceSyncV2Option);
    parser.process(app);

    if (parser.isSet(testGuiOption)) {
        QTimer::singleShot(1000, &app, &QGuiApplication::quit);
    }

    Zalos::Core::Platform::SingleInstance singleInstance(Zalos::Core::Platform::SingleInstance::defaultId());
    const bool diagnosticRun = parser.isSet(testGuiOption) || parser.isSet(screenshotOption);
    if (!diagnosticRun && qEnvironmentVariable("ZALOS_ALLOW_MULTIPLE") != QLatin1String("1")
        && !singleInstance.start()) {
        std::cout << "Zalos đang chạy: đã chuyển tới cửa sổ hiện có." << std::endl;
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle("Basic");
    }

    Zalos::Core::Platform::ThemeManager::registerEmojiFallbackFont();
    auto themeManager = new Zalos::Core::Platform::ThemeManager(&app);
    Q_UNUSED(themeManager);
    const auto zaloClientOwner = std::make_unique<Zalos::Core::Network::ZaloClient>();
    auto* zaloClient = zaloClientOwner.get();
    auto trayController = new Zalos::Core::Platform::SystemTrayController(&app);
    zaloClient->setLaunchHidden(zaloClient->trayAvailable()
                                && (parser.isSet(hiddenOption)
                                    || (parser.isSet(autostartOption) && zaloClient->startHiddenInTray())));
    zaloClient->storage()->setAvatarMemoryCleaner([]() { Zalos::Core::Platform::AvatarImageProvider::clearMemCache(); });

    QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::antiPeekingToggled,
                     zaloClient, &Zalos::Core::Network::ZaloClient::setAntiPeeking);
    QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::notificationsToggled,
                     zaloClient, &Zalos::Core::Network::ZaloClient::setNotifyNewMessage);
    QObject::connect(zaloClient, &Zalos::Core::Network::ZaloClient::settingsChanged, trayController,
                     [zaloClient, trayController]() {
        trayController->setAntiPeeking(zaloClient->antiPeeking());
        trayController->setNotificationsEnabled(zaloClient->notifyNewMessage());
    });
    trayController->setAntiPeeking(zaloClient->antiPeeking());
    trayController->setNotificationsEnabled(zaloClient->notifyNewMessage());
    QObject::connect(zaloClient, &Zalos::Core::Network::ZaloClient::appLanguageChanged, trayController,
                     [zaloClient, trayController]() { trayController->setLanguage(zaloClient->appLanguage()); });
    trayController->setLanguage(zaloClient->appLanguage());

    QObject::connect(zaloClient->conversationModel(), &Zalos::Core::Models::ConversationModel::unreadCountChanged,
                     trayController, &Zalos::Core::Platform::SystemTrayController::setUnreadCount);
    trayController->setUnreadCount(zaloClient->conversationModel()->totalUnreadCount(),
                                   zaloClient->conversationModel()->hasAnyUnreadMention());

    if (parser.isSet(tabOption)) {
        const QString tab = parser.value(tabOption);
        zaloClient->setCurrentNavigationTab(tab);
        if (tab == "contacts") {
            zaloClient->contactModel()->setCategoryFilter("friends");
        }
    }

    if (qEnvironmentVariable("ZALOS_TEXT_RENDERING").toLower() != "qt") {
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider("avatar", new Zalos::Core::Platform::AvatarImageProvider);
    engine.addImageProvider("cover", new Zalos::Core::Platform::AvatarImageProvider(false));
    engine.addImageProvider("media", new Zalos::Core::Platform::AvatarImageProvider(false));
    engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
    engine.addImportPath("qrc:/");
    engine.addImportPath("qrc:/qt/qml");

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [trayController, zaloClient, &singleInstance](QObject* obj, const QUrl& objUrl) {
        if (!obj) {
            std::cerr << "Failed to load QML root component: " << objUrl.toString().toStdString() << std::endl;
            QCoreApplication::exit(-1);
            return;
        }

        if (obj) {
            QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::toggleWindowRequested,
                             obj, [obj]() {
                QMetaObject::invokeMethod(obj, "toggleVisibility");
            });
            QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::openSettingsRequested,
                             obj, [obj, zaloClient]() {
                QMetaObject::invokeMethod(obj, "showAndActivate");
                zaloClient->openSettings(QStringLiteral("general"));
            });
            QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::toggleDarkModeRequested,
                             obj, [obj]() {
                QMetaObject::invokeMethod(obj, "toggleTheme");
            });
            QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::quitRequested,
                             obj, [obj, zaloClient, trayController]() {
                obj->setProperty("forceQuit", true);
                if (zaloClient->calls()) {
                    zaloClient->calls()->hangup();
                    zaloClient->calls()->shutdown();
                }
                trayController->shutdown();
                QGuiApplication::quit();
            });
            auto activate = [obj](const QString& token) {
                if (!token.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
                QMetaObject::invokeMethod(obj, "showAndActivate");
            };
            QObject::connect(&singleInstance, &Zalos::Core::Platform::SingleInstance::activationRequested, obj, activate);
            QObject::connect(zaloClient, &Zalos::Core::Network::ZaloClient::windowActivationRequested, obj, activate);
        }
    }, Qt::QueuedConnection);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("Zalos", "Main");
#else
    engine.load(QUrl(QStringLiteral("qrc:/Zalos/src/qml/Main.qml")));
#endif

    QObject::connect(&app, &QCoreApplication::aboutToQuit, zaloClient, [zaloClient, trayController]() {
        Zalos::Core::Platform::RenderGuard::markFirstFrame();
        if (zaloClient->calls()) zaloClient->calls()->shutdown();
        trayController->shutdown();
    });

    auto restartArgs = []() {
        QStringList args;
        for (const QString& a : QCoreApplication::arguments().mid(1)) {
            if (a != QLatin1String("--hidden") && a != QLatin1String("--autostart")) args << a;
        }
        return args;
    };
    auto restarting = std::make_shared<bool>(false);
    auto performRestart = [&engine, &singleInstance, zaloClient, trayController, restartArgs, restarting]() {
        if (*restarting) return;
        *restarting = true;
        for (QObject* obj : engine.rootObjects()) {
            if (obj) obj->setProperty("forceQuit", true);
        }
        if (zaloClient->calls()) {
            zaloClient->calls()->hangup();
            zaloClient->calls()->shutdown();
        }
        trayController->shutdown();
        const QString exe = qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath());
        singleInstance.release();
        QProcess::startDetached(exe, restartArgs());
        QCoreApplication::exit(0);
    };
    QObject::connect(zaloClient, &Zalos::Core::Network::ZaloClient::restartRequested, &app, performRestart);

    for (QObject* obj : engine.rootObjects()) {
        auto* window = qobject_cast<QQuickWindow*>(obj);
        if (!window) continue;
        window->installEventFilter(&wheelBoost);
        window->installEventFilter(&middleAutoScroll);
        auto attemptMarked = std::make_shared<bool>(false);
        auto firstFrameSwapped = std::make_shared<bool>(false);
        if (window->isVisible() && renderProfile.guarded()) {
            *attemptMarked = true;
            Zalos::Core::Platform::RenderGuard::markAttempt(renderProfile.backend);
        }
        auto firstFrame = std::make_shared<QMetaObject::Connection>();
        *firstFrame = QObject::connect(window, &QQuickWindow::frameSwapped, window, [firstFrame, firstFrameSwapped]() {
            *firstFrameSwapped = true;
            Zalos::Core::Platform::RenderGuard::markFirstFrame();
            QObject::disconnect(*firstFrame);
        });
        auto* bgTrimTimer = new QTimer(window);
        bgTrimTimer->setSingleShot(true);
        bgTrimTimer->setInterval(30000);
        QObject::connect(bgTrimTimer, &QTimer::timeout, window, [window, &engine, zaloClient]() {
            if (window->isVisible()) return;
            if (zaloClient->calls() && !zaloClient->calls()->phase().isEmpty()
                && zaloClient->calls()->phase() != QLatin1String("ended")) {
                return;
            }
            zaloClient->trimMemoryForBackground();
            QPixmapCache::clear();
            engine.trimComponentCache();
            engine.collectGarbage();
            window->releaseResources();
#ifdef __GLIBC__
            malloc_trim(0);
#endif
            markColdAnonymousMappings();
        });
        zaloClient->setWindowVisible(window->isVisible());
        QObject::connect(window, &QWindow::visibleChanged, bgTrimTimer,
                         [bgTrimTimer, zaloClient, attemptMarked, firstFrameSwapped, renderProfile](bool visible) {
            if (visible && !*attemptMarked && !*firstFrameSwapped && renderProfile.guarded()) {
                *attemptMarked = true;
                Zalos::Core::Platform::RenderGuard::markAttempt(renderProfile.backend);
            }
            zaloClient->setWindowVisible(visible);
            if (visible) bgTrimTimer->stop();
            else bgTrimTimer->start();
        });
        if (!window->isVisible()) bgTrimTimer->start();
        QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
                         [renderProfile, performRestart](QQuickWindow::SceneGraphError, const QString& message) {
            std::cerr << "[zalos] scene graph error: " << message.toStdString() << std::endl;
            if (!renderProfile.guarded()) return;
            Zalos::Core::Platform::RenderGuard::recordFailure(renderProfile.backend);
            performRestart();
        });
        QObject::connect(zaloClient, &Zalos::Core::Network::ZaloClient::memoryTrimRequested, window, [window, &engine]() {
            engine.collectGarbage();
            engine.trimComponentCache();
            window->releaseResources();
#ifdef __GLIBC__
            malloc_trim(0);
#endif
        });
    }

    if (parser.isSet(screenshotOption)) {
        const QString shotPath = parser.value(screenshotOption);
        QTimer::singleShot(parser.value(screenshotDelayOption).toInt(), &app, [&engine, shotPath]() {
            for (QObject* obj : engine.rootObjects()) {
                if (auto* window = qobject_cast<QQuickWindow*>(obj)) {
                    window->grabWindow().save(shotPath);
                }
            }
            QGuiApplication::quit();
        });
    }

    return app.exec();
}
