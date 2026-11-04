#include <QApplication>
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
#include <QQuickWindow>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <iostream>
#include <cstdio>

#include "core/network/zalo_client.h"
#include "core/platform/theme_manager.h"
#include "core/platform/system_tray.h"
#include "core/crypto/db_cross.h"
#include "core/platform/avatar_image_provider.h"
#include "core/platform/icon_image_provider.h"
#include "core/platform/render_profile.h"
#include "core/platform/single_instance.h"

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace {

bool isProcessRunning(const QString& commName) {
    const QDir procDir("/proc");
    const QStringList pids = procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& pid : pids) {
        bool isNumeric = false;
        pid.toInt(&isNumeric);
        if (!isNumeric) {
            continue;
        }
        QFile commFile("/proc/" + pid + "/comm");
        if (commFile.open(QIODevice::ReadOnly) && QString::fromUtf8(commFile.readAll()).trimmed() == commName) {
            return true;
        }
    }
    return false;
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

bool sessionBusHasName(const QString& name) {
    QProcess proc;
    proc.start("dbus-send", {"--session", "--print-reply", "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus",
                             "org.freedesktop.DBus.NameHasOwner", "string:" + name});
    if (!proc.waitForFinished(1500) || proc.exitCode() != 0) return false;
    return proc.readAllStandardOutput().contains("boolean true");
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
    const bool fcitxRunning = isProcessRunning("fcitx5") || isProcessRunning("fcitx")
                              || sessionBusHasName("org.fcitx.Fcitx5") || sessionBusHasName("org.freedesktop.portal.Fcitx");
    const bool ibusRunning = isProcessRunning("ibus-daemon") || sessionBusHasName("org.freedesktop.IBus")
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
    if (chosen == "ibus" && !ibusRunning && fcitxRunning) {
        qputenv("IBUS_USE_PORTAL", "1");
    }
    if (QFile::exists("/.flatpak-info") || QFile::exists("/run/.containerenv") || QFile::exists("/.dockerenv")) {
        qputenv("IBUS_USE_PORTAL", "1");
    }
}

}

int main(int argc, char* argv[]) {
#ifdef __GLIBC__
    mallopt(M_ARENA_MAX, 2);
#endif

    QCoreApplication::setOrganizationName("Zalos");
    QCoreApplication::setApplicationName("Zalos");
    bool foregroundRun = qEnvironmentVariableIsSet("ZALOS_DEBUG");
    for (int i = 1; i < argc && !foregroundRun; ++i) {
        const QByteArray a(argv[i]);
        foregroundRun = a == "--debug" || a == "--test-gui" || a.startsWith("--screenshot")
                        || a == "-h" || a == "--help" || a == "--version" || a == "-v";
    }
    if (!foregroundRun) {
        const QString logDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("logs");
        QDir().mkpath(logDir);
        const QByteArray logPath = QDir(logDir).filePath("zalos.log").toLocal8Bit();
        if (std::freopen(logPath.constData(), "a", stdout)) std::setvbuf(stdout, nullptr, _IOLBF, 0);
        std::freopen(logPath.constData(), "a", stderr);
    }

    if (qEnvironmentVariableIsEmpty("QT_MESSAGE_PATTERN")) {
        qputenv("QT_MESSAGE_PATTERN", "[%{time hh:mm:ss.zzz}] %{if-category}%{category}: %{endif}%{message}");
    }

    using Zalos::Core::Platform::RenderProfile;
    const RenderProfile renderProfile = RenderProfile::decide(RenderProfile::probeSystem());
    renderProfile.apply();
    Zalos::Core::Platform::RenderGuard::markAttempt(renderProfile.backend);
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

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        const auto env = QProcessEnvironment::systemEnvironment();
        const QString desktop = env.value("XDG_CURRENT_DESKTOP").toLower();
        if (desktop.contains("gnome") || desktop.contains("ubuntu") || desktop.contains("kde")) {
            qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
        }
    }

    configureInputMethod();

    QApplication app(argc, argv);
    app.setApplicationName("Zalos");
    app.setOrganizationName("Zalos");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/Zalos/assets/icons/zalos.svg"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Zalos - Zalo desktop client ported to Qt6 (C++/QML) for Linux");
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
    parser.process(app);

    if (parser.isSet(testGuiOption)) {
        QTimer::singleShot(1000, &app, &QApplication::quit);
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

    auto themeManager = new Zalos::Core::Platform::ThemeManager(&app);
    Q_UNUSED(themeManager);
    auto zaloClient = new Zalos::Core::Network::ZaloClient(&app);
    auto trayController = new Zalos::Core::Platform::SystemTrayController(&app);
    zaloClient->setLaunchHidden(zaloClient->trayAvailable()
                                && (parser.isSet(hiddenOption)
                                    || (parser.isSet(autostartOption) && zaloClient->startHiddenInTray())));
    zaloClient->storage()->setAvatarMemoryCleaner([]() { Zalos::Core::Platform::AvatarImageProvider::clearMemCache(); });

    QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::antiPeekingToggled,
                     zaloClient, &Zalos::Core::Network::ZaloClient::setAntiPeeking);
    QObject::connect(zaloClient, &Zalos::Core::Network::ZaloClient::settingsChanged, trayController,
                     [zaloClient, trayController]() { trayController->setAntiPeeking(zaloClient->antiPeeking()); });
    trayController->setAntiPeeking(zaloClient->antiPeeking());

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
            QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::toggleDarkModeRequested,
                             obj, [obj]() {
                QMetaObject::invokeMethod(obj, "toggleTheme");
            });
            QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::quitRequested,
                             &QApplication::quit);
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

    for (QObject* obj : engine.rootObjects()) {
        auto* window = qobject_cast<QQuickWindow*>(obj);
        if (!window) continue;
        auto firstFrame = std::make_shared<QMetaObject::Connection>();
        *firstFrame = QObject::connect(window, &QQuickWindow::frameSwapped, window, [firstFrame]() {
            Zalos::Core::Platform::RenderGuard::markFirstFrame();
            QObject::disconnect(*firstFrame);
        });
        QObject::connect(window, &QQuickWindow::sceneGraphError, &app,
                         [renderProfile, &singleInstance](QQuickWindow::SceneGraphError, const QString& message) {
            std::cerr << "[zalos] scene graph error: " << message.toStdString() << std::endl;
            if (renderProfile.backend != RenderProfile::Backend::Vulkan) return;
            Zalos::Core::Platform::RenderGuard::vulkanFailedBefore();
            const QString exe = qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath());
            singleInstance.release();
            QProcess::startDetached(exe, QCoreApplication::arguments().mid(1));
            QCoreApplication::exit(0);
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
            QApplication::quit();
        });
    }

    return app.exec();
}
