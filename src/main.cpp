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
#include <iostream>

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

// Returns true when a process with the exact given command name is running.
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

// Returns true when the Qt platform input context plugin for the given module key exists on disk.
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

// Returns true when a D-Bus name is owned on the session bus. Uses the dbus-send CLI so it works
// before QGuiApplication exists (QT_IM_MODULE must be decided before the platform plugin loads).
bool sessionBusHasName(const QString& name) {
    QProcess proc;
    proc.start("dbus-send", {"--session", "--print-reply", "--dest=org.freedesktop.DBus", "/org/freedesktop/DBus",
                             "org.freedesktop.DBus.NameHasOwner", "string:" + name});
    if (!proc.waitForFinished(1500) || proc.exitCode() != 0) return false;
    return proc.readAllStandardOutput().contains("boolean true");
}

// Chooses the Qt input context so Vietnamese input methods (IBus-Bamboo/Unikey, Fcitx5-Unikey/Bamboo)
// work. Telex/VNI engines rely on preedit + surrounding-text deletion, which the Wayland text-input
// bridge (used when QT_IM_MODULE is unset on KDE/GNOME Wayland) handles poorly, so the native
// IBus/Fcitx plugins are always preferred when the daemon is reachable.
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
    // Process scan covers the host; D-Bus names also cover containers/sandboxes sharing the session bus.
    const bool fcitxRunning = isProcessRunning("fcitx5") || isProcessRunning("fcitx")
                              || sessionBusHasName("org.fcitx.Fcitx5") || sessionBusHasName("org.freedesktop.portal.Fcitx");
    const bool ibusRunning = isProcessRunning("ibus-daemon") || sessionBusHasName("org.freedesktop.IBus")
                             || sessionBusHasName("org.freedesktop.portal.IBus");
    const bool hasFcitxPlugin = hasInputContextPlugin("fcitx");
    const bool hasIbusPlugin = hasInputContextPlugin("ibus");

    // Respect an explicit, working choice.
    if ((current == "ibus" && ibusRunning && hasIbusPlugin) ||
        ((current == "fcitx" || current == "fcitx5") && fcitxRunning && hasFcitxPlugin) ||
        (!current.isEmpty() && current != "ibus" && current != "fcitx" && current != "fcitx5" && current != "wayland")) {
        return;
    }

    QString chosen;
    if (fcitxRunning && hasFcitxPlugin) {
        chosen = "fcitx";
    } else if ((ibusRunning || fcitxRunning) && hasIbusPlugin) {
        // Fcitx5 also implements the IBus D-Bus frontend, so the IBus plugin works with it too.
        chosen = "ibus";
    }

    if (chosen.isEmpty()) {
        // No native bridge available: fall back to the compositor's text-input protocol.
        if (!current.isEmpty() && current != "wayland") qunsetenv("QT_IM_MODULE");
        return;
    }

    qputenv("QT_IM_MODULE", chosen.toUtf8());
    qputenv("XMODIFIERS", QByteArray("@im=") + (fcitxRunning ? "fcitx" : "ibus"));
    if (chosen == "ibus" && !ibusRunning && fcitxRunning) {
        qputenv("IBUS_USE_PORTAL", "1");
    }
    if (QFile::exists("/.flatpak-info") || QFile::exists("/run/.containerenv") || QFile::exists("/.dockerenv")) {
        // Inside a sandbox/container the per-user IBus socket is unreachable; use the D-Bus portal.
        qputenv("IBUS_USE_PORTAL", "1");
    }
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef __GLIBC__
    // glibc gives every thread its own arena (zalos runs 30+ threads); freed memory then stays scattered and
    // is rarely returned to the OS. Two arenas keep RSS close to what is really in use.
    mallopt(M_ARENA_MAX, 2);
#endif

    // Timestamped Qt warnings so terminal messages can be matched with logs/realtime.log.
    if (qEnvironmentVariableIsEmpty("QT_MESSAGE_PATTERN")) {
        qputenv("QT_MESSAGE_PATTERN", "[%{time hh:mm:ss.zzz}] %{if-category}%{category}: %{endif}%{message}");
    }

    // 0. Rendering setup chosen from the hardware (GPU, Vulkan driver, RAM, cores) — see RenderProfile.
    using Zalos::Core::Platform::RenderProfile;
    const RenderProfile renderProfile = RenderProfile::decide(RenderProfile::probeSystem());
    renderProfile.apply();
    Zalos::Core::Platform::RenderGuard::markAttempt(renderProfile.backend);
    std::cerr << "[zalos] renderer: " << RenderProfile::backendName(renderProfile.backend).toStdString()
              << " (" << renderProfile.reason.toStdString() << ")" << std::endl;

    // 1. Wayland & Xorg Fallback Protocol
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        const auto env = QProcessEnvironment::systemEnvironment();
        if (!env.value("WAYLAND_DISPLAY").isEmpty() || env.value("XDG_SESSION_TYPE").compare("wayland", Qt::CaseInsensitive) == 0) {
            qputenv("QT_QPA_PLATFORM", "wayland;xcb");
        } else if (!env.value("DISPLAY").isEmpty()) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    }

    // 2. GTK Desktop Portal Integration for Theme and File Choosers
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        const auto env = QProcessEnvironment::systemEnvironment();
        const QString desktop = env.value("XDG_CURRENT_DESKTOP").toLower();
        // KDE too: the Plasma platform theme loads ~55 KF6/Kirigami libraries and switches Qt Quick
        // Controls to org.kde.desktop. ThemeManager reads light/dark/accent itself (portal + kdeglobals) and
        // the portal still shows the native KDE file dialog, out of our process.
        if (desktop.contains("gnome") || desktop.contains("ubuntu") || desktop.contains("kde")) {
            qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
        }
    }

    // 3. Input Method (IBus / Fcitx) reconciliation for Vietnamese typing
    configureInputMethod();

    // Qt Application Initialization
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
    QCommandLineOption tabOption("tab", "Select default navigation tab (messages, contacts, todo, cloud).", "name");
    parser.addOption(tabOption);
    QCommandLineOption screenshotOption("screenshot", "Render the main window to an image file and exit (headless verification).", "path");
    parser.addOption(screenshotOption);
    QCommandLineOption screenshotDelayOption("screenshot-delay", "Delay in milliseconds before capturing the screenshot (default 2500).", "ms", "2500");
    parser.addOption(screenshotDelayOption);
    QCommandLineOption hiddenOption("hidden", "Start hidden in the system tray.");
    parser.addOption(hiddenOption);
    QCommandLineOption autostartOption("autostart", "Launched by the desktop's autostart entry (honours \"start minimized\").");
    parser.addOption(autostartOption);
    parser.process(app);

    if (parser.isSet(testGuiOption)) {
        QTimer::singleShot(1000, &app, &QApplication::quit);
    }

    // One instance per user and data directory (shared offline.db, one Zalo session). A second launch brings the
    // running window to the front and exits. Diagnostics (--test-gui, --screenshot) and ZALOS_ALLOW_MULTIPLE=1
    // (developer runs) skip the check.
    Zalos::Core::Platform::SingleInstance singleInstance(Zalos::Core::Platform::SingleInstance::defaultId());
    const bool diagnosticRun = parser.isSet(testGuiOption) || parser.isSet(screenshotOption);
    if (!diagnosticRun && qEnvironmentVariable("ZALOS_ALLOW_MULTIPLE") != QLatin1String("1")
        && !singleInstance.start()) {
        std::cout << "Zalos đang chạy: đã chuyển tới cửa sổ hiện có." << std::endl;
        return 0;
    }

    // Quick Controls Style adaptation
    // Zalos draws its own Zalo-styled controls on top of the Basic style (custom contentItem/background);
    // a desktop style such as org.kde.desktop breaks them (ComboBox positionToRectangle errors) and loads
    // Kirigami. The QML files import QtQuick.Controls.Basic directly (Qt 6.11's qmlcachegen no longer resolves the
    // style-selected `import QtQuick.Controls`, so its types would not compile to C++); the style set here keeps
    // any Controls type created from C++ or plugins on Basic too. QT_QUICK_CONTROLS_STYLE cannot restyle them.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle("Basic");
    }

    // Initialize Core Controllers
    auto themeManager = new Zalos::Core::Platform::ThemeManager(&app);
    Q_UNUSED(themeManager); // reached from QML as the ThemeManager singleton
    auto zaloClient = new Zalos::Core::Network::ZaloClient(&app);
    auto trayController = new Zalos::Core::Platform::SystemTrayController(&app);
    // Hidden start: --hidden, or the autostart entry with "Khởi động thu nhỏ vào khay" on — only with a tray to
    // bring the window back from. A launch from the app icon always shows the window.
    zaloClient->setLaunchHidden(zaloClient->trayAvailable()
                                && (parser.isSet(hiddenOption)
                                    || (parser.isSet(autostartOption) && zaloClient->startHiddenInTray())));
    zaloClient->storage()->setAvatarMemoryCleaner([]() { Zalos::Core::Platform::AvatarImageProvider::clearMemCache(); });

    // Anti-peeking quick switch in the tray menu, kept in sync with the setting (window, Settings, shortcut).
    QObject::connect(trayController, &Zalos::Core::Platform::SystemTrayController::antiPeekingToggled,
                     zaloClient, &Zalos::Core::Network::ZaloClient::setAntiPeeking);
    QObject::connect(zaloClient, &Zalos::Core::Network::ZaloClient::settingsChanged, trayController,
                     [zaloClient, trayController]() { trayController->setAntiPeeking(zaloClient->antiPeeking()); });
    trayController->setAntiPeeking(zaloClient->antiPeeking());

    // Sync Unread Count to System Tray Badge Icon
    QObject::connect(zaloClient->conversationModel(), &Zalos::Core::Models::ConversationModel::unreadCountChanged,
                     trayController, &Zalos::Core::Platform::SystemTrayController::setUnreadCount);
    trayController->setUnreadCount(zaloClient->conversationModel()->totalUnreadCount());

    if (parser.isSet(tabOption)) {
        const QString tab = parser.value(tabOption);
        zaloClient->setCurrentNavigationTab(tab);
        if (tab == "contacts") {
            zaloClient->contactModel()->setCategoryFilter("friends");
        }
    }

    // QML Application Engine Setup
    QQmlApplicationEngine engine;
    engine.addImageProvider("avatar", new Zalos::Core::Platform::AvatarImageProvider);
    engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider); // ZIcon glyphs
    engine.addImportPath("qrc:/");
    engine.addImportPath("qrc:/qt/qml");

    // Register Context Properties
    // ZaloClient, ThemeManager and TrayController are typed QML singletons (QML_SINGLETON + create()),
    // which lets qmlcachegen compile bindings that use them to C++. They resolve to the instances above.

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [trayController, zaloClient, &singleInstance](QObject* obj, const QUrl& objUrl) {
        if (!obj) {
            std::cerr << "Failed to load QML root component: " << objUrl.toString().toStdString() << std::endl;
            QCoreApplication::exit(-1);
            return;
        }

        // Connect tray toggle window event to QML root window visibility
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
            // A second launch or a click on a notification: show, raise and focus the window. The xdg-activation
            // token (Wayland) must be in the environment when the window asks for focus.
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
    // Qt 6.4 (Ubuntu 24.04): no loadFromModule; qt_add_qml_module puts the module under :/Zalos/.
    engine.load(QUrl(QStringLiteral("qrc:/Zalos/src/qml/Main.qml")));
#endif

    // Render guard: the first frame proves the chosen backend works. If the scene graph cannot start
    // (e.g. a broken Vulkan driver), remember it and restart once with OpenGL instead of a blank window.
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
            Zalos::Core::Platform::RenderGuard::vulkanFailedBefore(); // pending vulkan -> marked broken
            // Inside an AppImage the binary lives in a mount that disappears with us: relaunch the image.
            const QString exe = qEnvironmentVariable("APPIMAGE", QCoreApplication::applicationFilePath());
            singleInstance.release(); // the relaunched process must become the primary instance
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

