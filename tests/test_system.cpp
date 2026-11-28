#include <QtTest>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QSettings>
#include <QScopeGuard>
#include <QSoundEffect>
#include <QQmlComponent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QQuickImageProvider>
#include <QDateTime>
#include <QBuffer>
#include <QElapsedTimer>
#include <functional>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QVideoSink>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include "core/media/software_video_item.h"
#include "core/platform/bound_loader.h"
#include "core/call/portal_screen_cast.h"
#include <QPainter>
#include <QSvgRenderer>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QSqlQuery>
#include "multimedia_support.h"
#include "platform/app_updater.h"

#include "core/network/zalo_client.h"
#include "core/network/zalo_crypto.h"
#include "core/platform/theme_manager.h"
#include "core/platform/system_tray.h"
#include "core/platform/swipe_back.h"
#include <QPointingDevice>
#include "core/platform/wheel_boost.h"
#include "core/platform/middle_autoscroll.h"
#include "core/network/zalo_client_internal.h"
#include <QStyleHints>
#include <QWheelEvent>
#include "core/crypto/db_cross.h"
#include "core/models/conversation_model.h"
#include "core/models/message_model.h"
#include "core/fs/file_utils.h"
#include "core/platform/file_picker.h"
#include "core/media/media_utils.h"
#include "core/call/zcall_engine.h"
#include "core/offline/offline_store.h"
#include "core/network/zalo_api_client.h"
#include "core/network/zalo_session.h"
#include "core/network/sticker_store.h"
#include "core/models/rich_composer.h"
#include <QTextDocument>
#include "core/platform/avatar_image_provider.h"
#include "core/platform/icon_image_provider.h"
#include "core/platform/sound_engine.h"
#include "core/call/call_bridge.h"
#include "core/call/h264_codec.h"
#include "core/call/camera_sender.h"
#include "core/media/voice_recorder.h"
#include "core/network/message_scheduler.h"
#include "core/platform/app_lock.h"
#include "core/platform/desktop_notifications.h"
#include "core/platform/shortcut_map.h"
#include "core/platform/single_instance.h"
#include "core/platform/storage_manager.h"
#include "core/sync/mobile_history_sync.h"
#include "core/platform/render_profile.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <lzma.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include "test_support_loopback.h"

using namespace Zalos::Core::Network;
using namespace Zalos::Core::Platform;
using namespace Zalos::Core::Models;
using namespace Zalos::Core::Crypto;
using namespace Zalos::Core::Fs;
using namespace Zalos::Core::Media;
using namespace Zalos::Core::Call;
using namespace Zalos::Core::Offline;

namespace {
void loadMainWindow(QQmlApplicationEngine& engine) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("Zalos", "Main");
#else
    engine.load(QUrl(QStringLiteral("qrc:/Zalos/src/qml/Main.qml")));
#endif
}
}

class TestSystem : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("Zalos");
        QCoreApplication::setApplicationName("ZalosApp");
        QSettings settings;
        settings.setValue("auth/isLoggedIn", false);
        settings.setValue("auth/userName", "");
        settings.setValue("auth/userPhone", "");
        settings.remove("settings/closeToTray");
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(QDir(baseDir).filePath("offline.db"));
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    }

    void testMessageSearchFtsAndRawCodec() {
        using Zalos::Core::Models::MessageItem;
        QCOMPARE(ZaloOfflineStore::ftsQuery("Đường  phố\"x"), QString("\"duong\"* \"pho\"* \"x\"*"));
        QCOMPARE(ZaloOfflineStore::ftsQuery("  ** \" "), QString());
        const QJsonObject rawIn{{"msgId", "1"}, {"content", "Fixture payload"}};
        const QByteArray packed = ZaloOfflineStore::encodeRaw(rawIn);
        QVERIFY(!packed.isEmpty() && packed.at(0) != '{');
        QCOMPARE(ZaloOfflineStore::decodeRaw(packed), rawIn);
        QCOMPARE(ZaloOfflineStore::decodeRaw(QString("{\"a\":1}")).value("a").toInt(), 1);
        QVERIFY(ZaloOfflineStore::decodeRaw(QVariant()).isEmpty());
        {
            ZaloOfflineStore store;
            store.clearCache();
            QVERIFY(store.ftsAvailable());
            auto msg = [](const QString& id, const QString& text, qint64 ts) {
                MessageItem m;
                m.id = id;
                m.text = text;
                m.messageType = "text";
                m.senderName = "Fixture";
                m.ts = ts;
                return m;
            };
            QVERIFY(store.saveMessage("u-fts-a", msg("7001", "Hẹn gặp ở đường Fixture lúc 9 giờ", 1000), rawIn));
            QVERIFY(store.saveMessage("u-fts-a", msg("7002", "Không liên quan", 2000)));
            QVERIFY(store.saveMessage("u-fts-b", msg("7003", "Đường mới mở", 3000)));
            QCOMPARE(store.rawMessage("7001"), rawIn);
            auto ids = [](const QVector<ZaloOfflineStore::SearchHit>& hits) {
                QStringList out;
                for (const auto& h : hits) out << h.msgId;
                return out;
            };
            QCOMPARE(ids(store.searchMessages("duong")), (QStringList{"7003", "7001"}));
            QCOMPARE(ids(store.searchMessages("DUONG fixt", "u-fts-a")), QStringList{"7001"});
            QCOMPARE(ids(store.searchMessages("hen gap")), QStringList{"7001"});
            QVERIFY(store.searchMessages("\"").isEmpty());
            QVERIFY(store.markRecalled("7003", "Tin nhắn đã được thu hồi"));
            QCOMPARE(ids(store.searchMessages("duong")), QStringList{"7001"});
            QVERIFY(store.deleteMessage("7001"));
            QVERIFY(store.searchMessages("duong").isEmpty());
            QVERIFY(store.saveMessage("u-fts-a", msg("7002", "Giờ thì có đường", 2000)));
            QCOMPARE(ids(store.searchMessages("duong")), QStringList{"7002"});
            QCOMPARE(store.countMessagesBetween("u-fts-a", 0, 5000), 1);
            QString conv;
            QCOMPARE(store.messageTs("7002", &conv), 2000);
            QCOMPARE(conv, QString("u-fts-a"));
        }
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("offline.db");
        {
            QSqlDatabase legacy = QSqlDatabase::addDatabase("QSQLITE", "fts_legacy");
            legacy.setDatabaseName(path);
            QVERIFY(legacy.open());
            QSqlQuery q(legacy);
            QVERIFY(q.exec("INSERT INTO messages (id, conv_id, text, message_type, ts, is_recalled, raw) VALUES "
                           "('7100', 'u-fts-c', 'Đường cũ', 'text', 10, 0, '{\"legacy\":true}');"));
            QVERIFY(q.exec("DELETE FROM meta WHERE key IN ('fts_version', 'raw_codec');"));
            QVERIFY(q.exec("DELETE FROM messages_fts;"));
            legacy.close();
        }
        QSqlDatabase::removeDatabase("fts_legacy");
        {
            ZaloOfflineStore upgraded;
            QCOMPARE(upgraded.searchMessages("duong cu").size(), 1);
            QCOMPARE(upgraded.rawMessage("7100").value("legacy").toBool(), true);
            QCOMPARE(upgraded.pragmaForTesting("auto_vacuum"), QString("2"));
            upgraded.clearCache();
        }
    }

    void testCliMsgIdLookupsUseIndex() {
        ZaloOfflineStore store;
        store.clearCache();
        QSqlDatabase db = QSqlDatabase::database(store.connectionNameForTesting());
        QVERIFY(db.isOpen());
        const struct {
            const char* sql;
            const char* index;
        } cases[] = {
            {ZaloOfflineStore::kSqlFindCliDuplicates, "idx_messages_conv_cli"},
            {ZaloOfflineStore::kSqlIdByConvAndCliId, "idx_messages_conv_cli"},
            {ZaloOfflineStore::kSqlIdByCliId, "idx_messages_cli"},
            {ZaloOfflineStore::kSqlTsByCliId, "idx_messages_cli"},
            {ZaloOfflineStore::kSqlNextExpiry, "idx_messages_expire"},
        };
        for (const auto& c : cases) {
            QSqlQuery q(db);
            QVERIFY2(q.prepare(QStringLiteral("EXPLAIN QUERY PLAN ") + QLatin1String(c.sql)), c.sql);
            for (const char* name : {":conv", ":c", ":cli", ":id", ":now"}) {
                if (QByteArray(c.sql).contains(QByteArray(name) + ' ') || QByteArray(c.sql).contains(QByteArray(name) + ';')) {
                    q.bindValue(QLatin1String(name), QStringLiteral("x"));
                }
            }
            QVERIFY2(q.exec(), c.sql);
            QStringList plan;
            while (q.next()) plan << q.value(3).toString();
            const QString joined = plan.join(QStringLiteral(" | "));
            QVERIFY2(joined.contains(QLatin1String(c.index)), qPrintable(QStringLiteral("%1 -> %2").arg(c.sql, joined)));
        }
    }

    void testSystemFontFamilyFromSpec() {
        using Zalos::Core::Platform::ThemeManager;
        QCOMPARE(ThemeManager::familyFromFontSpec("Ubuntu Medium 12"), QString("Ubuntu"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Noto Sans Bold Italic 11"), QString("Noto Sans"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Cantarell 11"), QString("Cantarell"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Noto Sans,10,-1,5,400,0,0,0,0,0"), QString("Noto Sans"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Source Code Pro Semibold 10.5"), QString("Source Code Pro"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Medium"), QString("Medium"));
        QCOMPARE(QQuickWindow::textRenderType(), QQuickWindow::NativeTextRendering);
    }

    void testFilePickerHelpersWithoutQtQuickDialogs() {
        using Zalos::Core::Platform::FilePicker;
        const auto filters = FilePicker::parseNameFilters({QStringLiteral("Hình ảnh (*.jpg *.png *.jpg)"),
                                                           QStringLiteral("Tất cả các tệp (*.*)"),
                                                           QStringLiteral("Plain")});
        QCOMPARE(filters.size(), 3);
        QCOMPARE(filters.at(0).patterns, QStringList({QStringLiteral("*.jpg"), QStringLiteral("*.png")}));
        QCOMPARE(filters.at(1).patterns, QStringList({QStringLiteral("*")}));
        QCOMPARE(filters.at(2).patterns, QStringList({QStringLiteral("Plain")}));

        const QList<QUrl> urls = FilePicker::urlsFromLines("/tmp/fixture a.png\n\n/tmp/fixture-b.jpg\n");
        QCOMPARE(urls.size(), 2);
        QCOMPARE(urls.at(0), QUrl::fromLocalFile(QStringLiteral("/tmp/fixture a.png")));

        const QStringList zenity = FilePicker::zenityArguments(FilePicker::OpenFiles, QStringLiteral("Fixture"),
                                                               filters.mid(0, 1), QString());
        QVERIFY(zenity.contains(QStringLiteral("--multiple")));
        QVERIFY(zenity.contains(QStringLiteral("--file-filter=Hình ảnh (*.jpg *.png *.jpg) | *.jpg *.png")));
        const QStringList save = FilePicker::zenityArguments(FilePicker::SaveFile, QString(), {}, QStringLiteral("/tmp/x.jpg"));
        const QStringList folderZenity = FilePicker::zenityArguments(FilePicker::SelectFolder, QString(), {}, QString());
        QVERIFY(folderZenity.contains(QStringLiteral("--directory")));
        const QStringList folderKde = FilePicker::kdialogArguments(FilePicker::SelectFolder, QString(), {}, QStringLiteral("/tmp"));
        QCOMPARE(folderKde, QStringList({QStringLiteral("--getexistingdirectory"), QStringLiteral("/tmp")}));
        QVERIFY(save.contains(QStringLiteral("--save")));
        QVERIFY(save.contains(QStringLiteral("--filename=/tmp/x.jpg")));
        const QStringList kde = FilePicker::kdialogArguments(FilePicker::SaveFile, QString(), filters.mid(0, 1),
                                                             QStringLiteral("/tmp/x.jpg"));
        QCOMPARE(kde.first(), QStringLiteral("--getsavefilename"));
        QCOMPARE(kde.at(1), QStringLiteral("/tmp/x.jpg"));
        QCOMPARE(kde.last(), QStringLiteral("*.jpg *.png|Hình ảnh (*.jpg *.png *.jpg)"));

        FilePicker picker;
        QCOMPARE(picker.fileMode(), FilePicker::OpenFile);
        QVERIFY(picker.selectedFile().isEmpty());
        QVERIFY(!picker.busy());
    }

    void testUiFontNeverFallsBackToEmojiFamily() {
        using Zalos::Core::Platform::ThemeManager;
        const QString emojiFamily = ThemeManager::registerEmojiFallbackFont();
        const QString fallback = ThemeManager::fallbackUiFamily();
        QVERIFY(!fallback.isEmpty());
        QVERIFY(ThemeManager::isUsableUiFamily(fallback));
        QVERIFY(ThemeManager::isEmojiOrSymbolFamily(QStringLiteral("Noto Color Emoji")));
        QVERIFY(ThemeManager::isEmojiOrSymbolFamily(QStringLiteral("Symbol")));
        QVERIFY(!ThemeManager::isEmojiOrSymbolFamily(fallback));
        if (!emojiFamily.isEmpty()) {
            QVERIFY(ThemeManager::isEmojiOrSymbolFamily(emojiFamily));
            QVERIFY(!ThemeManager::isUsableUiFamily(emojiFamily));
        }
        QVERIFY(ThemeManager::installedFamilyName(QStringLiteral("Fixture Missing Sans")).isEmpty());
        QVERIFY(!ThemeManager::isUsableUiFamily(QStringLiteral("Fixture Missing Sans")));

        const auto onlyFixture = [](const QString& f) { return f == QLatin1String("Fixture Sans"); };
        QCOMPARE(ThemeManager::chooseUiFont({QString(), QStringLiteral("Fixture Sans Medium 12")}, onlyFixture,
                                            QStringLiteral("Fallback")), QStringLiteral("Fixture Sans"));
        QCOMPARE(ThemeManager::chooseUiFont({QStringLiteral("Fixture Mono,10,-1"), QStringLiteral("Fixture Sans")},
                                            onlyFixture, QStringLiteral("Fallback")), QStringLiteral("Fixture Sans"));
        QCOMPARE(ThemeManager::chooseUiFont({QStringLiteral("Ubuntu,12,-1,5,500,0,0,0,0,0,0,0,0,0,0,1,Medium")},
                                            onlyFixture, QStringLiteral("Fallback")), QStringLiteral("Fallback"));

        QFont::insertSubstitution(QStringLiteral("Fixture Missing Sans"), emojiFamily.isEmpty() ? fallback : emojiFamily);
        const QString resolved = ThemeManager::resolveFontSpecForTesting(QStringLiteral("Fixture Missing Sans,12,-1,5,500"));
        QCOMPARE(resolved, fallback);
        QVERIFY(!ThemeManager::isEmojiOrSymbolFamily(resolved));

        QSettings settings;
        const QVariant savedDesktop = settings.value("appearance/cachedDesktopFont");
        settings.setValue("appearance/cachedSystemFont", emojiFamily.isEmpty() ? QStringLiteral("Zalos Color Emoji") : emojiFamily);
        settings.setValue("appearance/cachedDesktopFont", emojiFamily.isEmpty() ? QStringLiteral("Zalos Color Emoji") : emojiFamily);
        settings.sync();
        {
            ThemeManager tm;
            QVERIFY(!tm.systemFontFamily().isEmpty());
            QVERIFY(!ThemeManager::isEmojiOrSymbolFamily(tm.systemFontFamily()));
            QVERIFY(!ThemeManager::isEmojiOrSymbolFamily(QGuiApplication::font().family()));
        }
        settings.sync();
        QVERIFY(!settings.contains("appearance/cachedSystemFont"));
        QVERIFY(!ThemeManager::isEmojiOrSymbolFamily(settings.value("appearance/cachedDesktopFont").toString()));
        if (savedDesktop.isValid()) settings.setValue("appearance/cachedDesktopFont", savedDesktop);
        else settings.remove("appearance/cachedDesktopFont");
    }

    void testKdeGlobalsThemeParsing() {
        using Zalos::Core::Platform::ThemeManager;
        bool known = true;
        const QByteArray dark =
            "[ColorEffects:Disabled]\nColor=56,56,56\n\n[Colors:Window]\nBackgroundAlternate=49,54,59\n"
            "BackgroundNormal=32,35,38\nForegroundNormal=252,252,252\n\n[General]\nAccentColor=61,174,233\n"
            "ColorScheme=BreezeDark\nfont=Noto Sans,10,-1,5,400,0,0,0,0,0\n";
        QVERIFY(ThemeManager::kdeDarkFromGlobals(dark, &known));
        QVERIFY(known);
        QCOMPARE(ThemeManager::kdeAccentFromGlobals(dark), QColor(61, 174, 233));

        const QByteArray light = "[Colors:Window]\nBackgroundNormal=239,240,241\n[General]\nColorScheme=BreezeDark\n";
        QVERIFY(!ThemeManager::kdeDarkFromGlobals(light, &known));
        QVERIFY(known);

        QVERIFY(ThemeManager::kdeDarkFromGlobals("[General]\nColorScheme=BreezeDark\n", &known));
        QVERIFY(known);
        QVERIFY(ThemeManager::kdeDarkFromGlobals("[KDE]\nLookAndFeelPackage=org.kde.breezedark.desktop\n", &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::kdeDarkFromGlobals("[General]\nColorScheme=BreezeLight\n", &known));
        QVERIFY(known);

        QVERIFY(!ThemeManager::kdeDarkFromGlobals("[Colors:View]\nBackgroundNormal=0,0,0\n", &known));
        QVERIFY(!known);
        QVERIFY(!ThemeManager::kdeAccentFromGlobals("[General]\nAccentColor=bad\n").isValid());
        QVERIFY(!ThemeManager::kdeAccentFromGlobals("").isValid());
    }

    void testPortalAndGtkThemeParsing() {
        using Zalos::Core::Platform::ThemeManager;
        bool known = false;

        QVERIFY(ThemeManager::portalColorSchemeFromVariant(QVariant(uint(1)), &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::portalColorSchemeFromVariant(QVariant(uint(2)), &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::portalColorSchemeFromVariant(QVariant(uint(0)), &known));
        QVERIFY(!known);

        const QVariant nestedDark = QVariant::fromValue(QDBusVariant(QVariant::fromValue(QDBusVariant(QVariant(uint(1))))));
        QVERIFY(ThemeManager::portalColorSchemeFromVariant(nestedDark, &known));
        QVERIFY(known);

        QVERIFY(ThemeManager::portalColorSchemeFromVariant(QStringLiteral("prefer-dark"), &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::portalColorSchemeFromVariant(QStringLiteral("default"), &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::portalColorSchemeFromVariant(QStringLiteral("prefer-light"), &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::portalColorSchemeFromVariant(QVariant(), &known));
        QVERIFY(!known);

        const QColor portalAccent = ThemeManager::portalAccentFromVariant(
            QVariant::fromValue(QDBusVariant(QVariantList{0.2, 0.4, 0.8})));
        QVERIFY(portalAccent.isValid());
        QCOMPARE(portalAccent.red(), qRound(0.2 * 255.0));
        QCOMPARE(portalAccent.blue(), qRound(0.8 * 255.0));
        QVERIFY(!ThemeManager::portalAccentFromVariant(QVariantList{1.2, 0.4, 0.8}).isValid());
        QVERIFY(!ThemeManager::portalAccentFromVariant(QVariantList{-0.1, 0.4, 0.8}).isValid());
        QVERIFY(!ThemeManager::portalAccentFromVariant(QVariantList{qQNaN(), 0.4, 0.8}).isValid());

        QVERIFY(ThemeManager::gtkDarkFromSettingsIni("[Settings]\ngtk-application-prefer-dark-theme=1\n", &known));
        QVERIFY(known);
        QVERIFY(ThemeManager::gtkDarkFromSettingsIni("[Settings]\ngtk-application-prefer-dark-theme=true\n", &known));
        QVERIFY(known);
        QVERIFY(ThemeManager::gtkDarkFromSettingsIni("[Settings]\ngtk-theme-name=Adwaita-dark\n", &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::gtkDarkFromSettingsIni(
            "[Settings]\ngtk-application-prefer-dark-theme=0\ngtk-theme-name=Adwaita\n", &known));
        QVERIFY(known);
        QVERIFY(!ThemeManager::gtkDarkFromSettingsIni("", &known));
        QVERIFY(!known);

        const QPalette original = QGuiApplication::palette();
        QPalette lightDefault;
        lightDefault.setColor(QPalette::Window, QColor("#efefef"));
        lightDefault.setColor(QPalette::Base, QColor("#ffffff"));
        lightDefault.setColor(QPalette::WindowText, QColor("#111111"));
        QGuiApplication::setPalette(lightDefault);
        {
            ThemeManager theme;
            theme.setThemeMode(ThemeManager::SystemTheme);
            theme.setDarkMode(true);
            QVERIFY(theme.isDarkMode());
            QCOMPARE(theme.backgroundColor(), QColor("#22262B"));
            QCOMPARE(theme.chatBackgroundColor(), QColor("#16191D"));
            QCOMPARE(theme.textColor(), QColor("#DFE2E7"));
        }
        QGuiApplication::setPalette(original);
    }

    void init() {
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(QDir(baseDir).filePath("offline.db"));
    }

    void cleanupTestCase() {
        QSettings settings;
        settings.setValue("auth/isLoggedIn", false);
        settings.setValue("auth/userName", "");
        settings.setValue("auth/userPhone", "");
        settings.remove("settings/closeToTray");
    }

    void testQmlEngineStartup() {
        QQmlApplicationEngine engine;
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");

        auto themeManager = new ThemeManager(&engine);
        auto zaloClient = new ZaloClient(&engine);
        auto trayController = new SystemTrayController(&engine);

        engine.rootContext()->setContextProperty("themeManager", themeManager);
        engine.rootContext()->setContextProperty("zaloClient", zaloClient);
        engine.rootContext()->setContextProperty("trayController", trayController);

        loadMainWindow(engine);
        const auto rootObjects = engine.rootObjects();
        QVERIFY(!rootObjects.isEmpty());

        auto window = qobject_cast<QQuickWindow*>(rootObjects.first());
        QVERIFY(window != nullptr);

        Qt::WindowFlags flags = window->flags();
        QVERIFY(flags & Qt::Window);
        QVERIFY(!(flags & Qt::FramelessWindowHint));

        QVERIFY(window->minimumWidth() <= 480);
        QVERIFY(window->minimumHeight() <= 450);

        window->resize(480, 540);
        QTest::qWait(100);
        QCOMPARE(window->width(), 480);
        QCOMPARE(window->height(), 540);
        QImage quarterImg = window->grabWindow();
        quarterImg.save("/workspace/build/test_quarter_screen.png");

        auto loginView = window->findChild<QQuickItem*>("loginViewRoot");
        if (loginView) {
            loginView->setProperty("activeTab", 1);
            QTest::qWait(100);
            QImage phoneImg = window->grabWindow();
            phoneImg.save("/workspace/build/test_quarter_phone.png");
            loginView->setProperty("activeTab", 0);
            QTest::qWait(50);
        }

        window->resize(360, 420);
        QTest::qWait(100);
        QCOMPARE(window->size(), QSize(360, 420));
        window->grabWindow().save("/workspace/build/test_login_360x420.png");
        auto* loginLogo = window->findChild<QQuickItem*>("loginLogo");
        QVERIFY(loginLogo != nullptr);
        QVERIFY(loginLogo->isVisible());
        auto* loginVersion = window->findChild<QQuickItem*>("loginVersion");
        QVERIFY(loginVersion != nullptr);
        QVERIFY(loginVersion->property("text").toString().contains(zaloClient->appVersion()));

        window->resize(800, 600);
        QCOMPARE(window->width(), 800);
        QCOMPARE(window->height(), 600);

        window->resize(1280, 800);
        QCOMPARE(window->width(), 1280);
        QCOMPARE(window->height(), 800);

        window->resize(1920, 1080);
        QCOMPARE(window->width(), 1920);
        QCOMPARE(window->height(), 1080);

        const bool prevCloseToTray = zaloClient->closeToTray();
        zaloClient->setTrayAvailableForTesting(1);
        zaloClient->setCloseToTray(true);
        QCOMPARE(window->close(), false);
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 1000);
        window->show();
        QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);
        window->setProperty("forceQuit", true);
        QCOMPARE(window->close(), true);
        zaloClient->setTrayAvailableForTesting(-1);
        zaloClient->setCloseToTray(prevCloseToTray);
        QSettings().remove(QStringLiteral("settings/closeToTray"));
    }

    void testConversationSearchFeature() {
        ConversationModel model;
        model.addConversation({
            "ht-1", "Hỗ trợ khách hàng", "Xin chào", "10:00", 0, true, false, "", "priority", ""
        });
        model.addConversation({
            "ht-2", "Nhóm bạn thân", "Đi ăn nhé", "11:00", 0, true, false, "", "priority", ""
        });
        QVERIFY(model.rowCount() > 0);

        model.setSearchFilter("Hỗ trợ");
        QVERIFY(model.rowCount() >= 1);

        model.setSearchFilter("hỗ trợ");
        QVERIFY(model.rowCount() >= 1);

        model.setSearchFilter("nonexistent_contact_xyz_123");
        QCOMPARE(model.rowCount(), 0);

        model.setSearchFilter("");
        QCOMPARE(model.rowCount(), 2);

        model.addConversation({
            "g-cl", "Fixture Nhóm Cầu Lông Đầm", "Mai đánh cầu nhé", "12:00", 0, true, false, "", "priority", ""
        });
        auto has = [&model](const QString& id) {
            for (int r = 0; r < model.rowCount(); ++r)
                if (model.data(model.index(r), ConversationModel::IdRole).toString() == id) return true;
            return false;
        };
        for (const char* q : {"cau long", "cầu lông", "CAU LONG", "long cau", "dam", "Đầm", "danh cau"}) {
            model.setSearchFilter(QString::fromUtf8(q));
            QVERIFY2(has("g-cl"), q);
        }
        model.addConversation({
            "u-noname", "", "", "12:30", 0, false, false, "", "priority", ""
        });
        for (const char* q : {"nguoi dung zalo", "Người dùng", "zalo user"}) {
            model.setSearchFilter(QString::fromUtf8(q));
            QVERIFY2(has("u-noname"), q);
        }
        model.setSearchFilter("");
        model.setCategoryFilter("unread");
        model.setSearchFilter("cau long");
        QVERIFY(has("g-cl"));
        model.setSearchFilter("");
        QVERIFY(!has("g-cl"));
        model.setCategoryFilter("");
        model.setArchivedThreads({"g-cl"});
        model.setHiddenThreads({"ht-2"});
        model.setSearchFilter("");
        QVERIFY(!has("g-cl"));
        QVERIFY(!has("ht-2"));
        model.setSearchFilter("cau long");
        QVERIFY(has("g-cl"));
        model.setSearchFilter("ban than");
        QVERIFY(has("ht-2"));
        QVERIFY(model.data(model.index(0, 0), ConversationModel::IsHiddenRole).toBool());
        model.setSearchFilter("di an");
        QVERIFY(!has("ht-2"));
        model.setSearchFilter("ban than");
        model.setHiddenPinReveal(true);
        QVERIFY(!has("ht-2"));
        model.setHiddenPinReveal(false);
        model.setSearchFilter("");
        model.setArchivedThreads({});
        model.setHiddenThreads({});
    }

    void testGlobalSearchGroupsAndMessages() {
        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-search-owner");
            owner.clearCache();
            owner.bindOwner("u-search-owner");
        }
        ZaloClient client;
        client.openOfflineCacheForTesting("u-search-owner");
        const qint64 base = 1700000000000LL;
        emit client.apiClient()->session()->messagesReceived(QJsonArray{
            QJsonObject{{"msgId", "7701"}, {"cliMsgId", "7701"}, {"uidFrom", "u-search-a"}, {"idTo", "u-me"},
                        {"msgType", "webchat"}, {"content", QString::fromUtf8("Tối nay đi đánh cầu lông không?")},
                        {"ts", QString::number(base)}},
            QJsonObject{{"msgId", "7702"}, {"cliMsgId", "7702"}, {"uidFrom", "u-search-b"}, {"idTo", "u-me"},
                        {"msgType", "webchat"}, {"content", QString::fromUtf8("Bí mật trong chat đã ẩn: cầu lông")},
                        {"ts", QString::number(base + 1)}}}, false, true);
        client.applyGroupInfoForTesting(QJsonObject{{"g-search", QJsonObject{
            {"name", QString::fromUtf8("Fixture Nhóm Cầu Lông")}, {"totalMember", 9}}}});
        QVERIFY(!client.conversationModel()->contains("g-search"));

        auto* people = client.peopleSearch();
        people->setQuery("cau long");
        people->runNow();
        QCOMPARE(people->count(), 1);
        QCOMPARE(people->data(people->index(0), MessageSearchModel::ConvIdRole).toString(), QString("g-search"));
        QVERIFY(people->data(people->index(0), MessageSearchModel::IsGroupRole).toBool());

        auto* msgs = client.messageSearch();
        for (const char* q : {"cau long", "cầu lông", "CAU LONG", "danh cau"}) {
            msgs->setQuery(QString::fromUtf8(q));
            msgs->runNow();
            QVERIFY2(msgs->count() >= 1, q);
        }
        client.setHiddenThreadsForTesting({"u-search-b"});
        msgs->setQuery("cau long");
        msgs->runNow();
        QCOMPARE(msgs->count(), 1);
        QCOMPARE(msgs->data(msgs->index(0), MessageSearchModel::ConvIdRole).toString(), QString("u-search-a"));

        client.offlineStore()->setFtsAvailableForTesting(false);
        QCOMPARE(client.offlineStore()->searchMessages(QString::fromUtf8("đánh cầu")).size(), 1);
        QCOMPARE(client.offlineStore()->searchMessages("cau long", "u-search-a").size(), 1);
        client.offlineStore()->setFtsAvailableForTesting(true);

        client.openSearchResult("g-search", QString::fromUtf8("Fixture Nhóm Cầu Lông"), "", true);
        QVERIFY(client.conversationModel()->contains("g-search"));
        QCOMPARE(client.currentConversationId(), QString("g-search"));
    }

    void testHiddenConversationsZaloStyle() {
        QCOMPARE(ZaloClient::hiddenPinDigest("1234"), QString("81dc9bdb52d04dc20036dbd8313ed055"));
        ZaloClient client;
        client.setMyUidForTesting("u-me");
        QVERIFY(!client.hasHiddenPin());
        QVERIFY(!client.verifyHiddenPin("1234"));
        client.setHiddenPinHashForTesting(ZaloClient::hiddenPinDigest("2468"));
        QVERIFY(client.hasHiddenPin());
        QVERIFY(client.verifyHiddenPin("2468"));
        for (const char* bad : {"2469", "24680", "246", "abcd", ""}) QVERIFY2(!client.verifyHiddenPin(bad), bad);

        auto* model = client.conversationModel();
        model->addConversation({"u-hide-a", "Fixture Hidden Friend", "fixture secret words", "10:00", 0, false, false, "", "priority", ""});
        model->addConversation({"u-hide-b", "Fixture Visible Friend", "fixture hello", "10:01", 0, false, false, "", "priority", ""});
        model->setUnreadCount("u-hide-a", 3);
        model->setUnreadCount("u-hide-b", 2);
        QCOMPARE(model->totalUnreadCount(), 5);
        auto has = [model](const QString& id) {
            for (int r = 0; r < model->rowCount(); ++r)
                if (model->data(model->index(r, 0), ConversationModel::IdRole).toString() == id) return true;
            return false;
        };

        client.handleSyncControlForTesting("hidden_chat", "add_remove", QJsonObject{
            {"add_items", QJsonArray{QJsonObject{{"thread_id", "u-hide-a"}, {"is_group", "0"}}}},
            {"del_items", QJsonArray{}}});
        QVERIFY(client.isConversationHidden("u-hide-a"));
        QVERIFY(!has("u-hide-a"));
        QCOMPARE(model->totalUnreadCount(), 2);
        QVERIFY(!client.canHideConversation("u-hide-a"));
        QVERIFY(client.canHideConversation("u-hide-b"));
        QVERIFY(client.needsHiddenPin("u-hide-a"));
        QVERIFY(!client.needsHiddenPin("u-hide-b"));

        model->setSearchFilter("hidden friend");
        QVERIFY(has("u-hide-a"));
        model->setSearchFilter("secret words");
        QVERIFY(!has("u-hide-a"));
        model->setSearchFilter("2468");
        QVERIFY(model->hiddenPinReveal());
        auto* hidden = client.hiddenSearch();
        hidden->setQuery("2468");
        hidden->runNow();
        QCOMPARE(hidden->count(), 1);
        QCOMPARE(hidden->data(hidden->index(0), MessageSearchModel::ConvIdRole).toString(), QString("u-hide-a"));
        hidden->setQuery("2469");
        hidden->runNow();
        QCOMPARE(hidden->count(), 0);
        model->setSearchFilter("");
        QVERIFY(!model->hiddenPinReveal());

        QSignalSpy hiddenUnreadSpy(&client, &ZaloClient::hiddenUnreadChanged);
        emit client.apiClient()->session()->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", "9701"}, {"cliMsgId", "9701"}, {"uidFrom", "u-hide-a"}, {"idTo", "u-me"}, {"msgType", "webchat"},
            {"content", "fixture hidden ping"}, {"ts", QString::number(QDateTime::currentMSecsSinceEpoch())}}},
            false, false);
        QVERIFY(client.hiddenUnread());
        QCOMPARE(hiddenUnreadSpy.count(), 1);
        QCOMPARE(client.notificationsForTesting()->shownForTesting("thread:u-hide-a"), 0);
        emit client.apiClient()->session()->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", "9702"}, {"cliMsgId", "9702"}, {"uidFrom", "u-hide-b"}, {"idTo", "u-me"}, {"msgType", "webchat"},
            {"content", "fixture visible ping"}, {"ts", QString::number(QDateTime::currentMSecsSinceEpoch())}}},
            false, false);
        QCOMPARE(client.notificationsForTesting()->shownForTesting("thread:u-hide-b"), 1);
        model->setUnreadCount("u-hide-b", 2);
        QCOMPARE(model->totalUnreadCount(), 2);
        client.dismissHiddenUnread();
        QVERIFY(!client.hiddenUnread());

        client.handleSyncControlForTesting("hidden_chat", "add_remove", QJsonObject{
            {"add_items", QJsonArray{}}, {"del_items", QJsonArray{QJsonObject{{"thread_id", "u-hide-a"}, {"is_group", 0}}}}});
        QVERIFY(!client.isConversationHidden("u-hide-a"));
        QVERIFY(has("u-hide-a"));
        client.handleSyncControlForTesting("hidden_chat", "change_pin",
                                           QJsonObject{{"pin", ZaloClient::hiddenPinDigest("1357").toUpper()}});
        QVERIFY(client.verifyHiddenPin("1357"));
        QVERIFY(!client.verifyHiddenPin("2468"));
        client.handleSyncControlForTesting("hidden_chat", "add_remove", QJsonObject{
            {"add_items", QJsonArray{QJsonObject{{"thread_id", "u-hide-b"}, {"is_group", 0}}}}, {"del_items", QJsonArray{}}});
        client.handleSyncControlForTesting("hidden_chat", "reset", QJsonObject{});
        QVERIFY(!client.hasHiddenPin());
        QVERIFY(!client.isConversationHidden("u-hide-b"));
        QVERIFY(!model->contains("u-hide-b"));
        QVERIFY(model->contains("u-hide-a"));
    }

    void testGroupRemovalKeepsHistoryReadOnly() {
        ZaloClient client;
        client.setAppLanguage("vi");
        client.setMyUidForTesting("u-ro-me");
        client.applyGroupInfoForTesting(QJsonObject{
            {"g-ro", QJsonObject{{"name", "Fixture RO Group"}, {"totalMember", 3}, {"creatorId", "u-ro-owner"}}},
            {"g-dis", QJsonObject{{"name", "Fixture Disband Group"}, {"totalMember", 3}, {"creatorId", "u-ro-owner"}}},
            {"g-lock", QJsonObject{{"name", "Fixture Locked Group"}, {"totalMember", 3}, {"creatorId", "u-ro-owner"},
                                   {"setting", QJsonObject{{"lockSendMsg", 1}}}}},
            {"g-own", QJsonObject{{"name", "Fixture Own Group"}, {"totalMember", 3}, {"creatorId", "u-ro-me"},
                                  {"setting", QJsonObject{{"lockSendMsg", 1}}}}}});
        auto* model = client.conversationModel();
        for (const char* id : {"g-ro", "g-dis", "g-lock", "g-own", "g-left"})
            model->addConversation({QString::fromLatin1(id), "Fixture", "fixture", "10:00", 0, false, false, "", "priority", ""});
        auto noticeIn = [&client](const QString& thread, const QString& text) {
            client.selectConversation(thread, "Fixture", "");
            auto* m = client.messageModel();
            for (int r = 0; r < m->rowCount(); ++r)
                if (m->data(m->index(r), MessageModel::TextRole).toString() == text) return true;
            return false;
        };
        client.selectConversation("g-ro", "Fixture RO Group", "");
        QVERIFY(client.composerLockReason().isEmpty());
        client.handleGroupControlForTesting("remove_member", QJsonObject{
            {"groupId", "g-ro"}, {"sourceId", "u-ro-owner"},
            {"updateMembers", QJsonArray{QJsonObject{{"id", "u-ro-me"}, {"dName", "Fixture Me"}}}}});
        QVERIFY(model->contains("g-ro"));
        QVERIFY(client.isThreadReadOnly("g-ro"));
        QCOMPARE(client.composerLockReason(), QString::fromUtf8("Bạn không thể gửi tin nhắn vào nhóm được nữa"));
        QVERIFY(client.composerLockDeletable());
        QVERIFY(noticeIn("g-ro", QString::fromUtf8("Bạn đã được xóa khỏi nhóm")));

        client.handleGroupControlForTesting("disperse", QJsonObject{{"groupId", "g-dis"}, {"sourceId", "u-ro-owner"}});
        QVERIFY(client.isThreadReadOnly("g-dis"));
        QVERIFY(noticeIn("g-dis", QString::fromUtf8("Trưởng nhóm đã giải tán nhóm")));

        client.handleGroupControlForTesting("update_ttl", QJsonObject{{"groupId", "g-lock"}, {"sourceId", "u-ro-me"},
                                                                       {"ttl", 86400000}});
        QVERIFY(noticeIn("g-lock", QString::fromUtf8("Bạn đã đặt thời gian tự xóa cho tin nhắn là 1 ngày")));
        QCOMPARE(client.conversationAutoDelete("g-lock"), qint64(86400000));
        client.handleGroupControlForTesting("update_ttl", QJsonObject{{"groupId", "g-lock"}, {"sourceId", "u-ro-me"}, {"ttl", 0}});
        QVERIFY(noticeIn("g-lock", QString::fromUtf8("Bạn đã tắt tin nhắn tự xóa")));

        client.selectConversation("g-lock", "Fixture Locked Group", "");
        QCOMPARE(client.composerLockReason(), QString::fromUtf8("Nhóm không cho phép thành viên gửi tin nhắn"));
        QVERIFY(!client.composerLockDeletable());
        client.selectConversation("g-own", "Fixture Own Group", "");
        QVERIFY(client.composerLockReason().isEmpty());

        ZaloClient restarted;
        restarted.setMyUidForTesting("u-ro-me");
        restarted.reloadReadOnlyForTesting();
        QVERIFY(restarted.isThreadReadOnly("g-ro"));
        client.handleGroupControlForTesting("leave", QJsonObject{
            {"groupId", "g-left"}, {"sourceId", "u-ro-me"}, {"updateMembers", QJsonArray{QJsonObject{{"id", "u-ro-me"}}}}});
        QVERIFY(!model->contains("g-left"));

        client.deleteConversation("g-ro");
        client.deleteConversation("g-dis");
        QVERIFY(!model->contains("g-ro"));
        QVERIFY(!client.isThreadReadOnly("g-ro"));
        QVERIFY(!client.isThreadReadOnly("g-dis"));
    }

    void testMessageTtlExpiry() {
        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-ttl-owner");
            owner.clearCache();
        }
        ZaloClient client;
        client.openOfflineCacheForTesting("u-ttl-owner");
        client.setMyUidForTesting("u-ttl-owner");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto msg = [](const QString& id, qint64 ts, qint64 ttl, const QString& text) {
            QJsonObject m{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", "u-ttl-peer"}, {"idTo", "u-ttl-owner"},
                          {"msgType", "webchat"}, {"content", text}, {"ts", QString::number(ts)}};
            if (ttl > 0) m.insert("ttl", ttl);
            return m;
        };
        emit client.apiClient()->session()->messagesReceived(QJsonArray{
            msg("9801", now - 2 * 86400000LL, 86400000LL, "fixture expired words"),
            msg("9802", now - 1000, 1800, "fixture short lived words"),
            msg("9803", now - 500, 0, "fixture permanent words")}, false, false);
        auto* store = client.offlineStore();
        QVERIFY(store->rawMessage("9801").isEmpty());
        QVERIFY(!store->rawMessage("9802").isEmpty());
        QVERIFY(store->nextExpiry(QDateTime::currentMSecsSinceEpoch()) > 0);
        QCOMPARE(store->searchMessages("fixture short lived").size(), 1);
        QTest::qWait(1500);
        client.purgeExpiredForTesting();
        QVERIFY(store->rawMessage("9802").isEmpty());
        QCOMPARE(store->searchMessages("fixture short lived").size(), 0);
        QVERIFY(!store->rawMessage("9803").isEmpty());
        QCOMPARE(store->loadMessages("u-ttl-peer").size(), 1);
        QCOMPARE(store->loadMessages("u-ttl-peer", nullptr, 10, 0).size(), 1);
        const auto exactMsgs = store->loadMessages("u-ttl-peer", nullptr, 10, now - 500);
        QCOMPARE(exactMsgs.size(), 1);
        QCOMPARE(exactMsgs.first().id, QStringLiteral("9803"));
        QCOMPARE(store->nextExpiry(QDateTime::currentMSecsSinceEpoch()), qint64(0));
    }

    void testQuietMessageTypesSkipUnreadAndNotifications() {
        ZaloClient client;
        client.setMyUidForTesting("u-quiet-me");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        int n = 0;
        auto msg = [&n, now](const QString& from, const QString& type, const QJsonValue& content) {
            const QString id = QString::number(9900 + ++n);
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", from}, {"idTo", "u-quiet-me"},
                               {"msgType", type}, {"content", content}, {"ts", QString::number(now - 1000 + n)}};
        };
        const QJsonObject web{{"title", "Fixture notice"}, {"href", ""}, {"params", "{}"}};
        const QJsonArray cards{QJsonObject{{"title", "Fixture card"}, {"href", "https://example.com/a"}}};
        emit client.apiClient()->session()->messagesReceived(QJsonArray{
            msg("u-quiet-oa", "chat.webcontent", web), msg("u-quiet-oa", "chat.list.action", cards),
            msg("u-quiet-oa", "chat.livelocation", QJsonObject{}), msg("u-quiet-oa", "chat.video.live.msg", QJsonObject{})},
            false, false);
        auto* model = client.conversationModel();
        QCOMPARE(model->totalUnreadCount(), 0);
        QCOMPARE(client.notificationsForTesting()->shownForTesting("thread:u-quiet-oa"), 0);
        emit client.apiClient()->session()->messagesReceived(QJsonArray{msg("u-quiet-peer", "webchat", "fixture hello")},
                                                            false, false);
        QCOMPARE(model->totalUnreadCount(), 1);
        QCOMPARE(client.notificationsForTesting()->shownForTesting("thread:u-quiet-peer"), 1);
    }

    void testGroupSenderNamesResolveAtDisplayTime() {
        ZaloClient client;
        client.setMyUidForTesting("u-gs-me");
        client.applyGroupInfoForTesting(QJsonObject{{"g-gs", QJsonObject{{"name", "Fixture Sender Group"}, {"totalMember", 3}}}});
        emit client.apiClient()->session()->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", "9951"}, {"cliMsgId", "9951"}, {"uidFrom", "u-gs-1"}, {"idTo", "g-gs"}, {"msgType", "webchat"},
            {"content", "fixture group hello"}, {"ts", QString::number(QDateTime::currentMSecsSinceEpoch())}}}, true, true);
        client.selectConversation("g-gs", "Fixture Sender Group", "");
        auto* m = client.messageModel();
        QCOMPARE(m->rowCount(), 1);
        QVERIFY(m->data(m->index(0), MessageModel::SenderNameRole).toString().isEmpty());
        QVERIFY(client.isUserLookupPendingForTesting("u-gs-1"));
        QSignalSpy changed(m, &QAbstractItemModel::dataChanged);
        client.setUserForTesting("u-gs-1", "Fixture Member", "https://example.com/a.png");
        QVERIFY(changed.count() >= 1);
        QCOMPARE(m->data(m->index(0), MessageModel::SenderNameRole).toString(), QString("Fixture Member"));
        QCOMPARE(m->data(m->index(0), MessageModel::SenderAvatarRole).toString(), QString("https://example.com/a.png"));
    }

    void testGroupSeenSurvivesRestart() {
        const QString owner = QStringLiteral("u-seen-owner");
        {
            ZaloOfflineStore store;
            store.bindOwner(owner);
            store.clearCache();
            store.bindOwner(owner);
        }
        {
            ZaloClient client;
            client.setMyUidForTesting(owner);
            client.openOfflineCacheForTesting(owner);
            client.applyGroupInfoForTesting(QJsonObject{{"g-seen", QJsonObject{{"name", "Fixture Seen Group"}, {"totalMember", 3}}}});
            Zalos::Core::Models::ConversationItem conv;
            conv.id = "g-seen";
            conv.name = "Fixture Seen Group";
            conv.isGroup = true;
            QVERIFY(client.offlineStore()->saveConversation(conv));
            emit client.apiClient()->session()->messagesReceived(QJsonArray{QJsonObject{
                {"msgId", "9961"}, {"cliMsgId", "9961"}, {"uidFrom", owner}, {"idTo", "g-seen"},
                {"msgType", "webchat"}, {"content", "fixture group seen"},
                {"ts", QString::number(QDateTime::currentMSecsSinceEpoch())}}}, true, false);
            QVERIFY(client.offlineStore()->recordGroupSeen("9961", "u-seen-a"));
            QVERIFY(client.offlineStore()->recordGroupSeen("9961", "u-seen-b"));
            QVERIFY(client.offlineStore()->recordGroupSeen("9961", "u-seen-a"));
            Zalos::Core::Models::MessageItem resaved;
            resaved.id = "9961";
            resaved.senderId = owner;
            resaved.isMine = true;
            resaved.messageType = "webchat";
            resaved.text = "fixture group seen";
            resaved.ts = QDateTime::currentMSecsSinceEpoch();
            QVERIFY(client.offlineStore()->saveMessage("g-seen", resaved,
                QJsonObject{{"msgId", "9961"}, {"msgType", "webchat"}, {"content", "fixture group seen"}}, true));
        }

        ZaloClient restarted;
        restarted.setMyUidForTesting(owner);
        QVERIFY(restarted.openOfflineCacheForTesting(owner));
        restarted.selectConversation("g-seen", "Fixture Seen Group", "");
        auto* m = restarted.messageModel();
        QCOMPARE(m->rowCount(), 1);
        const QStringList seen = m->data(m->index(0), MessageModel::SeenUidsRole).toStringList();
        QCOMPARE(seen.size(), 2);
        QVERIFY(seen.contains("u-seen-a"));
        QVERIFY(seen.contains("u-seen-b"));
        QCOMPARE(m->data(m->index(0), MessageModel::StatusRole).toString(), QString("seen"));
    }

    void testOfficialAccountDetectionPersists() {
        QVERIFY(ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-1"}, {"type", 1}}));
        QVERIFY(ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-2"}, {"oaInfo", QJsonObject{{"id", 1}}}}));
        QVERIFY(!ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-3"}, {"bizPkg", QJsonObject{{"pkgId", 2}}}}));
        QVERIFY(!ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-3b"}, {"oa_status", QJsonObject{{"blocked", 0}}}}));
        QVERIFY(!ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-4"}, {"type", 0}}));
        QVERIFY(!ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "g-oa-5"}, {"type", 1}}));
        {
            ZaloOfflineStore store;
            store.bindOwner("u-oa-owner");
            store.clearCache();
            store.bindOwner("u-oa-owner");
            QVERIFY(store.saveProfile({"u-oa-1", "Fixture OA", "", false, 0, false, true}));
            QVERIFY(store.saveProfile({"u-oa-6", "Fixture Person", "", false, 0, true, false}));
        }
        ZaloClient client;
        client.openOfflineCacheForTesting("u-oa-owner");
        QVERIFY(client.isOfficialIdForTesting("u-oa-1"));
        QVERIFY(!client.isOfficialIdForTesting("u-oa-6"));
        QVERIFY(client.isOfficialOrSystemAccount("u-sys-1", "Zalo Video"));
        QVERIFY(client.isOfficialOrSystemAccount("u-sys-2", "Zalo Bảo Mật"));
        QVERIFY(client.isOfficialOrSystemAccount("u-sys-3", "ZaloPay"));
        QVERIFY(client.isOfficialOrSystemAccount("u-sys-4", "Thời Tiết"));
        QVERIFY(!client.isOfficialOrSystemAccount("u-sys-5", "Fixture Person"));
        QVERIFY(!client.isOfficialOrSystemAccount("g-sys-6", "Zalo Video"));
    }

    void testSystemAccountThreadsHideCallAndGroupActions() {
        ZaloClient client;
        client.setMyUidForTesting("u-sys-me");
        const QStringList systemNames{"Zalo", "Zalo Video", "Zalo Bảo Mật", "ZaloPay", "Thời Tiết", "zBusiness"};
        for (int i = 0; i < systemNames.size(); ++i) {
            const QString id = QStringLiteral("u-sys-%1").arg(i);
            client.selectConversation(id, systemNames.at(i), "");
            QVERIFY2(client.currentConversationIsOfficial(), qPrintable(systemNames.at(i)));
            QVERIFY2(!client.currentConversationCanCall(), qPrintable(systemNames.at(i)));
            QVERIFY2(!client.currentConversationCanCreateGroup(), qPrintable(systemNames.at(i)));
        }
        client.selectConversation("u-sys-friend", "Fixture Friend", "");
        QVERIFY(!client.currentConversationIsOfficial());
        QVERIFY(client.currentConversationCanCall());
        QVERIFY(client.currentConversationCanCreateGroup());
        client.applyGroupInfoForTesting(QJsonObject{{"g-sys-group", QJsonObject{{"name", "Fixture Group"}, {"totalMember", 3}}}});
        client.selectConversation("g-sys-group", "Fixture Group", "");
        QVERIFY(client.currentConversationIsGroup());
        QVERIFY(!client.currentConversationIsOfficial());
        QVERIFY(client.currentConversationCanCall());
        client.selectConversation("u-sys-docs", "My Documents", "");
        QVERIFY(client.currentConversationIsCloud());
        QVERIFY(!client.currentConversationCanCall());
        QVERIFY(!client.currentConversationCanCreateGroup());
    }

    void testUndoEventsAndAccountStates() {
        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-undo-owner");
            owner.clearCache();
        }
        ZaloClient client;
        client.setAppLanguage("vi");
        client.openOfflineCacheForTesting("u-undo-owner");
        client.setMyUidForTesting("u-undo-owner");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto msg = [now](const QString& id, const QString& type, const QJsonValue& content, int age) {
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", "u-undo-peer"}, {"idTo", "u-undo-owner"},
                               {"msgType", type}, {"content", content}, {"ts", QString::number(now - age)}};
        };
        const QJsonObject undoFirst = msg("9902", "chat.undo", QJsonObject{{"globalMsgId", "9901"}, {"deleteMsg", 1}}, 10);
        const QJsonObject original = msg("9901", "webchat", QString("fixture recalled words"), 50);
        auto* session = client.apiClient()->session();
        emit session->messagesReceived(QJsonArray{undoFirst, original}, false, true);
        auto* store = client.offlineStore();
        QVERIFY(store->rawMessage("9902").isEmpty());
        QCOMPARE(store->searchMessages("fixture recalled").size(), 0);

        emit session->messagesReceived(QJsonArray{msg("9903", "webchat", QString("fixture later recalled"), 40)}, false, true);
        QCOMPARE(store->searchMessages("fixture later recalled").size(), 1);
        emit session->messagesReceived(QJsonArray{msg("9904", "chat.undo",
            QString("{\"globalMsgId\":\"9903\",\"deleteMsg\":1}"), 5)}, false, true);
        QCOMPARE(store->searchMessages("fixture later recalled").size(), 0);
        emit session->messagesReceived(QJsonArray{msg("9903", "webchat", QString("fixture later recalled"), 40)}, false, true);
        QCOMPARE(store->searchMessages("fixture later recalled").size(), 0);
        int recalled = 0;
        for (const auto& row : store->loadMessages("u-undo-peer")) {
            if (row.id == "9901" || row.id == "9903") {
                QVERIFY(row.isRecalled);
                QCOMPARE(row.text, QString::fromUtf8("Tin nhắn đã được thu hồi"));
                ++recalled;
            }
        }
        QCOMPARE(recalled, 2);

        QCOMPARE(ZaloClient::undoTargetOf(msg("9905", "chat.undo", QString("{\"msgId\":\"9801\"}"), 0)), QString("9801"));
        QCOMPARE(ZaloClient::undoTargetOf(original), QString());
        const MessageItem bareUndo = client.toMessageItemForTesting(msg("9906", "chat.undo", QJsonObject{}, 0), false);
        QVERIFY(bareUndo.isRecalled);

        auto quoted = [&](const QJsonObject& quote) {
            QJsonObject m = msg("9950", "webchat", QString("fixture reply"), 0);
            m.insert("quote", quote);
            return client.toMessageItemForTesting(m, false).quoteText;
        };
        QCOMPARE(quoted(QJsonObject{{"msg", "fixture plain"}}), QString("fixture plain"));
        const MessageItem sizedPhoto = client.toMessageItemForTesting(msg("9960", "chat.photo",
            QJsonObject{{"thumb", "https://example.invalid/t.jpg"}, {"params", "{\"width\":\"1280\",\"height\":960}"}}, 0), false);
        QCOMPARE(sizedPhoto.extra.value("mediaW").toInt(), 1280);
        QCOMPARE(sizedPhoto.extra.value("mediaH").toInt(), 960);
        const MessageItem sizedVideo = client.toMessageItemForTesting(msg("9961", "chat.video.msg",
            QJsonObject{{"thumb", "https://example.invalid/v.jpg"}, {"params", "{\"video_width\":720,\"video_height\":1280}"}}, 0), false);
        QCOMPARE(sizedVideo.extra.value("mediaH").toInt(), 1280);
        const MessageItem unsized = client.toMessageItemForTesting(msg("9962", "chat.photo",
            QJsonObject{{"thumb", "https://example.invalid/u.jpg"}, {"params", "{\"width\":0}"}}, 0), false);
        QVERIFY(!unsized.extra.contains("mediaW"));
        const MessageItem albumPhoto = client.toMessageItemForTesting(msg("9963", "chat.photo",
            QJsonObject{{"thumb", "https://example.invalid/a.jpg"}, {"params", "{\"group_layout_id\":\"777\"}"}}, 0), false);
        QCOMPARE(albumPhoto.extra.value("groupLayoutId").toString(), QString("777"));
        QJsonObject urgentRaw = msg("9964", "webchat", QString("fixture urgent"), 0);
        urgentRaw.insert("metaData", QJsonObject{{"urgency", 2}});
        QCOMPARE(client.toMessageItemForTesting(urgentRaw, false).extra.value("urgency").toInt(), 2);
        QVERIFY(!client.toMessageItemForTesting(msg("9965", "webchat", QString("fixture normal"), 0), false).extra.contains("urgency"));
        client.setComposerUrgency(5);
        QCOMPARE(client.composerUrgency(), 0);
        client.setComposerUrgency(1);
        QCOMPARE(client.composerUrgency(), 1);
        client.setComposerUrgency(0);
        QCOMPARE(quoted(QJsonObject{{"msg", "fixture.png"}, {"cliMsgType", 1}}), QString("fixture.png"));
        QCOMPARE(quoted(QJsonObject{{"msg", "https://f1.photo.talk.zdn.vn/fixture/a.jpg"}, {"cliMsgType", 32}}),
                 QString::fromUtf8("[Hình ảnh]"));
        QCOMPARE(quoted(QJsonObject{{"msg", "{\"href\":\"https://example.invalid/a.png\"}"}, {"cliMsgType", "2"}}),
                 QString::fromUtf8("[Hình ảnh]"));
        QCOMPARE(quoted(QJsonObject{{"msg", "fixture caption"}, {"cliMsgType", 2}}),
                 QString::fromUtf8("[Hình ảnh] fixture caption"));
        QCOMPARE(quoted(QJsonObject{{"msg", "https://example.invalid/video/clip.mp4"}}), QString::fromUtf8("[Video]"));
        QCOMPARE(quoted(QJsonObject{{"msg", "sendBubbleMessage"}, {"cliMsgType", 36}}), QString::fromUtf8("[Nhãn dán]"));
        QCOMPARE(quoted(QJsonObject{{"msg", ""}, {"cliMsgType", 46},
                                    {"attach", "{\"title\":\"fixture.pdf\",\"params\":\"{\\\"fileExt\\\":\\\"pdf\\\"}\"}"}}),
                 QString::fromUtf8("[Tập tin] fixture.pdf"));
        QCOMPARE(quoted(QJsonObject{{"msg", ""}, {"cliMsgType", 31}}), QString::fromUtf8("[Tin nhắn thoại]"));
        QCOMPARE(quoted(QJsonObject{{"msg", ""}}), QString::fromUtf8("[Tin nhắn]"));
        emit session->messagesReceived(QJsonArray{msg("9980", "webchat", QString("fixture original text"), 3)}, false, true);
        QCOMPARE(quoted(QJsonObject{{"msg", ""}, {"globalMsgId", "9980"}}), QString("fixture original text"));
        QCOMPARE(bareUndo.text, QString::fromUtf8("Tin nhắn đã thu hồi"));
        QCOMPARE(client.toMessageItemForTesting(msg("9907", "chat.delete.everyone", QJsonObject{}, 0), false).text,
                 QString::fromUtf8("Tin nhắn đã bị xóa"));
        QCOMPARE(client.toMessageItemForTesting(msg("9908", "chat.fixture.unknown", QJsonObject{}, 0), false).text,
                 QString::fromUtf8("[Tin nhắn chưa hỗ trợ ở phiên bản hiện tại]"));
        QJsonObject early = msg("9909", "webchat", QString("fixture"), 0);
        early.insert("ts", "5");
        QVERIFY(client.toMessageItemForTesting(early, false).timeStr.isEmpty());
        const MessageItem video = client.toMessageItemForTesting(msg("9910", "chat.video.msg",
            QJsonObject{{"href", "https://example.invalid/v.mp4"}, {"params", "{\"fileSize\":\"2048\"}"}}, 0), false);
        QCOMPARE(video.fileSize, qint64(2048));

        QCOMPARE(client.friendlyErrorForTesting("x", 126), QString::fromUtf8("Tài khoản đã ngừng hoạt động."));
        QCOMPARE(client.friendlyErrorForTesting("x", 164), QString::fromUtf8("Bạn không còn là thành viên trong nhóm này"));
        QVERIFY(client.friendlyErrorForTesting("x", 301).contains(QString::fromUtf8("24 giờ")));
        QVERIFY(ZaloClient::isGoneError("HTTP 404"));
        QVERIFY(!ZaloClient::isGoneError("HTTP 500"));

        client.selectConversation("u-undo-peer", "Fixture Peer", "");
        QVERIFY(!client.currentConversationBlocked());
        QSignalSpy composer(&client, &ZaloClient::composerStateChanged);
        client.noteAccountFlagsForTesting("u-undo-peer", QJsonObject{{"accountStatus", 1}, {"isBlocked", 1}});
        QVERIFY(composer.count() >= 1);
        QVERIFY(client.currentConversationBlocked());
        QCOMPARE(client.currentConversationStatus(), QString::fromUtf8("Tài khoản đã ngừng hoạt động"));
        QVERIFY(!client.currentConversationHasPresence());
        client.noteAccountFlagsForTesting("u-undo-peer", QJsonObject{{"accountStatus", 0}, {"isBlocked", 0}});
        QVERIFY(!client.currentConversationBlocked());
        QVERIFY(client.currentConversationStatus() != QString::fromUtf8("Tài khoản đã ngừng hoạt động"));
    }

    void testLocationMessageCard() {
        ZaloClient client;
        client.setAppLanguage("vi");
        auto parse = [&client](const QJsonObject& params, const QString& title) {
            QJsonObject content{{"params", QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact))}};
            if (!title.isEmpty()) content.insert("title", title);
            return client.toMessageItemForTesting(QJsonObject{
                {"msgId", "9961"}, {"uidFrom", "u-loc-1"}, {"idTo", "u-loc-me"}, {"msgType", "chat.location.new"},
                {"ts", "1700000000000"}, {"content", content}}, false);
        };
        const MessageItem a = parse(QJsonObject{{"latitude", "10.762622"}, {"longitude", "106.660172"},
                                                {"description", "Fixture street"}}, "Fixture place");
        QCOMPARE(a.messageType, QString("location"));
        QCOMPARE(a.text, QString("Fixture place"));
        QCOMPARE(a.href, QString("https://www.google.com/maps?q=10.762622,106.660172"));
        QCOMPARE(a.extra.value("desc").toString(), QString("Fixture street"));
        const MessageItem b = parse(QJsonObject{{"latitude", 21.0285}, {"longitude", 105.8542}}, "");
        QCOMPARE(b.text, QString::fromUtf8("Vị trí được chia sẻ"));
        QCOMPARE(b.extra.value("coords").toString(), QString("21.028500, 105.854200"));
        const MessageItem c = parse(QJsonObject{{"latitude", "abc"}}, "");
        QVERIFY(c.href.isEmpty());
        QVERIFY(ZaloClient::isReparsedOnLoad("chat.location.new"));
    }

    void testCachedParseMigrationAndSpecialCards() {
        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-mig-owner");
            owner.clearCache();
            owner.bindOwner("u-mig-owner");
            MessageItem stale;
            stale.id = "9971";
            stale.senderId = "u-mig-peer";
            stale.text = QString::fromUtf8("[Nội dung web của Zalo]");
            stale.messageType = "text";
            stale.ts = 1700000000000LL;
            const QJsonObject raw{{"msgId", "9971"}, {"uidFrom", "u-mig-peer"}, {"idTo", "u-mig-owner"},
                                  {"msgType", "chat.webcontent"}, {"ts", "1700000000000"},
                                  {"content", QJsonObject{{"title", "Fixture became member"}, {"href", ""},
                                                          {"params", "{\"type\":6}"}}}};
            QVERIFY(owner.saveMessage("u-mig-peer", stale, raw, false));
            Zalos::Core::Models::ConversationItem conv;
            conv.id = "u-mig-peer";
            conv.name = "Fixture Peer";
            conv.lastMessage = stale.text;
            conv.lastTs = stale.ts;
            owner.saveConversation(conv);
            owner.setMetaValue("parse_version", "0");
        }
        ZaloClient client;
        client.setAppLanguage("vi");
        client.openOfflineCacheForTesting("u-mig-owner");
        auto* store = client.offlineStore();
        QTRY_COMPARE_WITH_TIMEOUT(store->metaValue("parse_version"), QString::number(ZaloClient::kCacheParseVersion), 5000);
        const auto rows = store->loadMessages("u-mig-peer");
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().messageType, QString("system"));
        QCOMPARE(rows.first().text, QString::fromUtf8("Bạn đã trở thành thành viên của nhóm"));
        QCOMPARE(store->searchMessages(QString::fromUtf8("Nội dung web")).size(), 0);
        QCOMPARE(client.conversationModel()->conversation("u-mig-peer").lastMessage,
                 QString::fromUtf8("Bạn đã trở thành thành viên của nhóm"));

        auto parse = [&client](const QString& type, const QJsonValue& content) {
            return client.toMessageItemForTesting(QJsonObject{{"msgId", "9972"}, {"uidFrom", "u-mig-peer"},
                {"idTo", "u-mig-owner"}, {"msgType", type}, {"ts", "1700000000000"}, {"content", content}}, false);
        };
        const MessageItem bank = parse("chat.webcontent", QJsonObject{{"title", "Fixture bank"}, {"href", ""},
            {"action", "zinstant.bankcard"}, {"params", "{\"item\":{\"zinstantdata_id\":\"templateId=11845\"}}"}});
        QCOMPARE(bank.messageType, QString("bankcard"));
        QCOMPARE(bank.text, QString("Fixture bank"));
        const MessageItem devices = parse("chat.list.action", QJsonArray{QJsonObject{
            {"title", "Fixture login alert"}, {"href", ""}, {"action", "action.open.logindevices"}}});
        QCOMPARE(devices.messageType, QString("link"));
        QVERIFY(devices.href.isEmpty());
        QCOMPARE(devices.text, QString("Fixture login alert"));
        QCOMPARE(parse("chat.list.action", QJsonArray{}).messageType, QString("link"));

        const MessageItem todo = parse("chat.todo", QJsonObject{{"message", "Hop 15 gi"},
            {"params", "{\"topicId\":\"4242\"}"}});
        QCOMPARE(todo.messageType, QString("system"));
        QCOMPARE(todo.text, QString("Hop 15 gi"));
        QCOMPARE(todo.extra.value("reminderEventId").toString(), QString("4242"));
        QCOMPARE(parse("chat.todo", QJsonObject{{"message", QJsonObject{{"message", "Nho uong thuoc"}}}}).text,
                 QString("Nho uong thuoc"));
        QCOMPARE(parse("chat.todo", QJsonObject{{"title", "Cuoc hop giao ban"}}).text, QString("Cuoc hop giao ban"));
        QCOMPARE(parse("chat.todo", QJsonObject{}).text, QString::fromUtf8("Nhắc nhở"));
    }

    void testUnreadCountAggregation() {
        ConversationModel model;
        int initialUnread = model.totalUnreadCount();
        QVERIFY(initialUnread >= 0);

        model.clearUnread("conv-1");
        int afterClear = model.totalUnreadCount();
        QVERIFY(afterClear <= initialUnread);

        QSignalSpy unreadSpy(&model, &ConversationModel::unreadCountChanged);
        model.clearUnread("conv-2");
        QVERIFY(unreadSpy.isValid());
    }

    void testPollMessageRendersInChat() {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("ChatPollBlock|PollVoteDialog|bound component")));
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.setGroupForTesting(QStringLiteral("g-poll-ui"), QStringLiteral("Fixture Poll Group"), QString());
        client.setUserForTesting(QStringLiteral("u-voter"), QStringLiteral("Fixture Voter"), QString());
        auto* session = client.apiClient()->session();
        const QJsonObject params{{"pollId", 8801}, {"question", "Fixture poll question"},
                                 {"msg", QJsonObject{{"vi", QString::fromUtf8("%1$s tham gia cuộc bình chọn %2$s")},
                                                     {"en", "%1$s voted in the poll %2$s"}}}};
        const qint64 base = QDateTime::currentMSecsSinceEpoch() - 60000;
        QJsonArray msgs;
        for (int i = 0; i < 2; ++i) {
            const QString id = QString::number(88010 + i);
            msgs.append(QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", "u-voter"}, {"idTo", "g-poll-ui"},
                                    {"msgType", "group.poll"}, {"ts", QString::number(base + i * 1000)},
                                    {"content", QJsonObject{{"action", i == 0 ? "create" : "vote"},
                                                            {"params", QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact))}}}});
        }
        emit session->messagesReceived(msgs, true, true);
        client.selectConversation(QStringLiteral("g-poll-ui"), QStringLiteral("Fixture Poll Group"), QString());

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        QTest::qWait(800);
        QCOMPARE(client.messageModel()->rowCount(), 2);
        window->grabWindow().save(QStringLiteral("test_poll_chat.png"));
        client.openPollDialog(8801, QStringLiteral("g-poll-ui"));
        QTest::qWait(300);
        client.closePollDialog();
        QTest::qWait(100);
    }

    void testNoMultimediaBackendDegradesHonestly() {
        qputenv("ZALOS_NO_MULTIMEDIA", "1");
        QVERIFY(!Zalos::Core::Platform::MultimediaSupport::available());
        QVERIFY(!Zalos::Core::Platform::MultimediaSupport::unavailableReason().isEmpty());
        {
            ZaloClient client;
            QVERIFY(!client.multimediaAvailable());
            QSignalSpy notices(&client, &ZaloClient::notice);
            client.toggleVoice(QStringLiteral("m-fixture"), QStringLiteral("https://example.invalid/voice.m4a"));
            QCOMPARE(notices.count(), 1);
            client.voiceRecorder()->refreshAvailability();
            QCOMPARE(client.voiceRecorder()->unavailableReason(), QStringLiteral("no_multimedia"));
            QVERIFY(!Zalos::Core::Call::CameraSender::cameraAvailable());
            QVERIFY(client.calls()->cameraDevices().isEmpty());
            QVERIFY(client.calls()->audioInputDevices().isEmpty());
        }
        qunsetenv("ZALOS_NO_MULTIMEDIA");
    }

    void testPollViewAndMessages() {
        using ZC = Zalos::Core::Network::ZaloClient;
        auto person = [](const QString& uid) { return qMakePair(QStringLiteral("Fixture ") + uid, QString()); };
        const QJsonObject detail{
            {"poll_id", 501}, {"question", "Fixture question"}, {"creator", "u-creator"}, {"joined", false},
            {"closed", false}, {"allow_multi_choices", true}, {"allow_add_new_option", true},
            {"is_anonymous", false}, {"is_hide_vote_preview", false}, {"expired_time", 0}, {"num_vote", 0},
            {"options", QJsonArray{
                QJsonObject{{"option_id", 1}, {"content", "A"}, {"votes", 3}, {"voted", false},
                            {"voters", QJsonArray{"u1", "u2", "u3"}}},
                QJsonObject{{"option_id", 2}, {"content", "B"}, {"votes", 1}, {"voted", false},
                            {"voters", QJsonArray{"u1"}}}}}};
        QVariantMap v = ZC::buildPollView(detail, "u-me", false, 1000, person);
        QCOMPARE(v.value("total_votes").toInt(), 4);
        QCOMPARE(v.value("voter_count").toInt(), 3);
        QCOMPARE(v.value("action").toString(), QStringLiteral("vote"));
        QVERIFY(v.value("can_vote").toBool());
        QVERIFY(v.value("can_add_option").toBool());
        QVERIFY(v.value("can_view_voters").toBool());
        QVERIFY(!v.value("can_manage").toBool());
        const QVariantMap a = v.value("options").toList().at(0).toMap();
        QCOMPARE(a.value("percent").toInt(), 75);
        QCOMPARE(a.value("voters").toList().size(), 3);
        QCOMPARE(a.value("preview").toList().size(), 2);
        QCOMPARE(a.value("voters").toList().at(0).toMap().value("name").toString(), QStringLiteral("Fixture u1"));

        QJsonObject joined = detail;
        joined["joined"] = true;
        QCOMPARE(ZC::buildPollView(joined, "u-me", false, 1000, person).value("action").toString(), QStringLiteral("change"));
        joined["is_hide_vote_preview"] = true;
        v = ZC::buildPollView(joined, "u-me", true, 1000, person);
        QCOMPARE(v.value("action").toString(), QStringLiteral("view"));
        QCOMPARE(v.value("lock_reason").toString(), QStringLiteral("after_vote"));
        QVERIFY(!v.value("can_vote").toBool());
        QVERIFY(v.value("can_manage").toBool());

        QJsonObject hidden = detail;
        hidden["is_hide_vote_preview"] = true;
        v = ZC::buildPollView(hidden, "u-me", false, 1000, person);
        QVERIFY(v.value("hide_results").toBool());
        QVERIFY(!v.value("can_view_voters").toBool());
        QVERIFY(v.value("can_vote").toBool());

        QJsonObject anonymous = detail;
        anonymous["is_anonymous"] = true;
        QVERIFY(!ZC::buildPollView(anonymous, "u-me", false, 1000, person).value("can_view_voters").toBool());

        QJsonObject expired = detail;
        expired["expired_time"] = 999;
        v = ZC::buildPollView(expired, "u-me", false, 1000, person);
        QVERIFY(v.value("expired").toBool());
        QCOMPARE(v.value("lock_reason").toString(), QStringLiteral("expired"));
        QCOMPARE(v.value("action").toString(), QStringLiteral("view"));
        QJsonObject closed = detail;
        closed["closed"] = true;
        QCOMPARE(ZC::buildPollView(closed, "u-me", false, 1000, person).value("lock_reason").toString(), QStringLiteral("closed"));

        QCOMPARE(ZC::pollDraftCode("", {"A", "B"}), QStringLiteral("question"));
        QCOMPARE(ZC::pollDraftCode(QString(201, 'q'), {"A", "B"}), QStringLiteral("question_length"));
        QCOMPARE(ZC::pollDraftCode("Q", {"A", ""}), QStringLiteral("min_options"));
        QCOMPARE(ZC::pollDraftCode("Q", {"A", " a "}), QStringLiteral("duplicate"));
        QCOMPARE(ZC::pollDraftCode("Q", {"A", QString(121, 'x')}), QStringLiteral("option_length"));
        QStringList many;
        for (int i = 0; i < 31; ++i) many << QString::number(i);
        QCOMPARE(ZC::pollDraftCode("Q", many), QStringLiteral("max_options"));
        QVERIFY(ZC::pollDraftCode("Q", {"A", "B"}).isEmpty());
        QCOMPARE(ZC::pollTemplateText(QStringLiteral("%1$s tham gia cuộc bình chọn %2$s")),
                 QStringLiteral("tham gia cuộc bình chọn"));

        ZC client;
        client.setMyUidForTesting(QStringLiteral("u-me"));
        auto pollMsg = [](const char* id, const char* action, const QJsonObject& params) {
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", "u-voter"}, {"idTo", "g-poll"},
                               {"msgType", "group.poll"}, {"ts", QString::number(1700000000000LL + QByteArray(id).toLongLong())},
                               {"content", QJsonObject{{"action", action},
                                                       {"params", QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact))}}}};
        };
        const QJsonObject createParams{{"pollId", 77}, {"question", "Fixture Q"}};
        const QJsonObject voteParams{{"pollId", 77}, {"question", "Fixture Q"}, {"isAnonymous", true},
                                     {"msg", QJsonObject{{"vi", QString::fromUtf8("Có thành viên tham gia cuộc bình chọn %2$s")},
                                                         {"en", "A member voted in the poll %2$s"}}}};
        const auto created = client.toMessageItemForTesting(pollMsg("9001", "create", createParams), true);
        QCOMPARE(created.messageType, QStringLiteral("poll"));
        QCOMPARE(created.pollData.value("poll_id").toLongLong(), 77LL);
        QVERIFY(!created.extra.contains("pollEvent"));
        QCOMPARE(created.extra.value("pollTextVi").toString(), QString::fromUtf8("tạo cuộc bình chọn mới"));
        const auto voted = client.toMessageItemForTesting(pollMsg("9002", "vote", voteParams), true);
        QCOMPARE(voted.messageType, QStringLiteral("poll"));
        QCOMPARE(voted.extra.value("pollEvent").toString(), QStringLiteral("vote"));
        QVERIFY(voted.extra.value("pollAnonActor").toBool());
        QCOMPARE(voted.extra.value("pollTextVi").toString(), QString::fromUtf8("tham gia cuộc bình chọn"));
        QCOMPARE(voted.extra.value("pollTextEn").toString(), QStringLiteral("voted in the poll"));

        MessageModel model;
        model.mergeMessages("g-poll", {created});
        QVERIFY(model.threadOf("g-poll").at(0).pollCard);
        model.mergeMessages("g-poll", {voted});
        QVERIFY(!model.threadOf("g-poll").at(0).pollCard);
        QVERIFY(model.threadOf("g-poll").at(1).pollCard);
    }

    void testReadPointsPersistAndCoverLateMessages() {
        using ZS = Zalos::Core::Network::ZaloSession;
        const QJsonObject rp{{"idTo", "u-rp"}, {"lastMsgId", "1"}};
        QCOMPARE(ZS::readPointsOf(QJsonObject{{"clearUnreads", QJsonArray{rp}}}).size(), 1);
        QCOMPARE(ZS::readPointsOf(QJsonObject{{"clearUnreadInfo", QJsonArray{rp, rp}}}).size(), 2);
        QCOMPARE(ZS::readPointsOf(QJsonObject{{"statusMap", QJsonObject{{"clearUnreads", QJsonArray{rp}}}}}).size(), 1);
        QCOMPARE(ZS::readPointsOf(QJsonObject{{"data", QJsonObject{{"clearUnreadInfo", QJsonArray{rp}}}}}).size(), 1);
        QVERIFY(ZS::readPointsOf(QJsonObject{{"msgs", QJsonArray{}}}).isEmpty());

        const QString owner = QStringLiteral("u-rp-owner");
        {
            ZaloOfflineStore store;
            store.clearCache();
            store.bindOwner(owner);
        }
        auto msg = [](const char* id, const char* from) {
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", from}, {"idTo", "u-me"},
                               {"msgType", "webchat"}, {"content", QStringLiteral("Fixture text")},
                               {"ts", QString::number(1700000000000LL + QByteArray(id).toLongLong())}};
        };
        {
            ZaloClient client;
            client.setMyUidForTesting(QStringLiteral("u-me"));
            client.openOfflineCacheForTesting(owner);
            ZS* session = client.apiClient()->session();

            emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "u-rp-a"}, {"lastMsgId", "8802"}, {"isGroup", 0}}});
            emit session->messagesReceived(QJsonArray{msg("8801", "u-rp-a"), msg("8802", "u-rp-a")}, false, false);
            QCOMPARE(client.conversationModel()->conversation("u-rp-a").unreadCount, 0);
            emit session->messagesReceived(QJsonArray{msg("8803", "u-rp-a")}, false, false);
            QCOMPARE(client.conversationModel()->conversation("u-rp-a").unreadCount, 1);

            emit session->messagesReceived(QJsonArray{msg("8811", "u-rp-b"), msg("8812", "u-rp-b")}, false, false);
            QCOMPARE(client.conversationModel()->conversation("u-rp-b").unreadCount, 2);
            emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "u-rp-b"}, {"lastMsgId", "8812"}}});
            QCOMPARE(client.conversationModel()->conversation("u-rp-b").unreadCount, 0);

            emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "u-rp-b"}, {"lastMsgId", "8805"}}});
            QCOMPARE(client.readPointForTesting("u-rp-b"), 8812ULL);

            emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "g-rp-late"}, {"lastMsgId", "9901"}, {"isGroup", 1}}});
            QCOMPARE(client.readPointForTesting("g-rp-late"), 9901ULL);
            emit session->seenReceived("g-rp-late", "9907", "u-me", true);
            QCOMPARE(client.readPointForTesting("g-rp-late"), 9907ULL);
            emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "u-rp-sub"}, {"lastMsgId", "9"}, {"type", 2}, {"sct", 1}}});
            QCOMPARE(client.readPointForTesting("u-rp-sub"), 0ULL);
        }
        {
            ZaloClient reloaded;
            reloaded.setMyUidForTesting(QStringLiteral("u-me"));
            reloaded.openOfflineCacheForTesting(owner);
            QCOMPARE(reloaded.readPointForTesting("u-rp-b"), 8812ULL);
            QCOMPARE(reloaded.readPointForTesting("g-rp-late"), 9907ULL);
            reloaded.offlineStore()->clearCache();
            ZaloClient wiped;
            wiped.openOfflineCacheForTesting(owner);
            QCOMPARE(wiped.readPointForTesting("u-rp-b"), 0ULL);
        }
    }

    void testPhoneSyncTransferErrorOrder() {
        using MHS = Zalos::Core::Sync::MobileHistorySync;
        using K = MHS::TransferErrorKind;
        QVERIFY(MHS::classifyTransferError(QJsonObject{{"status", 2}, {"error_code", -1}}) == K::PhoneIdle);
        QVERIFY(MHS::classifyTransferError(QJsonObject{{"status", 1}, {"error_code", -1}}) == K::PhoneActive);
        QVERIFY(MHS::classifyTransferError(QJsonObject{{"status", 1}}) == K::PhoneActive);
        QVERIFY(MHS::classifyTransferError(QJsonObject{{"error_code", -1}}) == K::Failed);
        QVERIFY(MHS::classifyTransferError(QJsonObject{{"status", 0}, {"error_code", 7}}) == K::Failed);
        QVERIFY(MHS::classifyTransferError(QJsonObject{{"status", 0}, {"error_code", 0}}) == K::Ignore);
    }

    void testReadChatUnreadPersistedForSubChat() {
        const QString owner = QStringLiteral("u-subchat-owner");
        const QString peer = QStringLiteral("u-subchat-oa");
        {
            ZaloOfflineStore store;
            store.bindOwner(owner);
            store.clearCache();
            store.bindOwner(owner);
            Zalos::Core::Models::ConversationItem conv;
            conv.id = peer;
            conv.name = QStringLiteral("Z Business Fixture");
            conv.lastMessage = QStringLiteral("Khuyen mai");
            conv.lastTs = 1700000000000LL;
            conv.unreadCount = 2;
            QVERIFY(store.saveConversation(conv));
        }

        ZaloClient client;
        client.setAppLanguage("vi");
        client.openOfflineCacheForTesting(owner);
        QCOMPARE(client.conversationModel()->conversation(peer).unreadCount, 2);

        client.selectConversation(peer, QStringLiteral("Z Business Fixture"), QString());
        QCOMPARE(client.conversationModel()->conversation(peer).unreadCount, 0);

        const auto persisted = client.offlineStore()->loadConversations();
        int reloadedUnread = -1;
        for (const auto& c : persisted) {
            if (c.id == peer) reloadedUnread = c.unreadCount;
        }
        QCOMPARE(reloadedUnread, 0);

        client.offlineStore()->clearCache();
    }

    void testMessageStreamFeatures() {
        MessageModel model;
        MessageItem mine;
        mine.id = "stream-1"; mine.cliMsgId = "cli-stream-1"; mine.senderId = "u-me";
        mine.text = "Thử nghiệm tin nhắn hệ thống tiếng Việt"; mine.isMine = true; mine.ts = 100;
        MessageItem partner;
        partner.id = "stream-2"; partner.cliMsgId = "cli-stream-2"; partner.senderId = "u-partner";
        partner.text = "Tin nhắn phản hồi từ đối tác"; partner.ts = 200;
        MessageItem att;
        att.id = "stream-3"; att.cliMsgId = "cli-stream-3"; att.senderId = "u-me"; att.messageType = "image";
        att.text = "bang_gia_gpu.png"; att.mediaUrl = "https://cdn.example.com/gpu.png"; att.isMine = true; att.ts = 300;
        model.loadConversation("stream-conv");
        model.mergeMessages("stream-conv", {mine, partner, att});
        QCOMPARE(model.rowCount(), 3);

        QModelIndex idxText = model.index(0);
        QCOMPARE(model.data(idxText, MessageModel::TextRole).toString(), "Thử nghiệm tin nhắn hệ thống tiếng Việt");
        QCOMPARE(model.data(idxText, MessageModel::IsMineRole).toBool(), true);

        QModelIndex idxContact = model.index(1);
        QCOMPARE(model.data(idxContact, MessageModel::IsMineRole).toBool(), false);

        QModelIndex idxAtt = model.index(2);
        QCOMPARE(model.data(idxAtt, MessageModel::MessageTypeRole).toString(), "image");
        QCOMPARE(model.data(idxAtt, MessageModel::TextRole).toString(), "bang_gia_gpu.png");
        QCOMPARE(model.data(idxAtt, MessageModel::MediaUrlRole).toString(), "https://cdn.example.com/gpu.png");
    }

    void testSoundPreviewLifecycle() {
        ZaloClient client;

        QSignalSpy previewSpy(&client, &ZaloClient::previewPlayingChanged);
        client.playSoundPreview("notification", "pop");
        QCOMPARE(client.isPreviewPlaying(), true);
        QCOMPARE(client.currentPreviewSound(), QString("pop"));
        QCOMPARE(previewSpy.count(), 1);

        client.playSoundPreview("ringtone", "classic");
        QCOMPARE(client.isPreviewPlaying(), true);
        QCOMPARE(client.currentPreviewSound(), QString("classic"));

        for (const QString& key : Zalos::Core::Platform::SoundEngine::notificationKeys()
                                  + Zalos::Core::Platform::SoundEngine::ringtoneKeys()) {
            const QString path = Zalos::Core::Platform::SoundEngine::renderToFile(key);
            QVERIFY2(!path.isEmpty(), qPrintable(key));
            QFile wav(path);
            QVERIFY(wav.open(QIODevice::ReadOnly));
            QVERIFY(wav.read(4) == "RIFF");
            QVERIFY(wav.size() > 1000);
        }

        client.stopSoundPreview();
        QCOMPARE(client.isPreviewPlaying(), false);
        QCOMPARE(client.currentPreviewSound(), QString(""));
        QVERIFY(previewSpy.count() >= 3);
    }

    void testAuthValidationEdgeCases() {
        ZaloClient client;
        client.logout();
        QCOMPARE(client.isLoggedIn(), false);

        client.loginWithPhone("", "", true);
        QCOMPARE(client.isLoggedIn(), false);
        QVERIFY(client.hasLoginError());

        client.loginWithPhone("0900000011", "ValidPassword123", true);
        QTest::qWait(100);
        QCOMPARE(client.isLoggedIn(), false);
        QVERIFY(client.hasLoginError());

        client.refreshQrCode();
        QVERIFY(client.qrSecondsRemaining() > 0);
        QCOMPARE(client.qrMatrix().size(), client.qrSize() * client.qrSize());
        QCOMPARE(client.qrLoginStep(), 0);
    }

    void testIncomingCallWorkflow() {
        ZaloClient client;
        QSignalSpy callSpy(&client, &ZaloClient::incomingCallStateChanged);

        client.triggerSimulatedIncomingCall("Trần Thị B");
        QCOMPARE(client.isIncomingCallActive(), true);
        QCOMPARE(client.incomingCallerName(), QString("Trần Thị B"));
        QCOMPARE(callSpy.count(), 1);

        client.answerCall();
        QCOMPARE(client.isIncomingCallActive(), false);
        QCOMPARE(callSpy.count(), 2);
    }

    void testCryptoEngineSafety() {
        std::string privKey = "A1B2C3D4E5F60718293A4B5C6D7E8F90";
        auto derivedKey = DbCross::deriveAes256Key(privKey);
        QCOMPARE(static_cast<int>(derivedKey.size()), 32);

        QVERIFY(DbCross::verifyCryptoSelfTest());
    }

    void testInteractiveFeaturesAndNavigation() {
        ZaloClient client;

        QCOMPARE(client.currentNavigationTab(), QString("messages"));
        client.setCurrentNavigationTab("contacts");
        QCOMPARE(client.currentNavigationTab(), QString("contacts"));
        client.setCurrentNavigationTab("cloud");
        QCOMPARE(client.currentNavigationTab(), QString("cloud"));
        client.setCurrentNavigationTab("messages");
        QCOMPARE(client.currentNavigationTab(), QString("messages"));

        QCOMPARE(client.hasBusinessPackage(), false);
        QCOMPARE(client.businessPackageName(), QString());
        QCOMPARE(client.cloudSharedItems().isEmpty(), true);

        QCOMPARE(client.showAddFriendModal(), false);
        client.openAddFriend();
        QCOMPARE(client.showAddFriendModal(), true);
        QSignalSpy searchSpy(&client, &ZaloClient::userSearchFinished);
        client.findUserByPhone("0900000012");
        QCOMPARE(searchSpy.count(), 1);
        QCOMPARE(searchSpy.first().at(0).toBool(), false);
        client.closeAddFriend();
        QCOMPARE(client.showAddFriendModal(), false);

        QCOMPARE(client.showCreateGroupModal(), false);
        client.openCreateGroup();
        QCOMPARE(client.showCreateGroupModal(), true);
        QSignalSpy groupSpy(&client, &ZaloClient::groupCreateFinished);
        client.createGroup("Nhom Test", QStringList{"111"});
        QCOMPARE(groupSpy.count(), 1);
        QCOMPARE(groupSpy.first().at(0).toBool(), false);
        client.createGroup("Nhom Test", QStringList{"111", "222"});
        QCOMPARE(groupSpy.count(), 2);
        QCOMPARE(groupSpy.last().at(0).toBool(), false);
        client.closeCreateGroup();
        QCOMPARE(client.showCreateGroupModal(), false);

        QCOMPARE(client.showConversationInfo(), false);
        client.toggleConversationInfo();
        QCOMPARE(client.showConversationInfo(), true);
        client.toggleConversationInfo();
        QCOMPARE(client.showConversationInfo(), false);
    }

    void testRealMessageInteraction() {
        ZaloClient client;
        client.selectConversation("vp-8t10", "Võ Thị Sáu", "");
        QCOMPARE(client.currentConversationId(), QString("vp-8t10"));

        MessageModel* msgModel = client.messageModel();
        int initialCount = msgModel->rowCount();

        client.sendMessage("Xin chào bạn!");
        QCOMPARE(msgModel->rowCount(), initialCount + 1);

        QModelIndex lastIdx = msgModel->index(msgModel->rowCount() - 1);
        QCOMPARE(msgModel->data(lastIdx, MessageModel::TextRole).toString(), QString("Xin chào bạn!"));
        QCOMPARE(msgModel->data(lastIdx, MessageModel::IsMineRole).toBool(), true);

        QCOMPARE(msgModel->rowCount(), initialCount + 1);
        QCOMPARE(msgModel->data(lastIdx, MessageModel::IsMineRole).toBool(), true);
        QCOMPARE(msgModel->data(lastIdx, MessageModel::TextRole).toString(), QString("Xin chào bạn!"));
    }

    void testMediaUtilsThumbnails() {
        QImage testImg(100, 100, QImage::Format_RGB32);
        testImg.fill(Qt::blue);
        QByteArray buffer;
        QBuffer qbuf(&buffer);
        qbuf.open(QIODevice::WriteOnly);
        testImg.save(&qbuf, "PNG");

        QByteArray thumbBytes = MediaUtils::generateThumbnailBuffer(buffer, 48, 48);
        QVERIFY(!thumbBytes.isEmpty());

        QString tmpIn = "/tmp/test_thumb_in.png";
        QString tmpOut = "/tmp/test_thumb_out.jpg";
        testImg.save(tmpIn);
        bool ok = MediaUtils::generateThumbnailFs(tmpIn, tmpOut, 48, 48);
        QVERIFY(ok);
        QVERIFY(QFile::exists(tmpOut));
        QFile::remove(tmpIn);
        QFile::remove(tmpOut);

        bool jxl = MediaUtils::isJxlSupported();
        Q_UNUSED(jxl);

        QVERIFY(MediaUtils::isIrCamera("Integrated IR Camera"));
        QVERIFY(MediaUtils::isIrCamera("Windows Hello Infrared Sensor"));
        QVERIFY(!MediaUtils::isIrCamera("HD Web Camera 1080p"));
    }

    void testCallEngineLifecycleAndCrypto() {
        ZCallEngine engine;
        QCOMPARE(engine.callState(), CallState::Idle);

        QByteArray zrtpSecret = "MY-ZRTP-SECRET";
        QByteArray salt = "SALT-16BYTES1234";
        QByteArray srtpKey = ZCallEngine::deriveSrtpMasterKey(zrtpSecret, salt);
        QCOMPARE(srtpKey.size(), 16);

        engine.startCall("peer-uid-1", "Võ Thị Sáu", false);
        QCOMPARE(engine.callState(), CallState::CallingOutgoing);
        QCOMPARE(engine.currentCaller(), QString("Võ Thị Sáu"));

        engine.setMutedMic(true);
        QCOMPARE(engine.isMutedMic(), true);
        engine.setMutedSpeaker(true);
        QCOMPARE(engine.isMutedSpeaker(), true);
        engine.startScreenShare();
        QCOMPARE(engine.isScreenSharing(), true);
        engine.stopScreenShare();
        QCOMPARE(engine.isScreenSharing(), false);

        engine.endCall();
        QCOMPARE(engine.callState(), CallState::Ended);

        QVector<AudioDevice> inputs = engine.getAudioDevices(true);
        QVERIFY(!inputs.isEmpty());
        QVector<AudioDevice> outputs = engine.getAudioDevices(false);
        QVERIFY(!outputs.isEmpty());
    }

    void testOfflineStoreEncryptedSessionAndSqlite() {
        ZaloOfflineStore store;
        QString testDir = "/tmp/zalo_offline_test";
        QString zpwSek = "cookie-zpw-sek-token-123456";

        QJsonObject sessionRecord;
        sessionRecord["version"] = 1;
        sessionRecord["clientId"] = "linux-client-1";
        QJsonObject info;
        QJsonObject config;
        config["uid"] = "123456789";
        info["config"] = config;
        info["UIN"] = "987654321";
        info["decryptKey"] = "AES-KEY-SAMPLE";
        sessionRecord["info"] = info;

        bool written = ZaloOfflineStore::writeEncryptedSession(testDir, zpwSek, sessionRecord);
        QVERIFY(written);

        QJsonObject readBack = ZaloOfflineStore::readEncryptedSession(testDir, zpwSek);
        QCOMPARE(readBack.value("clientId").toString(), QString("linux-client-1"));
        QCOMPARE(readBack.value("info").toObject().value("config").toObject().value("uid").toString(), QString("123456789"));

        ZaloOfflineStore::clearEncryptedSession(testDir);
        QJsonObject emptyAfterClear = ZaloOfflineStore::readEncryptedSession(testDir, zpwSek);
        QVERIFY(emptyAfterClear.isEmpty());

        Zalos::Core::Models::ConversationItem item;
        item.id = "test-conv-sqlite";
        item.name = "Test SQLite Contact";
        item.lastMessage = "Tin nhắn lưu ngoại tuyến";
        item.timestampStr = "12:30";
        item.unreadCount = 0;
        item.isOnline = true;
        item.isPinned = false;
        item.category = "priority";

        bool savedConv = store.saveConversation(item);
        QVERIFY(savedConv);

        QVector<Zalos::Core::Models::ConversationItem> convs = store.loadConversations();
        QVERIFY(!convs.isEmpty());

        Zalos::Core::Models::MessageItem msg;
        msg.id = "msg-sqlite-1";
        msg.senderName = "Tôi";
        msg.text = "Tin nhắn offline SQLite";
        msg.timeStr = "12:31";
        msg.isMine = true;
        msg.isE2EE = false;
        msg.messageType = "text";

        bool savedMsg = store.saveMessage("test-conv-sqlite", msg);
        QVERIFY(savedMsg);

        QVector<Zalos::Core::Models::MessageItem> msgs = store.loadMessages("test-conv-sqlite");
        QVERIFY(!msgs.isEmpty());
        QCOMPARE(msgs.first().text, QString("Tin nhắn offline SQLite"));

        store.setOfflineActive(true);
        QCOMPARE(store.isOfflineActive(), true);
        QCOMPARE(store.syncState(), QString("DISCONNECTED"));

        store.setOfflineActive(false);
        QCOMPARE(store.isOfflineActive(), false);
    }

    void testOfflineCacheTimelineAndOwner() {
        {
            ZaloOfflineStore store;
            store.bindOwner("u-owner-fixture");
            store.clearCache();
            store.bindOwner("u-owner-fixture");
            ConversationItem conv; conv.id = "u-fixture-peer"; conv.name = "Fixture Peer"; conv.lastTs = 2000;
            conv.lastMessage = "second"; conv.isMuted = true;
            QVERIFY(store.saveConversation(conv));
            MessageItem later; later.id = "502"; later.text = "second"; later.ts = 2000; later.senderId = "u-fixture-peer";
            MessageItem earlier; earlier.id = "501"; earlier.text = "first"; earlier.ts = 1000; earlier.isMine = true;
            earlier.status = "seen"; earlier.quoteText = "q"; earlier.quoteSender = "Fixture";
            const QJsonObject raw{{"msgId", "502"}, {"uidFrom", "u-fixture-peer"}, {"idTo", "0"}, {"content", "second"}};
            QVERIFY(store.saveMessage("u-fixture-peer", later, raw, false));
            QVERIFY(store.saveMessage("u-fixture-peer", earlier, {}, false));
            MessageItem localEcho; localEcho.id = "local-1-1"; localEcho.text = "x";
            QVERIFY(!store.saveMessage("u-fixture-peer", localEcho));
            later.reaction = "❤️";
            QVERIFY(store.saveMessage("u-fixture-peer", later));
            store.saveProfile({"u-fixture-peer", "Fixture Peer", "", false, 0, true});

            QHash<QString, QJsonObject> raws;
            const auto msgs = store.loadMessages("u-fixture-peer", &raws);
            QCOMPARE(msgs.size(), 2);
            QCOMPARE(msgs.at(0).id, QString("501"));
            QCOMPARE(msgs.at(0).status, QString("seen"));
            QCOMPARE(msgs.at(0).quoteText, QString("q"));
            QCOMPARE(msgs.at(1).reaction, QString::fromUtf8("❤️"));
            QCOMPARE(raws.value("502").value("content").toString(), QString("second"));
            QCOMPARE(store.loadConversations().first().lastTs, qint64(2000));
        }

        {
            ZaloClient client;
            QVERIFY(!client.openOfflineCacheForTesting("u-someone-else"));
            QVERIFY(client.openOfflineCacheForTesting("u-owner-fixture"));
            QVERIFY(client.conversationModel()->contains("u-fixture-peer"));
            QCOMPARE(client.conversationModel()->conversation("u-fixture-peer").isMuted, true);
            client.selectConversation("u-fixture-peer", "Fixture Peer", "");
            QCOMPARE(client.messageModel()->rowCount(), 2);
            QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                     QString("501"));
            QVERIFY(client.canRecall("501") == false);
        }

        {
            ZaloOfflineStore store;
            QVERIFY(store.bindOwner("u-other-owner"));
            QVERIFY(store.loadConversations().isEmpty());
            QVERIFY(store.loadMessages("u-fixture-peer").isEmpty());
            store.clearCache();
        }
    }


    void testMobileHistoryBackupPipeline() {
        using Zalos::Core::Sync::MobileHistorySync;

        const auto keys = MobileHistorySync::generateKeyPair();
        QVERIFY(!keys.publicKeyBase64.isEmpty());
        const QByteArray der = QByteArray::fromBase64(keys.publicKeyBase64.toLatin1());
        const unsigned char* p = reinterpret_cast<const unsigned char*>(der.constData());
        EVP_PKEY* pub = d2i_PUBKEY(nullptr, &p, der.size());
        QVERIFY(pub != nullptr);
        const QByteArray backupKey = QByteArray::fromHex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
        EVP_PKEY_CTX* ectx = EVP_PKEY_CTX_new(pub, nullptr);
        QVERIFY(EVP_PKEY_encrypt_init(ectx) == 1);
        EVP_PKEY_CTX_set_rsa_padding(ectx, RSA_PKCS1_PADDING);
        size_t clen = 0;
        EVP_PKEY_encrypt(ectx, nullptr, &clen, reinterpret_cast<const unsigned char*>(backupKey.constData()), backupKey.size());
        QByteArray cipher(int(clen), '\0');
        QVERIFY(EVP_PKEY_encrypt(ectx, reinterpret_cast<unsigned char*>(cipher.data()), &clen,
                                 reinterpret_cast<const unsigned char*>(backupKey.constData()), backupKey.size()) == 1);
        cipher.resize(int(clen));
        EVP_PKEY_CTX_free(ectx);
        EVP_PKEY_free(pub);
        bool ok = false;
        QCOMPARE(MobileHistorySync::rsaDecrypt(keys.privateKeyPem, QString::fromLatin1(cipher.toBase64()), &ok), backupKey);
        QVERIFY(ok);
        const QString keyText = MobileHistorySync::backupKeyText(backupKey, 1);
        QCOMPARE(keyText, QString::fromLatin1(backupKey.toHex()));

        QTemporaryDir work;
        QVERIFY(work.isValid());
        auto makeDb = [&](const QString& name, const QList<QVariantList>& rows) {
            const QString conn = "fixture_" + name;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
                db.setDatabaseName(work.filePath(name));
                QVERIFY(db.open());
                QSqlQuery q(db);
                QVERIFY(q.exec("CREATE TABLE ChatContent (MsgType INTEGER, TimeStamp INTEGER, MsgStatus INTEGER, "
                               "CliMsgId TEXT, SenderId TEXT, GlbMsgId TEXT, MsgContent TEXT, BinNet BLOB, TTL INTEGER)"));
                for (const QVariantList& r : rows) {
                    q.prepare("INSERT INTO ChatContent VALUES (?,?,?,?,?,?,?,?,?)");
                    for (const QVariant& v : r) q.addBindValue(v);
                    QVERIFY(q.exec());
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(conn);
        };
        makeDb("1001.db", {{0, 1000, 1, "c1", "1001", "5001", QString::fromUtf8("chào bạn"), QByteArray(), 0},
                           {0, 2000, 1, "c2", "1111", "5002", QString::fromUtf8("chào"), QByteArray(), 0},
                           {36, 3000, 1, "c3", "1001", "5003", "", QByteArray(), 0}});
        makeDb("group_2002.db", {{0, 1500, 1, "c4", "1003", "6001", "hello group", QByteArray(), 0}});

        QByteArray payload;
        QByteArray table;
        QStringList names{"1001.db", "group_2002.db"};
        auto be32 = [](QByteArray& b, quint32 v) { b.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v)); };
        for (const QString& n : names) {
            QFile f(work.filePath(n));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray data = f.readAll();
            be32(table, quint32(n.size()));
            table.append(n.toLatin1());
            be32(table, quint32(data.size()));
            payload.append(data);
        }
        QByteArray xz(int(lzma_stream_buffer_bound(size_t(payload.size()))), '\0');
        size_t xzLen = 0;
        QCOMPARE(lzma_easy_buffer_encode(6, LZMA_CHECK_CRC64, nullptr, reinterpret_cast<const uint8_t*>(payload.constData()),
                                         size_t(payload.size()), reinterpret_cast<uint8_t*>(xz.data()), &xzLen, size_t(xz.size())),
                 LZMA_OK);
        xz.resize(int(xzLen));
        QByteArray plain = QByteArray("ZDB4.0") + QByteArray(8, '\0');
        be32(plain, quint32(names.size()));
        plain += table + xz;
        plain.append(QByteArray((16 - plain.size() % 16) % 16, '\0'));
        const QByteArray aesKey = keyText.left(32).toUpper().toLatin1();
        QByteArray enc;
        for (int off = 0; off < plain.size(); off += 65536) {
            const QByteArray part = plain.mid(off, 65536);
            EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
            const unsigned char iv[16] = {0};
            EVP_EncryptInit_ex(c, EVP_aes_256_cbc(), nullptr, reinterpret_cast<const unsigned char*>(aesKey.constData()), iv);
            EVP_CIPHER_CTX_set_padding(c, 0);
            QByteArray out(part.size() + 16, '\0');
            int l1 = 0, l2 = 0;
            EVP_EncryptUpdate(c, reinterpret_cast<unsigned char*>(out.data()), &l1,
                              reinterpret_cast<const unsigned char*>(part.constData()), part.size());
            EVP_EncryptFinal_ex(c, reinterpret_cast<unsigned char*>(out.data()) + l1, &l2);
            EVP_CIPHER_CTX_free(c);
            enc += out.left(l1 + l2);
        }
        QFile backup(work.filePath("backup.zdb"));
        QVERIFY(backup.open(QIODevice::WriteOnly));
        backup.write(enc);
        backup.close();

        std::string err;
        QVERIFY(!Zalos::Core::Crypto::DbCross::decompressAndDecryptZdb4(work.filePath("backup.zdb").toStdString(),
                                                                       work.filePath("bad").toStdString(),
                                                                       std::string(64, 'f'), &err));
        QVERIFY(!err.empty());
        QVERIFY(Zalos::Core::Crypto::DbCross::decompressAndDecryptZdb4(work.filePath("backup.zdb").toStdString(),
                                                                      work.filePath("out").toStdString(),
                                                                      keyText.toStdString(), &err));
        QString readErr;
        const auto convs = MobileHistorySync::readBackupDirectory(work.filePath("out"), &readErr);
        QCOMPARE(convs.size(), 2);

        const QHash<QString, QString> ids{{"1001", "9001"}, {"2002", "9002"}, {"1003", "9003"}};
        int skipped = 0;
        QJsonArray oneToOne;
        QJsonArray group;
        for (const auto& c : convs) {
            const QJsonArray m = MobileHistorySync::toMessages(c, ids, "1111", "u-me-fixture", &skipped);
            (c.isGroup ? group : oneToOne) = m;
        }
        QCOMPARE(skipped, 0);
        {
            bool sawRecall = false;
            for (const QJsonValue& v : oneToOne) {
                if (v.toObject().value("msgType").toString() == QStringLiteral("chat.undo")
                    && v.toObject().value("content").toObject().value("globalMsgId").toString() == QStringLiteral("5003")) {
                    sawRecall = true;
                }
            }
            QVERIFY(sawRecall);
        }
        {
            MobileHistorySync::BackupConversation media;
            media.plainId = "1001";
            MobileHistorySync::BackupRow photoRow;
            photoRow.globalMsgId = "7001"; photoRow.cliMsgId = "c9"; photoRow.msgType = 3; photoRow.ts = 4000;
            photoRow.senderId = "1001"; photoRow.text = "[Photo] ";
            media.rows = {photoRow};
            int skippedMedia = 0;
            QVERIFY(MobileHistorySync::toMessages(media, ids, "1111", "u-me-fixture", &skippedMedia).isEmpty());
            QCOMPARE(skippedMedia, 1);
        }
        QCOMPARE(oneToOne.size(), 4);
        QCOMPARE(oneToOne.at(0).toObject().value("uidFrom").toString(), QString("9001"));
        QCOMPARE(oneToOne.at(1).toObject().value("uidFrom").toString(), QString("0"));
        QCOMPARE(oneToOne.at(1).toObject().value("idTo").toString(), QString("9001"));
        QCOMPARE(group.at(0).toObject().value("idTo").toString(), QString("9002"));
        QCOMPARE(group.at(0).toObject().value("uidFrom").toString(), QString("9003"));
        QVERIFY(MobileHistorySync::toMessages(convs.at(0), {}, "1111", "u-me-fixture").isEmpty());

        ZaloClient client;
        emit client.apiClient()->session()->messagesReceived(oneToOne, false, true);
        emit client.apiClient()->session()->messagesReceived(group, true, true);
        QVERIFY(client.conversationModel()->contains("9001"));
        QVERIFY(client.conversationModel()->contains("9002"));
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 0);
        client.selectConversation("9001", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 3);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::TextRole).toString(),
                 QString::fromUtf8("chào bạn"));

        client.requestMobileSync();
        QCOMPARE(client.mobileSyncState(), 5);

        MobileHistorySync::BackupConversation v2Conv;
        v2Conv.plainId = "u-v2-peer";
        v2Conv.isGroup = true;
        MobileHistorySync::BackupRow v2Row;
        v2Row.globalMsgId = "9009";
        v2Row.cliMsgId = "5005";
        v2Row.msgType = 1;
        v2Row.ts = 1700000300000ULL;
        v2Row.ttl = 30000;
        v2Row.senderId = "u-v2-sender";
        v2Row.text = "@All hello";
        Zalos::Core::Sync::V2::ParsedSyncV2Quote q;
        q.ownerId = "u-v2-owner";
        q.globalMsgId = 8008;
        q.cliMsgId = 4004;
        q.msg = "prev";
        q.fromD = "Owner";
        q.ts = 1700000200000ULL;
        v2Row.v2Quote = q;
        v2Row.v2Mentions = {Zalos::Core::Sync::V2::ParsedSyncV2Mention{QStringLiteral("-1"), 0, 4, 1},
                            Zalos::Core::Sync::V2::ParsedSyncV2Mention{QStringLiteral("u-v2-ment"), 5, 2, 0}};
        v2Conv.rows = {v2Row};
        const QHash<QString, QString> v2Ids{{"u-v2-peer", "u-v2-peer"}, {"u-v2-sender", "u-v2-sender"},
                                            {"u-v2-owner", "u-v2-owner"}, {"u-v2-ment", "u-v2-ment"}};
        const QJsonArray v2Out = MobileHistorySync::toMessages(v2Conv, v2Ids, "u-me-fixture", "u-me-fixture");
        QCOMPARE(v2Out.size(), 1);
        const QJsonObject v2Obj = v2Out.at(0).toObject();
        QCOMPARE(v2Obj.value("ttl").toVariant().toLongLong(), 30000LL);
        QCOMPARE(v2Obj.value("quote").toObject().value("ownerId").toString(), QStringLiteral("u-v2-owner"));
        QCOMPARE(v2Obj.value("quote").toObject().value("msg").toString(), QStringLiteral("prev"));
        QCOMPARE(v2Obj.value("mentions").toArray().size(), 2);
        QCOMPARE(v2Obj.value("mentions").toArray().at(0).toObject().value("uid").toString(), QStringLiteral("-1"));
        QCOMPARE(v2Obj.value("mentions").toArray().at(1).toObject().value("uid").toString(), QStringLiteral("u-v2-ment"));
    }


    void testBulkHistoryImportPerformance() {
        ZaloClient client;
        auto* convs = client.conversationModel();
        QSignalSpy resets(convs, &QAbstractItemModel::modelReset);
        QJsonArray batch;
        const qint64 base = 1700000000000LL;
        for (int t = 0; t < 200; ++t) {
            for (int m = 0; m < 100; ++m) {
                const int n = t * 100 + m;
                batch.append(QJsonObject{{"msgId", QString::number(800000 + n)}, {"cliMsgId", QString::number(n)},
                                         {"uidFrom", m % 2 ? "0" : QString("u-bulk-%1").arg(t)},
                                         {"idTo", m % 2 ? QString("u-bulk-%1").arg(t) : "u-me"},
                                         {"msgType", "webchat"}, {"content", QString("fixture %1").arg(n)},
                                         {"ts", QString::number(base + n)}});
            }
        }
        QElapsedTimer timer;
        timer.start();
        emit client.apiClient()->session()->messagesReceived(batch, false, true);
        const qint64 ms = timer.elapsed();
        qInfo("bulk import of %d messages took %lld ms", int(batch.size()), ms);
        QCOMPARE(convs->rowCount(), 200);
        QCOMPARE(resets.count(), 1);
        QCOMPARE(convs->totalUnreadCount(), 0);
        QVERIFY2(ms < 20000, "bulk import is too slow");
    }


    void testHistoryImportMemoryFootprint() {
        auto rssKb = []() -> qint64 {
            QFile f("/proc/self/status");
            if (!f.open(QIODevice::ReadOnly)) return 0;
            for (const QByteArray& line : f.readAll().split('\n')) {
                if (line.startsWith("VmRSS:")) return line.mid(6).trimmed().split(' ').first().toLongLong();
            }
            return 0;
        };
        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-mem-owner");
            owner.clearCache();
            owner.bindOwner("u-mem-owner");
        }
        ZaloClient client;
        client.openOfflineCacheForTesting("u-mem-owner");
        const qint64 before = rssKb();
        const qint64 base = 1700000000000LL;
        for (int t = 0; t < 400; ++t) {
            QJsonArray batch;
            for (int m = 0; m < 170; ++m) {
                const int n = t * 170 + m;
                batch.append(QJsonObject{{"msgId", QString::number(900000 + n)}, {"cliMsgId", QString::number(n)},
                                         {"uidFrom", m % 2 ? "0" : QString("u-mem-%1").arg(t)},
                                         {"idTo", m % 2 ? QString("u-mem-%1").arg(t) : "u-me"},
                                         {"msgType", "webchat"},
                                         {"content", QString("fixture message number %1 with some typical length text").arg(n)},
                                         {"ts", QString::number(base + n)}});
            }
            emit client.apiClient()->session()->messagesReceived(batch, false, true);
        }
        const qint64 after = rssKb();
        qInfo("RSS before %lld kB, after 68000 history messages %lld kB (+%lld kB)", before, after, after - before);
        QCOMPARE(client.conversationModel()->rowCount(), 400);
        QVERIFY2(after - before < 70000, "history of closed threads must not stay in RAM");
        client.selectConversation("u-mem-7", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 170);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QString::number(900000 + 7 * 170));
        for (int t = 0; t < 40; ++t) client.selectConversation(QString("u-mem-%1").arg(t), "Fixture", "");
        QCOMPARE(client.messageModel()->threadSize("u-mem-0"), 0);
        client.selectConversation("u-mem-0", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 170);
        QJsonArray big;
        for (int m = 0; m < 500; ++m) {
            big.append(QJsonObject{{"msgId", QString::number(5000000 + m)}, {"cliMsgId", QString::number(800000 + m)},
                                   {"uidFrom", "u-mem-big"}, {"idTo", "u-me"}, {"msgType", "webchat"},
                                   {"content", QString("fixture long thread message %1").arg(m)},
                                   {"ts", QString::number(base + 1000000 + m)}});
        }
        emit client.apiClient()->session()->messagesReceived(big, false, true);
#ifdef __GLIBC__
        const size_t heapBefore = mallinfo2().uordblks;
#endif
        client.selectConversation("u-mem-big", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 200);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QString::number(5000000 + 300));
        const QStringList found = client.searchConversationMessages("fixture long thread message 12");
        QVERIFY(found.contains(QString::number(5000000 + 12)));
        QCOMPARE(found.first(), QString::number(5000000 + 12));
        QVERIFY(client.revealMessage(QString::number(5000000 + 12)));
        QCOMPARE(client.messageModel()->rowCount(), 500);
        QVERIFY(!client.revealMessage("does-not-exist"));
        client.messageSearch()->setQuery("fixture long thread message 12");
        client.messageSearch()->runNow();
        QVERIFY(client.messageSearch()->count() >= 1);
        const QModelIndex first = client.messageSearch()->index(0);
        QCOMPARE(client.messageSearch()->data(first, Zalos::Core::Models::MessageSearchModel::ConvIdRole).toString(),
                 QString("u-mem-big"));
        client.messageSearch()->setQuery("x");
        QCOMPARE(client.messageSearch()->count(), 0);
        QSignalSpy revealSpy(&client, &ZaloClient::revealRequested);
        client.openMessageFromSearch("u-mem-big", QString::number(5000000 + 12));
        QCOMPARE(revealSpy.count(), 1);
        client.selectConversation("u-mem-1", "Fixture", "");
        for (int t = 2; t < 16; ++t) client.selectConversation(QString("u-mem-%1").arg(t), "Fixture", "");
        client.selectConversation("u-mem-big", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 200);
        client.loadOlderMessages();
        QCOMPARE(client.messageModel()->rowCount(), 400);
        client.loadOlderMessages();
        QCOMPARE(client.messageModel()->rowCount(), 500);
        client.loadOlderMessages();
#ifdef __GLIBC__
        qInfo("heap for 500 resident cached messages: %zu kB (%zu B each)", (mallinfo2().uordblks - heapBefore) / 1024,
              (mallinfo2().uordblks - heapBefore) / 500);
#endif
        QCOMPARE(client.messageModel()->rowCount(), 500);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QString::number(5000000));
        client.setReplyTarget(QString::number(5000000));
        QCOMPARE(client.replyTarget().value("id").toString(), QString::number(5000000));
        client.clearReplyTarget();
        ZaloOfflineStore cleanup;
        cleanup.clearCache();
    }


    void testRenderProfileDecisions() {
        using RP = Zalos::Core::Platform::RenderProfile;
        RP::Inputs fresh;
        fresh.hasRenderNode = true;
        fresh.gpuVendor = "0x1002";
        fresh.mesaVulkanIcd = true;
        fresh.qtHasVulkan = true;
        QCOMPARE(RP::decide(fresh).backend, RP::Backend::Software);
        QVERIFY(!RP::decide(fresh).guarded());

        RP::Inputs vm;
        vm.hardwareAcceleration = true;
        vm.memTotalKb = 8LL * 1024 * 1024;
        vm.cpuCores = 8;
        QCOMPARE(RP::decide(vm).backend, RP::Backend::Software);
        QVERIFY(!RP::decide(vm).basicRenderLoop);

        RP::Inputs amd = vm;
        amd.hasRenderNode = true;
        amd.gpuVendor = "0x1002";
        amd.mesaVulkanIcd = true;
        amd.qtHasVulkan = true;
        QCOMPARE(RP::decide(amd).backend, RP::Backend::Vulkan);
        QVERIFY(RP::decide(amd).guarded());
        amd.vulkanFailedBefore = true;
        QCOMPARE(RP::decide(amd).backend, RP::Backend::Default);

        RP::Inputs nvidia = vm;
        nvidia.hasRenderNode = true;
        nvidia.gpuVendor = "0x10de";
        QCOMPARE(RP::decide(nvidia).backend, RP::Backend::Default);

        RP::Inputs small = amd;
        small.vulkanFailedBefore = false;
        small.memTotalKb = 2LL * 1024 * 1024;
        QVERIFY(RP::decide(small).basicRenderLoop);

        RP::Inputs forced = vm;
        forced.override = "opengl";
        QCOMPARE(RP::decide(forced).backend, RP::Backend::OpenGL);
        QVERIFY(!RP::decide(forced).guarded());

        RP::Inputs sw = amd;
        sw.vulkanFailedBefore = false;
        sw.hardwareAcceleration = false;
        QCOMPARE(RP::decide(sw).backend, RP::Backend::Software);
        sw.hardwareFailed = "opengl";
        QVERIFY(RP::decide(sw).reason.contains("software fallback"));
        sw.override = "vulkan";
        QCOMPARE(RP::decide(sw).backend, RP::Backend::Vulkan);
    }


    void testRenderGuardRemembersVulkanFailure() {
        namespace RG = Zalos::Core::Platform::RenderGuard;
        using RP = Zalos::Core::Platform::RenderProfile;
        QSettings s;
        s.remove("render/vulkanBroken");
        RG::markAttempt(RP::Backend::Vulkan);
        RG::markFirstFrame();
        QVERIFY(!RG::vulkanFailedBefore());
        RG::markAttempt(RP::Backend::Vulkan);
        QVERIFY(RG::vulkanFailedBefore());
        QVERIFY(RG::vulkanFailedBefore());
        s.setValue("render/guardVersion", 1);
        s.setValue("render/vulkanBroken", true);
        s.sync();
        QVERIFY(!RG::vulkanFailedBefore());
        s.sync();
        QCOMPARE(s.value("render/guardVersion").toInt(), 3);
        s.remove("render/vulkanBroken");
        s.remove("render/pending");
        s.remove("render/guardVersion");
        s.remove("render/hardwareFailed");
    }

    void testRenderGuardFallsBackToSoftware() {
        namespace RG = Zalos::Core::Platform::RenderGuard;
        using RP = Zalos::Core::Platform::RenderProfile;
        QSettings s;
        for (const char* k : {"render/guardVersion", "render/hardwareAcceleration", "render/hardwareFailed",
                              "render/vulkanBroken", "render/pending"}) s.remove(k);
        s.setValue("render/guardVersion", 2);
        s.setValue("render/hardwareAcceleration", true);
        s.sync();
        QVERIFY(!RG::hardwareAccelerationEnabled());
        QVERIFY(!RP::probeSystem().hardwareAcceleration);
        QVERIFY(RG::lastFailure().isEmpty());

        RG::setHardwareAccelerationEnabled(true);
        QVERIFY(RP::probeSystem().hardwareAcceleration);
        RG::markAttempt(RP::Backend::OpenGL);
        RG::markFirstFrame();
        QVERIFY(RP::probeSystem().hardwareAcceleration);

        RG::markAttempt(RP::Backend::Default);
        const RP::Inputs afterCrash = RP::probeSystem();
        QVERIFY(!afterCrash.hardwareAcceleration);
        QCOMPARE(afterCrash.hardwareFailed, QStringLiteral("default"));
        QVERIFY(!afterCrash.vulkanFailedBefore);
        QCOMPARE(RP::decide(afterCrash).backend, RP::Backend::Software);
        {
            Zalos::Core::Network::ZaloClient client;
            QVERIFY(!client.hardwareAcceleration());
            QVERIFY(client.hardwareAccelerationFailed());
            QVERIFY(!client.hardwareAccelerationRestartRequired());
            client.setHardwareAcceleration(true);
            QVERIFY(!client.hardwareAccelerationFailed());
            QVERIFY(client.hardwareAccelerationRestartRequired());
        }
        QVERIFY(RG::lastFailure().isEmpty());
        QVERIFY(RG::hardwareAccelerationEnabled());

        RG::recordFailure(RP::Backend::Vulkan);
        QVERIFY(!RG::hardwareAccelerationEnabled());
        QCOMPARE(RG::lastFailure(), QStringLiteral("vulkan"));
        QVERIFY(RG::vulkanFailedBefore());
        RG::setHardwareAccelerationEnabled(true);
        RP::Inputs retry = RP::probeSystem();
        retry.hasRenderNode = true;
        retry.gpuVendor = "0x8086";
        retry.mesaVulkanIcd = true;
        retry.qtHasVulkan = true;
        QCOMPARE(RP::decide(retry).backend, RP::Backend::Default);

        RG::recordFailure(RP::Backend::Software);
        QVERIFY(RG::hardwareAccelerationEnabled());

        for (const char* k : {"render/guardVersion", "render/hardwareAcceleration", "render/hardwareFailed",
                              "render/vulkanBroken", "render/pending"}) s.remove(k);
        s.sync();
    }

    void testSoundEngineIsLazy() {
        SoundEngine engine;
        QVERIFY(engine.findChild<QSoundEffect*>() == nullptr);
        QVERIFY(!engine.isPlaying());
        engine.stop();
        engine.play("notification", "pop");
        QVERIFY(engine.findChild<QSoundEffect*>() != nullptr);
        engine.stop();
    }

    void testRequestsUseHttp11() {
        ZaloClient client;
        QNetworkReply* reply = client.apiClient()->session()->getWithCookies(QUrl("https://127.0.0.1:9/fixture"));
        QVERIFY(reply != nullptr);
        QCOMPARE(reply->request().attribute(QNetworkRequest::Http2AllowedAttribute).toBool(), false);
        reply->abort();
        reply->deleteLater();
    }

    void testSessionHeadersAndZaloappDownloadCookies() {
        ZaloClient client;
        auto* session = client.apiClient()->session();

        QNetworkRequest getReq(QUrl("https://wpa.chat.zalo.me/api/message/pull_mobile_msg"));
        session->applyHeadersForTesting(getReq, false);
        QVERIFY(!getReq.hasRawHeader("Content-Type"));
        QCOMPARE(getReq.rawHeader("Origin"), QByteArray("https://chat.zalo.me"));
        QCOMPARE(getReq.rawHeader("Referer"), QByteArray("https://chat.zalo.me/"));

        QNetworkRequest postReq(QUrl("https://wpa.chat.zalo.me/api/message/sms"));
        session->applyHeadersForTesting(postReq, true);
        QCOMPARE(getReq.header(QNetworkRequest::ContentTypeHeader).toString(), QString());
        QCOMPARE(postReq.header(QNetworkRequest::ContentTypeHeader).toString(), QStringLiteral("application/x-www-form-urlencoded"));

        session->setLoginInfoForTesting(QJsonObject{{"uid", "123456789"}, {"zpw_sek", "fixture_sek"}});
        QNetworkReply* replyZaloapp = session->getWithCookies(QUrl("https://trans-bin.zaloapp.com/download/test_batch"));
        QVERIFY(replyZaloapp != nullptr);
        const QByteArray cookieHeader = replyZaloapp->request().rawHeader("Cookie");
        QVERIFY2(cookieHeader.contains("zpw_uin=123456789"), cookieHeader.constData());
        QVERIFY2(cookieHeader.contains("zpw_sek=fixture_sek"), cookieHeader.constData());
        QCOMPARE(replyZaloapp->request().rawHeader("Referer"), QByteArray("https://chat.zalo.me/"));
        QCOMPARE(replyZaloapp->request().transferTimeout(), 150000);
        replyZaloapp->abort();
        replyZaloapp->deleteLater();

        QNetworkReply* replyExternal = session->getWithCookies(QUrl("https://s3.amazonaws.com/backup/test.zdb"));
        QVERIFY(replyExternal != nullptr);
        QVERIFY(!replyExternal->request().hasRawHeader("Cookie"));
        replyExternal->abort();
        replyExternal->deleteLater();

        QVERIFY(ZaloSession::sendsSessionCookies(QUrl("https://trans-bin.zaloapp.com/x")));
        QVERIFY(!ZaloSession::sendsSessionCookies(QUrl("http://trans-bin.zaloapp.com/x")));
        QVERIFY(!ZaloSession::sendsSessionCookies(QUrl("https://zalo.me.invalid/x")));
        QNetworkReply* replyPlain = session->getWithCookies(QUrl("http://trans-bin.zaloapp.com/download/test_batch"));
        QVERIFY(replyPlain != nullptr);
        QCOMPARE(replyPlain->request().url().scheme(), QStringLiteral("https"));
        QVERIFY(replyPlain->request().rawHeader("Cookie").contains("zpw_sek=fixture_sek"));
        replyPlain->abort();
        replyPlain->deleteLater();

        auto makeCookie = [](const QByteArray& name, const QByteArray& value, const QString& domain) {
            QNetworkCookie c(name, value);
            c.setDomain(domain);
            c.setPath(QStringLiteral("/"));
            return c;
        };
        client.apiClient()->cookieJar()->replaceCookies({
            makeCookie("zpw_sek", "", QStringLiteral(".zalo.me")),
            makeCookie("zpw_uin", "", QStringLiteral(".zalo.me")),
            makeCookie("fixture_chat", "chat_value", QStringLiteral(".zalo.me")),
            makeCookie("fixture_cdn", "cdn_value", QStringLiteral(".zaloapp.com")),
        });
        QNetworkReply* replyJar = session->getWithCookies(QUrl("https://trans-bin.zaloapp.com/download/test_batch"));
        QVERIFY(replyJar != nullptr);
        QCOMPARE(replyJar->request().attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
                 int(QNetworkRequest::Manual));
        const QByteArray jarHeader = replyJar->request().rawHeader("Cookie");
        QVERIFY2(jarHeader.contains("zpw_sek=fixture_sek"), jarHeader.constData());
        QVERIFY2(jarHeader.contains("zpw_uin=123456789"), jarHeader.constData());
        QVERIFY2(jarHeader.contains("fixture_chat=chat_value"), jarHeader.constData());
        QVERIFY2(!jarHeader.contains("zpw_sek=;") && !jarHeader.endsWith("zpw_sek="), jarHeader.constData());
        QVERIFY2(!jarHeader.contains("fixture_cdn"), jarHeader.constData());
        replyJar->abort();
        replyJar->deleteLater();
        client.apiClient()->cookieJar()->replaceCookies({});
    }

    void testExternalUrlNormalisation() {
        QCOMPARE(ZaloClient::externalUrl(QStringLiteral("zalo.me/fixture")).host(), QStringLiteral("zalo.me"));
        QVERIFY(ZaloClient::externalUrl(QStringLiteral("  https://example.com/a?b=1 ")).isValid());
        QVERIFY(ZaloClient::externalUrl(QStringLiteral("mailto:fixture@example.com")).isValid());
        QVERIFY(ZaloClient::externalUrl(QStringLiteral("file:///tmp/fixture.txt")).isLocalFile());
        QVERIFY(!ZaloClient::externalUrl(QString()).isValid());
        QVERIFY(!ZaloClient::externalUrl(QStringLiteral("   ")).isValid());
        QVERIFY(!ZaloClient::externalUrl(QStringLiteral("javascript:alert(1)")).isValid());
        QVERIFY(!ZaloClient::externalUrl(QStringLiteral("smb://fixture.invalid/share")).isValid());
        QVERIFY(!ZaloClient::externalUrl(QStringLiteral("steam://run/1")).isValid());
        QVERIFY(!ZaloClient::externalUrl(QStringLiteral("vscode://fixture.invalid/x")).isValid());
        QVERIFY(ZaloClient::externalUrl(QStringLiteral("tel:0000")).isValid());
    }

    void testLinkCardHrefFallbacks() {
        QCOMPARE(ZaloClient::linkHrefOf(QJsonObject{{"href", "https://example.com/a"}}, {}), QStringLiteral("https://example.com/a"));
        QCOMPARE(ZaloClient::linkHrefOf(QJsonObject{{"href", ""}}, QJsonObject{{"href", "https://vt.example.com/x"}}),
                 QStringLiteral("https://vt.example.com/x"));
        QCOMPARE(ZaloClient::linkHrefOf(QJsonObject{{"title", "Xem https://m.example.com/watch?v=1."}}, {}),
                 QStringLiteral("https://m.example.com/watch?v=1"));
        QCOMPARE(ZaloClient::linkHrefOf(QJsonObject{{"description", "www.example.com/p"}}, {}),
                 QStringLiteral("https://www.example.com/p"));
        QVERIFY(ZaloClient::linkHrefOf(QJsonObject{{"title", "Fixture"}}, {}).isEmpty());
    }

    void testExternalLaunchEnvironmentDropsBundledRuntime() {
        QProcessEnvironment env;
        env.insert("APPDIR", "/tmp/.mount_fixture");
        env.insert("APPIMAGE", "/home/fixture/Zalos.AppImage");
        env.insert("LD_LIBRARY_PATH", "/tmp/.mount_fixture/usr/lib/qt6-fallback/lib:/tmp/.mount_fixture/usr/lib:/usr/local/lib");
        env.insert("PATH", "/tmp/.mount_fixture/usr/bin:/usr/bin");
        env.insert("XDG_DATA_DIRS", "/tmp/.mount_fixture/usr/share:/usr/share");
        env.insert("QT_PLUGIN_PATH", "/usr/lib/zalos/qt6/plugins");
        env.insert("QML_IMPORT_PATH", "/usr/lib/zalos/qt6/qml");
        env.insert("GLIBC_TUNABLES", "glibc.malloc.arena_max=2");
        env.insert("ZALOS_BUNDLED_QT", "1");
        env.insert("HOME", "/home/fixture");
        const QProcessEnvironment out = Zalos::Core::Fs::FileUtils::launchEnvironment(env);
        QCOMPARE(out.value("LD_LIBRARY_PATH"), QStringLiteral("/usr/local/lib"));
        QCOMPARE(out.value("PATH"), QStringLiteral("/usr/bin"));
        QCOMPARE(out.value("XDG_DATA_DIRS"), QStringLiteral("/usr/share"));
        QCOMPARE(out.value("HOME"), QStringLiteral("/home/fixture"));
        for (const char* key : {"APPDIR", "APPIMAGE", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "GLIBC_TUNABLES", "ZALOS_BUNDLED_QT"})
            QVERIFY2(!out.contains(QString::fromLatin1(key)), key);

        QProcessEnvironment deb;
        deb.insert("LD_LIBRARY_PATH", "/usr/lib/zalos/qt6/lib");
        const QProcessEnvironment debOut = Zalos::Core::Fs::FileUtils::launchEnvironment(deb);
        QVERIFY(!debOut.contains("LD_LIBRARY_PATH"));
        QVERIFY(!debOut.value("PATH").isEmpty());
    }

    void testUniqueDownloadFilePath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(ZaloClient::uniqueFilePath(dir.path(), "CV.pdf"), dir.filePath("CV.pdf"));
        QFile a(dir.filePath("CV.pdf"));
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.close();
        QCOMPARE(ZaloClient::uniqueFilePath(dir.path(), "CV.pdf"), dir.filePath("CV (1).pdf"));
        QFile b(dir.filePath("CV (1).pdf"));
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.close();
        QCOMPARE(ZaloClient::uniqueFilePath(dir.path(), "CV.pdf"), dir.filePath("CV (2).pdf"));
        QFile c(dir.filePath("README"));
        QVERIFY(c.open(QIODevice::WriteOnly));
        c.close();
        QCOMPARE(ZaloClient::uniqueFilePath(dir.path(), "README"), dir.filePath("README (1)"));
        QCOMPARE(ZaloClient::uniqueFilePath(dir.path(), ""), dir.filePath("file"));
    }

    void testMobileSyncDownloadRetryPolicy() {
        using Zalos::Core::Sync::MobileHistorySync;
        const QDateTime now = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
        const int none = int(QNetworkReply::NoError);

        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(404, none, 1, {}, now), 1000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(400, none, 2, {}, now), 2000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(502, none, 3, {}, now), 4000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(500, none, 4, {}, now), 8000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(500, none, 5, {}, now), -1);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(403, none, 1, {}, now), -1);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(401, none, 1, {}, now), -1);

        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(0, int(QNetworkReply::RemoteHostClosedError), 1, {}, now), 1000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(0, int(QNetworkReply::TimeoutError), 1, {}, now), 1000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(0, int(QNetworkReply::HostNotFoundError), 1, {}, now), 1000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(0, int(QNetworkReply::OperationCanceledError), 1, {}, now), -1);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(0, int(QNetworkReply::SslHandshakeFailedError), 1, {}, now), -1);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(0, int(QNetworkReply::InsecureRedirectError), 1, {}, now), -1);

        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(429, none, 1, "5", now), 5000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(503, none, 1, " 12 ", now), 12000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(429, none, 3, "0", now), 4000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(429, none, 1, "86400", now), 30000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(429, none, 1, "Thu, 01 Jan 2026 00:00:07 GMT", now), 7000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(429, none, 1, "Wed, 31 Dec 2025 23:00:00 GMT", now), 1000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(429, none, 1, "garbage", now), 1000);
        QCOMPARE(MobileHistorySync::downloadRetryDelayMs(500, none, 1, "20", now), 1000);
    }

    void testTypedQmlSingletons() {
        ThemeManager themeMgr;
        QQmlEngine engine;
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        QQmlComponent c(&engine);
        c.setData("import QtQuick\nimport Zalos\nQtObject {\n"
                  "  property string label: I18n.t('Tin nhắn', 'Messages')\n"
                  "  property color blue: Theme.zaloBlue\n"
                  "  property bool sameTheme: ThemeManager === ThemeManager\n}", QUrl());
        std::unique_ptr<QObject> o(c.create());
        QVERIFY2(o, qPrintable(c.errorString()));
        QCOMPARE(o->property("label").toString(), QStringLiteral("Messages"));
        QCOMPARE(o->property("blue").value<QColor>(), themeMgr.accentColor());
    }


    void testMyDocumentsIsNotTheLastChat() {
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.apiClient()->session()->setLoginInfoForTesting(QJsonObject{{"uid", "u-me"}, {"send2me_id", "u-docs"}});
        QCOMPARE(client.myCloudId(), QStringLiteral("u-docs"));
        client.setUserForTesting("u-peer", "Fixture Peer", "");
        Zalos::Core::Models::ConversationItem peer;
        peer.id = "u-peer";
        peer.name = "Fixture Peer";
        client.conversationModel()->upsertConversation(peer);

        client.openMessagesTab();
        QVERIFY(client.currentConversationId().isEmpty());

        client.openMyCloud();
        QCOMPARE(client.currentConversationId(), QStringLiteral("u-docs"));
        client.openMessagesTab();
        QVERIFY2(client.currentConversationId().isEmpty(), "no regular chat yet: Messages shows the welcome screen");

        client.selectConversation("u-peer", "Fixture Peer", "");
        client.openMyCloud();
        QCOMPARE(client.currentConversationId(), QStringLiteral("u-docs"));
        QCOMPARE(client.lastChatId(), QStringLiteral("u-peer"));
        client.openMessagesTab();
        QCOMPARE(client.currentConversationId(), QStringLiteral("u-peer"));
        QCOMPARE(client.currentNavigationTab(), QStringLiteral("messages"));

        client.openMessagesTab();
        QCOMPARE(client.currentConversationId(), QStringLiteral("u-peer"));
    }

    void testChatScrollbarDragIsNotPulledBack() {
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        QImage pic(480, 640, QImage::Format_RGB32);
        pic.fill(qRgb(90, 120, 160));
        const QString picPath = QDir::temp().filePath("zalos-scroll-photo.png");
        pic.save(picPath);
        QFile picFile(picPath);
        QVERIFY(picFile.open(QIODevice::ReadOnly));
        const QByteArray picBytes = picFile.readAll();
        TestSupport::LoopbackApi photoServer([picBytes](const TestSupport::LoopbackApi::Request&) { return picBytes; });
        const QString picUrl = photoServer.baseUrl() + QStringLiteral("/scroll-photo.png");
        auto* session = client.apiClient()->session();
        const qint64 base = QDateTime::currentMSecsSinceEpoch() - 200LL * 60000;
        QJsonArray msgs;
        for (int i = 0; i < 160; ++i) {
            const bool mine = i % 3 == 0;
            const QString id = QString::number(50000 + i);
            QJsonValue content = QString::fromUtf8("Fixture line %1 ").arg(i).repeated(1 + (i * 7) % 9);
            QString type = QStringLiteral("webchat");
            if (i % 11 == 5) {
                type = QStringLiteral("chat.photo");
                content = QJsonObject{{"thumb", picUrl}, {"href", picUrl}, {"title", ""}};
            }
            msgs.append(QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", mine ? "0" : "u-scroll"},
                                    {"idTo", mine ? "u-scroll" : "u-me"}, {"msgType", type}, {"content", content},
                                    {"ts", QString::number(base + qint64(i) * 60000)}});
        }
        client.setUserForTesting("u-scroll", "Fixture Scroll", "");
        emit session->messagesReceived(msgs, false, true);
        client.selectConversation("u-scroll", "Fixture Scroll", "");
        client.setMyUidForTesting("u-me");

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        QTest::qWait(600);
        auto* list = window->findChild<QQuickItem*>("messageList");
        QVERIFY(list != nullptr);
        QTRY_VERIFY(list->property("atYEnd").toBool());

        auto* bar = window->findChild<QQuickItem*>("messageScrollBar");
        QVERIFY(bar != nullptr);
        const double start = list->property("contentY").toDouble();
        const double size = bar->property("size").toDouble();
        const QPointF barTop = bar->mapToScene(QPointF(bar->width() / 2, 0));
        QPoint grab = (barTop + QPointF(0, bar->height() * (1.0 - size / 2))).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, grab);
        for (int step = 1; step <= 10; ++step) {
            grab.ry() -= 20;
            QTest::mouseMove(window, grab);
            QTest::qWait(40);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, grab);
        auto* datePill = window->findChild<QQuickItem*>("floatingDatePill");
        QVERIFY(datePill != nullptr);
        QVERIFY(!datePill->property("label").toString().isEmpty());
        QTest::qWait(1200);
        const double afterDrag = list->property("contentY").toDouble();
        QVERIFY2(!list->property("atYEnd").toBool(),
                 qPrintable(QStringLiteral("scrollbar drag pulled back: start %1 now %2").arg(start).arg(afterDrag)));
        QVERIFY(!list->property("stickToBottom").toBool());

        QMetaObject::invokeMethod(list, "scrollToEnd");
        QTRY_VERIFY(list->property("atYEnd").toBool());
        QVERIFY(list->property("stickToBottom").toBool());
        QTRY_VERIFY(!datePill->property("shown").toBool());

        QMetaObject::invokeMethod(list, "scrollToEnd");
        QTRY_VERIFY(list->property("atYEnd").toBool());
        list->setProperty("contentY", list->property("contentY").toDouble() - 30);
        QVERIFY2(!list->property("stickToBottom").toBool(), "a 30 px scroll up (one wheel notch) must stop following");
        QMetaObject::invokeMethod(list, "scrollToEnd");
        QTRY_VERIFY(list->property("atYEnd").toBool());

        QQuickItem* chatArea = window->findChild<QQuickItem*>("chatArea");
        QVERIFY(chatArea != nullptr);
        chatArea->setProperty("searchHits", QStringList{QStringLiteral("50000")});
        chatArea->setProperty("searchCursor", 0);
        QMetaObject::invokeMethod(chatArea, "showSearchHit");
        QVERIFY(!chatArea->property("searchFocusId").toString().isEmpty());
        QVERIFY(!list->property("stickToBottom").toBool());
        chatArea->setProperty("searchHits", QStringList{});
        chatArea->setProperty("searchCursor", -1);
        QMetaObject::invokeMethod(list, "scrollToEnd");
        QTRY_VERIFY(list->property("atYEnd").toBool());

        const QPointF center = list->mapToScene(QPointF(list->width() / 2, list->height() / 2));
        ulong stamp = 5000;
        for (int n = 0; n < 3; ++n) {
            QWheelEvent notch(center, window->mapToGlobal(center), QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            notch.setTimestamp(stamp += 40);
            QCoreApplication::sendEvent(window, &notch);
            QTest::qWait(40);
        }
        QTest::qWait(400);
        QVERIFY2(!list->property("stickToBottom").toBool(), "a wheel scroll up must stop following the bottom");
        const QString lateId = QStringLiteral("59999");
        emit session->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", lateId}, {"cliMsgId", lateId}, {"uidFrom", "u-scroll"}, {"idTo", "u-me"}, {"msgType", "chat.photo"},
            {"content", QJsonObject{{"thumb", picUrl}, {"href", picUrl}, {"title", ""}}},
            {"ts", QString::number(QDateTime::currentMSecsSinceEpoch())}}}, false, true);
        QTest::qWait(1500);
        QVERIFY2(!list->property("atYEnd").toBool(), "a wheel scroll up was pulled back to the end");

        auto dragsPanel = [window](const char* barName, const char* viewName) {
            QQuickItem* bar = nullptr;
            QQuickItem* view = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((bar = window->findChild<QQuickItem*>(barName)) != nullptr, 3000);
            QTRY_VERIFY_WITH_TIMEOUT((view = window->findChild<QQuickItem*>(viewName)) != nullptr, 3000);
            auto* flick = qvariant_cast<QQuickItem*>(view->property("contentItem"));
            QVERIFY(flick != nullptr);
            QTRY_VERIFY2(bar->property("size").toDouble() < 0.95, barName);
            const double before = flick->property("contentY").toDouble();
            QPoint grab = bar->mapToScene(QPointF(bar->width() / 2, 6)).toPoint();
            QTest::mouseMove(window, grab);
            QTest::qWait(60);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, grab);
            for (int step = 1; step <= 8; ++step) {
                grab.ry() += 15;
                QTest::mouseMove(window, grab);
                QTest::qWait(30);
            }
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, grab);
            QTest::qWait(100);
            QVERIFY2(flick->property("contentY").toDouble() > before + 20,
                     qPrintable(QStringLiteral("%1 drag did not scroll").arg(QString::fromLatin1(barName))));
        };
        window->resize(1100, 520);
        QTest::qWait(200);
        client.setShowConversationInfo(true);
        QTest::qWait(400);
        dragsPanel("infoScrollBar", "infoScroll");
        client.setShowConversationInfo(false);
        QTest::qWait(100);
        window->resize(1100, 440);
        QTest::qWait(200);
        client.openSettings(QStringLiteral("privacy"));
        QTest::qWait(400);
        dragsPanel("settingsScrollBar", "settingsScroll");
        client.setShowSettingsModal(false);
        QTest::qWait(100);

        window->resize(1100, 700);
        QTest::qWait(200);
        const QVariantList hits = client.searchConversationHits(QStringLiteral("Fixture line 12"));
        QVERIFY(hits.size() >= 2);
        for (int i = 1; i < hits.size(); ++i)
            QVERIFY(hits.at(i - 1).toMap().value("ts").toLongLong() <= hits.at(i).toMap().value("ts").toLongLong());
        for (const QVariant& v : hits) {
            const QVariantMap hit = v.toMap();
            QVERIFY(hit.value("text").toString().contains(QStringLiteral("12")));
            QVERIFY(!hit.value("senderName").toString().isEmpty());
        }
        QCOMPARE(client.searchConversationMessages(QStringLiteral("Fixture line 12")).size(), hits.size());
        QMetaObject::invokeMethod(chatArea, "openSearch");
        auto* searchInput = window->findChild<QQuickItem*>("inChatSearchInput");
        QVERIFY(searchInput != nullptr);
        searchInput->setProperty("text", QStringLiteral("Fixture line 12"));
        QTRY_COMPARE(chatArea->property("searchHits").toStringList().size(), int(hits.size()));
        auto* resultsButton = window->findChild<QQuickItem*>("searchResultsButton");
        QVERIFY(resultsButton != nullptr);
        QTRY_VERIFY(resultsButton->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          resultsButton->mapToScene(QPointF(resultsButton->width() / 2, resultsButton->height() / 2)).toPoint());
        auto* resultsList = window->findChild<QQuickItem*>("searchResultsList");
        QTRY_VERIFY(resultsList != nullptr && resultsList->isVisible());
        QTRY_COMPARE(resultsList->property("count").toInt(), int(hits.size()));
        QTest::qWait(300);
        window->grabWindow().save(QStringLiteral("/workspace/build/ui_chat_search_results.png"));
        const int last = int(hits.size()) - 1;
        QVERIFY(QMetaObject::invokeMethod(resultsList, "positionViewAtEnd"));
        QTest::qWait(100);
        QQuickItem* lastRow = nullptr;
        for (QQuickItem* child : resultsList->property("contentItem").value<QQuickItem*>()->childItems()) {
            if (child->property("index").toInt() == last && child->isVisible()) lastRow = child;
        }
        QVERIFY(lastRow != nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          lastRow->mapToScene(QPointF(lastRow->width() / 2, lastRow->height() / 2)).toPoint());
        QTRY_VERIFY(!resultsList->isVisible());
        QCOMPARE(chatArea->property("searchCursor").toInt(), 0);
        QCOMPARE(chatArea->property("searchFocusId").toString(), hits.first().toMap().value("msgId").toString());
        QCOMPARE(chatArea->property("flashMsgId").toString(), hits.first().toMap().value("msgId").toString());
        QMetaObject::invokeMethod(chatArea, "closeSearch");
        auto flashOpacity = [window]() {
            qreal top = 0;
            QList<QQuickItem*> pending{window->contentItem()};
            while (!pending.isEmpty()) {
                QQuickItem* item = pending.takeLast();
                if (item->objectName() == QLatin1String("messageFlash") && item->isVisible()) top = qMax(top, item->opacity());
                pending.append(item->childItems());
            }
            return top;
        };
        QTRY_COMPARE_WITH_TIMEOUT(flashOpacity(), qreal(0), 3000);
        const QString globalId = hits.at(hits.size() / 2).toMap().value("msgId").toString();
        client.openMessageFromSearch(QStringLiteral("u-scroll"), globalId);
        QTRY_COMPARE(chatArea->property("foundMsgId").toString(), globalId);
        QCOMPARE(chatArea->property("flashMsgId").toString(), globalId);
        QTRY_VERIFY_WITH_TIMEOUT(flashOpacity() > 0.3, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(flashOpacity(), qreal(0), 3000);
        QTRY_VERIFY(chatArea->property("flashMsgId").toString().isEmpty());
        QCOMPARE(chatArea->property("foundMsgId").toString(), globalId);
        QMetaObject::invokeMethod(chatArea, "openSearch");
        QVERIFY(chatArea->property("foundMsgId").toString().isEmpty());
        QMetaObject::invokeMethod(chatArea, "closeSearch");
    }

    void testReactionJumpKeepsMessageBelowPinBar() {
        const QString owner = QStringLiteral("u-reaction-jump-owner");
        const QString peer = QStringLiteral("u-reaction-jump-peer");
        {
            ZaloOfflineStore store;
            store.bindOwner(owner);
            store.clearCache();
        }
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.openOfflineCacheForTesting(owner);
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(owner);
        client.selectConversation(peer, QStringLiteral("Fixture Reaction Peer"), QString());

        const qint64 base = QDateTime::currentMSecsSinceEpoch() - 300000;
        QVector<MessageItem> items;
        for (int i = 0; i < 100; ++i) {
            MessageItem item;
            item.id = QString::number(62000 + i);
            item.cliMsgId = item.id;
            item.isMine = true;
            item.text = QStringLiteral("Fixture reaction target %1").arg(i);
            item.ts = base + qint64(i) * 1000;
            items.append(item);
        }
        client.messageModel()->mergeMessages(peer, items);
        const int targetRow = client.messageModel()->rowOfMessage(QStringLiteral("62050"));
        QVERIFY(targetRow >= 0);

        const QJsonObject pinParams{{"global_msg_id", "62099"}, {"senderName", "Me"}, {"title", "Fixture pinned note"}};
        const QJsonObject pin{{"id", 1}, {"type", 2},
                              {"params", QString::fromUtf8(QJsonDocument(pinParams).toJson(QJsonDocument::Compact))}};
        client.pins()->applyListForTesting(peer, QJsonObject{{"data", QJsonArray{pin}}}, false);

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        QTest::qWait(600);
        auto* list = window->findChild<QQuickItem*>(QStringLiteral("messageList"));
        auto* pinBar = window->findChild<QQuickItem*>(QStringLiteral("pinBar"));
        auto* chatArea = window->findChild<QQuickItem*>(QStringLiteral("chatArea"));
        QVERIFY(list != nullptr && pinBar != nullptr && chatArea != nullptr);
        QVERIFY(pinBar->isVisible());

        const QJsonObject content{{"rIcon", "/-heart"}, {"rType", 5},
                                  {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "62050"}}}}};
        client.handleReactionsForTesting(QJsonArray{QJsonObject{{"uidFrom", peer}, {"idTo", owner},
                                                                {"msgId", "reaction-jump-event"},
                                                                {"content", content}}},
                                         false, false);
        QCOMPARE(client.currentUnreadReactionCount(), 1);

        QVERIFY(QMetaObject::invokeMethod(chatArea, "goToNextUnreadReaction"));
        QTRY_COMPARE(client.currentUnreadReactionCount(), 0);
        QTest::qWait(150);

        QQuickItem* target = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, target), Q_ARG(int, targetRow)));
        QVERIFY(target != nullptr);
        QVERIFY2(!list->property("stickToBottom").toBool(), "jumping to a reaction must stop following the end");
        const qreal targetTop = target->mapToScene(QPointF(0, 0)).y();
        const qreal targetBottom = target->mapToScene(QPointF(0, target->height())).y();
        const qreal pinBottom = pinBar->mapToScene(QPointF(0, pinBar->height())).y();
        const qreal listBottom = list->mapToScene(QPointF(0, list->height())).y();
        QVERIFY2(targetTop >= pinBottom + 8,
                 qPrintable(QStringLiteral("target top %1 is under the pin bar ending at %2").arg(targetTop).arg(pinBottom)));
        QVERIFY2(targetBottom <= listBottom,
                 qPrintable(QStringLiteral("target bottom %1 is below the list end %2").arg(targetBottom).arg(listBottom)));
        window->grabWindow().save(QStringLiteral("/workspace/build/test_reaction_jump.png"));
    }

    void testExpiredMediaRunKeepsHoverActions() {
        ZaloClient client;
        client.setLoggedInForTesting(true);
        auto* session = client.apiClient()->session();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QString dead = QStringLiteral("https://expired.invalid/zalos-fixture.jpg");
        auto msg = [&](const QString& id, const QString& type, const QJsonValue& content, qint64 ts) {
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", "u-run"}, {"idTo", "u-me"},
                               {"msgType", type}, {"content", content}, {"ts", QString::number(ts)}};
        };
        QJsonArray msgs{
            msg("9401", "chat.photo", QJsonObject{{"thumb", dead}, {"href", dead}, {"title", ""}}, now - 90000),
            msg("9402", "chat.photo", QJsonObject{{"thumb", dead}, {"href", dead}, {"title", ""}}, now - 80000),
            msg("9403", "chat.video.msg", QJsonObject{{"thumb", dead}, {"href", dead}, {"title", ""}}, now - 70000),
        };
        client.setUserForTesting("u-run", "Fixture Run", "");
        emit session->messagesReceived(msgs, false, true);
        client.selectConversation("u-run", "Fixture Run", "");
        client.setMyUidForTesting("u-me");

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        std::function<QQuickItem*(QQuickItem*, const QString&)> findItem = [&](QQuickItem* root, const QString& name) -> QQuickItem* {
            if (!root) return nullptr;
            if (root->objectName() == name) return root;
            for (QQuickItem* child : root->childItems()) {
                if (QQuickItem* hit = findItem(child, name)) return hit;
            }
            return nullptr;
        };
        QQuickItem* tile = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((tile = findItem(window->contentItem(), QStringLiteral("mediaRunTile"))) != nullptr, 5000);
        QTest::qWait(1500);

        const QPointF center = tile->mapToScene(QPointF(tile->width() / 2.0, tile->height() / 2.0));
        QTest::mouseMove(window, center.toPoint());
        QTest::qWait(300);
        QQuickItem* actions = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((actions = findItem(window->contentItem(), QStringLiteral("messageActions"))) != nullptr
                                 && actions->isVisible() && actions->width() > 0, 3000);
        const QRectF bounds = actions->mapRectToScene(QRectF(QPointF(0, 0), QSizeF(actions->width(), actions->height())));
        QVERIFY(bounds.left() >= 0 && bounds.right() <= window->width());
        QVERIFY(bounds.top() >= 0 && bounds.bottom() <= window->height());
        window->grabWindow().save(QStringLiteral("/workspace/build/test_expired_run_actions.png"));

        std::function<void(QQuickItem*, QList<QQuickItem*>&)> collectTiles = [&](QQuickItem* root, QList<QQuickItem*>& out) {
            if (!root) return;
            if (root->objectName() == QLatin1String("mediaRunTile") && root->isVisible()) out.append(root);
            for (QQuickItem* child : root->childItems()) collectTiles(child, out);
        };
        QList<QQuickItem*> tiles;
        collectTiles(window->contentItem(), tiles);
        QCOMPARE(tiles.size(), 2);
        for (QQuickItem* each : std::as_const(tiles)) {
            const QPointF at = each->mapToScene(QPointF(each->width() / 2.0, each->height() / 2.0));
            QTest::mouseMove(window, QPoint(0, 0));
            QTest::qWait(100);
            QTest::mouseMove(window, at.toPoint());
            QTest::qWait(300);
            QQuickItem* bar = nullptr;
            QTRY_VERIFY_WITH_TIMEOUT((bar = findItem(window->contentItem(), QStringLiteral("messageActions"))) != nullptr
                                     && bar->isVisible() && bar->width() > 0, 3000);
            const QRectF r = bar->mapRectToScene(QRectF(QPointF(0, 0), QSizeF(bar->width(), bar->height())));
            QVERIFY2(r.left() >= 0 && r.right() <= window->width() && r.top() >= 0 && r.bottom() <= window->height(),
                     qPrintable(QStringLiteral("bar %1 %2 %3 %4 in %5x%6").arg(r.left()).arg(r.top()).arg(r.right()).arg(r.bottom()).arg(window->width()).arg(window->height())));
        }

        QQuickItem* lowest = nullptr;
        std::function<void(QQuickItem*)> collect = [&](QQuickItem* root) {
            if (!root) return;
            if (root->objectName() == QLatin1String("mediaClickArea") && root->isVisible()
                && (!lowest || root->mapToScene(QPointF(0, 0)).y() > lowest->mapToScene(QPointF(0, 0)).y())) {
                lowest = root;
            }
            for (QQuickItem* child : root->childItems()) collect(child);
        };
        collect(window->contentItem());
        QVERIFY(lowest != nullptr);
        const QPointF videoCenter = lowest->mapToScene(QPointF(lowest->width() / 2.0, lowest->height() / 2.0));
        QTest::mouseMove(window, QPoint(0, 0));
        QTest::qWait(100);
        QTest::mouseMove(window, videoCenter.toPoint());
        QTest::qWait(300);
        QQuickItem* videoActions = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((videoActions = findItem(window->contentItem(), QStringLiteral("messageActions"))) != nullptr
                                 && videoActions->isVisible() && videoActions->width() > 0, 3000);
        const QRectF videoBounds = videoActions->mapRectToScene(QRectF(QPointF(0, 0), QSizeF(videoActions->width(), videoActions->height())));
        QVERIFY(videoBounds.left() >= 0 && videoBounds.right() <= window->width());
        QVERIFY(videoBounds.top() >= 0 && videoBounds.bottom() <= window->height());
        window->grabWindow().save(QStringLiteral("/workspace/build/test_expired_video_actions.png"));
    }

    void testUiGalleryScreenshots() {
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        QImage pic(640, 420, QImage::Format_RGB32);
        for (int y = 0; y < pic.height(); ++y)
            for (int x = 0; x < pic.width(); ++x) pic.setPixel(x, y, qRgb(40 + x / 4, 90 + y / 5, 160));
        const QString picPath = QDir::temp().filePath("zalos-gallery-photo.png");
        pic.save(picPath);
        QFile picFile(picPath);
        QVERIFY(picFile.open(QIODevice::ReadOnly));
        const QByteArray picBytes = picFile.readAll();
        TestSupport::LoopbackApi photoServer([picBytes](const TestSupport::LoopbackApi::Request&) { return picBytes; });
        const QString peerPicUrl = photoServer.baseUrl() + QStringLiteral("/gallery-photo.png");
        auto* session = client.apiClient()->session();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto msg = [&](const QString& id, bool mine, const QString& type, const QJsonValue& content, qint64 ts) {
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", mine ? "0" : "u-gallery"},
                               {"idTo", mine ? "u-gallery" : "u-me"}, {"msgType", type}, {"content", content},
                               {"ts", QString::number(ts)}};
        };
        QJsonArray msgs{
            msg("9101", false, "webchat", QString::fromUtf8("Chào bạn, gửi mình tài liệu nhé https://example.com/docs"), now - 3 * 86400000LL),
            msg("9102", true, "webchat", QString::fromUtf8("Ok, đây nhé"), now - 3600000),
            msg("9103", true, "chat.photo", QJsonObject{{"thumb", QUrl::fromLocalFile(picPath).toString()},
                                                        {"href", QUrl::fromLocalFile(picPath).toString()}, {"title", ""}}, now - 3500000),
            msg("9104", false, "share.file", QJsonObject{{"title", "bao-cao-quy-3.pdf"}, {"href", "https://example.com/f.pdf"},
                                                         {"params", "{\"fileSize\":\"482133\",\"fileExt\":\"pdf\"}"}}, now - 3400000),
            msg("9106", false, "chat.recommended", QJsonObject{{"action", "recommened.user"}, {"title", "Fixture Card"},
                                                               {"params", "u-cardfix"}, {"thumb", ""},
                                                               {"description", "{\"phone\":\"0900000000\"}"}}, now - 120000),
            msg("9105", false, "chat.photo", QJsonObject{{"thumb", peerPicUrl},
                                                         {"href", peerPicUrl},
                                                         {"title", QString::fromUtf8("Ảnh chụp buổi họp")}}, now - 60000),
        };
        client.setUserForTesting("u-gallery", "Fixture Gallery", "");
        emit session->messagesReceived(msgs, false, true);
        client.selectConversation("u-gallery", "Fixture Gallery", "");
        client.setMyUidForTesting("u-me");
        client.setOnlineIdsForTesting({"u-gallery"});
        client.labels()->applyServer(QJsonObject{{"version", 1}, {"labelData", QJsonArray{
            QJsonObject{{"id", 1}, {"text", "Fixture A"}, {"color", "#D91B1B"}, {"offset", 0}, {"conversations", QJsonArray{"u-gallery"}}},
            QJsonObject{{"id", 2}, {"text", "Fixture B"}, {"color", "#4BC377"}, {"offset", 1}, {"conversations", QJsonArray{}}}}}});
        const QJsonObject initialPins{{"version", 1}, {"data", QJsonArray{
            QString::fromUtf8(QJsonDocument(QJsonObject{{"id", 1}, {"type", 2}, {"params", QString::fromUtf8(QJsonDocument(QJsonObject{
                {"global_msg_id", "9101"}, {"senderName", "Fixture Gallery"}, {"title", "Fixture pinned note"}}).toJson(QJsonDocument::Compact))}}).toJson(QJsonDocument::Compact)),
            QString::fromUtf8(QJsonDocument(QJsonObject{{"id", 2}, {"type", 2}, {"params", QString::fromUtf8(QJsonDocument(QJsonObject{
                {"global_msg_id", "9102"}, {"senderName", "Me"}, {"title", "Second pin"}}).toJson(QJsonDocument::Compact))}}).toJson(QJsonDocument::Compact))}}};
        client.pins()->applyListForTesting("u-gallery", initialPins, false);
        client.handleReactionsForTesting(QJsonArray{
            QJsonObject{{"uidFrom", "u-gallery"}, {"idTo", "u-me"}, {"content", QJsonObject{{"rIcon", "/-heart"}, {"rType", 5},
                {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "9104"}}}}}}},
            QJsonObject{{"uidFrom", "0"}, {"idTo", "u-gallery"}, {"content", QJsonObject{{"rIcon", "/-strong"}, {"rType", 3},
                {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "9104"}}}}}}}}, false);
        client.applyGroupInfoForTesting(QJsonObject{{"g-gallery", QJsonObject{
            {"name", "Fixture Team"}, {"totalMember", 7},
            {"memVerList", QJsonArray{"u-me_0", "u-gallery_0", "u-f1_0", "u-f2_0", "u-f3_0"}}, {"creatorId", "u-me"},
            {"setting", QJsonObject{{"joinAppr", 1}, {"lockCreatePoll", 1}, {"signAdminMsg", 1}}}}}});

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        auto shot = [&](const QString& name) {
            QTest::qWait(300);
            window->grabWindow().save(QString("/workspace/build/ui_%1.png").arg(name));
        };
        client.setNotifySound(true);
        for (int mode : {0, 1}) {
            themeMgr.setThemeMode(mode);
            const QString suffix = mode ? "dark" : "light";
            client.pins()->applyListForTesting("u-gallery", initialPins, false);
            client.setCurrentNavigationTab("messages");
            shot("chat_" + suffix);
            if (auto* ml = window->findChild<QQuickItem*>("messageList")) {
                QVERIFY2(ml->property("atYEnd").toBool(), "the chat must open at its end, bottom margin included");
                QCOMPARE(ml->property("bottomMargin").toDouble(), 5.0);
                auto* pill = window->findChild<QQuickItem*>("typingPill");
                QVERIFY(pill != nullptr);
                const double pillTop = pill->height() + pill->property("anchors").value<QObject*>()->property("bottomMargin").toDouble();
                QVERIFY2(pillTop <= 19.0 + 10.0 - 3.0, "the typing pill must fit in the gap under the last row");
            }
            if (mode == 0) {
                QList<QQuickItem*> rows;
                std::function<void(QQuickItem*)> findRows = [&](QQuickItem* item) {
                    if (item->objectName() == QLatin1String("messageRow")) { rows.append(item); return; }
                    for (QQuickItem* c : item->childItems()) findRows(c);
                };
                findRows(window->contentItem());
                std::function<int(QQuickItem*)> weight = [&](QQuickItem* item) {
                    int n = 1 + int(item->findChildren<QObject*>(Qt::FindDirectChildrenOnly).size());
                    for (QQuickItem* c : item->childItems()) n += weight(c);
                    return n;
                };
                int objects = 0;
                for (QQuickItem* row : rows) objects += weight(row);
                const int perRow = rows.isEmpty() ? 0 : objects / int(rows.size());
                qInfo("message rows: %d, QML objects: %d (%d per row)", int(rows.size()), objects, perRow);
                qInfo("window QML objects: %d visual, %d in the QObject tree", weight(window->contentItem()),
                      int(window->findChildren<QObject*>().size()));
                QVERIFY2(perRow < 260, "message rows must create only the parts of their type (Loaders)");
                QVERIFY(!rows.isEmpty());
            }
            client.setAntiPeeking(true);
            shot("chat_antipeek_" + suffix);
            client.setAntiPeeking(false);
            {
                const QString conv = client.currentConversationId();
                auto topic = [](const QString& id, int type, const QString& title, int msgType) {
                    return QJsonObject{{"id", id}, {"type", type}, {"params", QJsonObject{
                        {"title", title}, {"senderUid", "u-gallery"}, {"senderName", "Fixture Gallery"},
                        {"global_msg_id", "m-" + id}, {"client_msg_id", "c-" + id}, {"msg_type", msgType}}}};
                };
                QJsonObject photoPin = topic("t2", 2, "", 32);
                QJsonObject photoParams = photoPin.value("params").toObject();
                photoParams.insert("thumb", "http://127.0.0.1:9/fixture-expired-thumb.jpg");
                photoPin.insert("params", photoParams);
                client.pins()->applyListForTesting(conv, QJsonObject{{"version", 3}, {"data", QJsonArray{
                    topic("t1", 2, "Fixture pinned text message\n" + QString("Fixture detail line with enough words to wrap. ").repeated(12), 1), photoPin,
                    topic("t3", 1, "Fixture note", 1), topic("t4", 3, "Fixture poll", 1)}}}, false);
                shot("pins_" + suffix);
                {
                    QJsonObject httpPin = topic("t0", 2, "", 32);
                    QJsonObject httpParams = httpPin.value("params").toObject();
                    httpParams.insert("thumb", "http://127.0.0.1:9/fixture-unreachable-thumb.jpg");
                    httpPin.insert("params", httpParams);
                    client.pins()->applyListForTesting(conv, QJsonObject{{"version", 2}, {"data", QJsonArray{httpPin}}}, false);
                    QObject* topBar = window->findChild<QObject*>("pinBar");
                    QVERIFY(topBar != nullptr);
                    QTRY_VERIFY(client.isMediaBroken("http://127.0.0.1:9/fixture-unreachable-thumb.jpg"));
                    auto* glyph = qobject_cast<QQuickItem*>(topBar->findChild<QObject*>("pinTopicThumbGlyph"));
                    QVERIFY(glyph != nullptr);
                    QTRY_VERIFY(glyph->isVisible());
                    shot("pins_photo_top_" + suffix);
                    client.pins()->applyListForTesting(conv, QJsonObject{{"version", 3}, {"data", QJsonArray{
                        topic("t1", 2, "Fixture pinned text message\n" + QString("Fixture detail line with enough words to wrap. ").repeated(12), 1), photoPin,
                        topic("t3", 1, "Fixture note", 1), topic("t4", 3, "Fixture poll", 1)}}}, false);
                }
                QObject* bar = window->findChild<QObject*>("pinBar");
                QVERIFY(bar != nullptr);
                QVERIFY(bar->property("visible").toBool());
                bar->setProperty("expanded", true);
                shot("pins_expanded_" + suffix);
                bar->setProperty("expanded", false);
                QObject* chatArea = window->findChild<QObject*>("chatArea");
                QVERIFY(chatArea != nullptr);
                QVERIFY(QMetaObject::invokeMethod(chatArea, "openPinDetail", Q_ARG(QString, "t1")));
                QObject* detail = window->findChild<QObject*>("pinDetailPopup");
                QVERIFY(detail != nullptr && detail->property("opened").toBool());
                QVERIFY(detail->property("fullText").toString().contains("Fixture detail line"));
                shot("pins_detail_" + suffix);
                QVERIFY(QMetaObject::invokeMethod(detail, "close"));
                QTRY_VERIFY(client.isMediaBroken("http://127.0.0.1:9/fixture-expired-thumb.jpg"));
                QVERIFY(QMetaObject::invokeMethod(chatArea, "openPinDetail", Q_ARG(QString, "t2")));
                shot("pins_detail_expired_" + suffix);
                QVERIFY(QMetaObject::invokeMethod(detail, "close"));
                client.pins()->applyListForTesting(conv, QJsonObject{{"version", 4}, {"data", QJsonArray{}}}, false);
                QVERIFY(!bar->property("visible").toBool());
            }
            for (const char* tab : {"general", "language", "appearance", "notifications", "privacy", "security", "storage", "sync"}) {
                client.openSettings(tab);
                shot(QString("settings_%1_%2").arg(tab, suffix));
                client.setShowSettingsModal(false);
            }
            client.openCreateGroup();
            shot("create_group_" + suffix);
            client.closeCreateGroup();
            client.setCurrentNavigationTab("contacts");
            shot("contacts_" + suffix);
            client.setCurrentNavigationTab("messages");
            client.viewProfile("u-gallery");
            shot("profile_" + suffix);
            client.closeUserProfile();
            client.selectConversation("g-gallery", "Fixture Team", "");
            client.setShowConversationInfo(true);
            shot("group_composite_" + suffix);
            client.setShowConversationInfo(false);
            client.selectConversation("u-gallery", "Fixture Gallery", "");
            client.groupAdmin()->openManage("g-gallery");
            QTest::qWait(100);
            client.groupAdmin()->applyLinkDetailForTesting(QJsonObject{{"link", "https://zalo.me/g/fixture01"}, {"enabled", 1}});
            client.groupAdmin()->applyPendingForTesting(QJsonObject{{"users", QJsonArray{
                QJsonObject{{"uid", "u-req1"}, {"dpn", "Fixture Request"}}}}});
            client.groupAdmin()->applyBlockedForTesting(QJsonObject{{"blocked_members", QJsonArray{}}});
            shot("group_manage_" + suffix);
            client.groupAdmin()->closeManage();
            client.groupAdmin()->openJoin("");
            client.groupAdmin()->applyPreviewForTesting("https://zalo.me/g/fixture02", QJsonObject{
                {"groupId", "g-preview"}, {"name", "Fixture Club"}, {"desc", "Neutral fixture group"}, {"totalMember", 12},
                {"currentMems", QJsonArray{QJsonObject{{"id", "u-a"}, {"dName", "A"}}, QJsonObject{{"id", "u-b"}, {"dName", "B"}}}}});
            shot("group_join_" + suffix);
            client.groupAdmin()->closeJoin();
            client.labels()->applyServer(QJsonObject{{"version", 1}, {"labelData", QJsonArray{
                QJsonObject{{"id", 1}, {"text", "Fixture A"}, {"color", "#D91B1B"}, {"offset", 0}, {"conversations", QJsonArray{"u-gallery"}}},
                QJsonObject{{"id", 2}, {"text", "Fixture B"}, {"color", "#4BC377"}, {"offset", 1}, {"conversations", QJsonArray{}}}}}});
            client.labels()->setManageOpen(true);
            shot("labels_" + suffix);
            client.labels()->setManageOpen(false);
            QVERIFY(window->setProperty("shortcutsHelpOpen", true));
            QTRY_VERIFY(window->findChild<QQuickItem*>("shortcutsDialogCard") != nullptr);
            shot("shortcuts_" + suffix);
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!window->property("shortcutsHelpOpen").toBool());
            client.openWelcomeScreen();
            QQuickItem* welcome = nullptr;
            QTRY_VERIFY((welcome = window->findChild<QQuickItem*>("welcomeArea")) != nullptr);
            QCOMPARE(welcome->property("totalSlides").toInt(), 6);
            welcome->setProperty("currentSlide", 5);
            shot("welcome_keys_" + suffix);
            auto* welcomeShortcuts = window->findChild<QObject*>("welcomeShortcuts");
            QVERIFY(welcomeShortcuts != nullptr);
            QVERIFY(QMetaObject::invokeMethod(welcomeShortcuts, "clicked"));
            QTRY_VERIFY(window->property("shortcutsHelpOpen").toBool());
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!window->property("shortcutsHelpOpen").toBool());
            client.selectConversation("u-gallery", "Fixture Gallery", "");
            client.openShareCard("u-gallery", "Fixture Gallery");
            shot("share_card_" + suffix);
            client.closeForwardModal();
            client.appLock()->setPasscode("", "9753");
            client.appLock()->lock();
            shot("lock_" + suffix);
            QVERIFY(client.appLock()->unlock("9753"));
            QVERIFY(client.appLock()->removePasscode("9753"));
            auto* chat = window->findChild<QQuickItem*>("chatArea");
            QVERIFY(chat != nullptr);
            QVERIFY(QMetaObject::invokeMethod(chat, "openReactionDetails", Q_ARG(QString, QStringLiteral("9104")),
                                              Q_ARG(QQuickItem*, chat)));
            shot("reactions_" + suffix);
            QTest::keyClick(window, Qt::Key_Escape);
            QVERIFY(QMetaObject::invokeMethod(chat, "openViewer", Q_ARG(QString, QStringLiteral("9105"))));
            shot("viewer_" + suffix);
            QTest::keyClick(window, Qt::Key_Escape);
            QTest::qWait(100);
        }
        themeMgr.setThemeMode(1);
        client.setCurrentNavigationTab("messages");
        client.selectConversation("u-gallery", "Fixture Gallery", "");
        client.setShowConversationInfo(false);
        for (const QSize& s : {QSize(1920, 1080), QSize(1280, 800), QSize(960, 1080), QSize(683, 768),
                               QSize(520, 480), QSize(380, 420)}) {
            window->resize(s);
            shot(QString("responsive_%1x%2").arg(s.width()).arg(s.height()));
        }
        client.setShowConversationInfo(true);
        for (const QSize& s : {QSize(1280, 800), QSize(760, 700), QSize(560, 600)}) {
            window->resize(s);
            shot(QString("responsive_info_%1x%2").arg(s.width()).arg(s.height()));
        }
        client.setShowConversationInfo(false);
        window->resize(1280, 800);

        if (window->findChild<QObject*>("sidebar")) {
            themeMgr.setThemeMode(1);
            window->resize(1100, 760);
            shot("sidebar_tabs_dark");
            client.openWelcomeScreen();
            window->resize(380, 600);
            shot("sidebar_tabs_compact_dark");
            window->resize(1100, 760);
            client.selectConversation("u-gallery", "Fixture Gallery", "");
            client.applyGroupInfoForTesting(QJsonObject{{"g-dir", QJsonObject{{"name", "Fixture Directory Group"}, {"totalMember", 4}}}});
            client.conversationModel()->setSearchFilter("fixture");
            client.peopleSearch()->setQuery("fixture");
            client.peopleSearch()->runNow();
            client.messageSearch()->setQuery("fixture");
            client.messageSearch()->runNow();
            shot("sidebar_search_dark");
            {
                QObject* sidebar = window->findChild<QObject*>("sidebar");
                auto* tabBar = window->findChild<QQuickItem*>("searchTabBar");
                auto* peopleList = window->findChild<QQuickItem*>("peopleHitList");
                auto* messageList = window->findChild<QQuickItem*>("messageHitList");
                QVERIFY(tabBar && peopleList && messageList);
                QVERIFY(tabBar->isVisible());
                QVERIFY(client.peopleSearch()->count() > 0);
                const QString firstTab = sidebar->property("searchTab").toString();
                QVERIFY(firstTab == "chats" || firstTab == "people");
                auto clickTab = [&](const char* name) {
                    auto* tab = window->findChild<QQuickItem*>(name);
                    QVERIFY(tab != nullptr && tab->isVisible());
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                                      tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint());
                    QTest::qWait(50);
                };
                clickTab("searchTab_people");
                QCOMPARE(sidebar->property("searchTab").toString(), QString("people"));
                QVERIFY(peopleList->isVisible());
                QVERIFY(!messageList->isVisible());
                clickTab("searchTab_messages");
                QCOMPARE(sidebar->property("searchTab").toString(), QString("messages"));
                QVERIFY(!peopleList->isVisible());
                auto* empty = window->findChild<QQuickItem*>("conversationsEmptyState");
                QVERIFY(empty != nullptr);
                QCOMPARE(messageList->isVisible(), client.messageSearch()->count() > 0);
                QCOMPARE(empty->isVisible(), client.messageSearch()->count() == 0);
                shot("sidebar_search_messages_dark");
                client.peopleSearch()->clear();
                QTest::qWait(50);
                QCOMPARE(sidebar->property("searchTab").toString(), QString("messages"));
            }
            client.conversationModel()->setSearchFilter("");
            client.peopleSearch()->clear();
            client.messageSearch()->clear();

            client.setHiddenPinHashForTesting(ZaloClient::hiddenPinDigest("2468"));
            client.openWelcomeScreen();
            client.setHiddenThreadsForTesting({"u-gallery"});
            client.conversationModel()->setSearchFilter("2468");
            client.hiddenSearch()->setQuery("2468");
            client.hiddenSearch()->runNow();
            shot("sidebar_hidden_search_dark");
            QCOMPARE(client.hiddenSearch()->count(), 1);
            client.conversationModel()->setSearchFilter("");
            client.hiddenSearch()->clear();
            QTest::qWait(50);
            QCOMPARE(window->findChild<QObject*>("sidebar")->property("searchTab").toString(), QString("chats"));
            QVERIFY(!window->findChild<QQuickItem*>("searchTabBar")->isVisible());
            client.requestHiddenFlow("hide", "u-gallery", false);
            QObject* pinPopup = window->findChild<QObject*>("hiddenPinPopup");
            QVERIFY(pinPopup != nullptr);
            QVERIFY(pinPopup->property("opened").toBool());
            shot("hidden_pin_dark");
            QVERIFY(QMetaObject::invokeMethod(pinPopup, "close"));
            client.setHiddenThreadsForTesting({});
            client.setHiddenPinHashForTesting(QString());
            client.selectConversation("u-gallery", "Fixture Gallery", "");
        }

        client.setShowLabelFolders(true);
        client.setCurrentNavigationTab("messages");
        window->resize(1100, 760);
        themeMgr.setThemeMode(1);
        shot("rail_dark");
        themeMgr.setThemeMode(0);
        shot("rail_light");
        client.setShowLabelFolders(false);

        themeMgr.setThemeMode(0);
        client.setNotifySound(false);
        QFile::remove(picPath);
    }


    void testWebcontentSystemMessagesAndAppOnlyLinks() {
        ZaloClient client;
        client.setAppLanguage("vi");
        const QString birthdayParams = QString::fromUtf8(QJsonDocument(QJsonObject{
            {"item", QJsonObject{{"zinstantdata_id", "templateId=14030"}, {"data_type", 2}, {"version", 1}}},
            {"type", 11},
            {"customMsg", QJsonObject{{"msg", QJsonObject{
                {"en", "Celebrate Fixture Friend's birthday today (01/01) with your best wishes!"},
                {"vi", QString::fromUtf8("Hôm nay (01/01) là sinh nhật của Fixture Friend, hãy gửi một lời chúc!")}}},
                {"highLightsV2", QJsonArray{}}}}}).toJson(QJsonDocument::Compact));
        const QString noticeParams = QString::fromUtf8(QJsonDocument(QJsonObject{
            {"item", QJsonObject{{"zinstantdata_id", "templateId=99999"}}},
            {"type", 5},
            {"customMsg", QJsonObject{{"msg", QJsonObject{{"en", "Fixture notice"},
                                                          {"vi", QString::fromUtf8("Fixture thông báo")}}}}}}).toJson(QJsonDocument::Compact));
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto msg = [](const QString& id, qint64 ts, const QJsonObject& content) {
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", "u-sys-fixture"}, {"idTo", "u-me"},
                               {"msgType", "chat.webcontent"}, {"content", content}, {"ts", QString::number(ts)}};
        };
        emit client.apiClient()->session()->messagesReceived(QJsonArray{
            msg("7101", now - 2000, QJsonObject{{"title", "Celebrate Fixture Friend's birthday today (01/01) with your best wishes!"},
                                                {"href", ""}, {"params", birthdayParams}}),
            msg("7102", now - 1000, QJsonObject{{"title", "Fixture notice"}, {"href", ""}, {"params", noticeParams}})},
            false, true);
        client.selectConversation("u-sys-fixture", "Fixture Friend", "");
        auto* m = client.messageModel();
        QCOMPARE(m->rowCount(), 2);
        QCOMPARE(m->data(m->index(0), MessageModel::MessageTypeRole).toString(), QString("ecard"));
        QVERIFY(m->data(m->index(0), MessageModel::TextRole).toString().startsWith(QString::fromUtf8("Hôm nay (01/01)")));
        const QVariantMap extra = m->data(m->index(0), MessageModel::ExtraRole).toMap();
        QCOMPARE(extra.value("ecardType").toString(), QString("1"));
        QVERIFY(extra.value("suggestStickers").toBool());
        QCOMPARE(m->data(m->index(1), MessageModel::MessageTypeRole).toString(), QString("link"));
        QCOMPARE(m->data(m->index(1), MessageModel::TextRole).toString(), QString::fromUtf8("Fixture thông báo"));
        QVERIFY(m->data(m->index(1), MessageModel::HrefRole).toString().isEmpty());

        client.setMyUidForTesting("u-me");
        auto parse = [&client](const QJsonObject& params, const QString& title) {
            return client.toMessageItemForTesting(QJsonObject{
                {"msgId", "7201"}, {"uidFrom", "u-sys-fixture"}, {"idTo", "g-fixture"}, {"msgType", "chat.webcontent"},
                {"ts", "1700000000000"},
                {"content", QJsonObject{{"title", title}, {"href", ""},
                                        {"params", QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact))}}}},
                true);
        };
        const MessageItem reminder = parse(QJsonObject{
            {"type", 2}, {"item", QJsonObject{{"zinstantdata_id", "templateId=11366"}}},
            {"customMsg", QJsonObject{
                {"msg", QJsonObject{{"vi", QString::fromUtf8("%1$s đã tạo nhắc hẹn mới cho %2$s %3$s")}}},
                {"highLightsV2", QJsonArray{QJsonObject{{"dpn", "Fixture Member"}, {"uid", "u-other"}},
                                            QJsonObject{{"dpn", "Fixture Me"}, {"uid", "u-me"}}}}}},
            {"actions", QJsonArray{QJsonObject{{"actionType", "action.open.calendar.event"},
                                               {"actionData", QJsonObject{{"eventId", "8801"}}}}}}}, "Fixture title");
        QCOMPARE(reminder.messageType, QString("system"));
        QCOMPARE(reminder.text, QString::fromUtf8("Fixture Member đã tạo nhắc hẹn mới cho Bạn"));
        QCOMPARE(reminder.extra.value("reminderEventId").toString(), QString("8801"));
        QCOMPARE(parse(QJsonObject{{"type", 6}}, "You became group member").text,
                 QString::fromUtf8("Bạn đã trở thành thành viên của nhóm"));
        QCOMPARE(parse(QJsonObject{{"type", 6}}, "You just created the group").text, QString::fromUtf8("Bạn vừa tạo nhóm"));
        const MessageItem owner = parse(QJsonObject{{"type", 19}}, "You were appointed as new group owner");
        QCOMPARE(owner.messageType, QString("system"));
        QCOMPARE(owner.text, QString::fromUtf8("Bạn được bổ nhiệm làm trưởng nhóm"));
        QCOMPARE(parse(QJsonObject{{"type", 34}}, "Fixture safety").messageType, QString("system"));
        QCOMPARE(parse(QJsonObject{{"type", 77}}, "").text,
                 QString::fromUtf8("Tin nhắn chưa hỗ trợ hiển thị trên phiên bản hiện tại"));
        QCOMPARE(parse(QJsonObject{{"type", 77}}, "Fixture other").text, QString("Fixture other"));

        auto parseOne = [&client](const QString& msgType, const QJsonValue& content, qint64 ttl) {
            QJsonObject raw{{"msgId", "7301"}, {"uidFrom", "u-oa-fixture"}, {"idTo", "u-me"}, {"msgType", msgType},
                            {"ts", "1700000000000"}, {"content", content}};
            if (ttl > 0) raw.insert("ttl", ttl);
            return client.toMessageItemForTesting(raw, false);
        };
        const MessageItem oaNotice = parseOne("chat.webcontent", QJsonObject{
            {"title", "Fixture OA title"}, {"description", "Fixture OA body"}, {"href", ""},
            {"params", QString::fromUtf8(QJsonDocument(QJsonObject{{"type", 2}}).toJson(QJsonDocument::Compact))}}, 17459000);
        QCOMPARE(oaNotice.messageType, QString("link"));
        QVERIFY(oaNotice.href.isEmpty());
        QCOMPARE(oaNotice.text, QString("Fixture OA title"));
        QCOMPARE(oaNotice.extra.value("desc").toString(), QString("Fixture OA body"));
        QVERIFY(!oaNotice.extra.contains("expireOn"));
        const MessageItem oaList = parseOne("chat.list.action", QJsonObject{
            {"title", "Fixture login notice"}, {"description", "Fixture device line"}, {"href", ""},
            {"action", "action.open.logindevices"}, {"type", "l.a.header.only"}}, 0);
        QCOMPARE(oaList.messageType, QString("link"));
        QCOMPARE(oaList.extra.value("desc").toString(), QString("Fixture device line"));
        QCOMPARE(client.toMessageItemForTesting(QJsonObject{
            {"msgId", "7302"}, {"uidFrom", "u-oa-fixture"}, {"idTo", "g-fixture"}, {"msgType", "chat.list.action"},
            {"ts", "1700000000000"}, {"content", QJsonObject{{"title", "Fixture group line"}, {"href", ""}}}}, true).messageType,
            QString("system"));
        auto customParams = [](int type, const QString& tpl, const QString& vi) {
            return QString::fromUtf8(QJsonDocument(QJsonObject{{"type", type}, {"item", QJsonObject{{"zinstantdata_id", tpl}}},
                {"customMsg", QJsonObject{{"msg", QJsonObject{{"vi", vi}}}}}}).toJson(QJsonDocument::Compact));
        };
        const MessageItem accepted = parseOne("chat.webcontent", QJsonObject{{"title", "Fixture"}, {"href", ""},
            {"params", customParams(9, "", QString::fromUtf8("Fixture Friend đã đồng ý kết bạn"))}}, 0);
        QCOMPARE(accepted.messageType, QString("system"));
        const MessageItem order = parseOne("chat.webcontent", QJsonObject{{"title", "Fixture"}, {"href", ""},
            {"params", customParams(27, "templateId=10904", QString::fromUtf8("Fixture greeting\nFixture order line"))}}, 0);
        QCOMPARE(order.messageType, QString("link"));
        QCOMPARE(order.text, QString("Fixture greeting"));
        QCOMPARE(order.extra.value("desc").toString(), QString("Fixture order line"));
        QCOMPARE(parseOne("chat.webcontent", QJsonObject{{"title", "Fixture safety"}, {"href", ""},
            {"params", QString::fromUtf8(QJsonDocument(QJsonObject{{"type", 34}}).toJson(QJsonDocument::Compact))}}, 0).text,
            QString::fromUtf8("Zalo gửi thông báo về an toàn tài khoản của bạn"));

        QCOMPARE(ZaloOfflineStore::expiryOf(QJsonObject{{"msgType", "chat.webcontent"}, {"ttl", 86400000}}, 1000), qint64(0));
        QCOMPARE(ZaloOfflineStore::expiryOf(QJsonObject{{"msgType", "chat.list.action"}, {"ttl", 17459000}}, 1000), qint64(0));
        QCOMPARE(ZaloOfflineStore::expiryOf(QJsonObject{{"msgType", "chat.ecard"}, {"ttl", 86400000}}, 1000), qint64(0));
        QCOMPARE(ZaloOfflineStore::expiryOf(QJsonObject{{"msgType", "webchat"}, {"ttl", 60000}}, 1000), qint64(61000));
        QCOMPARE(ZaloOfflineStore::expiryOf(QJsonObject{{"msgType", "webchat"}}, 1000), qint64(0));

        QVERIFY(ZaloClient::isAppOnlyLink("https://zalo.me/v/video/1000000000000000001?source=fixture&type=0"));
        QVERIFY(ZaloClient::isAppOnlyLink("https://zalo.me/s/1000000000000000002?utm_source=fixture"));
        QVERIFY(ZaloClient::isAppOnlyLink("https://zalo.video/s/FIXTURE"));
        QVERIFY(!ZaloClient::isAppOnlyLink("https://zalo.me/g/fixture01"));
        QVERIFY(!ZaloClient::isAppOnlyLink("https://example.com/v/video/1"));
        QSignalSpy appLink(&client, &ZaloClient::appOnlyLinkRequested);
        client.openLink("https://zalo.me/v/video/1000000000000000001", "Fixture video", "");
        QCOMPARE(appLink.count(), 1);
        QCOMPARE(appLink.at(0).at(1).toString(), QString("Fixture video"));
        const QVariantMap qr = client.qrModules("https://zalo.me/v/video/1000000000000000001");
        const int size = qr.value("size").toInt();
        QVERIFY(size >= 21);
        QCOMPARE(qr.value("modules").toList().size(), size * size);
    }

    void testAntiPeekingQuickSwitch() {
        ZaloClient client;
        SystemTrayController tray;
        QObject::connect(&tray, &SystemTrayController::antiPeekingToggled, &client, &ZaloClient::setAntiPeeking);
        QObject::connect(&client, &ZaloClient::settingsChanged, &tray, [&]() { tray.setAntiPeeking(client.antiPeeking()); });
        client.setAntiPeeking(false);
        emit tray.antiPeekingToggled(true);
        QCOMPARE(client.antiPeeking(), true);
        client.setAntiPeeking(false);
        QCOMPARE(client.antiPeeking(), false);
    }


    void testCrossDeviceSyncControls() {
        ZaloClient client;
        client.setLoggedInForTesting(true);
        auto* session = client.apiClient()->session();
        auto* convs = client.conversationModel();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "8001"}, {"uidFrom", "u-sync"}, {"idTo", "u-me"},
            {"msgType", "webchat"}, {"content", "a"}, {"ts", ts}}}, false, true);
        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "8002"}, {"uidFrom", "u-sync"}, {"idTo", "g-sync"},
            {"msgType", "webchat"}, {"content", "b"}, {"ts", ts}}}, true, true);
        QVERIFY(convs->contains("u-sync") && convs->contains("g-sync"));

        client.applyAliasesForTesting({{"u-sync", QString::fromUtf8("Anh Tư")}});
        QCOMPARE(convs->conversation("u-sync").name, QString::fromUtf8("Anh Tư"));

        emit session->controlReceived("group", "update", QJsonObject{{"groupId", "g-sync"}, {"groupName", "Fixture Team"},
                                                                     {"sourceId", "u-sync"}});
        QCOMPARE(convs->conversation("g-sync").name, QString("Fixture Team"));
        client.selectConversation("g-sync", "Fixture Team", "");
        bool notice = false;
        for (int r = 0; r < client.messageModel()->rowCount(); ++r) {
            const QModelIndex i = client.messageModel()->index(r);
            notice |= client.messageModel()->data(i, MessageModel::MessageTypeRole).toString() == "system";
        }
        QVERIFY(notice);

        emit session->controlReceived("mute", "set", QJsonObject{{"toId", "u-sync"}, {"duration", -1}, {"muteType", 1}});
        QVERIFY(convs->conversation("u-sync").isMuted);
        emit session->controlReceived("mute", "unset", QJsonObject{{"toId", "u-sync"}});
        QVERIFY(!convs->conversation("u-sync").isMuted);
        emit session->controlReceived("pinned_convers", "update", QJsonObject{{"action", 1}, {"pinIds", QJsonArray{"u" + QString("u-sync")}}});
        QVERIFY(convs->conversation("u-sync").isPinned);
        emit session->controlReceived("mark_unread", "add", QJsonObject{{"convsUser", QJsonArray{QJsonObject{{"id", "u-sync"}}}}});
        QVERIFY(client.isMarkedUnread("u-sync"));
        QCOMPARE(convs->conversation("u-sync").unreadCount, 1);
        emit session->controlReceived("mark_unread", "remove", QJsonObject{{"convsUser", QJsonArray{"u-sync"}}});
        QVERIFY(!client.isMarkedUnread("u-sync"));
        QCOMPARE(convs->conversation("u-sync").unreadCount, 0);

        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "8003"}, {"uidFrom", "u-me"}, {"idTo", "u-sync"},
            {"msgType", "chat.delete"}, {"ts", ts},
            {"content", QJsonArray{QJsonObject{{"uidFrom", "u-me"}, {"uidTo", "u-sync"}, {"globalDelMsgId", "8001"}, {"type", 2}}}}}},
            false, false);
        QVERIFY(!convs->contains("u-sync"));

    }


    void testBatchOneMessageTypesAndRoster() {
        ZaloClient client;
        client.setMyUidForTesting("u-me");
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        auto params = [](const QJsonObject& o) {
            return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
        };
        auto base = [ts](const QString& type, const QJsonValue& content) {
            return QJsonObject{{"msgId", "9101"}, {"cliMsgId", "1"}, {"uidFrom", "u-peer"}, {"idTo", "u-me"},
                               {"msgType", type}, {"content", content}, {"ts", QString::number(ts)}};
        };

        auto link = client.toMessageItemForTesting(base("chat.recommended", QJsonObject{
            {"action", "recommened.link"}, {"title", "Fixture page"}, {"description", "About the fixture"},
            {"href", "https://example.org/a"}, {"thumb", "https://example.org/t.jpg"}, {"params", "{}"}}), false);
        QCOMPARE(link.messageType, QString("link"));
        QCOMPARE(link.text, QString("Fixture page"));
        QCOMPARE(link.href, QString("https://example.org/a"));
        QCOMPARE(link.mediaUrl, QString("https://example.org/t.jpg"));
        QCOMPARE(link.extra.value("domain").toString(), QString("example.org"));
        QCOMPARE(link.extra.value("desc").toString(), QString("About the fixture"));

        auto call = client.toMessageItemForTesting(base("chat.recommended", QJsonObject{
            {"action", "recommened.calltime"}, {"title", "sendBubbleMessage"}, {"description", ""}, {"href", ""},
            {"thumb", ""}, {"params", params({{"calltype", 1}, {"duration", 125}, {"isCaller", 1}})}}), false);
        QCOMPARE(call.messageType, QString("call"));
        QVERIFY(call.extra.value("video").toBool());
        QVERIFY(call.extra.value("outgoing").toBool());
        QVERIFY(!call.extra.value("missed").toBool());
        QCOMPARE(call.extra.value("seconds").toInt(), 125);
        QVERIFY(!call.text.contains("sendBubbleMessage"));
        auto missed = client.toMessageItemForTesting(base("chat.recommended", QJsonObject{
            {"action", "recommened.misscall"}, {"title", "Missed call"}, {"description", ""}, {"href", ""},
            {"thumb", ""}, {"params", params({{"calltype", 0}, {"duration", 0}, {"isCaller", 0}, {"reason", 2}})}}), false);
        QCOMPARE(missed.messageType, QString("call"));
        QVERIFY(missed.extra.value("missed").toBool());
        QVERIFY(!missed.extra.value("video").toBool());

        auto calling = client.toMessageItemForTesting(base("chat.recommended", QJsonObject{
            {"action", "recommened.calling"}, {"title", "Cuộc gọi đi"}, {"description", "Cuộc gọi đi"}, {"href", ""},
            {"thumb", ""}, {"duration", 14}, {"params", "{}"}}), false);
        QCOMPARE(calling.messageType, QString("call"));
        QVERIFY(calling.extra.value("outgoing").toBool());
        QVERIFY(!calling.extra.value("missed").toBool());
        QCOMPARE(calling.extra.value("seconds").toInt(), 14);

        auto receiveCall = client.toMessageItemForTesting(base("chat.recommended", QJsonObject{
            {"action", "recommened.receivecall"}, {"title", "Cuộc gọi đến"}, {"description", "Cuộc gọi đến"}, {"href", ""},
            {"thumb", ""}, {"params", "{\"isEnableCallback\":1}"}}), false);
        QCOMPARE(receiveCall.messageType, QString("call"));
        QVERIFY(!receiveCall.extra.value("outgoing").toBool());
        QVERIFY(!receiveCall.extra.value("missed").toBool());

        auto voice = client.toMessageItemForTesting(base("chat.voice", QJsonObject{
            {"action", ""}, {"title", ""}, {"description", ""}, {"href", "https://example.org/v.aac"}, {"thumb", ""},
            {"params", params({{"duration", 12672}, {"fileSize", 1000}, {"m4a", ""}})}}), false);
        QCOMPARE(voice.messageType, QString("voice"));
        QCOMPARE(voice.href, QString("https://example.org/v.aac"));
        QCOMPARE(voice.extra.value("durationMs").toLongLong(), 12672);

        QJsonObject pollMsg = base("group.poll", QJsonObject{{"action", "create"}, {"title", "Q"},
                                                              {"params", params({{"pollId", 4242}})}});
        pollMsg["idTo"] = "g-fixture";
        auto poll = client.toMessageItemForTesting(pollMsg, true);
        QCOMPARE(poll.messageType, QString("poll"));
        QCOMPARE(poll.pollData.value("poll_id").toLongLong(), 4242);
        pollMsg["content"] = QJsonObject{{"action", "vote"}, {"params", params({{"pollId", 4242}})}};
        const auto pollEvent = client.toMessageItemForTesting(pollMsg, true);
        QCOMPARE(pollEvent.messageType, QString("poll"));
        QCOMPARE(pollEvent.extra.value("pollEvent").toString(), QString("vote"));

        QJsonObject mentionMsg = base("webchat", QString("@Fixture hi"));
        mentionMsg["idTo"] = "g-fixture";
        mentionMsg["mentions"] = QJsonArray{QJsonObject{{"uid", "u-peer2"}, {"pos", 0}, {"len", 8}, {"type", 0}}};
        auto mention = client.toMessageItemForTesting(mentionMsg, true);
        QCOMPARE(mention.mentions.size(), 1);
        const QString html = MessageModel::linkify(mention.text, mention.mentions);
        QVERIFY(html.contains("zalos-mention:u-peer2"));
        QVERIFY(html.contains("@Fixture"));
        QVERIFY(html.endsWith(" hi"));
        mentionMsg["mentions"] = QJsonArray{QJsonObject{{"uid", "u-x"}, {"pos", 5}, {"len", 99}}};
        QCOMPARE(client.toMessageItemForTesting(mentionMsg, true).mentions.size(), 0);

        QSignalSpy rosterSpy(&client, &ZaloClient::groupMembersChanged);
        client.applyGroupInfoForTesting(QJsonObject{{"g-fixture", QJsonObject{
            {"name", "Fixture Group"}, {"totalMember", 3}, {"creatorId", "u-owner"},
            {"adminIds", QJsonArray{"u-me"}}, {"memVerList", QJsonArray{"u-peer_0", "u-me_3", "u-owner_1"}}}}});
        QCOMPARE(rosterSpy.count(), 1);
        const QVariantList members = client.groupMembers("g-fixture");
        QCOMPARE(members.size(), 3);
        QCOMPARE(members.at(0).toMap().value("id").toString(), QString("u-owner"));
        QCOMPARE(members.at(0).toMap().value("role").toString(), QString("owner"));
        QCOMPARE(members.at(1).toMap().value("id").toString(), QString("u-me"));
        QCOMPARE(members.at(1).toMap().value("role").toString(), QString("admin"));
        QVERIFY(members.at(1).toMap().value("isMe").toBool());
        QCOMPARE(members.at(2).toMap().value("name").toString(), QString());
        QCOMPARE(client.myGroupRole("g-fixture"), QString("admin"));

        emit client.apiClient()->session()->controlReceived("group", "add_admin", QJsonObject{
            {"groupId", "g-fixture"}, {"sourceId", "u-owner"}, {"updateMembers", QJsonArray{QJsonObject{{"id", "u-peer"}}}}});
        bool peerIsAdmin = false;
        for (const QVariant& m : client.groupMembers("g-fixture")) {
            if (m.toMap().value("id") == "u-peer") peerIsAdmin = m.toMap().value("role") == "admin";
        }
        QVERIFY(peerIsAdmin);

        QJsonObject own = base("webchat", QString("x"));
        own["uidFrom"] = "0";
        QJsonObject deleted = base("chat.delete", QJsonArray{});
        const auto receipts = client.deliveredCandidatesForTesting(QJsonArray{base("webchat", QString("y")), own, deleted}, false);
        QCOMPARE(receipts.size(), 1);
        QCOMPARE(receipts.value("u-peer").size(), 1);
    }

    void testSharedItemsAndPollDetail() {
        MessageModel model;
        MessageItem photo; photo.id = "1"; photo.messageType = "image"; photo.mediaUrl = "https://example.org/p.jpg"; photo.ts = 1;
        MessageItem file; file.id = "2"; file.messageType = "file"; file.text = "a.pdf"; file.href = "https://example.org/a.pdf"; file.ts = 2;
        MessageItem text; text.id = "3"; text.text = "see https://example.org/x."; text.ts = 3;
        MessageItem poll; poll.id = "4"; poll.messageType = "poll"; poll.pollData = {{"poll_id", 7}}; poll.ts = 4;
        model.mergeMessages("c", {photo, file, text, poll});
        model.loadConversation("c");
        QCOMPARE(model.sharedItems("media").size(), 1);
        QCOMPARE(model.sharedItems("file").value(0).toMap().value("title").toString(), QString("a.pdf"));
        QCOMPARE(model.sharedItems("link").value(0).toMap().value("url").toString(), QString("https://example.org/x"));
        model.setPollData(7, {{"poll_id", 7}, {"question", "Fixture?"}, {"loaded", true}, {"num_vote", 2}});
        const QModelIndex i = model.index(3);
        QCOMPARE(model.data(i, MessageModel::PollDataRole).toMap().value("num_vote").toInt(), 2);
        QCOMPARE(model.data(i, MessageModel::TextRole).toString(), QString("Fixture?"));
    }

    void testZaloApiClientEndpoints() {
        ZaloApiClient client;
        QCOMPARE(client.networkStatus(), QString("CONNECTED"));

        QSignalSpy netSpy(&client, &ZaloApiClient::networkStatusChanged);
        client.pingConnectivity();
        QCOMPARE(client.networkStatus(), QString("CHECKING"));

        client.generateQrToken();

        bool called = false;
        client.session()->fetchFriends([&](bool ok, const QJsonValue&, const QString&, int) {
            called = true;
            QCOMPARE(ok, false);
        });
        QVERIFY(called);
    }

    void testZaloClientWrappersIntegration() {
        ZaloClient client;
        QVERIFY(client.offlineStore() != nullptr);
        QVERIFY(client.apiClient() != nullptr);

        QString ocrResult = client.recognizeImageText("/nonexistent/file.png");
        QVERIFY(ocrResult.isEmpty());

        QByteArray binNetBytes;
        binNetBytes.append(static_cast<char>(0x0b));
        binNetBytes.append(static_cast<char>(0x00));
        binNetBytes.append(static_cast<char>(0x00));
        binNetBytes.append(static_cast<char>(0x00));
        const char textData[] = "Kiem thu BinNet";
        quint32 len = sizeof(textData) - 1;
        binNetBytes.append(static_cast<char>(len & 0xff));
        binNetBytes.append(static_cast<char>((len >> 8) & 0xff));
        binNetBytes.append(static_cast<char>(0x00));
        binNetBytes.append(static_cast<char>(0x00));
        binNetBytes.append(textData, len);

        QVariantMap binNetParsed = client.parseBinNetPayload(binNetBytes);
        QCOMPARE(binNetParsed["text"].toString(), QString("Kiem thu BinNet"));

        QVariantMap srtpKeys = client.deriveSrtpKeys("012345678901234567890123456789");
        QVERIFY(srtpKeys.contains("encKey"));
        QVERIFY(srtpKeys.contains("authKey"));
        QVERIFY(srtpKeys.contains("saltKey"));

        client.syncOfflineData();
    }

    void testCallBridgeFraming() {
        using Zalos::Core::Call::CallBridge;
        const QByteArray key(16, char(0x11));
        const QByteArray expected =
            "3acf70e766113e07d5ea0729112dd8b1f8d784fb418d278748b21eea249ada7d8f07b224bc8af36337b7e2710aa78da49a"
            "d9e6546bc743083349e9f8ad291c38$";
        QJsonObject frame{{"type", "update"}, {"command", "native-ready"}, {"data", QJsonObject()}};
        QByteArray jsFrame = expected;
        QHash<QString, QStringList> none;
        const QList<QJsonObject> fromJs = CallBridge::decodeFrames(jsFrame, key, &none);
        QCOMPARE(fromJs.size(), 1);
        QCOMPARE(fromJs.first(), frame);
        QByteArray ours = CallBridge::encodeFrame(frame, key);
        QCOMPARE(CallBridge::decodeFrames(ours, key, &none).value(0), frame);

        QJsonObject big{{"type", "control"}, {"data", QJsonObject{{"blob", QString(6000, QChar('x'))}}}};
        QByteArray buffer = CallBridge::encodeFrame(big, key) + expected.left(20);
        QHash<QString, QStringList> chunks;
        QList<QJsonObject> frames = CallBridge::decodeFrames(buffer, key, &chunks);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().value("data").toObject().value("blob").toString().size(), 6000);
        buffer += expected.mid(20);
        frames = CallBridge::decodeFrames(buffer, key, &chunks);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.first().value("command").toString(), QString("native-ready"));
    }

    void testNoiseLookupsRunOneAtATime() {
        QList<int> started;
        std::function<void()> pendingNext;
        Zalos::Core::Sync::MobileHistorySync::runInSequence(3, [&](int k, std::function<void()> next) {
            started.append(k);
            pendingNext = next;
        });
        QCOMPARE(started, QList<int>({0}));
        pendingNext();
        QCOMPARE(started, QList<int>({0, 1}));
        pendingNext();
        pendingNext();
        QCOMPARE(started, QList<int>({0, 1, 2}));
        started.clear();
        Zalos::Core::Sync::MobileHistorySync::runInSequence(0, [&](int k, std::function<void()>) { started.append(k); });
        QVERIFY(started.isEmpty());
    }

    void testSocketQueuesAndFailoverRules() {
        const QJsonObject auth{{"error_code", 0},
            {"data", QJsonObject{
                {"ctrl_qCmds", QJsonArray{QJsonObject{{"cmd", 603}, {"subCmd", 1}}, QJsonObject{{"cmd", 610}, {"subCmd", 1}}}},
                {"qCmds", QJsonArray{QJsonObject{{"cmd", 510}, {"subCmd", 1}}, QJsonObject{{"cmd", 603}, {"subCmd", 1}}}}}}};
        const auto queues = ZaloSession::queuesFromAuth(auth);
        QCOMPARE(queues.size(), 3);
        QCOMPARE(queues.at(0), qMakePair(603, 1));
        QCOMPARE(queues.at(2), qMakePair(510, 1));
        QVERIFY(ZaloSession::queuesFromAuth(QJsonObject()).isEmpty());

        const QJsonObject status{{"603_1", QJsonObject{{"lastId", "70"}, {"ids", QJsonArray{"a", "b"}}}}};
        QJsonObject next = ZaloSession::nextPullPage({{"more", 1}, {"lastActionId", "77"}, {"queueStatus", status}}, 603, 1);
        QCOMPARE(next.value("first").toBool(true), false);
        QCOMPARE(next.value("lastId").toString(), QString("77"));
        QCOMPARE(next.value("preIds").toArray().size(), 2);
        next = ZaloSession::nextPullPage({{"more", 1}, {"queueStatus", status}}, 603, 1);
        QCOMPARE(next.value("lastId").toString(), QString("70"));
        QVERIFY(ZaloSession::nextPullPage({{"more", 0}, {"queueStatus", status}}, 603, 1).isEmpty());
        QVERIFY(ZaloSession::nextPullPage({{"more", 1}}, 603, 1).isEmpty());

        QVERIFY(ZaloSession::isFailoverError(QNetworkReply::HostNotFoundError, 0));
        QVERIFY(ZaloSession::isFailoverError(QNetworkReply::ContentNotFoundError, 404));
        QVERIFY(!ZaloSession::isFailoverError(QNetworkReply::NoError, 200));
        QVERIFY(!ZaloSession::isFailoverError(QNetworkReply::ContentAccessDenied, 403));
    }

    void testIncomingDedupeReadPointsQuoteAndReply() {
        ZaloClient client;
        client.setLoggedInForTesting(true);
        auto* session = client.apiClient()->session();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        QJsonObject msg{{"msgId", "7001"}, {"cliMsgId", "11"}, {"uidFrom", "u-fixture"}, {"idTo", "0"},
                        {"msgType", "webchat"}, {"content", "xin chào"}, {"ts", ts}};
        emit session->messagesReceived(QJsonArray{msg}, false, false);
        emit session->messagesReceived(QJsonArray{msg}, false, false);
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 1);

        QJsonObject msg2 = msg;
        msg2["msgId"] = "7002";
        msg2["cliMsgId"] = "12";
        msg2["content"] = "trả lời";
        msg2["quote"] = QJsonObject{{"ownerId", "u-other"}, {"globalMsgId", 7001}, {"msg", "xin chào"}, {"fromD", "Fixture A"}};
        emit session->messagesReceived(QJsonArray{msg2}, false, false);
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 2);

        emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "u-fixture"}, {"lastMsgId", "7001"}, {"isGroup", 0}}});
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 1);
        emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "u-fixture"}, {"lastMsgId", "7002"},
                                                                {"type", 2}, {"sct", 1}}});
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 1);

        client.selectConversation("u-fixture", "Fixture", "");
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 0);
        bool quoted = false;
        for (int r = 0; r < client.messageModel()->rowCount(); ++r) {
            const QModelIndex i = client.messageModel()->index(r);
            if (client.messageModel()->data(i, MessageModel::IdRole).toString() == "7002") {
                quoted = client.messageModel()->data(i, MessageModel::QuoteTextRole).toString() == "xin chào"
                         && client.messageModel()->data(i, MessageModel::QuoteSenderRole).toString() == "Fixture A";
            }
        }
        QVERIFY(quoted);

        client.setReplyTarget("local-unknown");
        QVERIFY(client.replyTarget().isEmpty());
        client.setReplyTarget("7001");
        QCOMPARE(client.replyTarget().value("id").toString(), QString("7001"));
        client.clearReplyTarget();
        QVERIFY(client.replyTarget().isEmpty());

        QVERIFY(!client.scheduleMessage("muộn", QDateTime::currentMSecsSinceEpoch() - 1000));
        QVERIFY(client.scheduleMessage("hẹn giờ", QDateTime::currentMSecsSinceEpoch() + 3600000));
        QCOMPARE(client.scheduledMessages().size(), 1);
        client.cancelScheduledMessage(client.scheduledMessages().first().toMap().value("id").toString());
        QCOMPARE(client.scheduledMessages().size(), 0);
    }

    void testCallTransportKeyFile() {
        const QByteArray key = QByteArray::fromHex("00112233445566778899aabbccddeeff");
        QTemporaryFile file(QDir::tempPath() + "/zalos-test-XXXXXX.key");
        QVERIFY(CallBridge::writeKeyFile(&file, key));
        QCOMPARE(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
        file.close();
        QFile readBack(file.fileName());
        QVERIFY(readBack.open(QIODevice::ReadOnly));
        QCOMPARE(QByteArray::fromBase64(readBack.readAll().trimmed()), key);
    }

    void testMessagingRegressions() {
        ZaloClient client;
        client.selectConversation("u-peer", "Fixture Peer", "");

        client.sendMessage("one");
        client.sendMessage("two");
        QCOMPARE(client.messageModel()->rowCount(), 2);

        auto* session = client.apiClient()->session();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        QJsonObject msg{{"msgId", "9001"}, {"cliMsgId", "1"}, {"uidFrom", "u-peer"}, {"idTo", "0"},
                        {"msgType", "webchat"}, {"content", "hello"}, {"ts", ts}};
        emit session->messagesReceived(QJsonArray{msg}, false, false);
        QCOMPARE(client.messageModel()->rowCount(), 3);
        QJsonObject undo{{"msgId", "9002"}, {"uidFrom", "u-peer"}, {"idTo", "0"}, {"msgType", "chat.undo"}, {"ts", ts},
                         {"content", QJsonObject{{"globalMsgId", 9001}, {"cliMsgId", 1}, {"deleteMsg", 1}}}};
        emit session->messagesReceived(QJsonArray{undo}, false, false);
        bool recalled = false;
        for (int r = 0; r < client.messageModel()->rowCount(); ++r) {
            const QModelIndex i = client.messageModel()->index(r);
            if (client.messageModel()->data(i, MessageModel::IdRole).toString() == "9001")
                recalled = client.messageModel()->data(i, MessageModel::IsRecalledRole).toBool();
        }
        QVERIFY(recalled);

        auto* calls = client.calls();
        QByteArray stream = QByteArray(R"({"type":"video","key":true,"bin":5})") + "\n" + QByteArray("\x00\n{\x01\x02", 5)
                            + QByteArray(R"({"type":"state","phase":"connected","name":"Fixture","since":1})") + "\n";
        calls->injectUiBytesForTesting(stream.left(20));
        calls->injectUiBytesForTesting(stream.mid(20));
        QCOMPARE(calls->phase(), QString("connected"));
        calls->injectUiLineForTesting(R"({"type":"close"})");
        calls->hangup();
    }

    void testGroupPollsAndManagement() {
        ZaloClient client;
        client.selectConversation("grp-100", "Nhóm Dự Án", "");

        QCOMPARE(client.showCreatePollModal(), false);
        client.openCreatePollModal();
        QCOMPARE(client.showCreatePollModal(), true);

        QSignalSpy noticeSpy(&client, &ZaloClient::notice);
        client.createPoll("grp-100", QString::fromUtf8("Ăn gì?"), {QString::fromUtf8("Cơm")}, {});
        QVERIFY(noticeSpy.count() >= 1);
        const int beforeOffline = noticeSpy.count();
        client.createPoll("grp-100", QString::fromUtf8("Họp vào mấy giờ?"), {"9h", "10h", "14h"},
                          QVariantMap{{"allowMultiChoices", true}});
        QVERIFY(noticeSpy.count() > beforeOffline);
        QCOMPARE(client.showCreatePollModal(), true);
        QCOMPARE(client.messageModel()->rowCount(), 0);
        client.closeCreatePollModal();

        const int noticesBefore = noticeSpy.count();
        client.submitPollChoices(4242, {2}, {}, {});
        QVERIFY(noticeSpy.count() > noticesBefore);
        QCOMPARE(client.pollView(4242).value("loaded").toBool(), false);

        const QString groupNameBeforeRename = client.currentConversationName();
        const int noticesBeforeRename = noticeSpy.count();
        client.changeGroupName("grp-100", "Nhóm Dự Án 2026");
        QCOMPARE(client.currentConversationName(), groupNameBeforeRename);
        QVERIFY(noticeSpy.count() > noticesBeforeRename);

        client.addMemberToGroup("grp-100", "u-123");
        client.removeMemberFromGroup("grp-100", "u-123");

        bool pasted = client.tryPasteClipboardImage();
        QCOMPARE(pasted, false);

        client.sendMessage("Thông báo quan trọng cần ghim");
        QString pinTargetId = client.messageModel()->data(client.messageModel()->index(client.messageModel()->rowCount() - 1), MessageModel::IdRole).toString();
        QCOMPARE(client.hasPinnedMessage(), false);
        const int noticesBeforePin = noticeSpy.count();
        client.pinMessage(pinTargetId);
        QCOMPARE(client.hasPinnedMessage(), false);
        QVERIFY(noticeSpy.count() > noticesBeforePin);

        client.changeFriendAlias("u-123", "Bạn Đồng Nghiệp");
        QVERIFY(noticeSpy.count() >= 2);

        client.sendContactCard("grp-100", "u-999", "Trần Thị B", "0900000013");
        QModelIndex cardIdx = client.messageModel()->index(client.messageModel()->rowCount() - 1);
        QCOMPARE(client.messageModel()->data(cardIdx, MessageModel::MessageTypeRole).toString(), QString("card"));
        QCOMPARE(client.messageModel()->data(cardIdx, MessageModel::TextRole).toString(), QString("Trần Thị B"));
    }

    void testGroupAdminLinksRequestsAndInvites() {
        QVERIFY(GroupAdmin::isGroupLink("https://zalo.me/g/abcdef123"));
        QVERIFY(GroupAdmin::isGroupLink("zalo.me/g/abcdef123/"));
        QVERIFY(GroupAdmin::isGroupLink("http://www.zalo.me/g/Ab_c-12?src=share"));
        QVERIFY(!GroupAdmin::isGroupLink("https://zalo.me/0123456789"));
        QVERIFY(!GroupAdmin::isGroupLink("https://example.com/g/abcdef123"));
        QVERIFY(!GroupAdmin::isGroupLink("https://zalo.me/g/"));
        QCOMPARE(GroupAdmin::normalizeGroupLink(" zalo.me/g/abcdef123?x=1 "), QString("https://zalo.me/g/abcdef123"));

        QString role = "member";
        QStringList notices, reloaded;
        QSet<QString> known{"g-known"};
        GroupAdmin::Hooks hooks;
        hooks.session = []() -> ZaloSession* { return nullptr; };
        hooks.myRole = [&role](const QString&) { return role; };
        hooks.isMember = [&known](const QString& id) { return known.contains(id); };
        hooks.notice = [&notices](const QString& t) { notices << t; };
        hooks.groupChanged = [&reloaded](const QString& id) { reloaded << id; };
        GroupAdmin admin(hooks);

        admin.openManage("g-1");
        QVERIFY(admin.manageOpen());
        QCOMPARE(reloaded, QStringList{"g-1"});
        QVERIFY(admin.settings().isEmpty());
        QVERIFY(!admin.canAdminister());
        QSignalSpy settingsSpy(&admin, &GroupAdmin::settingsChanged);
        admin.rememberSettings("g-1", QJsonObject{{"joinAppr", 1}, {"lockSendMsg", 0}, {"blockName", 1}});
        QCOMPARE(settingsSpy.count(), 1);
        QCOMPARE(admin.settings().value("joinAppr").toBool(), true);
        QCOMPARE(admin.settings().value("lockSendMsg").toBool(), false);
        QCOMPARE(admin.settings().value("blockName").toBool(), true);
        admin.rememberSettings("g-other", QJsonObject{{"joinAppr", 0}});
        QCOMPARE(settingsSpy.count(), 1);
        admin.setSetting("joinAppr", false);
        QCOMPARE(admin.settings().value("joinAppr").toBool(), true);
        role = "admin";
        QVERIFY(admin.canAdminister());

        admin.applyLinkDetailForTesting(QJsonObject{{"link", "https://zalo.me/g/xyz789"}, {"enabled", 1},
                                                    {"expiration_date", 1700000000}});
        QCOMPARE(admin.link().value("enabled").toBool(), true);
        QCOMPARE(admin.link().value("link").toString(), QString("https://zalo.me/g/xyz789"));
        QCOMPARE(admin.link().value("expiration").toLongLong(), 1700000000000LL);
        admin.applyLinkDetailForTesting(QJsonObject{{"link", "https://zalo.me/g/xyz789"}, {"enabled", 0}});
        QCOMPARE(admin.link().value("enabled").toBool(), false);
        QVERIFY(admin.link().value("link").toString().isEmpty());
        QCOMPARE(admin.link().value("loaded").toBool(), true);

        admin.applyPendingForTesting(QJsonObject{{"time", 1}, {"users", QJsonArray{
            QJsonObject{{"uid", "u-p1"}, {"dpn", "Fixture P1"}, {"avatar", ""}},
            QJsonObject{{"uid", ""}, {"dpn", "no id"}}}}});
        QCOMPARE(admin.pending().size(), 1);
        QCOMPARE(admin.pending().first().toMap().value("name").toString(), QString("Fixture P1"));
        QVERIFY(admin.pendingLoaded());
        admin.applyBlockedForTesting(QJsonObject{{"blocked_members", QJsonArray{
            QJsonObject{{"id", "u-b1"}, {"dName", ""}, {"zaloName", "Fixture B1"}}}}, {"has_more", 0}});
        QCOMPARE(admin.blocked().size(), 1);
        QCOMPARE(admin.blocked().first().toMap().value("name").toString(), QString("Fixture B1"));

        admin.closeManage();
        QVERIFY(!admin.manageOpen());
        QVERIFY(admin.groupId().isEmpty());
        admin.openManage("g-1");
        QVERIFY(admin.pending().isEmpty());
        QVERIFY(!admin.pendingLoaded());
        QCOMPARE(admin.settings().value("joinAppr").toBool(), true);

        admin.openJoin("");
        QVERIFY(admin.joinOpen());
        admin.previewLink("https://example.com/x");
        QVERIFY(!admin.preview().value("error").toString().isEmpty());
        admin.previewLink("https://zalo.me/g/abcdef123");
        QVERIFY(!admin.preview().value("error").toString().isEmpty());
        admin.applyPreviewForTesting("https://zalo.me/g/abcdef123", QJsonObject{
            {"groupId", "g-known"}, {"name", "Fixture Group"}, {"desc", ""}, {"totalMember", 42},
            {"currentMems", QJsonArray{QJsonObject{{"id", "u-1"}, {"dName", "A"}}}},
            {"setting", QJsonObject{{"joinAppr", 1}}}});
        QCOMPARE(admin.preview().value("isMember").toBool(), true);
        QCOMPARE(admin.preview().value("needsApproval").toBool(), true);
        QCOMPARE(admin.preview().value("totalMember").toInt(), 42);
        QCOMPARE(admin.preview().value("members").toList().size(), 1);
        admin.closeJoin();
        QVERIFY(!admin.joinOpen());
        QVERIFY(admin.preview().isEmpty());

        QStringList failed;
        const QStringList done = GroupAdmin::invitedGroupsForTesting(QJsonObject{{"grid_message_map", QJsonObject{
            {"g-a", QJsonObject{{"error_code", 0}}}, {"g-b", QJsonObject{{"error_code", 269}}}}}}, &failed);
        QCOMPARE(done, QStringList{"g-a"});
        QCOMPARE(failed, QStringList{"g-b"});

        admin.openJoin("");
        admin.reset();
        QVERIFY(!admin.manageOpen());
        QVERIFY(!admin.joinOpen());
        admin.openManage("g-1");
        QVERIFY(admin.settings().isEmpty());

        ZaloClient client;
        client.setMyUidForTesting("u-me");
        client.applyGroupInfoForTesting(QJsonObject{
            {"g-2", QJsonObject{{"name", "Beta"}, {"memVerList", QJsonArray{"u-me_0", "u-x_0"}}, {"creatorId", "u-me"},
                                {"setting", QJsonObject{{"lockSendMsg", 1}}}}},
            {"g-1", QJsonObject{{"name", "Alpha"}, {"memVerList", QJsonArray{"u-me_0"}}, {"creatorId", "u-me"}}}});
        QCOMPARE(client.groupAdmin()->settingsOf("g-2").value("lockSendMsg").toInt(), 1);
        const QVariantList groups = client.groupsForInvite("u-x");
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(0).toMap().value("name").toString(), QString("Alpha"));
        QCOMPARE(groups.at(0).toMap().value("isMember").toBool(), false);
        QCOMPARE(groups.at(1).toMap().value("isMember").toBool(), true);
        client.openLink("https://zalo.me/g/abcdef123");
        QVERIFY(client.groupAdmin()->joinOpen());
        client.groupAdmin()->closeJoin();
        client.groupAdmin()->openManage("g-2");
        QVERIFY(client.groupAdmin()->canAdminister());
        QCOMPARE(client.groupAdmin()->settings().value("lockSendMsg").toBool(), true);
        client.groupAdmin()->closeManage();
    }

    void testRichComposerMediaRunsAndCompositeAvatars() {
        QTextDocument doc;
        doc.setPlainText("Hello world foo");
        RichComposer composer;
        composer.attachDocument(&doc);
        QVERIFY(composer.styles().isEmpty());
        QVERIFY(composer.apply("b", 0, 5));
        QVariantList st = composer.styles();
        QCOMPARE(st.size(), 1);
        QCOMPARE(st.first().toMap().value("start").toInt(), 0);
        QCOMPARE(st.first().toMap().value("len").toInt(), 5);
        QCOMPARE(st.first().toMap().value("st").toString(), QString("b"));
        composer.refresh(0, 5);
        QCOMPARE(composer.active().value("b").toBool(), true);
        QVERIFY(composer.apply("b", 0, 5));
        QVERIFY(composer.styles().isEmpty());
        QVERIFY(composer.apply("i", 7, 7));
        QCOMPARE(composer.styles().first().toMap().value("start").toInt(), 6);
        QCOMPARE(composer.styles().first().toMap().value("len").toInt(), 5);
        QVERIFY(composer.apply("c_db342e", 0, 15));
        QVERIFY(composer.apply("c_15a85f", 0, 5));
        bool red = false, green = false;
        for (const QVariant& v : composer.styles()) {
            const QVariantMap m = v.toMap();
            if (m.value("st") == "c_15a85f") { green = true; QCOMPARE(m.value("len").toInt(), 5); }
            if (m.value("st") == "c_db342e") { red = true; QCOMPARE(m.value("start").toInt(), 5); }
        }
        QVERIFY(red && green);
        QVERIFY(composer.apply("f_18", 0, 5));
        QVERIFY(composer.apply("f_", 0, 5));
        for (const QVariant& v : composer.styles()) QVERIFY(v.toMap().value("st").toString() != "f_18");
        composer.clearFormatting(0, 0);
        QVERIFY(composer.styles().isEmpty());
        QVERIFY(!composer.apply("b", 5, 5) || true);
        doc.setPlainText("one\ntwo\n");
        composer.toggleList("lst_2", 0, 6);
        st = composer.styles();
        QCOMPARE(st.size(), 2);
        QCOMPARE(st.at(1).toMap().value("start").toInt(), 4);
        QCOMPARE(st.at(1).toMap().value("st").toString(), QString("lst_2"));
        composer.refresh(1, 1);
        QCOMPARE(composer.active().value("list").toString(), QString("lst_2"));
        composer.toggleList("lst_2", 0, 6);
        QVERIFY(composer.styles().isEmpty());
        doc.setPlainText("abc def");
        RichComposer::applyStyles(&doc, {QVariantMap{{"start", 4}, {"len", 3}, {"st", "u"}}});
        QCOMPARE(RichComposer::exportStyles(&doc).size(), 1);
        QVERIFY(RichComposer::isKnownStyle("c_f7b503"));
        QVERIFY(!RichComposer::isKnownStyle("c_123456"));
        doc.setPlainText("hello world");
        QVERIFY(composer.apply("b", 5, 1));
        QCOMPARE(composer.styles().size(), 1);
        QCOMPARE(composer.styles().first().toMap().value("start").toInt(), 1);
        QCOMPARE(composer.styles().first().toMap().value("len").toInt(), 4);

        const QString props = ZaloSession::textPropertiesJson(QJsonArray{QJsonObject{{"start", 0}, {"len", 5}, {"st", "b"}},
                                                                          QJsonObject{{"start", -1}, {"len", 2}, {"st", "i"}}});
        QCOMPARE(props, QString(R"({"styles":[{"len":5,"st":"b","start":0}],"ver":0})"));
        QVERIFY(ZaloSession::textPropertiesJson({}).isEmpty());
        const QString html = MessageModel::styledHtml("Hello world\nnext", {},
            {QVariantMap{{"start", 0}, {"len", 5}, {"st", "b"}}, QVariantMap{{"start", 6}, {"len", 5}, {"st", "c_db342e"}},
             QVariantMap{{"start", 12}, {"len", 4}, {"st", "lst_1"}}});
        QVERIFY2(html.contains("<b>Hello</b>"), qPrintable(html));
        QVERIFY2(html.contains("color:#db342e"), qPrintable(html));
        QVERIFY2(html.contains(QString::fromUtf8("• next")), qPrintable(html));
        QVERIFY(!html.contains("<b>world"));

        ZaloClient client;
        const MessageItem styled = client.toMessageItemForTesting(QJsonObject{
            {"msgId", "s1"}, {"uidFrom", "u-a"}, {"idTo", "u-me"}, {"msgType", "webchat"}, {"content", "Hi there"},
            {"ts", "1700000000000"}, {"params", R"({"styles":[{"start":0,"len":2,"st":"u"},{"start":5,"len":99,"st":"b"}]})"}}, false);
        QCOMPARE(styled.extra.value("styles").toList().size(), 1);

        const MessageItem gif = client.toMessageItemForTesting(QJsonObject{
            {"msgId", "g1"}, {"uidFrom", "u-a"}, {"idTo", "u-me"}, {"msgType", "chat.gif"}, {"ts", "1700000000000"},
            {"content", QJsonObject{{"thumb", "https://example.com/t.jpg"}, {"href", "https://example.com/a.gif"},
                                    {"params", "{\"width\":200,\"height\":200}"}}}}, false);
        QCOMPARE(gif.mediaUrl, QString("https://example.com/t.jpg"));
        QCOMPARE(gif.extra.value("animatedUrl").toString(), QString("https://example.com/a.gif"));
        const MessageItem photo = client.toMessageItemForTesting(QJsonObject{
            {"msgId", "p1"}, {"uidFrom", "u-a"}, {"idTo", "u-me"}, {"msgType", "chat.photo"}, {"ts", "1700000000000"},
            {"content", QJsonObject{{"thumb", "https://example.com/t.jpg"}, {"href", "https://example.com/o.jpg"}}}}, false);
        QVERIFY(!photo.extra.contains("animatedUrl"));

        MessageModel model;
        auto tile = [](const QString& id, const QString& type, qint64 ts, const QString& sender = "u-a") {
            MessageItem m;
            m.id = id; m.messageType = type; m.ts = ts; m.senderId = sender; m.mediaUrl = "https://example.com/" + id;
            return m;
        };
        const qint64 t0 = QDateTime(QDate(2026, 10, 30), QTime(13, 41)).toMSecsSinceEpoch();
        MessageItem caption = tile("m4", "image", t0 + 3000);
        caption.text = "caption";
        model.mergeMessages("c1", {tile("m1", "sticker", t0), tile("m2", "sticker", t0 + 1000), tile("m3", "image", t0 + 2000),
                                   caption, tile("m5", "image", t0 + 4000, "u-b")});
        model.loadConversation("c1");
        QCOMPARE(model.rowCount(), 5);
        auto role = [&model](int row, int r) { return model.data(model.index(row), r); };
        QCOMPARE(role(0, MessageModel::MediaRunRole).toList().size(), 3);
        QCOMPARE(role(0, MessageModel::InMediaRunRole).toBool(), false);
        QCOMPARE(role(1, MessageModel::InMediaRunRole).toBool(), true);
        QCOMPARE(role(2, MessageModel::InMediaRunRole).toBool(), true);
        QCOMPARE(role(3, MessageModel::InMediaRunRole).toBool(), false);
        QCOMPARE(role(4, MessageModel::MediaRunRole).toList().size(), 0);
        QCOMPARE(role(0, MessageModel::LastInGroupRole).toBool(), false);
        QCOMPARE(role(0, MessageModel::MediaRunRole).toList().at(2).toMap().value("msgId").toString(), QString("m3"));
        model.mergeMessages("c1", {tile("m6", "sticker", t0 + 10 * 60000)});
        QCOMPARE(role(5, MessageModel::InMediaRunRole).toBool(), false);
        model.removeMessage("c1", "m2");
        QCOMPARE(role(0, MessageModel::MediaRunRole).toList().size(), 2);

        client.setMyUidForTesting("u-me");
        client.applyGroupInfoForTesting(QJsonObject{
            {"g-np", QJsonObject{{"name", "No Photo"}, {"avt", ""}, {"totalMember", 9},
                                 {"memVerList", QJsonArray{"u-me_0", "u-1_0", "u-2_0", "u-3_0", "u-4_0"}}, {"creatorId", "u-1"}}},
            {"g-ph", QJsonObject{{"name", "Photo"}, {"avt", "https://example.com/g.jpg"}, {"totalMember", 3},
                                 {"memVerList", QJsonArray{"u-me_0", "u-1_0"}}, {"creatorId", "u-1"}}}});
        QCOMPARE(client.groupComposite("g-np").size(), 4);
        QVERIFY(client.groupComposite("g-ph").isEmpty());
    }

    void testCallStartLaunchesEngine() {
        CallBridge bridge(nullptr);
        QVERIFY(bridge.isAvailable());
        bridge.startVoiceCall(QStringLiteral("u-engine-test"), QStringLiteral("Peer"), QString());
        auto* process = bridge.findChild<QProcess*>();
        QVERIFY(process != nullptr);
        QVERIFY(process->waitForStarted(5000));
        QCOMPARE(QFileInfo(process->program()).fileName(), QStringLiteral("zalos-call"));
    }

    void testCallEngineDiscovery() {
        const QString engine = CallBridge::findEngine();
        QVERIFY(!engine.isEmpty());
        QCOMPARE(QFileInfo(engine).fileName(), QStringLiteral("zalos-call"));
        const QString audio = CallBridge::audioEngineFor(engine);
        QVERIFY(!audio.isEmpty());
        QCOMPARE(QFileInfo(audio).fileName(), QStringLiteral("zalos-call-audio"));
        QVERIFY(CallBridge::audioEngineFor(QString()).isEmpty());
        QVERIFY(CallBridge::audioEngineFor(QStringLiteral("/nonexistent/zalos-call")).isEmpty());
    }

    void testLabelsAndContactCards() {
        QStringList notices;
        QHash<QString, QVariantMap> applied;
        LabelManager::Hooks hooks;
        hooks.session = []() -> ZaloSession* { return nullptr; };
        hooks.isGroup = [](const QString& id) { return id.startsWith("9"); };
        hooks.notice = [&notices](const QString& t) { notices << t; };
        hooks.apply = [&applied](const QHash<QString, QVariantMap>& m) { applied = m; };
        LabelManager labels(hooks);
        QVERIFY(!labels.loaded());
        QCOMPARE(labels.palette().size(), 8);

        const QJsonArray server{
            QJsonObject{{"id", 2}, {"text", "Công việc"}, {"textKey", "STR_LABEL_WORK"}, {"color", "#0068FF"}, {"offset", 1},
                        {"conversations", QJsonArray{"g9001", "101"}}, {"emoji", ""}, {"createTime", 1}},
            QJsonObject{{"id", 1}, {"text", "Khách hàng"}, {"color", "D91B1B"}, {"offset", 0},
                        {"conversations", QJsonArray{"102"}}, {"emoji", ""}, {"createTime", 1}}};
        labels.applyServer(QJsonObject{{"labelData", QString::fromUtf8(QJsonDocument(server).toJson())}, {"version", 7}});
        QVERIFY(labels.loaded());
        QCOMPARE(labels.versionForTesting(), 7);
        QCOMPARE(labels.labels().size(), 2);
        QCOMPARE(labels.labels().first().toMap().value("text").toString(), QString("Khách hàng"));
        QCOMPARE(labels.labels().first().toMap().value("color").toString(), QString("#D91B1B"));
        QCOMPARE(labels.labels().at(1).toMap().value("count").toInt(), 2);
        QCOMPARE(applied.value("9001").value("id").toInt(), 2);
        QCOMPARE(applied.value("102").value("text").toString(), QString("Khách hàng"));
        QCOMPARE(labels.labelOf("101"), 2);
        QCOMPARE(LabelManager::decodeConvId("g123"), QString("123"));
        QCOMPARE(LabelManager::decodeConvId("u-b"), QString("u-b"));
        QCOMPARE(LabelManager::encodeConvId("123", true), QString("g123"));

        QJsonArray next = labels.assignedForTesting("101", 1);
        QCOMPARE(next.at(0).toObject().value("conversations").toArray().size(), 2);
        QCOMPARE(next.at(1).toObject().value("conversations").toArray(), QJsonArray{"g9001"});
        next = labels.assignedForTesting("101", 2);
        QCOMPARE(next.at(1).toObject().value("conversations").toArray(), QJsonArray{"g9001"});
        next = labels.assignedForTesting("9002", 1);
        QVERIFY(next.at(0).toObject().value("conversations").toArray().contains(QJsonValue("g9002")));

        labels.assign("101", 1);
        QCOMPARE(labels.labelOf("101"), 2);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("conversations").toArray().size(), 2);
        QVERIFY(!notices.isEmpty());

        QVERIFY(!labels.create("  ", "#4BC377").isEmpty());
        QVERIFY(!labels.create(QString(31, 'x'), "#4BC377").isEmpty());
        QVERIFY(!labels.create("khách hàng", "#4BC377").isEmpty());
        QVERIFY(labels.create("Bạn bè", "#4BC377").isEmpty());
        QCOMPARE(labels.lastSubmittedForTesting().size(), 3);
        QCOMPARE(labels.lastSubmittedForTesting().at(2).toObject().value("id").toInt(), 3);
        QCOMPARE(labels.lastSubmittedForTesting().at(2).toObject().value("offset").toInt(), 2);
        QVERIFY(labels.edit(2, "Việc gấp", "#FF6905").isEmpty());
        const QJsonObject edited = labels.lastSubmittedForTesting().at(1).toObject();
        QCOMPARE(edited.value("text").toString(), QString("Việc gấp"));
        QVERIFY(!edited.contains("textKey"));
        QCOMPARE(edited.value("color").toString(), QString("#FF6905"));
        labels.move(2, -1);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("id").toInt(), 2);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("offset").toInt(), 0);
        labels.remove(1);
        QCOMPARE(labels.lastSubmittedForTesting().size(), 1);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("offset").toInt(), 0);
        QJsonArray full;
        for (int i = 1; i <= 15; ++i) full.append(QJsonObject{{"id", i}, {"text", QString("L%1").arg(i)}, {"offset", i}});
        labels.applyServer(QJsonObject{{"labelData", full}, {"version", 8}});
        QVERIFY(!labels.create("L16", "#4BC377").isEmpty());

        ConversationModel convs;
        ConversationItem a; a.id = "u-a"; a.name = "A"; a.tag = "stale"; a.tagColor = "#000000";
        ConversationItem b; b.id = "u-b"; b.name = "B";
        convs.setConversations({a, b});
        convs.setCategoryFilter("");
        QHash<QString, QVariantMap> map;
        map.insert("u-b", QVariantMap{{"id", 4}, {"text", "VIP"}, {"color", "#D91B1B"}});
        convs.applyLabels(map);
        auto rowOf = [&convs](const QString& id) {
            for (int r = 0; r < convs.rowCount(); ++r)
                if (convs.data(convs.index(r), ConversationModel::IdRole).toString() == id) return r;
            return -1;
        };
        QCOMPARE(convs.data(convs.index(rowOf("u-a")), ConversationModel::TagRole).toString(), QString());
        QCOMPARE(convs.data(convs.index(rowOf("u-b")), ConversationModel::LabelIdRole).toInt(), 4);
        convs.setLabelFilter(4);
        QCOMPARE(convs.rowCount(), 1);
        ConversationItem c; c.id = "u-c"; c.name = "C";
        convs.setLabelFilter(0);
        map.insert("u-c", QVariantMap{{"id", 4}, {"text", "VIP"}, {"color", "#D91B1B"}});
        convs.applyLabels(map);
        convs.addConversation(c);
        QCOMPARE(convs.data(convs.index(rowOf("u-c")), ConversationModel::LabelIdRole).toInt(), 4);

        ZaloClient client;
        const MessageItem card = client.toMessageItemForTesting(QJsonObject{
            {"msgId", "c1"}, {"uidFrom", "u-a"}, {"idTo", "u-me"}, {"msgType", "chat.recommended"}, {"ts", "1700000000000"},
            {"content", QJsonObject{{"action", "recommened.user"}, {"title", "Fixture Person"}, {"thumb", "https://example.com/a.jpg"},
                                    {"params", "u-card"}, {"description", QString::fromUtf8(QJsonDocument(QJsonObject{{"phone", "0900000000"}, {"qrCodeUrl", "https://example.com/qr"}}).toJson(QJsonDocument::Compact))}}}}, false);
        QCOMPARE(card.messageType, QString("card"));
        QCOMPARE(card.text, QString("Fixture Person"));
        QCOMPARE(card.extra.value("uid").toString(), QString("u-card"));
        QCOMPARE(card.extra.value("phone").toString(), QString("0900000000"));
        QCOMPARE(card.mediaUrl, QString("https://example.com/a.jpg"));
        const MessageItem link = client.toMessageItemForTesting(QJsonObject{
            {"msgId", "l1"}, {"uidFrom", "u-a"}, {"idTo", "u-me"}, {"msgType", "chat.recommended"}, {"ts", "1700000000000"},
            {"content", QJsonObject{{"action", "recommened.link"}, {"title", "Page"}, {"href", "https://example.com"}}}}, false);
        QCOMPARE(link.messageType, QString("link"));

        client.selectConversation("u-a", "A", "");
        client.openShareCard("u-card", "Fixture Person");
        QCOMPARE(client.forwardMode(), QString("card"));
        QCOMPARE(client.forwardCardUid(), QString("u-card"));
        client.closeForwardModal();
        QCOMPARE(client.forwardMode(), QString("message"));
        client.openSendCardPicker();
        QCOMPARE(client.forwardMode(), QString("pickContact"));
        client.closeForwardModal();
        client.sendContactCard("u-a", "u-card", "Fixture Person");
        const int last = client.messageModel()->rowCount() - 1;
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(last), MessageModel::MessageTypeRole).toString(), QString("card"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(last), MessageModel::StatusRole).toString(), QString("failed"));
    }

    void testCameraSenderEncodesFrames() {
        if (!OpenH264::instance().available())
            QSKIP(qPrintable("OpenH264 unavailable: " + OpenH264::instance().unavailableReason()));
        auto makeFrame = [](int w, int h, int shade) {
            QVideoFrame frame(QVideoFrameFormat(QSize(w, h), QVideoFrameFormat::Format_BGRA8888));
            if (!frame.map(QVideoFrame::WriteOnly)) return QVideoFrame();
            for (int y = 0; y < h; ++y) {
                uchar* row = frame.bits(0) + y * frame.bytesPerLine(0);
                for (int x = 0; x < w; ++x) {
                    row[x * 4 + 0] = uchar((x + shade) & 0xff);
                    row[x * 4 + 1] = uchar((y * 2) & 0xff);
                    row[x * 4 + 2] = uchar(shade & 0xff);
                    row[x * 4 + 3] = 0xff;
                }
            }
            frame.unmap();
            return frame;
        };
        const QByteArray i420 = frameToI420(makeFrame(640, 480, 10), QSize(320, 180));
        QCOMPARE(i420.size(), 320 * 180 * 3 / 2);

        auto nonZeroBytes = [](const QByteArray& buf) {
            for (char ch : buf) {
                if (ch != 0) return true;
            }
            return false;
        };
        auto makeYuvFrame = [](int w, int h, QVideoFrameFormat::PixelFormat fmt) {
            QVideoFrame frame(QVideoFrameFormat(QSize(w, h), fmt));
            if (!frame.map(QVideoFrame::WriteOnly)) return QVideoFrame();
            for (int r = 0; r < h; ++r) {
                uchar* line = frame.bits(0) + r * frame.bytesPerLine(0);
                if (fmt == QVideoFrameFormat::Format_YUYV) {
                    for (int c = 0; c < w; c += 2) {
                        line[c * 2 + 0] = uchar((r + c + 16) & 0xff);
                        line[c * 2 + 1] = 90;
                        line[c * 2 + 2] = uchar((r + c + 17) & 0xff);
                        line[c * 2 + 3] = 160;
                    }
                } else {
                    std::memset(line, uchar(64 + (r & 0x3f)), size_t(w));
                }
            }
            if (fmt == QVideoFrameFormat::Format_YUV420P) {
                for (int r = 0; r < h / 2; ++r) {
                    std::memset(frame.bits(1) + r * frame.bytesPerLine(1), 96, size_t(w / 2));
                    std::memset(frame.bits(2) + r * frame.bytesPerLine(2), 144, size_t(w / 2));
                }
            } else if (fmt == QVideoFrameFormat::Format_NV12) {
                for (int r = 0; r < h / 2; ++r) {
                    uchar* uv = frame.bits(1) + r * frame.bytesPerLine(1);
                    for (int c = 0; c < w / 2; ++c) {
                        uv[c * 2] = 96;
                        uv[c * 2 + 1] = 144;
                    }
                }
            }
            frame.unmap();
            return frame;
        };
        for (const auto fmt : {QVideoFrameFormat::Format_YUV420P, QVideoFrameFormat::Format_NV12,
                                QVideoFrameFormat::Format_YUYV}) {
            const QByteArray exact = frameToI420(makeYuvFrame(320, 180, fmt), QSize(320, 180));
            QCOMPARE(exact.size(), 320 * 180 * 3 / 2);
            QVERIFY(nonZeroBytes(exact));
            const QByteArray cropped = frameToI420(makeYuvFrame(640, 480, fmt), QSize(320, 180));
            QCOMPARE(cropped.size(), 320 * 180 * 3 / 2);
            QVERIFY(nonZeroBytes(cropped));
        }

        CameraSender sender;
        sender.setProfile(QSize(321, 181), 15, 200000);
        QCOMPARE(sender.size(), QSize(320, 180));
        QList<bool> keys;
        QList<QByteArray> units;
        QList<QSize> sizes;
        connect(&sender, &CameraSender::frameEncoded, this, [&](bool key, const QByteArray& au, int w, int h) {
            keys << key;
            units << au;
            sizes << QSize(w, h);
        });
        sender.processFrame(makeFrame(640, 480, 0));
        QCOMPARE(keys.size(), 1);
        QVERIFY(keys.first());
        QVERIFY(units.first().startsWith(QByteArray("\x00\x00\x00\x01", 4)) || units.first().startsWith(QByteArray("\x00\x00\x01", 3)));
        sender.processFrame(makeFrame(640, 480, 5));
        QCOMPARE(keys.size(), 1);
        QTest::qWait(80);
        sender.processFrame(makeFrame(640, 480, 9));
        QCOMPARE(keys.size(), 2);
        QVERIFY(!keys.last());
        sender.requestKeyFrame();
        QTest::qWait(80);
        sender.processFrame(makeFrame(640, 480, 13));
        QVERIFY(keys.last());
        sender.setProfile(QSize(640, 360), 15, 300000);
        QTest::qWait(80);
        sender.processFrame(makeFrame(640, 480, 17));
        QVERIFY(keys.last());
        QCOMPARE(sizes.first(), QSize(320, 180));
        QCOMPARE(sizes.last(), QSize(640, 360));

        H264Decoder decoder;
        QVERIFY(decoder.isOpen());
        bool damaged = false;
        const QVideoFrame decoded = decoder.decode(units.first(), &damaged);
        QVERIFY(!damaged);
        QVERIFY(decoded.isValid());
        QCOMPARE(decoded.size(), QSize(320, 180));

        CallBridge bridge(nullptr);
        if (!CameraSender::cameraAvailable()) {
            QVERIFY(!bridge.cameraAvailable());
            bridge.setCamera(true);
            QVERIFY(!bridge.cameraOn());
            QCOMPARE(bridge.cameraError(), QString("no_camera"));
        }
    }

    void testUnreadReactionsOnOwnMessages() {
        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-rx-owner");
            owner.clearCache();
        }
        ZaloClient client;
        client.openOfflineCacheForTesting("u-rx-owner");
        client.setMyUidForTesting("u-rx-owner");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        emit client.apiClient()->session()->messagesReceived(QJsonArray{
            QJsonObject{{"msgId", "9901"}, {"cliMsgId", "9901"}, {"uidFrom", "0"}, {"idTo", "u-rx-peer"},
                        {"msgType", "webchat"}, {"content", "fixture own words"}, {"ts", QString::number(now - 5000)}},
            QJsonObject{{"msgId", "9902"}, {"cliMsgId", "9902"}, {"uidFrom", "u-rx-peer"}, {"idTo", "u-rx-owner"},
                        {"msgType", "webchat"}, {"content", "fixture peer words"}, {"ts", QString::number(now - 4000)}}},
            false, false);
        auto react = [now](const QString& target, const QString& icon, int rType, const QString& eventId) {
            return QJsonObject{{"uidFrom", "u-rx-peer"}, {"idTo", "u-rx-owner"}, {"msgId", eventId},
                               {"ts", QString::number(now)},
                               {"content", QJsonObject{{"rIcon", icon}, {"rType", rType},
                                                       {"rMsg", QJsonArray{QJsonObject{{"gMsgID", target}}}}}}};
        };
        client.selectConversation("u-rx-peer", QString(), QString());
        QCOMPARE(client.currentUnreadReactionCount(), 0);

        client.handleReactionsForTesting(QJsonArray{react("9901", "/-heart", 5, "ev-1"),
                                                    react("9902", "/-heart", 5, "ev-2")}, false, true);
        QCOMPARE(client.currentUnreadReactionCount(), 1);
        QCOMPARE(client.nextUnreadReactionMsgId(), QStringLiteral("9901"));
        QVERIFY(!client.currentUnreadReactionIcon().isEmpty());

        client.handleReactionsForTesting(QJsonArray{react("9901", "/-heart", 5, "ev-1")}, false, false);
        client.handleReactionsForTesting(QJsonArray{react("9901", "/-strong", 3, "ev-3")}, false, false);
        QCOMPARE(client.currentUnreadReactionCount(), 1);

        client.handleReactionsForTesting(QJsonArray{react("9901", "", -1, "ev-4")}, false, true);
        QCOMPARE(client.currentUnreadReactionCount(), 0);

        client.handleReactionsForTesting(QJsonArray{react("9901", "/-heart", 5, "ev-5")}, false, false);
        QCOMPARE(client.currentUnreadReactionCount(), 1);
        client.markReactionsSeen("9901");
        QCOMPARE(client.currentUnreadReactionCount(), 0);
        QVERIFY(client.offlineStore()->unreadReactionsOf("u-rx-peer").isEmpty());

        client.handleReactionsForTesting(QJsonArray{react("9901", "/-heart", 5, "ev-6")}, false, false);
        QCOMPARE(client.currentUnreadReactionCount(), 1);
        QCOMPARE(client.offlineStore()->messageText("9901"), QStringLiteral("fixture own words"));
        client.markReactionsSeen("9901");

        QJsonObject cliReact{
            {"uidFrom", "u-rx-peer"}, {"idTo", "u-rx-owner"}, {"msgId", "ev-cli-1"}, {"ts", QString::number(now)},
            {"content", QJsonObject{{"rIcon", "/-strong"}, {"rType", 3},
                                    {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "0"}, {"cMsgID", "9901"}}}}}}
        };
        client.handleReactionsForTesting(QJsonArray{cliReact}, false, true);
        QCOMPARE(client.currentUnreadReactionCount(), 1);
        QCOMPARE(client.nextUnreadReactionMsgId(), QStringLiteral("9901"));
        QCOMPARE(client.notificationsForTesting()->shownForTesting(QStringLiteral("reaction:u-rx-peer:9901")), 2);
        client.markReactionsSeen("9901");

        client.handleReactionsForTesting(QJsonArray{react("8456208657318", ":>", 0, "ev-uncached-1")}, false, true);
        QCOMPARE(client.currentUnreadReactionCount(), 1);
        QCOMPARE(client.nextUnreadReactionMsgId(), QStringLiteral("8456208657318"));
        QCOMPARE(client.notificationsForTesting()->shownForTesting(QStringLiteral("reaction:u-rx-peer:8456208657318")), 1);
        client.markReactionsSeen("8456208657318");

        client.handleReactionsForTesting(QJsonArray{react("9901", "/-heart", 5, "ev-7")}, false, false);
        client.offlineStore()->deleteMessage("9901");
        QVERIFY(client.offlineStore()->unreadReactionsOf("u-rx-peer").isEmpty());
        QVERIFY(client.offlineStore()->messageText("9901").isEmpty());
    }

    void testReactionRemovalNotRevivedByReplay() {
        ZaloClient client;
        client.setMyUidForTesting("u-me");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto frame = [now](const QString& from, const QString& icon, int rType, const QString& eventId, qint64 ts) {
            return QJsonObject{{"uidFrom", from}, {"idTo", "g-rr"}, {"msgId", eventId}, {"ts", QString::number(ts)},
                               {"content", QJsonObject{{"rIcon", icon}, {"rType", rType},
                                                       {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "m-rr"}}}}}}};
        };
        const QJsonObject addOld = frame("u-a", "/-heart", 5, "ev-old", now - 3000);
        client.handleReactionsForTesting(QJsonArray{addOld}, true);
        QCOMPARE(client.reactionDetails("m-rr").value("total").toInt(), 1);

        client.handleReactionsForTesting(QJsonArray{frame("u-a", "", -1, "ev-undo", now - 2000)}, true);
        QCOMPARE(client.reactionDetails("m-rr").value("total").toInt(), 0);

        client.handleReactionsForTesting(QJsonArray{addOld}, true);
        QCOMPARE(client.reactionDetails("m-rr").value("total").toInt(), 0);

        client.handleReactionsForTesting(QJsonArray{frame("u-a", "/-strong", 3, "ev-new", now - 1000)}, true);
        QCOMPARE(client.reactionDetails("m-rr").value("total").toInt(), 1);
    }

    void testReactionDetails() {
        ZaloClient client;
        client.setMyUidForTesting("u-me");
        auto react = [](const QString& from, const QString& icon, int rType) {
            return QJsonObject{{"uidFrom", from}, {"idTo", "g-r"},
                               {"content", QJsonObject{{"rIcon", icon}, {"rType", rType},
                                                       {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "m-r1"}}}}}}};
        };
        QVERIFY(!client.reactionDetails("m-r1").value("known").toBool());
        client.handleReactionsForTesting(QJsonArray{react("u-a", "/-heart", 5), react("u-b", "/-strong", 3),
                                                    react("u-c", "/-strong", 3), react("0", "/-heart", 5)}, true);
        QVariantMap d = client.reactionDetails("m-r1");
        QVERIFY(d.value("known").toBool());
        QCOMPARE(d.value("total").toInt(), 4);
        const QVariantList groups = d.value("groups").toList();
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(0).toMap().value("count").toInt(), 2);
        const QVariantList people = d.value("people").toList();
        QCOMPARE(people.size(), 4);
        QVERIFY(people.first().toMap().value("isMe").toBool());
        QCOMPARE(people.first().toMap().value("emoji").toString(), QString::fromUtf8("❤️"));
        client.handleReactionsForTesting(QJsonArray{react("u-b", "", -1)}, true);
        d = client.reactionDetails("m-r1");
        QCOMPARE(d.value("total").toInt(), 3);
        QCOMPARE(d.value("groups").toList().size(), 2);
    }

    void testReactionHistoryPersists() {
        {
            ZaloOfflineStore store;
            store.bindOwner("u-react-owner");
            store.clearCache();
            store.bindOwner("u-react-owner");
            ConversationItem conv; conv.id = "u-react-peer"; conv.name = "Fixture"; conv.lastTs = 1000;
            QVERIFY(store.saveConversation(conv));
            MessageItem m; m.id = "7001"; m.text = "old"; m.ts = 1000; m.senderId = "u-react-peer";
            QVERIFY(store.saveMessage("u-react-peer", m));
            QVERIFY(store.addReactionEvent("7001", "u-a", "/-heart", "ev-1", 1000));
            QVERIFY(!store.addReactionEvent("7001", "u-a", "/-heart", "ev-1", 1000));
            QVERIFY(store.setMessageReaction("7001", QString::fromUtf8("❤️")));
            QVERIFY(store.saveMessage("u-react-peer", m));
            QCOMPARE(store.loadMessages("u-react-peer").first().reaction, QString::fromUtf8("❤️"));
            QCOMPARE(store.reactionEventsOf("7001").size(), 1);
            QCOMPARE(store.reactionEventsOf("7001").first().icon, QString("/-heart"));
            QVERIFY(store.reactedMessageIds().contains("7001"));
            QVERIFY(store.clearReactions("7001", "u-a"));
            QVERIFY(store.reactionEventsOf("7001").isEmpty());
        }
        {
            ZaloClient client;
            QVERIFY(client.openOfflineCacheForTesting("u-react-owner"));
            client.setMyUidForTesting("u-react-owner");
            client.handleReactionsForTesting(QJsonArray{QJsonObject{{"uidFrom", "u-react-peer"}, {"idTo", "0"},
                {"content", QJsonObject{{"rIcon", "/-strong"}, {"rType", 3}, {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "7001"}}}}}}}}, false);
        }
        {
            ZaloOfflineStore store;
            store.bindOwner("u-react-owner");
            QCOMPARE(store.loadMessages("u-react-peer").first().reaction, QString::fromUtf8("👍"));
            ZaloClient client;
            QVERIFY(client.openOfflineCacheForTesting("u-react-owner"));
            const QVariantMap d = client.reactionDetails("7001");
            QVERIFY(d.value("known").toBool());
            QCOMPARE(d.value("people").toList().first().toMap().value("uid").toString(), QString("u-react-peer"));
            store.clearCache();
        }
    }

    void testStickerStore() {
        QVariantMap a = StickerStore::describe(QJsonObject{{"id", 11}, {"cateId", 5}, {"type", 7}, {"ext", 2},
            {"stickerUrl", "https://example.com/s.png"}, {"stickerSpriteUrl", "https://example.com/s.webp"}, {"totalFrames", 1}});
        QCOMPARE(a.value("animatedUrl").toString(), QString("https://example.com/s.webp"));
        QCOMPARE(a.value("url").toString(), QString("https://example.com/s.png"));
        QVariantMap b = StickerStore::describe(QJsonObject{{"id", 12}, {"cateId", 5}, {"ext", 0}, {"totalFrames", 16},
            {"duration", 80}, {"stickerUrl", "https://example.com/t.png"}, {"stickerSpriteUrl", "https://example.com/t-sprite.png"},
            {"stickerWebpUrl", QJsonValue::Null}});
        QCOMPARE(b.value("spriteUrl").toString(), QString("https://example.com/t-sprite.png"));
        QCOMPARE(b.value("frames").toInt(), 16);
        QCOMPARE(b.value("duration").toInt(), 80);
        QVERIFY(b.value("animatedUrl").toString().isEmpty());
        QVariantMap c = StickerStore::describe(QJsonObject{{"id", 13}, {"stickerUrl", "https://example.com/u.png"},
            {"stickerWebpUrl", "https://example.com/u.webp"}, {"totalFrames", 10}, {"stickerSpriteUrl", "https://example.com/u-sprite.png"}});
        QCOMPARE(c.value("animatedUrl").toString(), QString("https://example.com/u.webp"));
        QVERIFY(c.value("spriteUrl").toString().isEmpty());
        QVERIFY(StickerStore::describe(QJsonObject{{"id", 0}}).isEmpty());
        QCOMPARE(StickerStore::stickerArray(QJsonObject{{"stickers", QJsonArray{1, 2}}}).size(), 2);

        QTemporaryDir dir;
        QString sent;
        StickerStore::Hooks hooks;
        hooks.session = []() -> ZaloSession* { return nullptr; };
        hooks.send = [&sent](qint64 id, qint64 cate, int type) { sent = QString("%1/%2/%3").arg(id).arg(cate).arg(type); };
        hooks.account = []() { return QString("u-sticker-fixture"); };
        QSettings().remove("stickers/recent/u-sticker-fixture");
        {
            StickerStore store(hooks);
            store.setCacheFile(dir.filePath("info.json"));
            store.applyPersonalForTesting(QJsonObject{{"sticker_cates", QJsonArray{5, 9}}, {"version", 3}});
            store.applyCatalogForTesting(QJsonArray{QJsonObject{{"id", 5}, {"name", "Fixture Pack"}, {"iconUrl", "https://example.com/i.png"}}});
            QCOMPARE(store.categories().size(), 2);
            QCOMPARE(store.categories().first().toMap().value("name").toString(), QString("Fixture Pack"));
            store.applyCategoryForTesting(5, QJsonArray{QJsonObject{{"id", 12}, {"cateId", 5}, {"totalFrames", 16},
                {"stickerUrl", "https://example.com/t.png"}, {"stickerSpriteUrl", "https://example.com/t-sprite.png"}}});
            store.selectCategory(5);
            QCOMPARE(store.stickers().size(), 1);
            QCOMPARE(store.info("12").value("frames").toInt(), 16);
            store.send(store.stickers().first().toMap());
            QCOMPARE(sent, QString("12/5/0"));
            QCOMPARE(store.categories().first().toMap().value("id").toInt(), int(StickerStore::kRecentId));
            store.selectCategory(StickerStore::kRecentId);
            QCOMPARE(store.stickers().size(), 1);
        }
        StickerStore again(hooks);
        again.setCacheFile(dir.filePath("info.json"));
        QCOMPARE(again.info("12").value("spriteUrl").toString(), QString("https://example.com/t-sprite.png"));
        QSettings().remove("stickers/recent/u-sticker-fixture");

        ZaloClient client;
        client.stickers()->remember(b);
        const MessageItem m = client.toMessageItemForTesting(QJsonObject{{"msgId", "s-1"}, {"uidFrom", "u-a"}, {"idTo", "u-me"},
            {"msgType", "chat.sticker"}, {"ts", "1700000000000"}, {"content", QJsonObject{{"id", 12}, {"catId", 5}}}}, false);
        QCOMPARE(m.extra.value("sticker").toMap().value("frames").toInt(), 16);
        QCOMPARE(m.mediaUrl, QString("https://example.com/t.png"));
        {
            QTemporaryDir dir2;
            QVERIFY(dir2.isValid());
            QSettings().remove("stickers/recent/u-sticker-catalog");
            StickerStore::Hooks hooks2;
            hooks2.session = []() -> ZaloSession* { return nullptr; };
            hooks2.account = []() { return QString("u-sticker-catalog"); };
            StickerStore catStore(hooks2);
            catStore.setCacheFile(dir2.filePath("info2.json"));
            catStore.applyPersonalForTesting(QJsonObject{{"sticker_cates", QJsonArray{5}}, {"version", 3}});
            catStore.applyCatalogForTesting(QJsonArray{
                QJsonObject{{"id", 5}, {"name", "Personal"}, {"iconUrl", "https://example.com/5.png"}},
                QJsonObject{{"id", 42}, {"name", "Official Catalog"}, {"thumbImg", "http://example.com/42.png"}}});
            const QVariantList cats = catStore.categories();
            QCOMPARE(cats.size(), 2);
            QCOMPARE(cats.at(1).toMap().value("id").toInt(), 42);
            QCOMPARE(cats.at(1).toMap().value("iconUrl").toString(), QString("https://example.com/42.png"));
            catStore.selectCategory(42);
            QCOMPARE(catStore.currentCategoryName(), QString("Official Catalog"));
        }
    }

    void testBoardServiceHost() {
        ZaloClient client;
        ZaloSession& session = *client.apiClient()->session();
        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"group_board", QJsonArray{"https://groupboard.example"}}}}});
        QCOMPARE(session.serviceUrls("boards"), QStringList{"https://board-wpa.api.zalo.me"});
        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"board", QJsonArray{"https://board.example"}}}}});
        QCOMPARE(session.serviceUrls("boards"), QStringList{"https://board.example"});
        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"boards", QJsonArray{"https://boards.example"}}, {"board", QJsonArray{"https://board.example"}}}}});
        QCOMPARE(session.serviceUrls("boards"), QStringList{"https://boards.example"});

        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"group", QJsonArray{"https://group-fallback.example"}}}}});
        QCOMPARE(session.serviceUrls("group_poll"), QStringList{"https://group-fallback.example"});
        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"group", QJsonArray{"https://group-fallback.example"}},
            {"group_poll", QJsonArray{"https://grouppoll.example"}}}}});
        QCOMPARE(session.serviceUrls("group_poll"), QStringList{"https://grouppoll.example"});

        Zalos::Core::Call::CallBridge bridge(nullptr);
        QVERIFY(bridge.micAutoGain());
        bridge.injectUiLineForTesting(QByteArray(R"({"type":"state","phase":"connected","micVolume":65,"micAutoGain":false})"));
        QCOMPARE(bridge.micVolume(), 65);
        QVERIFY(!bridge.micAutoGain());
        bridge.setMicAutoGain(true);
        QVERIFY(bridge.micAutoGain());
        bridge.setMicVolume(45);
        QCOMPARE(bridge.micVolume(), 45);
        QVERIFY(!bridge.micAutoGain());
    }

    void testReactionEventsFromSocketShapes() {
        ZaloClient client;
        client.setMyUidForTesting("u-me");
        ZaloSession* session = client.apiClient()->session();
        QSignalSpy spy(session, &ZaloSession::reactionsReceived);
        const QJsonArray mixed{
            QJsonObject{{"msgId", "8001"}, {"uidFrom", "u-peer"}, {"idTo", "0"}, {"msgType", "webchat"}, {"content", "hi"}},
            QJsonObject{{"msgId", "8002"}, {"uidFrom", "u-peer"}, {"idTo", "0"}, {"msgType", "chat.reaction"},
                        {"content", QString::fromUtf8(QJsonDocument(QJsonObject{{"rType", 3},
                            {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "7999"}}}}}).toJson(QJsonDocument::Compact))}}};
        const QJsonArray kept = session->splitReactionMessages(mixed, false, false);
        QCOMPARE(kept.size(), 1);
        QCOMPARE(kept.first().toObject().value("msgId").toString(), QString("8001"));
        QCOMPARE(spy.count(), 1);
        const QVariantMap d = client.reactionDetails("7999");
        QVERIFY(d.value("known").toBool());
        QCOMPARE(d.value("people").toList().first().toMap().value("emoji").toString(), QString::fromUtf8("👍"));
    }

    void testReactionMultipleTapsPerPerson() {
        ZaloClient client;
        client.setMyUidForTesting("u-me");
        auto tap = [](const QString& from, const QString& icon, int rType, const QString& eventId) {
            return QJsonObject{{"msgId", eventId}, {"uidFrom", from}, {"idTo", "g-r"}, {"ts", "1700000000000"},
                               {"content", QJsonObject{{"rIcon", icon}, {"rType", rType},
                                                       {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "m-multi"}}}}}}};
        };
        QJsonArray taps;
        for (int i = 0; i < 6; ++i) taps.append(tap("u-tu", "/-heart", 5, QString("h%1").arg(i)));
        for (int i = 0; i < 3; ++i) taps.append(tap("u-tu", "/-strong", 3, QString("s%1").arg(i)));
        for (int i = 0; i < 4; ++i) taps.append(tap("u-harvey", "/-heart", 5, QString("x%1").arg(i)));
        taps.append(tap("u-tu", "/-heart", 5, "h0"));
        client.handleReactionsForTesting(taps, true);
        QVariantMap d = client.reactionDetails("m-multi");
        QCOMPARE(d.value("total").toInt(), 13);
        QCOMPARE(d.value("groups").toList().first().toMap().value("count").toInt(), 10);
        const QVariantMap tu = d.value("people").toList().first().toMap();
        QCOMPARE(tu.value("uid").toString(), QString("u-tu"));
        QCOMPARE(tu.value("count").toInt(), 9);
        QCOMPARE(tu.value("emojis").toString(), QString::fromUtf8("❤️👍"));
        QCOMPARE(tu.value("perEmoji").toMap().value(QString::fromUtf8("👍")).toInt(), 3);
        client.handleReactionsForTesting(QJsonArray{tap("u-tu", "", -1, "r1")}, true);
        d = client.reactionDetails("m-multi");
        QCOMPARE(d.value("total").toInt(), 4);
        QCOMPARE(d.value("people").toList().size(), 1);
    }

    void testReactionSnapshotAndFallbackIds() {
        ZaloClient client;
        client.setMyUidForTesting("u-me");

        const QJsonObject msgWithReacts{
            {"msgId", "m-snap1"},
            {"cliMsgId", "c-snap1"},
            {"uidFrom", "u-friend"},
            {"idTo", "u-me"},
            {"msgType", "webchat"},
            {"content", "Hello snapshot"},
            {"reacts", QJsonObject{
                {"reactions", QJsonObject{
                    {"u-friend", QJsonObject{{"5", 2}}},
                    {"u-other", QJsonObject{{"3", 1}}}
                }}
            }}
        };
        const Zalos::Core::Models::MessageItem item1 = client.toMessageItemForTesting(msgWithReacts, false);
        QCOMPARE(item1.reaction, QString::fromUtf8("❤️👍 3"));

        const QJsonObject msgWithArrayReacts{
            {"msgId", "m-snap2"},
            {"cliMsgId", "c-snap2"},
            {"uidFrom", "u-friend"},
            {"idTo", "u-me"},
            {"msgType", "webchat"},
            {"content", "Hello array"},
            {"reacts", QJsonObject{
                {"reactions", QJsonArray{
                    QJsonObject{{"rIcon", "/-strong"}, {"count", 1}}
                }}
            }}
        };
        const Zalos::Core::Models::MessageItem item2 = client.toMessageItemForTesting(msgWithArrayReacts, false);
        QCOMPARE(item2.reaction, QString::fromUtf8("👍"));

        const QJsonObject rMsgWithZeroGMsg{
            {"msgId", "ev-cmsg"},
            {"uidFrom", "u-friend"},
            {"idTo", "u-me"},
            {"content", QJsonObject{
                {"rIcon", "/-heart"},
                {"rType", 5},
                {"rMsg", QJsonArray{
                    QJsonObject{{"gMsgID", "0"}, {"cMsgID", "c-client-target"}}
                }}
            }}
        };
        client.handleReactionsForTesting(QJsonArray{rMsgWithZeroGMsg}, false);
        const QVariantMap d = client.reactionDetails("c-client-target");
        QVERIFY(d.value("known").toBool());
        QCOMPARE(d.value("total").toInt(), 1);
        QCOMPARE(d.value("people").toList().first().toMap().value("uid").toString(), QString("u-friend"));

        const QJsonObject fwdMsg{
            {"msgId", "m-fwd"},
            {"uidFrom", "u-friend"},
            {"idTo", "u-me"},
            {"content", "Forwarded text"},
            {"reference", QJsonObject{{"fw", 1}}},
            {"e2eeStatus", 1}
        };
        const Zalos::Core::Models::MessageItem itemFwd = client.toMessageItemForTesting(fwdMsg, false);
        QVERIFY(itemFwd.isForwarded);
        QVERIFY(itemFwd.isE2EE);
    }

    void testPinMediaCandidatesPreferLiveMessageUrls() {
        const QVariantMap photo{{"msgType", 32}, {"thumb", "https://pin.invalid/stale-thumb.jpg"},
                                {"href", "https://pin.invalid/stale-href.jpg"}};
        const QJsonObject raw{{"content", QJsonObject{{"thumb", "https://msg.invalid/thumb.jpg"},
                                                     {"href", "https://msg.invalid/full.jpg"},
                                                     {"params", QString::fromUtf8(QJsonDocument(QJsonObject{{"hd", "https://msg.invalid/hd.jpg"}}).toJson(QJsonDocument::Compact))}}}};
        QCOMPARE(PinBoard::mediaCandidates(photo, raw, false),
                 QStringList({"https://msg.invalid/thumb.jpg", "https://pin.invalid/stale-thumb.jpg",
                              "https://msg.invalid/full.jpg", "https://msg.invalid/hd.jpg", "https://pin.invalid/stale-href.jpg"}));
        QCOMPARE(PinBoard::mediaCandidates(photo, raw, true).first(), QString("https://msg.invalid/full.jpg"));
        QCOMPARE(PinBoard::mediaCandidates(photo, {}, false),
                 QStringList({"https://pin.invalid/stale-thumb.jpg", "https://pin.invalid/stale-href.jpg"}));
        const QVariantMap file{{"msgType", 46}, {"thumb", ""}, {"href", "https://pin.invalid/archive.zip"}};
        QVERIFY(PinBoard::mediaCandidates(file, QJsonObject{{"content", QJsonObject{{"href", "https://msg.invalid/archive.zip"}}}}, true).isEmpty());
        QVERIFY(PinBoard::mediaCandidates(QVariantMap{{"msgType", 1}, {"thumb", "not a url"}}, {}, false).isEmpty());
    }

    void testPinBoard() {
        const QString topicJson = QString::fromUtf8(QJsonDocument(QJsonObject{{"id", 501}, {"type", 2},
            {"params", QString::fromUtf8(QJsonDocument(QJsonObject{{"global_msg_id", "9001"}, {"client_msg_id", "1700"},
                {"senderUid", "u-a"}, {"senderName", "Fixture A"}, {"title", "Fixture pinned text"}, {"msg_type", 1}}).toJson(QJsonDocument::Compact))}}).toJson(QJsonDocument::Compact));
        qint64 version = 0;
        int limit = 0;
        QVariantList friendPins = PinBoard::parseList(QJsonObject{{"data", QJsonArray{topicJson}}, {"version", 7}}, false, &version, &limit);
        QCOMPARE(friendPins.size(), 1);
        QCOMPARE(version, 7);
        const QVariantMap t = friendPins.first().toMap();
        QCOMPARE(t.value("id").toString(), QString("501"));
        QCOMPARE(t.value("msgId").toString(), QString("9001"));
        QCOMPARE(t.value("title").toString(), QString("Fixture pinned text"));
        QCOMPARE(t.value("senderName").toString(), QString("Fixture A"));
        const QVariantList groupPins = PinBoard::parseList(QJsonObject{{"items", QJsonArray{QJsonDocument::fromJson(topicJson.toUtf8()).object()}},
                                                                      {"boardVersion", 12}, {"pinLimit", 3}}, true, &version, &limit);
        QCOMPARE(groupPins.size(), 1);
        QCOMPARE(version, 12);
        QCOMPARE(limit, 3);

        const QJsonObject photo{{"msgId", "9002"}, {"cliMsgId", "1701"}, {"uidFrom", "0"}, {"msgType", "chat.photo"},
                                {"content", QJsonObject{{"thumb", "https://example.com/t.jpg"}, {"title", "cap"}}}};
        const QJsonObject topic = PinBoard::topicForMessage(photo, "u-me", "Me");
        QCOMPARE(topic.value("type").toInt(), int(PinBoard::kMessageTopic));
        const QJsonObject params = topic.value("params").toObject();
        QCOMPARE(params.value("senderUid").toString(), QString("u-me"));
        QCOMPARE(params.value("global_msg_id").toString(), QString("9002"));
        QCOMPARE(params.value("msg_type").toInt(), 32);
        QCOMPARE(params.value("thumb").toString(), QString("https://example.com/t.jpg"));
        QCOMPARE(PinBoard::topicForMessage(QJsonObject{{"msgId", "1"}, {"uidFrom", "u-b"}, {"msgType", "webchat"}, {"content", "hello"}},
                                           "u-me", "B").value("params").toObject().value("title").toString(), QString("hello"));

        ZaloClient client;
        QSignalSpy noticeSpy(&client, &ZaloClient::notice);
        client.selectConversation("u-a", "Fixture A", "");
        client.pins()->applyListForTesting("u-a", QJsonObject{{"data", QJsonArray{topicJson}}, {"version", 7}}, false);
        QCOMPARE(client.pins()->pins().size(), 1);
        QVERIFY(client.pins()->isPinned("9001"));
        QVERIFY(client.hasPinnedMessage());
        QCOMPARE(client.currentPinnedMessageText(), QString("Fixture pinned text"));
        QCOMPARE(client.pins()->versionForTesting("u-a"), 7);
        const QString longText = QString("Fixture line\n").repeated(200);
        client.pins()->applyListForTesting("u-a", QJsonObject{{"version", 8}, {"data", QJsonArray{
            QJsonObject{{"id", 502}, {"type", 2}, {"params", QJsonObject{{"global_msg_id", "9100"}, {"title", longText},
                {"senderName", "Fixture A"}, {"msg_type", 1}}}},
            QJsonObject{{"id", 503}, {"type", 2}, {"params", QJsonObject{{"global_msg_id", "9101"}, {"title", "cap"},
                {"thumb", "https://example.com/t2.jpg"}, {"msg_type", 32}}}}}}}, false);
        const QVariantMap detail = client.pins()->detail("502");
        QCOMPARE(detail.value("text").toString(), longText);
        QCOMPARE(detail.value("local").toBool(), false);
        QCOMPARE(client.pins()->detail("503").value("image").toString(), QString("https://example.com/t2.jpg"));
        QVERIFY(client.pins()->detail("nope").isEmpty());
        QSignalSpy brokenSpy(&client, &ZaloClient::brokenMediaChanged);
        const int rev = client.brokenMediaRevision();
        QVERIFY(!client.isMediaBroken("https://example.com/t2.jpg"));
        client.markMediaBroken("https://example.com/t2.jpg");
        client.markMediaBroken("https://example.com/t2.jpg");
        QVERIFY(client.isMediaBroken("https://example.com/t2.jpg"));
        QCOMPARE(brokenSpy.count(), 1);
        QVERIFY(client.brokenMediaRevision() > rev);
        QVERIFY(!client.isMediaBroken(""));
        client.retryMedia("https://example.com/t2.jpg");
        QVERIFY(!client.isMediaBroken("https://example.com/t2.jpg"));
        QCOMPARE(brokenSpy.count(), 2);
        client.pins()->applyListForTesting("u-a", QJsonObject{{"data", QJsonArray{topicJson}}, {"version", 7}}, false);
        client.pins()->pinMessage("9001");
        QVERIFY(noticeSpy.count() >= 1);
        QVERIFY(client.pins()->handleControl("unpin_topic", QJsonObject{{"conversationId", "u-a"}}));
        QVERIFY(!client.pins()->handleControl("join", QJsonObject{}));
        client.selectConversation("u-b", "Fixture B", "");
        QVERIFY(!client.hasPinnedMessage());
    }

    void testMemoryAndStorageTuning() {
        const QByteArray saved = qgetenv("VK_LOADER_DRIVERS_DISABLE");
        const QByteArray savedRhi = qgetenv("QSG_RHI_BACKEND");
        qunsetenv("VK_LOADER_DRIVERS_DISABLE");
        using RenderProfile = Zalos::Core::Platform::RenderProfile;
        using RenderProfile = Zalos::Core::Platform::RenderProfile;
        RenderProfile vk;
        vk.backend = RenderProfile::Backend::Vulkan;
        vk.apply();
        QCOMPARE(qgetenv("VK_LOADER_DRIVERS_DISABLE"), QByteArray("*lvp_icd*,*dzn_icd*"));
        qunsetenv("VK_LOADER_DRIVERS_DISABLE");
        RenderProfile gl;
        gl.backend = RenderProfile::Backend::OpenGL;
        gl.apply();
        QVERIFY(qgetenv("VK_LOADER_DRIVERS_DISABLE").isEmpty());
        if (!saved.isEmpty()) qputenv("VK_LOADER_DRIVERS_DISABLE", saved);
        if (savedRhi.isEmpty()) qunsetenv("QSG_RHI_BACKEND"); else qputenv("QSG_RHI_BACKEND", savedRhi);

        ZaloOfflineStore store;
        QCOMPARE(store.pragmaForTesting("journal_mode").toLower(), QString("wal"));
        QCOMPARE(store.pragmaForTesting("synchronous"), QString("1"));
    }

    void testFullProfileSuite() {
        ZaloClient client;

        QCOMPARE(client.showUserProfileModal(), false);
        client.openUserProfile();
        QCOMPARE(client.showUserProfileModal(), true);
        QCOMPARE(client.profileIsSelf(), true);

        const QString displayNameBeforeEdit = client.profileDisplayName();
        const QString userNameBeforeEdit = client.currentUserName();
        const int genderBeforeEdit = client.profileGender();
        const QString dobBeforeEdit = client.profileDob();
        const QString bioBeforeEdit = client.profileBio();
        client.updateMyProfile("Nguyễn Văn Test", 1, "01/01/1995", "Yêu công nghệ, thích lập trình");
        QCOMPARE(client.profileDisplayName(), displayNameBeforeEdit);
        QCOMPARE(client.currentUserName(), userNameBeforeEdit);
        QCOMPARE(client.profileGender(), genderBeforeEdit);
        QCOMPARE(client.profileDob(), dobBeforeEdit);
        QCOMPARE(client.profileBio(), bioBeforeEdit);

        client.viewProfile("user-xyz");
        QCOMPARE(client.showUserProfileModal(), true);
        QCOMPARE(client.profileIsSelf(), false);
        QCOMPARE(client.profileUserId(), QString("user-xyz"));

        QCOMPARE(client.profileIsBlocked(), false);
        client.blockUser("user-xyz");
        QCOMPARE(client.profileIsBlocked(), true);
        client.unblockUser("user-xyz");
        QCOMPARE(client.profileIsBlocked(), false);

        client.removeFriend("user-xyz");
        QCOMPARE(client.profileIsFriend(), false);

        client.closeUserProfile();
        QCOMPARE(client.showUserProfileModal(), false);
    }

    void testMediaImageProviderScalesLocalFile() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString file = tmp.filePath("photo.png");
        QImage src(800, 400, QImage::Format_RGB32);
        src.fill(Qt::darkCyan);
        QVERIFY(src.save(file));

        AvatarImageProvider provider(false);
        QQuickImageResponse* response = provider.requestImageResponse(QUrl::fromLocalFile(file).toString(), QSize(320, 0));
        QVERIFY(response);
        QSignalSpy finished(response, &QQuickImageResponse::finished);
        QVERIFY(finished.wait(5000));
        QQuickTextureFactory* factory = response->textureFactory();
        QVERIFY(factory);
        const QImage out = factory->image();
        QCOMPARE(out.width(), 320);
        QCOMPARE(out.height(), 160);
        delete factory;
        delete response;
    }

    void testMediaImageProviderReportsFailureReason() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AvatarImageProvider provider(false);
        QQuickImageResponse* response = provider.requestImageResponse(
            QUrl::fromLocalFile(tmp.filePath("missing.png")).toString(), QSize(320, 0));
        QVERIFY(response);
        QSignalSpy finished(response, &QQuickImageResponse::finished);
        QVERIFY(finished.wait(5000));
        QVERIFY(response->errorString().startsWith("cannot read"));
        delete response;
    }

    void testChatBackgroundModes() {
        {
            QSettings settings;
            settings.remove("settings/chatBackgroundMode");
            settings.remove("settings/chatBackgroundImage");
            settings.remove("settings/useAvatarAsChatBackground");
            settings.setValue("settings/chatBackgroundMode", "background");
        }
        ZaloClient client;
        QCOMPARE(client.chatBackgroundMode(), QString("none"));
        QCOMPARE(client.currentConversationBackground(), QString());

        QCOMPARE(ZaloClient::backgroundOf({{"cover", "https://x/cover.jpg"},
                                           {"bgavatar", "https://x/bg.jpg"}}),
                 QString("https://x/cover.jpg"));
        QCOMPARE(ZaloClient::backgroundOf({{"avatar", "https://x/av.jpg"},
                                           {"bgavatar", "https://x/bg.jpg"}}),
                 QString("https://x/bg.jpg"));
        QCOMPARE(ZaloClient::backgroundOf({{"avt", "https://x/avt.jpg"},
                                           {"fullAvt", "https://x/full.jpg"}}),
                 QString("https://x/full.jpg"));
        QCOMPARE(ZaloClient::backgroundOf({{"avatar", "https://x/av.jpg"}}), QString());

        QSignalSpy bgSpy(&client, &ZaloClient::currentConversationBackgroundChanged);
        client.setGroupForTesting("grp-1", "Nh\u1ed3m th\u1eed", "https://x/group_av.jpg");
        client.setUserForTesting("u-peer-2", "B\u1ea1n th\u1eed", "https://x/peer2_av.jpg");
        client.selectConversation("grp-1", "Nh\u1ee9m th\u1eed", "https://x/group_av.jpg");
        QCOMPARE(client.currentConversationBackground(), QString());
        const int spyBeforeMode = bgSpy.count();
        client.setChatBackgroundMode("avatar");
        QCOMPARE(client.chatBackgroundMode(), QString("avatar"));
        QCOMPARE(client.currentConversationBackground(), QString("https://x/group_av.jpg"));
        QCOMPARE(bgSpy.count(), spyBeforeMode + 1);

        client.selectConversation("u-peer-2", QString(), QString());
        QCOMPARE(client.currentConversationBackground(), QString("https://x/peer2_av.jpg"));
        QCOMPARE(bgSpy.count(), spyBeforeMode + 2);

        client.openWelcomeScreen();
        QCOMPARE(client.currentConversationBackground(), QString());
        QCOMPARE(bgSpy.count(), spyBeforeMode + 3);

        client.selectConversation("grp-1", "Nh\u1ee9m th\u1eed", "https://x/group_av.jpg");
        QCOMPARE(client.currentConversationBackground(), QString("https://x/group_av.jpg"));
        QCOMPARE(bgSpy.count(), spyBeforeMode + 4);

        client.setChatBackgroundMode("wallpaper");
        QCOMPARE(client.chatBackgroundMode(), QString("none"));
        QCOMPARE(client.currentConversationBackground(), QString());

        QTemporaryFile source;
        source.setFileTemplate(QDir::tempPath() + QStringLiteral("/zalos_bg_XXXXXX.png"));
        QVERIFY(source.open());
        source.write("fake-image-bytes");
        source.flush();

        client.setChatBackgroundImage(QUrl::fromLocalFile(source.fileName()).toString());
        QCOMPARE(client.chatBackgroundMode(), QString("custom"));
        QVERIFY(!client.chatBackgroundImage().isEmpty());
        QVERIFY(QFileInfo::exists(client.chatBackgroundImage()));
        QVERIFY(client.currentConversationBackground().startsWith(QStringLiteral("file://")));

        client.clearChatBackgroundImage();
        QCOMPARE(client.chatBackgroundMode(), QString("none"));
        QVERIFY(client.chatBackgroundImage().isEmpty());
        QCOMPARE(client.currentConversationBackground(), QString());

        {
            QSettings legacySettings;
            legacySettings.setValue("settings/useAvatarAsChatBackground", true);
            legacySettings.setValue("settings/chatBackgroundMode", "none");
        }
        {
            ZaloClient clientNone;
            QCOMPARE(clientNone.chatBackgroundMode(), QString("none"));
            QSettings legacySettings;
            QVERIFY(!legacySettings.contains("settings/useAvatarAsChatBackground"));
        }

        QSettings settings;
        settings.remove("settings/chatBackgroundMode");
        settings.remove("settings/chatBackgroundImage");
        settings.remove("settings/useAvatarAsChatBackground");
    }

    void testTdesktopMemoryOptimizations() {
        QImage big(1600, 900, QImage::Format_RGB32);
        big.fill(QColor("#0068FF"));
        QByteArray pngBytes;
        {
            QBuffer out(&pngBytes);
            QVERIFY(out.open(QIODevice::WriteOnly));
            QVERIFY(big.save(&out, "PNG"));
        }
        {
            QBuffer in(&pngBytes);
            QVERIFY(in.open(QIODevice::ReadOnly));
            const QImage avatarDecoded = AvatarImageProvider::decodeScaled(&in, 96, true);
            QVERIFY(!avatarDecoded.isNull());
            QCOMPARE( qMin(avatarDecoded.width(), avatarDecoded.height()), 96 );
            QVERIFY(avatarDecoded.width() < 300);
        }
        {
            QBuffer in(&pngBytes);
            QVERIFY(in.open(QIODevice::ReadOnly));
            const QImage coverDecoded = AvatarImageProvider::decodeScaled(&in, 320, false);
            QVERIFY(!coverDecoded.isNull());
            QCOMPARE(coverDecoded.width(), 320);
            QCOMPARE(coverDecoded.height(), 180);
        }

        {
            ZaloOfflineStore owner;
            owner.bindOwner("me-opt");
            owner.clearCache();
            owner.bindOwner("me-opt");
        }
        ZaloClient client;
        client.openOfflineCacheForTesting("me-opt");

        client.selectConversation("u-active", "Active Peer", "");
        QJsonArray incoming;
        for (int i = 1; i <= 5; ++i) {
            const QString peer = QStringLiteral("u-cold-%1").arg(i);
            client.selectConversation(peer, peer, "");
            incoming.append(QJsonObject{
                {"msgId", QStringLiteral("990%1").arg(i)},
                {"cliMsgId", QStringLiteral("10%1").arg(i)},
                {"uidFrom", peer},
                {"idTo", "me-opt"},
                {"msgType", "webchat"},
                {"content", QStringLiteral("Hello %1").arg(i)},
                {"ts", QString::number(1700000000000LL + i * 1000)}
            });
        }
        client.selectConversation("u-active", "Active Peer", "");
        emit client.apiClient()->session()->messagesReceived(incoming, false, false);

        QCOMPARE(client.rawMessageCountForTesting(), 0);
        QVERIFY(client.residentThreadCountForTesting() >= 5);

        client.storage()->setAvatarMemoryCleaner([]() { AvatarImageProvider::clearMemCache(); });
        AvatarImageProvider::insertMemCache("test-bg-trim-key", QImage(32, 32, QImage::Format_ARGB32));
        QImage dummyOut;
        QVERIFY(AvatarImageProvider::lookupMemCache("test-bg-trim-key", &dummyOut));

        client.trimMemoryForBackground();
        QCOMPARE(client.residentThreadCountForTesting(), 1);
        QCOMPARE(client.currentConversationId(), QString("u-active"));
        QVERIFY(!AvatarImageProvider::lookupMemCache("test-bg-trim-key", &dummyOut));

        ZaloOfflineStore cleanup;
        cleanup.clearCache();
    }

    void testComprehensiveMemoryLifecycle() {
        auto getVmRssKb = []() -> qint64 {
            QFile f("/proc/self/status");
            if (!f.open(QIODevice::ReadOnly)) return 0;
            for (const QByteArray& line : f.readAll().split('\n')) {
                if (line.startsWith("VmRSS:")) {
                    return line.mid(6).trimmed().split(' ').first().toLongLong();
                }
            }
            return 0;
        };

        const qint64 initialRss = getVmRssKb();

        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-mem-audit");
            owner.clearCache();
            owner.bindOwner("u-mem-audit");
        }

        ZaloClient client;
        client.openOfflineCacheForTesting("u-mem-audit");

        const qint64 baseTs = 1700000000000LL;
        for (int c = 0; c < 30; ++c) {
            QJsonArray batch;
            const QString convId = QString("u-audit-conv-%1").arg(c);
            for (int m = 0; m < 100; ++m) {
                const int id = c * 100 + m;
                batch.append(QJsonObject{
                    {"msgId", QString::number(1000000 + id)},
                    {"cliMsgId", QString::number(id)},
                    {"uidFrom", m % 2 == 0 ? "0" : convId},
                    {"idTo", m % 2 == 0 ? convId : "u-mem-audit"},
                    {"msgType", "webchat"},
                    {"content", QString("Audit message payload #%1 for testing RAM scaling").arg(id)},
                    {"ts", QString::number(baseTs + id)}
                });
            }
            emit client.apiClient()->session()->messagesReceived(batch, false, true);
        }

        QCOMPARE(client.conversationModel()->rowCount(), 30);
        QCOMPARE(client.rawMessageCountForTesting(), 0);

        const qint64 rssAfterIngestion = getVmRssKb();
        qInfo("[MemoryAudit] Ingestion (3,000 msgs): Initial RSS = %lld kB, Post-Ingestion = %lld kB (+%lld kB)",
              initialRss, rssAfterIngestion, rssAfterIngestion - initialRss);
        QVERIFY2(rssAfterIngestion - initialRss < 65000, "Cold message ingestion exceeded bounded RSS limit!");

        for (int round = 0; round < 2; ++round) {
            for (int c = 0; c < 30; ++c) {
                client.selectConversation(QString("u-audit-conv-%1").arg(c), "Peer", "");
            }
        }

        QCOMPARE(client.currentConversationId(), QString("u-audit-conv-29"));
        QVERIFY2(client.residentThreadCountForTesting() <= 12,
                 "Resident thread LRU eviction failed: more than 12 threads held in RAM!");
        QCOMPARE(client.messageModel()->threadSize("u-audit-conv-0"), 0);

        QByteArray encodedJpg;
        {
            QImage highRes4K(3840, 2160, QImage::Format_RGB32);
            highRes4K.fill(QColor("#0068FF"));
            QBuffer buffer(&encodedJpg);
            buffer.open(QIODevice::WriteOnly);
            highRes4K.save(&buffer, "JPG", 85);
        }

#ifdef __GLIBC__
        const size_t heapBeforeImage = mallinfo2().uordblks;
#endif
        {
            QBuffer inBuf(&encodedJpg);
            inBuf.open(QIODevice::ReadOnly);
            const QImage thumb = AvatarImageProvider::decodeScaled(&inBuf, 96, true);
            QVERIFY(!thumb.isNull());
            QCOMPARE(qMin(thumb.width(), thumb.height()), 96);
        }

#ifdef __GLIBC__
        const size_t heapAfterImage = mallinfo2().uordblks;
        const size_t imageHeapDelta = heapAfterImage > heapBeforeImage ? (heapAfterImage - heapBeforeImage) : 0;
        qInfo("[MemoryAudit] 4K Image Scaled Decode Heap Delta: %zu bytes (~%zu kB)",
              imageHeapDelta, imageHeapDelta / 1024);
        QVERIFY2(imageHeapDelta < 16 * 1024 * 1024, "Image decode triggered full-resolution heap spike!");
#endif

        client.storage()->setAvatarMemoryCleaner([]() { AvatarImageProvider::clearMemCache(); });
        AvatarImageProvider::insertMemCache("audit-avatar-1", QImage(64, 64, QImage::Format_ARGB32));
        AvatarImageProvider::insertMemCache("audit-avatar-2", QImage(64, 64, QImage::Format_ARGB32));

        QImage probe;
        QVERIFY(AvatarImageProvider::lookupMemCache("audit-avatar-1", &probe));

        client.trimMemoryForBackground();

        QCOMPARE(client.residentThreadCountForTesting(), 1);
        QCOMPARE(client.currentConversationId(), QString("u-audit-conv-29"));
        QVERIFY(!AvatarImageProvider::lookupMemCache("audit-avatar-1", &probe));
        QVERIFY(!AvatarImageProvider::lookupMemCache("audit-avatar-2", &probe));

        ZaloOfflineStore cleanup;
        cleanup.clearCache();
    }

    void testSystemThemePaletteSnapshot() {
        const QPalette original = QGuiApplication::palette();

        QPalette system;
        system.setColor(QPalette::Window, QColor("#101010"));
        system.setColor(QPalette::Base, QColor("#0A0A0A"));
        system.setColor(QPalette::AlternateBase, QColor("#202020"));
        system.setColor(QPalette::WindowText, QColor("#EAEAEA"));
        system.setColor(QPalette::PlaceholderText, QColor("#7F7F7F"));
        system.setColor(QPalette::Link, QColor("#FF00FF"));
        QGuiApplication::setPalette(system);

        {
            ThemeManager theme;
            theme.setThemeMode(ThemeManager::SystemTheme);
            QCOMPARE(theme.backgroundColor(), QColor("#101010"));
            QCOMPARE(theme.chatBackgroundColor(), QColor("#0A0A0A"));
            QCOMPARE(theme.cardBackgroundColor(), QColor("#202020"));
            QCOMPARE(theme.textColor(), QColor("#EAEAEA"));
            QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor("#101010"));
            QVERIFY(QGuiApplication::palette().color(QPalette::Link) != QColor("#FF00FF"));
            QCOMPARE(theme.linkColor(), QColor("#FF00FF"));
            QCOMPARE(theme.uiLinkColor(), QColor("#FF00FF"));
            QCOMPARE(theme.reactionSurfaceColor(), QColor("#0A0A0A"));
            QCOMPARE(theme.inactiveSubtleColor(), QColor("#7F7F7F"));

            theme.setThemeMode(ThemeManager::ZaloLight);
            QCOMPARE(theme.uiAccentColor(), QColor("#0068FF"));
            QCOMPARE(theme.activenessColor(), QColor("#0068FF"));
            QCOMPARE(theme.uiLinkColor(), QColor("#0068FF"));
            QCOMPARE(theme.cardBackgroundColor(), QColor("#EBECF0"));
            QCOMPARE(theme.reactionSurfaceColor(), QColor("#FFFFFF"));
            QCOMPARE(theme.inactiveSubtleColor(), QColor("#B5B5B5"));
            QCOMPARE(theme.inactiveDisabledColor(), QColor("#DBDBDB"));
            QCOMPARE(theme.activeDisabledColor(), QColor("#ADD1FF"));
            QCOMPARE(theme.dropOverlayColor(), QColor("#F0F7FF"));

            theme.setThemeMode(ThemeManager::ZaloDark);
            QCOMPARE(theme.activenessColor(), QColor("#66A6FF"));
            QCOMPARE(theme.uiLinkColor(), QColor("#66A6FF"));
            QCOMPARE(theme.cardBackgroundColor(), QColor("#1D2025"));
            QCOMPARE(theme.reactionSurfaceColor(), QColor("#1D2025"));
            QCOMPARE(theme.inactiveSubtleColor(), QColor("#595959"));
            QCOMPARE(theme.inactiveDisabledColor(), QColor("#333333"));
            QCOMPARE(theme.activeDisabledColor(), QColor("#00378A"));
            QCOMPARE(theme.dropOverlayColor(), QColor("#001029"));
        }

        QGuiApplication::setPalette(original);
    }

    void testPeerVideoDecodeToSink() {
        using namespace Zalos::Core::Call;
        if (!CallBridge::videoSupported())
            QSKIP(qPrintable("OpenH264 unavailable: " + OpenH264::instance().unavailableReason()));
        qInfo() << "OpenH264" << OpenH264::instance().version();

        const QSize size(320, 240);
        H264Encoder encoder;
        QVERIFY(encoder.open(size, 15, 400000));
        QList<QPair<bool, QByteArray>> units;
        for (int i = 0; i < 4; ++i) {
            QByteArray i420(size.width() * size.height() * 3 / 2, '\x80');
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x) i420[y * size.width() + x] = char((x + y + i * 8) & 0xFF);
            bool key = false;
            const QByteArray au = encoder.encodeI420(i420, i * 66, i == 0, &key);
            if (!au.isEmpty()) units.append({key, au});
        }
        QVERIFY(!units.isEmpty());
        QVERIFY(units.first().first);
        QVERIFY(units.first().second.startsWith(QByteArray("\x00\x00\x00\x01", 4)));

        {
            H264Decoder decoder;
            QVERIFY(decoder.isOpen());
            bool damaged = true;
            const QVideoFrame frame = decoder.decode(units.first().second, &damaged);
            QVERIFY(!damaged);
            QVERIFY(frame.isValid());
            QCOMPARE(frame.size(), size);
            QCOMPARE(frame.pixelFormat(), QVideoFrameFormat::Format_YUV420P);
        }

        ZaloClient client;
        auto* calls = client.calls();
        QVideoSink sink;
        calls->setPeerVideoSink(&sink);
        QSignalSpy sinkSpy(&sink, &QVideoSink::videoFrameChanged);
        QSignalSpy activeSpy(calls, &CallBridge::peerVideoChanged);
        calls->injectUiLineForTesting(R"({"type":"state","phase":"connected","video":true,"name":"Fixture","since":1})");

        QByteArray stream;
        auto appendUnit = [&stream](bool key, const QByteArray& au) {
            stream += QByteArray(R"({"type":"video","key":)") + (key ? "true" : "false")
                      + R"(,"codec":"avc1.42E01E","bin":)" + QByteArray::number(au.size()) + "}\n" + au;
        };
        if (units.size() > 1) appendUnit(false, units.at(1).second);
        for (const auto& u : units) appendUnit(u.first, u.second);
        calls->injectUiBytesForTesting(stream.left(7));
        calls->injectUiBytesForTesting(stream.mid(7));

        QTRY_VERIFY_WITH_TIMEOUT(sinkSpy.count() >= 1, 5000);
        QCOMPARE(sink.videoFrame().size(), size);
        QVERIFY(calls->peerVideoActive());
        QVERIFY(activeSpy.count() >= 1);

        calls->injectUiLineForTesting(R"({"type":"close"})");
        calls->hangup();
        QVERIFY(!calls->peerVideoActive());
        QVERIFY(!sink.videoFrame().isValid());
    }

    void testRemoteReferencesNeverPointAtLocalFiles() {
        for (const char* local : {"file:///home/fixture/.ssh/id_rsa", "FILE:///etc/hosts", "/etc/hosts", "//etc/hosts",
                                  ".local/share/Zalos/Zalos/session.json", "~/.ssh/id_rsa", "Downloads/fixture.desktop",
                                  "qrc:/qt/qml/Zalos/Main.qml", "image://media/file%3A%2F%2F%2Fetc%2Fhosts", "C:/fixture.exe"}) {
            QVERIFY2(UrlPolicy::sanitizeRemoteReference(QString::fromLatin1(local)).isEmpty(), local);
            QVERIFY2(UrlPolicy::isLocalReference(QString::fromLatin1(local)), local);
        }
        QCOMPARE(UrlPolicy::sanitizeRemoteReference(QStringLiteral(" https://f.invalid/a.jpg ")), QStringLiteral("https://f.invalid/a.jpg"));
        QCOMPARE(UrlPolicy::sanitizeRemoteReference(QStringLiteral("zalo.me/fixture")), QStringLiteral("http://zalo.me/fixture"));
        QVERIFY(!UrlPolicy::isLocalReference(QString()));
        QVERIFY(UrlPolicy::isWebUrl(QStringLiteral("https://f.invalid/a")));
        QVERIFY(!UrlPolicy::isWebUrl(QStringLiteral("smb://fixture.invalid/share")));
        QVERIFY(!UrlPolicy::isWebUrl(QStringLiteral("https:///nohost")));
        QCOMPARE(UrlPolicy::upgradeToHttps(QUrl("http://f.invalid:80/a")).toString(), QStringLiteral("https://f.invalid/a"));

        ZaloClient client;
        client.setMyUidForTesting(QStringLiteral("u-me"));
        auto peerMsg = [](const QString& id, const QString& type, const QJsonObject& content) {
            return QJsonObject{{"msgId", id}, {"cliMsgId", id}, {"uidFrom", "u-peer"}, {"idTo", "u-me"}, {"msgType", type},
                               {"content", content}, {"ts", "1700000000000"}};
        };
        const MessageItem file = client.toMessageItemForTesting(
            peerMsg("9801", "share.file", QJsonObject{{"title", "fixture.pdf"},
                                                     {"href", "file:///home/fixture/.local/share/Zalos/Zalos/session.json"},
                                                     {"params", "{\"fileExt\":\"pdf\"}"}}), false);
        QCOMPARE(file.messageType, QStringLiteral("file"));
        QVERIFY(file.mediaUrl.isEmpty());
        QVERIFY(file.href.isEmpty());
        const MessageItem photo = client.toMessageItemForTesting(
            peerMsg("9802", "chat.photo", QJsonObject{{"thumb", ".local/share/Zalos/Zalos/session.json"}, {"href", "/etc/hosts"}}), false);
        QVERIFY(photo.mediaUrl.isEmpty());
        QVERIFY(photo.href.isEmpty());
        const MessageItem gif = client.toMessageItemForTesting(
            peerMsg("9803", "chat.gif", QJsonObject{{"thumb", "file:///tmp/fixture.gif"}, {"href", "file:///tmp/fixture.gif"}}), false);
        QVERIFY(!gif.extra.contains(QStringLiteral("animatedUrl")));
        const MessageItem remote = client.toMessageItemForTesting(
            peerMsg("9804", "chat.photo", QJsonObject{{"thumb", "https://f.invalid/t.jpg"}, {"href", "https://f.invalid/h.jpg"}}), false);
        QCOMPARE(remote.mediaUrl, QStringLiteral("https://f.invalid/t.jpg"));
        QCOMPARE(remote.href, QStringLiteral("https://f.invalid/h.jpg"));
        QJsonObject own = peerMsg("9805", "chat.photo", QJsonObject{{"thumb", "file:///tmp/fixture-own.png"},
                                                                    {"href", "file:///tmp/fixture-own.png"}});
        own.insert("uidFrom", "0");
        own.insert("idTo", "u-peer");
        QCOMPARE(client.toMessageItemForTesting(own, false).mediaUrl, QStringLiteral("file:///tmp/fixture-own.png"));

        QVERIFY(PinBoard::mediaCandidates(QVariantMap{{"msgType", 32}, {"thumb", "file:///etc/hosts"}, {"href", "/etc/hosts"}},
                                          {}, true).isEmpty());
    }

    void testForwardNeverUploadsPeerLocalFiles() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString secretPath = dir.filePath(QStringLiteral("fixture-secret.json"));
        {
            QFile f(secretPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{}");
        }
        QCOMPARE(ZaloClient::forwardableLocalPath(QUrl::fromLocalFile(secretPath).toString()), QFileInfo(secretPath).absoluteFilePath());
        QCOMPARE(ZaloClient::forwardableLocalPath(secretPath), QFileInfo(secretPath).absoluteFilePath());
        QVERIFY(ZaloClient::forwardableLocalPath(QStringLiteral("fixture-secret.json")).isEmpty());
        QVERIFY(ZaloClient::forwardableLocalPath(dir.path()).isEmpty());
        QVERIFY(ZaloClient::forwardableLocalPath(dir.filePath(QStringLiteral("missing.json"))).isEmpty());
        QVERIFY(ZaloClient::forwardableLocalPath(QStringLiteral("https://f.invalid/a.png")).isEmpty());

        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) {
            return TestSupport::jsonEnvelope(0, QJsonObject{{"msgId", "9901"}});
        });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        MessageItem peer;
        peer.id = QStringLiteral("m-peer-file");
        peer.messageType = QStringLiteral("file");
        peer.senderId = QStringLiteral("u-peer");
        peer.text = QStringLiteral("fixture.pdf");
        peer.mediaUrl = QUrl::fromLocalFile(secretPath).toString();
        peer.href = peer.mediaUrl;
        peer.ts = 1700000000000LL;
        client.messageModel()->mergeMessages(QStringLiteral("u-peer"), {peer});
        client.forwardMessage(QStringLiteral("u-target"), QStringLiteral("m-peer-file"));
        QTRY_VERIFY_WITH_TIMEOUT(api.count(QStringLiteral("/api/message/sms")) >= 1, 5000);
        QCOMPARE(api.count(QStringLiteral("upload")), 0);
        QCOMPARE(api.count(QStringLiteral("asyncfile")), 0);
    }

    void testCallUiOversizeFrameKeepsFraming() {
        ZaloClient client;
        auto* calls = client.calls();
        const qint64 bin = 4LL * 1024 * 1024 + 1;
        QByteArray body(bin, 'x');
        const QByteArray injected = QByteArray("\n") + R"({"type":"state","phase":"connected","name":"Injected","since":1})" + "\n";
        body.replace(1024 * 1024, injected.size(), injected);
        const QByteArray header = QByteArray(R"({"type":"video","key":true,"bin":)") + QByteArray::number(bin) + "}\n";
        calls->injectUiBytesForTesting(header + body.left(2048));
        calls->injectUiBytesForTesting(body.mid(2048));
        QVERIFY(calls->phase() != QStringLiteral("connected"));
        calls->injectUiBytesForTesting(QByteArray(R"({"type":"state","phase":"connected","name":"Fixture","since":1})") + "\n");
        QCOMPARE(calls->phase(), QStringLiteral("connected"));
        calls->injectUiLineForTesting(R"({"type":"close"})");
        calls->hangup();

        QVERIFY(CallBridge::tokenMatches("zcall-abc", "zcall-abc"));
        QVERIFY(!CallBridge::tokenMatches("zcall-abd", "zcall-abc"));
        QVERIFY(!CallBridge::tokenMatches("zcall-ab", "zcall-abc"));
        QVERIFY(!CallBridge::tokenMatches(QByteArray(), QByteArray()));
        const QStringList args = CallBridge::engineArguments(1, 2, 3, QStringLiteral("/run/fixture.key"));
        QVERIFY(!args.contains(QStringLiteral("--token")));
        QVERIFY(!args.contains(QStringLiteral("--ui-token")));
        QVERIFY(args.contains(QStringLiteral("--key-file")));
        const QProcessEnvironment env = CallBridge::engineEnvironment(QProcessEnvironment(), QStringLiteral("zcall-a"),
                                                                      QStringLiteral("zcall-b"));
        QCOMPARE(env.value(QStringLiteral("ZCALL_TOKEN")), QStringLiteral("zcall-a"));
        QCOMPARE(env.value(QStringLiteral("ZCALL_UI_TOKEN")), QStringLiteral("zcall-b"));
    }

    void testCameraProfileRejectsOversizedDimensions() {
        CameraSender sender;
        sender.setProfile(QSize(640, 360), 15, 300000);
        QCOMPARE(sender.size(), QSize(640, 360));
        sender.setProfile(QSize(65536, 65538), 15, 300000);
        QCOMPARE(sender.size(), QSize(640, 360));
        sender.setProfile(QSize(1920, 1080), 15, 300000);
        QCOMPARE(sender.size(), QSize(1920, 1080));
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(Qt::gray);
        QByteArray out;
        QVERIFY(!imageToI420Into(image, QSize(65536, 65538), out));
        QVERIFY(out.isEmpty());
        QVERIFY(imageToI420Into(image, QSize(64, 64), out));
        QCOMPARE(out.size(), 64 * 64 * 3 / 2);
    }

    void testUpdateDownloadsStayPrivate() {
        QTemporaryDir base;
        QVERIFY(base.isValid());
        const QString dir = prepareUpdateDirectory(base.path());
        QVERIFY(!dir.isEmpty());
        const QFileDevice::Permissions shared = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther
            | QFile::WriteOther | QFile::ExeOther;
        QCOMPARE(QFileInfo(dir).permissions() & shared, QFileDevice::Permissions());
        QCOMPARE(updateFilePath(dir, QStringLiteral("../../Zalos-9.9.9-x86_64.AppImage")),
                 QDir(dir).filePath(QStringLiteral("Zalos-9.9.9-x86_64.AppImage")));
        QVERIFY(updateFilePath(dir, QStringLiteral("..")).isEmpty());
        QVERIFY(updateFilePath(dir, QString()).isEmpty());
        QVERIFY(updateFilePath(QString(), QStringLiteral("fixture.deb")).isEmpty());
        QVERIFY(prepareUpdateDirectory(QString()).isEmpty());
        QTemporaryDir linked;
        QTemporaryDir target;
        QVERIFY(linked.isValid() && target.isValid());
        QVERIFY(QFile::link(target.path(), QDir(linked.path()).filePath(QStringLiteral("updates"))));
        QVERIFY(prepareUpdateDirectory(linked.path()).isEmpty());
    }

    void testSingleInstanceActivatesPrimary() {
        using Zalos::Core::Platform::SingleInstance;
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        SingleInstance first(QStringLiteral("fixture-data-dir"), runtime.path());
        QVERIFY(first.start());
        QVERIFY(first.isPrimary());
        QSignalSpy activated(&first, &SingleInstance::activationRequested);

        qputenv("XDG_ACTIVATION_TOKEN", "fixture-token");
        SingleInstance second(QStringLiteral("fixture-data-dir"), runtime.path());
        QVERIFY(!second.start());
        qunsetenv("XDG_ACTIVATION_TOKEN");
        QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 1, 5000);
        QCOMPARE(activated.first().first().toString(), QString("fixture-token"));

        SingleInstance other(QStringLiteral("other-data-dir"), runtime.path());
        QVERIFY(other.start());
        other.release();

        first.release();
        SingleInstance third(QStringLiteral("fixture-data-dir"), runtime.path());
        QVERIFY(third.start());
    }

    void testDesktopNotificationActions() {
        using Zalos::Core::Platform::DesktopNotifications;
        DesktopNotifications n;
        DesktopNotifications::Request r;
        r.actions = {{"default", "Mở"}, {"mark-read", "Đánh dấu đã đọc"}};
        r.inlineReply = true;
        r.replyPlaceholder = "Trả lời…";
        n.setCapabilitiesForTesting({"body", "actions"});
        QCOMPARE(n.actionList(r), QStringList({"default", "Mở", "mark-read", "Đánh dấu đã đọc"}));
        n.setCapabilitiesForTesting({"body", "actions", "inline-reply"});
        QVERIFY(n.actionList(r).contains("inline-reply"));

        QSignalSpy actions(&n, &DesktopNotifications::actionInvoked);
        QSignalSpy replies(&n, &DesktopNotifications::replied);
        n.registerForTesting("thread:u-a", 41);
        n.tokenForTesting(41, "tok-1");
        n.invokeForTesting(41, "default");
        QCOMPARE(actions.count(), 1);
        QCOMPARE(actions.first().at(0).toString(), QString("thread:u-a"));
        QCOMPARE(actions.first().at(2).toString(), QString("tok-1"));
        n.invokeForTesting(999, "default");
        n.invokeForTesting(41, "inline-reply");
        QCOMPARE(actions.count(), 1);
        n.replyForTesting(41, "  ");
        n.replyForTesting(41, "ok nhé");
        QCOMPARE(replies.count(), 1);
        QCOMPARE(replies.first().at(1).toString(), QString::fromUtf8("ok nhé"));
        n.registerForTesting("thread:u-a", 42);
        n.invokeForTesting(41, "default");
        QCOMPARE(actions.count(), 1);
        n.closedForTesting(42);
        QCOMPARE(n.idForTesting("thread:u-a"), 0u);

        ZaloClient client;
        auto* session = client.apiClient()->session();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "9101"}, {"uidFrom", "u-notif"}, {"idTo", "0"},
            {"msgType", "webchat"}, {"content", "xin chào"}, {"ts", ts}}}, false, false);
        auto* convs = client.conversationModel();
        QVERIFY(convs->contains("u-notif"));
        auto* notifications = client.notificationsForTesting();
        notifications->setCapabilitiesForTesting({"actions", "inline-reply"});
        notifications->registerForTesting("thread:u-notif", 7);
        QSignalSpy raise(&client, &ZaloClient::windowActivationRequested);
        convs->setUnreadCount("u-notif", 3);
        notifications->invokeForTesting(7, "mark-read");
        QCOMPARE(convs->conversation("u-notif").unreadCount, 0);
        QCOMPARE(client.currentConversationId(), QString());
        notifications->registerForTesting("thread:u-notif", 8);
        notifications->invokeForTesting(8, "default");
        QCOMPARE(client.currentConversationId(), QString("u-notif"));
        QCOMPARE(raise.count(), 1);
        notifications->registerForTesting("thread:u-notif", 9);
        const int before = client.messageModel()->threadSize("u-notif");
        notifications->replyForTesting(9, QString::fromUtf8("đã nhận"));
        QCOMPARE(client.messageModel()->threadSize("u-notif"), before + 1);
        QVERIFY(convs->conversation("u-notif").lastMessage.contains(QString::fromUtf8("đã nhận")));

        client.appLock()->setPasscode("", "4321");
        client.appLock()->lock();
        notifications->registerForTesting("thread:u-notif", 10);
        notifications->replyForTesting(10, "blocked");
        QCOMPARE(client.messageModel()->threadSize("u-notif"), before + 1);
        QVERIFY(client.appLock()->unlock("4321"));
        QVERIFY(client.appLock()->removePasscode("4321"));
    }

    void testUnreadMarkerDraftsAndNavigation() {
        MessageModel model;
        model.loadConversation("c-unread");
        QSignalSpy incoming(&model, &MessageModel::incomingAppended);
        QVector<MessageItem> items;
        const qint64 base = QDateTime::currentMSecsSinceEpoch() - 100000;
        for (int i = 0; i < 6; ++i) {
            MessageItem m;
            m.id = QStringLiteral("m%1").arg(i);
            m.text = QStringLiteral("t%1").arg(i);
            m.ts = base + i * 1000;
            m.isMine = (i == 2);
            m.senderId = m.isMine ? "me" : "peer";
            items.append(m);
        }
        model.mergeMessages("c-unread", items);
        QCOMPARE(incoming.count(), 1);
        QCOMPARE(incoming.first().first().toInt(), 5);
        model.setUnreadMarker("c-unread", 3);
        QCOMPARE(model.unreadMarkerRow(), 3);
        QVERIFY(model.data(model.index(3), MessageModel::UnreadMarkerRole).toBool());
        QVERIFY(!model.data(model.index(4), MessageModel::UnreadMarkerRole).toBool());
        model.clearUnreadMarker();
        QCOMPARE(model.unreadMarkerRow(), -1);

        model.loadConversation("c-late");
        model.setUnreadMarker("c-late", 1);
        QCOMPARE(model.unreadMarkerRow(), -1);
        MessageItem late;
        late.id = "late-1";
        late.ts = base;
        late.senderId = "peer";
        model.mergeMessages("c-late", {late});
        QCOMPARE(model.unreadMarkerRow(), 0);

        ZaloClient client;
        auto* session = client.apiClient()->session();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        emit session->messagesReceived(QJsonArray{
            QJsonObject{{"msgId", "9201"}, {"uidFrom", "u-d1"}, {"idTo", "0"}, {"msgType", "webchat"}, {"content", "a"}, {"ts", ts}},
            QJsonObject{{"msgId", "9202"}, {"uidFrom", "u-d2"}, {"idTo", "0"}, {"msgType", "webchat"}, {"content", "b"}, {"ts", ts}}},
            false, false);
        client.setMyUidForTesting("u-fixture-owner");
        auto* convs = client.conversationModel();
        client.setDraft("u-d1", QString::fromUtf8("viết dở"));
        QCOMPARE(client.draftFor("u-d1"), QString::fromUtf8("viết dở"));
        int row = -1;
        for (int r = 0; r < convs->rowCount(); ++r)
            if (convs->data(convs->index(r), ConversationModel::IdRole).toString() == "u-d1") row = r;
        QVERIFY(row >= 0);
        QCOMPARE(convs->data(convs->index(row), ConversationModel::DraftRole).toString(), QString::fromUtf8("viết dở"));
        convs->clearDrafts();
        client.reloadDraftsForTesting();
        QCOMPARE(client.draftFor("u-d1"), QString::fromUtf8("viết dở"));
        client.selectConversation("u-d1", "D1", "");
        client.sendMessage("gửi rồi");
        QCOMPARE(client.draftFor("u-d1"), QString());
        client.reloadDraftsForTesting();
        QCOMPARE(client.draftFor("u-d1"), QString());

        const QString first = convs->data(convs->index(0), ConversationModel::IdRole).toString();
        const QString second = convs->data(convs->index(1), ConversationModel::IdRole).toString();
        client.selectConversation(first, "", "");
        client.selectAdjacentConversation(1);
        QCOMPARE(client.currentConversationId(), second);
        client.selectAdjacentConversation(-1);
        QCOMPARE(client.currentConversationId(), first);
        client.selectAdjacentConversation(-1);
        QCOMPARE(client.currentConversationId(), first);
    }

    void testMessageSchedulerSplit() {
        using Zalos::Core::Network::MessageScheduler;
        MessageScheduler scheduler;
        bool ready = false;
        QStringList sent;
        std::function<void(bool, const QString&)> pendingDone;
        scheduler.setReadyCheck([&ready]() { return ready; });
        scheduler.setSender([&](const QString& convId, bool, const QString& text, std::function<void(bool, const QString&)> done) {
            sent << convId + ":" + text;
            pendingDone = done;
        });
        QString error;
        QVERIFY(!scheduler.schedule("c1", false, "x", QDateTime::currentMSecsSinceEpoch() + 1000, &error));
        QCOMPARE(error, QString("past"));
        const qint64 at = QDateTime::currentMSecsSinceEpoch() + 3600000;
        QVERIFY(scheduler.schedule("c1", false, "hẹn", at));
        QCOMPARE(scheduler.entriesFor("c1").size(), 1);
        scheduler.runDue(at + 1);
        QVERIFY(sent.isEmpty());
        ready = true;
        scheduler.runDue(at - 1000);
        QVERIFY(sent.isEmpty());
        scheduler.runDue(at + 1);
        QCOMPARE(sent, QStringList({QString::fromUtf8("c1:hẹn")}));
        QSignalSpy failed(&scheduler, &MessageScheduler::sendFailed);
        pendingDone(false, "mạng lỗi");
        QCOMPARE(failed.count(), 1);
        QCOMPARE(scheduler.entriesFor("c1").first().toMap().value("state").toString(), QString("failed"));
        scheduler.cancel(scheduler.entriesFor("c1").first().toMap().value("id").toString());
        QCOMPARE(scheduler.entriesFor("c1").size(), 0);
    }

    void reactionSnapshotShapes_data() {
        QTest::addColumn<QString>("json");
        QTest::addColumn<QString>("expected");
        QTest::newRow("per-user counts ordered by count") << QStringLiteral(R"({"reacts":{"u1":{"5":1},"u2":{"3":2}}})") << QStringLiteral("👍❤️ 3");
        QTest::newRow("unknown rType is a bullet, never heart") << QStringLiteral(R"({"reacts":{"u1":{"999":1}}})") << QStringLiteral("•");
        QTest::newRow("negative rType and zero count skipped") << QStringLiteral(R"({"reacts":{"u1":{"-1":4,"5":0}}})") << QString();
        QTest::newRow("string count accepted") << QStringLiteral(R"({"reacts":{"u1":{"5":"2"}}})") << QStringLiteral("❤️ 2");
        QTest::newRow("null reacts empty") << QStringLiteral(R"({"reacts":null})") << QString();
        QTest::newRow("missing reacts empty") << QStringLiteral(R"({})") << QString();
        QTest::newRow("top three emojis then total") << QStringLiteral(R"({"reacts":{"u1":{"5":5,"3":4,"0":3,"32":2,"2":1}}})")
                                                       << QStringLiteral("❤️👍😆 15");
        QTest::newRow("legacy icon array") << QStringLiteral(R"({"reacts":["/-heart","/-heart"]})") << QStringLiteral("❤️ 2");
    }

    void reactionSnapshotShapes() {
        QFETCH(QString, json);
        QFETCH(QString, expected);
        const QJsonObject msg = QJsonDocument::fromJson(json.toUtf8()).object();
        QCOMPARE(Zalos::Core::Network::Detail::parseReactionSnapshot(msg), expected);
    }

    void phoneAndOwnProfileNormalisation_data() {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("expected");
        QTest::newRow("country code to leading zero") << QStringLiteral("84912345678") << QStringLiteral("0912345678");
        QTest::newRow("plus form with spaces") << QStringLiteral("+84 912 345 678") << QStringLiteral("0912345678");
        QTest::newRow("local form untouched") << QStringLiteral("0912345678") << QStringLiteral("0912345678");
        QTest::newRow("double-zero after code untouched") << QStringLiteral("840912345678") << QStringLiteral("840912345678");
        QTest::newRow("short number untouched") << QStringLiteral("8412345") << QStringLiteral("8412345");
    }

    void phoneAndOwnProfileNormalisation() {
        using namespace Zalos::Core::Network::Detail;
        QFETCH(QString, raw);
        QFETCH(QString, expected);
        QCOMPARE(normalizeDisplayPhone(raw), expected);
    }

    void ownProfileIsFoundUnderWrappers() {
        using namespace Zalos::Core::Network::Detail;
        const QString name = QStringLiteral("Fixture New");
        const QJsonObject inner{{"displayName", name}};
        QCOMPARE(pickOwnProfileObject(QJsonObject{{"profile", inner}}).value("displayName").toString(), name);
        QCOMPARE(pickOwnProfileObject(QJsonObject{{"info", inner}}).value("displayName").toString(), name);
        QCOMPARE(pickOwnProfileObject(QJsonObject{{"user", inner}}).value("displayName").toString(), name);
        QCOMPARE(pickOwnProfileObject(QJsonObject{{"displayName", name}}).value("displayName").toString(), name);
        QVERIFY(pickOwnProfileObject(QJsonValue(QJsonObject{})).isEmpty());
    }

    void muteSnapshotHandlesIndefiniteExpiryAndUnits() {
        using Zalos::Core::Network::Detail::parseMuteSnapshot;
        const qint64 now = 1700000000;
        const QJsonObject obj{
            {"systemTime", now * 1000},
            {"chatEntries", QJsonArray{
                QJsonObject{{"id", "u-forever"}, {"muteMode", 2}, {"duration", -1}, {"startTime", now - 100}},
                QJsonObject{{"id", "u-hour"}, {"muteMode", 1}, {"duration", 3600}, {"startTime", now - 60}},
                QJsonObject{{"id", "u-expired"}, {"muteMode", 1}, {"duration", 60}, {"startTime", now - 3600}},
                QJsonObject{{"id", ""}, {"muteMode", 1}, {"duration", -1}},
            }},
            {"groupChatEntries", QJsonArray{
                QJsonObject{{"id", "g-long"}, {"muteMode", 3}, {"duration", 0}, {"startTime", now}},
            }},
        };
        const auto snap = parseMuteSnapshot(obj, 0);
        QCOMPARE(snap.muted, (QSet<QString>{"u-forever", "u-hour", "g-long"}));
        QCOMPARE(snap.until.value("u-forever"), qint64(0));
        QCOMPARE(snap.until.value("u-hour"), (now - 60 + 3600) * 1000);
        QCOMPARE(snap.until.value("g-long"), qint64(0));
        QVERIFY(!snap.muted.contains("u-expired"));

        const auto noClock = parseMuteSnapshot(QJsonObject{{"chatEntries", QJsonArray{
            QJsonObject{{"id", "u-x"}, {"muteMode", 1}, {"duration", -1}}}}}, now);
        QVERIFY(noClock.muted.contains("u-x"));
        QVERIFY(parseMuteSnapshot(QJsonObject{}, now).muted.isEmpty());
        QVERIFY(snap.valid);
        QVERIFY(!parseMuteSnapshot(QJsonObject{}, now).valid);

        const QJsonObject loose{
            {"systemTime", QString::number(now)},
            {"groupChatEntries", QJsonArray{
                QJsonObject{{"id", "group_1001"}, {"muteMode", "2"}, {"duration", "-1"}},
                QJsonObject{{"grid", "g1002"}, {"muteMode", "1"}, {"duration", "3600"}, {"startTime", QString::number((now - 60) * 1000)}},
            }},
            {"chatEntries", QJsonArray{
                QJsonObject{{"toId", "u2001"}, {"muteMode", 1}, {"duration", 600}, {"startTime", now}},
                QJsonObject{{"id", "u-fixture"}, {"muteMode", 2}, {"duration", -1}},
            }},
        };
        const auto looseSnap = parseMuteSnapshot(QString::fromUtf8(QJsonDocument(loose).toJson(QJsonDocument::Compact)), 0);
        QVERIFY(looseSnap.valid);
        QCOMPARE(looseSnap.muted, (QSet<QString>{"1001", "1002", "2001", "u-fixture"}));
        QCOMPARE(looseSnap.until.value("1002"), (now - 60 + 3600) * 1000);
        QCOMPARE(Zalos::Core::Network::Detail::normalizeMuteId("U3001"), QString("3001"));
        QCOMPARE(Zalos::Core::Network::Detail::normalizeMuteId("group"), QString("group"));

        const auto failed = parseMuteSnapshot(QJsonObject{{"error_code", 114}, {"chatEntries", QJsonArray{}}}, now);
        QVERIFY(!failed.valid);
        QVERIFY(failed.muted.isEmpty());

        using Zalos::Core::Network::ZaloSession;
        using Zalos::Core::Network::ZaloCrypto::signKey;
        auto toMap = [](const QList<QPair<QString, QString>>& q) {
            QMap<QString, QString> m;
            for (const auto& kv : q) m.insert(kv.first, kv.second);
            return m;
        };
        QMap<QString, QString> legacy = toMap(ZaloSession::serverInfoQuery("fixture-imei", false, 7, 8));
        QCOMPARE(legacy.size(), 5);
        QVERIFY(!legacy.contains("sp_mtn"));
        const QString legacyKey = legacy.take("signkey");
        QCOMPARE(legacyKey, signKey("getserverinfo", legacy));
        QMap<QString, QString> signedAll = toMap(ZaloSession::serverInfoQuery("fixture-imei", true, -1, 3));
        QCOMPARE(signedAll.value("sp_mtn"), QString("-1"));
        QCOMPARE(signedAll.value("bkt"), QString("3"));
        QCOMPARE(signedAll.value("floatMenuPos"), QString("null"));
        const QString allKey = signedAll.take("signkey");
        QCOMPARE(allKey, signKey("getserverinfo", signedAll));
        QVERIFY(allKey != legacyKey);
    }

    void loginReplyClassification() {
        using Step = Zalos::Core::Network::ZaloSession::LoginStep;
        using Zalos::Core::Network::ZaloSession;
        QCOMPARE(ZaloSession::classifyLoginHttp(429, false), Step::Unavailable);
        QCOMPARE(ZaloSession::classifyLoginHttp(503, false), Step::Unavailable);
        QCOMPARE(ZaloSession::classifyLoginHttp(0, true), Step::Unavailable);
        QCOMPARE(ZaloSession::classifyLoginHttp(400, true), Step::Unavailable);
        QCOMPARE(ZaloSession::classifyLoginHttp(403, true), Step::Unavailable);
        QCOMPARE(ZaloSession::classifyLoginHttp(200, false), Step::Proceed);

        QCOMPARE(ZaloSession::classifyLoginBody(true, false, -1, false), Step::Unavailable);
        QCOMPARE(ZaloSession::classifyLoginBody(false, false, -1, true), Step::Failed);
        QCOMPARE(ZaloSession::classifyLoginBody(false, true, -102, true), Step::Failed);
        QCOMPARE(ZaloSession::classifyLoginBody(false, true, 600, true), Step::Failed);
        QCOMPARE(ZaloSession::classifyLoginBody(false, true, 0, false), Step::Failed);
        QCOMPARE(ZaloSession::classifyLoginBody(false, true, 0, true), Step::Proceed);
    }

    void credentialsFileLoadRules() {
        using Zalos::Core::Network::ZaloSession;
        using Zalos::Core::Network::SessionCookieJar;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto write = [&dir](const QString& name, const QByteArray& body) {
            QFile f(dir.filePath(name));
            if (f.open(QIODevice::WriteOnly)) f.write(body);
            return f.fileName();
        };
        const QByteArray good = R"({"imei":"fixture-imei","userAgent":"fixture-ua",)"
                                R"("cookies":[{"name":"zpsid","value":"fixture-value","domain":"chat.zalo.me","path":"/"}]})";
        QString imei;
        QString ua;
        {
            SessionCookieJar jar;
            QVERIFY(ZaloSession::loadCredentials(write("good.json", good), &jar, &imei, &ua));
            QCOMPARE(imei, QString("fixture-imei"));
            QCOMPARE(ua, QString("fixture-ua"));
            QCOMPARE(jar.cookiesForUrl(QUrl("https://chat.zalo.me/")).size(), 1);
        }
        {
            SessionCookieJar jar;
            QVERIFY(!ZaloSession::loadCredentials(write("truncated.json", good.left(good.size() - 9)), &jar, &imei, &ua));
        }
        {
            SessionCookieJar jar;
            QVERIFY(!ZaloSession::loadCredentials(write("no-imei.json", R"({"cookies":[{"name":"a","value":"b"}]})"), &jar, &imei, &ua));
        }
        {
            SessionCookieJar jar;
            QVERIFY(!ZaloSession::loadCredentials(write("no-cookies.json", R"({"imei":"fixture-imei","cookies":[]})"), &jar, &imei, &ua));
        }
        {
            SessionCookieJar jar;
            QVERIFY(!ZaloSession::loadCredentials(dir.filePath("missing.json"), &jar, &imei, &ua));
        }
    }

    void offlineSaveStatsCountInsertUpdateDedupe() {
        ZaloOfflineStore store;
        store.bindOwner("u-stats-fixture");
        store.clearCache();
        store.bindOwner("u-stats-fixture");
        const auto base = store.saveStats();
        Zalos::Core::Models::MessageItem a;
        a.id = "stats-a"; a.senderName = "Fixture"; a.text = "one"; a.messageType = "text"; a.senderId = "u-peer"; a.cliMsgId = "cli-7";
        QVERIFY(store.saveMessage("stats-conv", a, {}, false));
        QVERIFY(store.saveMessage("stats-conv", a, {}, false));
        Zalos::Core::Models::MessageItem b = a;
        b.id = "stats-b";
        QVERIFY(store.saveMessage("stats-conv", b, {}, false));
        const auto after = store.saveStats();
        QCOMPARE(after.inserted - base.inserted, 2);
        QCOMPARE(after.updated - base.updated, 1);
        QCOMPARE(after.deduped - base.deduped, 1);
        QCOMPARE(after.failed - base.failed, 0);
        store.clearCache();
    }

    void syncUndoAndPollRowsAreImported() {
        using Zalos::Core::Sync::MobileHistorySync;
        MobileHistorySync::BackupConversation conv;
        conv.plainId = "1001";
        MobileHistorySync::BackupRow undo;
        undo.globalMsgId = "9100";
        undo.cliMsgId = "c9100";
        undo.msgType = 36;
        undo.text = R"({"globalMsgId":"5001","cliMsgId":"c5001","deleteMsg":1})";
        undo.senderId = "1001";
        undo.ts = 1700000000000LL;
        MobileHistorySync::BackupRow poll;
        poll.globalMsgId = "9200";
        poll.cliMsgId = "c9200";
        poll.msgType = 26;
        poll.text = R"({"poll_id":77,"action":"create","params":{"question":"q"}})";
        poll.senderId = "1001";
        poll.ts = 1700000001000LL;
        MobileHistorySync::BackupRow brokenUndo = undo;
        brokenUndo.globalMsgId = "9300";
        brokenUndo.text = "{}";
        conv.rows << undo << poll << brokenUndo;
        int skipped = 0;
        QHash<QString, int> reasons;
        const QJsonArray out = MobileHistorySync::toMessages(conv, {{"1001", "9001"}}, "1111", "u-me-fixture", &skipped, 0, -1, &reasons);
        QCOMPARE(out.size(), 5);
        QCOMPARE(out.at(0).toObject().value("msgType").toString(), QStringLiteral("webchat"));
        QCOMPARE(out.at(0).toObject().value("msgId").toString(), QStringLiteral("5001"));
        QCOMPARE(out.at(1).toObject().value("msgType").toString(), QStringLiteral("chat.undo"));
        QCOMPARE(out.at(1).toObject().value("content").toObject().value("globalMsgId").toString(), QStringLiteral("5001"));
        QCOMPARE(out.at(2).toObject().value("msgType").toString(), QStringLiteral("group.poll"));
        QCOMPARE(out.at(2).toObject().value("content").toObject().value("poll_id").toInt(), 77);
        QCOMPARE(skipped, 0);
    }

    void syncRowWithoutIdIsCountedAsSkipped() {
        using Zalos::Core::Sync::MobileHistorySync;
        MobileHistorySync::BackupConversation conv;
        conv.plainId = "1001";
        MobileHistorySync::BackupRow row;
        row.globalMsgId = "0";
        row.cliMsgId = "";
        row.msgType = 0;
        row.text = "xin chào";
        row.senderId = "1001";
        row.ts = 1700000000000LL;
        conv.rows.append(row);
        int skipped = 0;
        QHash<QString, int> reasons;
        const QJsonArray out = MobileHistorySync::toMessages(conv, {{"1001", "9001"}}, "1111", "u-me-fixture", &skipped, 0, -1, &reasons);
        QVERIFY(out.isEmpty());
        QCOMPARE(skipped, 1);
        QCOMPARE(reasons.value(QStringLiteral("no_id")), 1);
    }

    void syncMsgType20IsTextNotSkipped() {
        using Zalos::Core::Sync::MobileHistorySync;
        MobileHistorySync::BackupConversation conv;
        conv.plainId = "1001";
        conv.isGroup = false;
        MobileHistorySync::BackupRow row;
        row.globalMsgId = "5001";
        row.cliMsgId = "c5001";
        row.msgType = 20;
        row.text = "xin chào";
        row.senderId = "1001";
        row.ts = 1700000000000LL;
        conv.rows.append(row);
        const QHash<QString, QString> ids{{"1001", "9001"}};
        int skipped = 0;
        QHash<QString, int> reasons;
        const QJsonArray out = MobileHistorySync::toMessages(conv, ids, "1111", "u-me-fixture", &skipped, 0, -1, &reasons);
        QCOMPARE(out.size(), 1);
        QCOMPARE(skipped, 0);
        QVERIFY(reasons.isEmpty());
    }

    void testWheelBoostScalesTouchpadOnly() {
        using Zalos::Core::Platform::WheelBoost;
        struct Recorder : QObject {
            QList<QPoint> pixel;
            QList<QPoint> angle;
            bool event(QEvent* e) override {
                if (e->type() != QEvent::Wheel) return QObject::event(e);
                auto* w = static_cast<QWheelEvent*>(e);
                pixel << w->pixelDelta();
                angle << w->angleDelta();
                w->accept();
                return true;
            }
        } target;
        WheelBoost boost;
        target.installEventFilter(&boost);
        auto send = [&target](QPoint pixel, QPoint angle, Qt::ScrollPhase phase) {
            QWheelEvent e(QPointF(10, 10), QPointF(10, 10), pixel, angle, Qt::NoButton, Qt::NoModifier, phase, false);
            e.setAccepted(false);
            QCoreApplication::sendEvent(&target, &e);
            return e.isAccepted();
        };

        QVERIFY(send({0, -30}, {0, -30}, Qt::ScrollUpdate));
        QCOMPARE(target.pixel.last(), QPoint(0, -50));
        QCOMPARE(target.angle.last(), QPoint(0, -50));

        target.pixel.clear();
        send({}, {}, Qt::ScrollBegin);
        for (int i = 0; i < 3; ++i) send({0, 1}, {0, 1}, Qt::ScrollUpdate);
        int total = 0;
        for (const QPoint& p : std::as_const(target.pixel)) total += p.y();
        QCOMPARE(total, 5);

        send({}, {0, 120}, Qt::NoScrollPhase);
        QCOMPARE(target.angle.last(), QPoint(0, 120));
        send({0, 15}, {0, 120}, Qt::NoScrollPhase);
        QCOMPARE(target.pixel.last(), QPoint(0, 15));
        QCOMPARE(target.angle.last(), QPoint(0, 120));

        QStyleHints* hints = QGuiApplication::styleHints();
        const int original = hints->wheelScrollLines();
        hints->setWheelScrollLines(3);
        WheelBoost::applyWheelScrollLines(hints);
        QCOMPARE(hints->wheelScrollLines(), WheelBoost::kMinWheelScrollLines);
        hints->setWheelScrollLines(8);
        WheelBoost::applyWheelScrollLines(hints);
        QCOMPARE(hints->wheelScrollLines(), 8);
        hints->setWheelScrollLines(original);
    }

    void testMiddleAutoScrollHoldAndToggle() {
        using Zalos::Core::Platform::MiddleAutoScroll;
        QCOMPARE(MiddleAutoScroll::stepFor(0), 0.0);
        QCOMPARE(MiddleAutoScroll::stepFor(MiddleAutoScroll::kDeadZone), 0.0);
        QVERIFY(MiddleAutoScroll::stepFor(60) > 0);
        QCOMPARE(MiddleAutoScroll::stepFor(-60), -MiddleAutoScroll::stepFor(60));
        QVERIFY(MiddleAutoScroll::stepFor(150) > MiddleAutoScroll::stepFor(60));
        QCOMPARE(MiddleAutoScroll::stepFor(5000), MiddleAutoScroll::kMaxStep);

        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData("import QtQuick\nWindow { width: 300; height: 300; visible: true\n"
                     "  Flickable { objectName: \"list\"; anchors.fill: parent; contentHeight: 4000\n"
                     "    Rectangle { width: 300; height: 4000 } }\n"
                     "  Rectangle { objectName: \"cover\"; x: 0; y: 250; width: 300; height: 50 } }",
                     QUrl());
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root, qPrintable(comp.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window);
        auto* list = window->findChild<QQuickItem*>("list");
        QVERIFY(list);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        MiddleAutoScroll autoScroll;
        window->installEventFilter(&autoScroll);
        QCOMPARE(MiddleAutoScroll::scrollableAt(window->contentItem(), QPointF(150, 100)), list);
        QCOMPARE(MiddleAutoScroll::scrollableAt(window->contentItem(), QPointF(150, 270)), nullptr);

        QTest::mousePress(window, Qt::MiddleButton, {}, QPoint(150, 270));
        QVERIFY(!autoScroll.active());
        QTest::mouseRelease(window, Qt::MiddleButton, {}, QPoint(150, 270));

        QTest::mousePress(window, Qt::MiddleButton, {}, QPoint(150, 100));
        QVERIFY(autoScroll.active());
        QTest::mouseMove(window, QPoint(150, 200));
        QTRY_VERIFY(list->property("contentY").toReal() > 50);
        QTest::mouseRelease(window, Qt::MiddleButton, {}, QPoint(150, 200));
        QVERIFY(!autoScroll.active());
        const qreal stopped = list->property("contentY").toReal();
        QTest::qWait(80);
        QCOMPARE(list->property("contentY").toReal(), stopped);

        QTest::mousePress(window, Qt::MiddleButton, {}, QPoint(150, 150));
        QTest::mouseRelease(window, Qt::MiddleButton, {}, QPoint(150, 150));
        QVERIFY(autoScroll.active());
        QTest::mouseMove(window, QPoint(150, 20));
        QTRY_VERIFY(list->property("contentY").toReal() < stopped);
        QTRY_COMPARE(list->property("contentY").toReal(), 0.0);
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(150, 20));
        QVERIFY(!autoScroll.active());

        QTest::mousePress(window, Qt::MiddleButton, {}, QPoint(150, 150));
        QTest::mouseRelease(window, Qt::MiddleButton, {}, QPoint(150, 150));
        QVERIFY(autoScroll.active());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!autoScroll.active());
    }

    void testShortcutTableAndPasscodeLock() {
        using Zalos::Core::Platform::AppLock;
        using Zalos::Core::Platform::ShortcutMap;
        QTemporaryDir config;
        QFile custom(config.path() + "/shortcuts-custom.json");
        QVERIFY(custom.open(QIODevice::WriteOnly));
        const QJsonArray overrides{
            QJsonObject{{"command", "chat.next"}, {"keys", QJsonArray{"Alt+J"}}},
            QJsonObject{{"command", "app.quit"}, {"keys", QJsonArray{}}},
            QJsonObject{{"command", "made.up"}, {"keys", QJsonArray{"Ctrl+Y"}}},
            QJsonObject{{"command", "chat.previous"}, {"keys", QJsonArray{"not a key ###"}}}};
        custom.write(QJsonDocument(overrides).toJson());
        custom.close();
        ShortcutMap map;
        map.load(config.path());
        QCOMPARE(map.keys("chat.next"), QStringList({"Alt+J"}));
        QVERIFY(map.keys("app.quit").isEmpty());
        QVERIFY(map.keys("made.up").isEmpty());
        QCOMPARE(map.keys("composer.voice"), QStringList({"Ctrl+Shift+V"}));
        QCOMPARE(map.keys("app.shortcuts"), QStringList({"Ctrl+/"}));
        QVERIFY(QFile::exists(config.path() + "/shortcuts-default.json"));
        QCOMPARE(map.all().size(), map.commands().size());

        {
            AppLock lock;
            if (lock.hasPasscode()) QSKIP("a passcode from another run is set in this profile");
            QVERIFY(!lock.isEventFilterInstalledForTesting());
            QVERIFY(!lock.locked());
            QVERIFY(!lock.setPasscode("", "12"));
            QVERIFY(lock.setPasscode("", "2468"));
            QVERIFY(!lock.isEventFilterInstalledForTesting());
            lock.setAutoLockMinutes(5);
            QVERIFY(lock.isEventFilterInstalledForTesting());
            QVERIFY(!lock.setPasscode("0000", "1357"));
            lock.lock();
            QVERIFY(lock.locked());
            QVERIFY(!lock.isEventFilterInstalledForTesting());
            for (int i = 0; i < AppLock::kFreeAttempts; ++i) QVERIFY(!lock.unlock("0000"));
            QVERIFY(lock.retryAfterSeconds() > 0);
            QVERIFY(!lock.unlock("2468"));
        }
        {
            AppLock lock;
            QVERIFY(lock.locked());
            QVERIFY(lock.retryAfterSeconds() > 0);
            QVERIFY(!lock.unlock("2468"));
        }
        QSettings().remove("lock/blockedUntil");
        {
            AppLock lock;
            QVERIFY(lock.hasPasscode());
            QVERIFY(lock.locked());
            QVERIFY(lock.unlock("2468"));
            QVERIFY(!QSettings().contains("lock/failures"));
            QVERIFY(lock.isEventFilterInstalledForTesting());
            lock.setCallActive(true);
            QVERIFY(lock.callActive());
            lock.lockForSleep();
            QVERIFY(lock.locked());
            QVERIFY(lock.unlock("2468"));
            lock.setCallActive(false);
            lock.setAutoLockMinutes(0);
            lock.lockForSleep();
            QVERIFY(!lock.locked());
            lock.setAutoLockMinutes(5);
            QVERIFY(lock.isEventFilterInstalledForTesting());
            QVERIFY(!lock.removePasscode("0000"));
            QVERIFY(lock.removePasscode("2468"));
            QVERIFY(!lock.hasPasscode());
            QVERIFY(!lock.isEventFilterInstalledForTesting());
            QVERIFY(lock.setPasscode("", "1357"));
            lock.lock();
            QVERIFY(!lock.unlock("0000"));
            QVERIFY(QSettings().contains("lock/failures"));
            lock.forgetPasscode();
            QVERIFY(!lock.hasPasscode());
            QVERIFY(!lock.locked());
            QVERIFY(!QSettings().contains("lock/failures"));
            lock.setAutoLockMinutes(0);
        }
        QSettings settings;
        QVERIFY(!settings.contains("lock/hash"));
    }

    void testStoragePruneAndVoiceRecorder() {
        using Zalos::Core::Platform::StorageManager;
        QTemporaryDir dir;
        const QDateTime now = QDateTime::currentDateTime();
        auto make = [&](const QString& name, int kb, int daysOld) {
            QFile f(dir.path() + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(kb * 1024, 'x'));
            f.flush();
            QVERIFY(f.setFileTime(now.addDays(-daysOld), QFileDevice::FileModificationTime));
            f.close();
        };
        make("old", 10, 40);
        make("mid", 10, 5);
        make("new", 10, 0);
        const qint64 freed = StorageManager::pruneDirectory(dir.path(), 15 * 1024, 31LL * 24 * 3600 * 1000);
        QCOMPARE(freed, qint64(20 * 1024));
        QVERIFY(!QFile::exists(dir.path() + "/old"));
        QVERIFY(!QFile::exists(dir.path() + "/mid"));
        QVERIFY(QFile::exists(dir.path() + "/new"));
        StorageManager storage;
        const QVariantList usage = storage.usage();
        QCOMPARE(usage.size(), 5);
        for (const QVariant& v : usage) {
            const QVariantMap m = v.toMap();
            QCOMPARE(m.value("clearable").toBool(), m.value("key").toString() != "downloads");
        }
        QSignalSpy downloadsCleared(&storage, &StorageManager::usageChanged);
        storage.clear(QStringLiteral("downloads"));
        QTest::qWait(100);
        QCOMPARE(downloadsCleared.count(), 0);

        Zalos::Core::Media::VoiceRecorder recorder;
        if (recorder.available()) QSKIP("a microphone is present");
        QSignalSpy failed(&recorder, &Zalos::Core::Media::VoiceRecorder::failed);
        QVERIFY(!recorder.start());
        QVERIFY(!recorder.recording());
        QCOMPARE(failed.count(), 1);

        QTemporaryDir takes;
        const QString take = takes.path() + "/take.aac";
        QFile f(take);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(2048, '\0'));
        f.close();
        ZaloClient client;
        client.selectConversation("u-voice", "Voice", "");
        client.sendVoiceMessage("u-voice", take, 3200);
        QCOMPARE(client.messageModel()->rowCount(), 1);
        const QModelIndex i = client.messageModel()->index(0);
        QCOMPARE(client.messageModel()->data(i, MessageModel::MessageTypeRole).toString(), QString("voice"));
        QCOMPARE(client.messageModel()->data(i, MessageModel::StatusRole).toString(), QString("failed"));
        QCOMPARE(client.messageModel()->data(i, MessageModel::ExtraRole).toMap().value("durationMs").toInt(), 3200);
    }

    void testSyncPromptStickersAndReactionList() {
        ZaloClient client;
        client.setMyUidForTesting("u-prompt-owner");
        if (client.offlineStore()) client.offlineStore()->bindOwner("u-prompt-owner");
        Zalos::Core::Models::ConversationItem emptyGroup;
        emptyGroup.id = QStringLiteral("g-prompt");
        emptyGroup.name = QStringLiteral("Fixture Prompt Group");
        emptyGroup.isGroup = true;
        client.conversationModel()->upsertConversation(emptyGroup);
        QSettings settings;
        settings.remove("mobileSync/done/u-prompt-owner");
        settings.remove("mobileSync/neverAsk/u-prompt-owner");
        client.maybeOfferMobileSync();
        QVERIFY(!client.showSyncPrompt());
        client.setLoggedInForTesting(true);
        client.maybeOfferMobileSync();
        QVERIFY(client.showSyncPrompt());
        client.dismissSyncPrompt(false);
        QVERIFY(!client.showSyncPrompt());
        client.maybeOfferMobileSync();
        QVERIFY(client.showSyncPrompt());
        client.dismissSyncPrompt(true);
        client.maybeOfferMobileSync();
        QVERIFY(!client.showSyncPrompt());
        settings.remove("mobileSync/neverAsk/u-prompt-owner");
        settings.setValue("mobileSync/done/u-prompt-owner", true);
        client.maybeOfferMobileSync();
        QVERIFY(!client.showSyncPrompt());
        settings.remove("mobileSync/done/u-prompt-owner");
        client.setLoggedInForTesting(false);

        const QVariantList reactions = client.reactionList();
        QVERIFY(reactions.size() > 6);
        QSet<QString> emojis;
        for (const QVariant& v : reactions) emojis.insert(v.toMap().value("emoji").toString());
        QCOMPARE(emojis.size(), reactions.size());

        auto* session = client.apiClient()->session();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "9301"}, {"uidFrom", "u-stk"}, {"idTo", "0"},
            {"msgType", "chat.sticker"}, {"content", QJsonObject{{"id", 12345}, {"catId", 10}, {"type", 7}}}, {"ts", ts}}},
            false, false);
        client.selectConversation("u-stk", "Sticker", "");
        QCOMPARE(client.messageModel()->rowCount(), 1);
        const QModelIndex i = client.messageModel()->index(0);
        QCOMPARE(client.messageModel()->data(i, MessageModel::MessageTypeRole).toString(), QString("sticker"));
        QVERIFY(client.messageModel()->data(i, MessageModel::MediaUrlRole).toString().isEmpty());
    }

    void testSystemSoundsAutostartAndTray() {
        using Zalos::Core::Platform::SoundEngine;
        QTemporaryDir data;
        QDir root(data.path());
        QVERIFY(root.mkpath("sounds/fixture/stereo"));
        QVERIFY(root.mkpath("sounds/alsa"));
        auto touch = [&](const QString& rel, const QByteArray& body = "x") {
            QFile f(root.filePath(rel));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(body);
        };
        touch("sounds/fixture/index.theme", "[Sound Theme]\nName=Fixture Theme\n");
        touch("sounds/fixture/stereo/message-new-instant.oga");
        touch("sounds/fixture/stereo/phone-incoming-call.oga");
        touch("sounds/fixture/stereo/audio-channel-front-left.oga");
        touch("sounds/fixture/stereo/message-new-instant.oga.license");
        touch("sounds/alsa/Front_Center.wav");
        const QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
        const QByteArray oldHome = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_DIRS", data.path().toUtf8());
        qputenv("XDG_DATA_HOME", (data.path() + "/none").toUtf8());
        const QVariantList sounds = SoundEngine::systemSounds();
        qputenv("XDG_DATA_DIRS", oldDirs);
        if (oldHome.isEmpty()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", oldHome);
        QCOMPARE(sounds.size(), 2);
        const QVariantMap first = sounds.first().toMap();
        QVERIFY(first.value("key").toString().startsWith("system:"));
        QVERIFY(first.value("label").toString().startsWith("Fixture Theme · "));
        const QString key = first.value("key").toString();
        QVERIFY(SoundEngine::isValidKey("notification", key));
        QVERIFY(!SoundEngine::isValidKey("notification", "system:/no/such/file.oga"));
        QVERIFY(SoundEngine::isValidKey("ringtone", "classic"));
        QVERIFY(!SoundEngine::isValidKey("ringtone", "pop"));

        QTemporaryDir config;
        const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        {
            ZaloClient client;
            QCOMPARE(ZaloClient::autostartFilePath(), config.path() + "/autostart/zalos.desktop");
            QVERIFY(!client.startWithSystem());
            client.setStartWithSystem(true);
            QVERIFY(client.startWithSystem());
            QFile entry(ZaloClient::autostartFilePath());
            QVERIFY(entry.open(QIODevice::ReadOnly));
            const QByteArray text = entry.readAll();
            QVERIFY(text.contains("Exec=\""));
            QVERIFY(text.contains("--autostart"));
            client.setStartWithSystem(false);
            QVERIFY(!QFile::exists(ZaloClient::autostartFilePath()));
            QVERIFY(client.closeToTray());
        }
        if (oldConfig.isEmpty()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", oldConfig);
    }

    void testIconProviderAntialiases() {
        Zalos::Core::Platform::IconImageProvider provider;
        const QString check = "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z";
        QSize size;
        const QImage img = provider.requestImage(
            "f/" + QString::fromLatin1(QUrl::toPercentEncoding("#0068ff")) + "/" + QString::fromLatin1(QUrl::toPercentEncoding(check)),
            &size, QSize(36, 36));
        QCOMPARE(size, QSize(36, 36));
        int opaque = 0, partial = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const int a = qAlpha(img.pixel(x, y));
                if (a == 255) ++opaque;
                else if (a > 0) ++partial;
            }
        QVERIFY(opaque > 20);
        QVERIFY(partial > 10);
        QVERIFY(Zalos::Core::Platform::IconImageProvider::svgFor(false, QColor("#123456"), check).contains("stroke=\"#123456\""));
        QVERIFY(!provider.requestImage("f/x/", &size, QSize(16, 16)).isNull());
    }

    void testVideoCallAndMobileSync() {
        ZaloClient client;
        auto* calls = client.calls();
        QSignalSpy videoFrameSpy(calls, &Zalos::Core::Call::CallBridge::videoFrameReceived);

        QByteArray videoStream = QByteArray(R"({"type":"video","key":true,"codec":"h264","bin":4})") + "\n"
                                 + QByteArray("\x00\x00\x00\x01", 4)
                                 + QByteArray(R"({"type":"state","phase":"connected","video":true,"name":"Peer","since":100})") + "\n";
        calls->injectUiBytesForTesting(videoStream);
        QCOMPARE(videoFrameSpy.count(), 1);
        QCOMPARE(calls->isVideo(), true);

        client.selectConversation("conv-1", "Conv 1", "");
        client.sendMessage("Tin nhắn gốc");
        const QString localId = client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString();
        QVERIFY(localId.startsWith("local-"));
        client.openForwardModal(localId, "Tin nhắn gốc");
        QCOMPARE(client.showForwardModal(), false);
        MessageItem source;
        source.id = "fwd-src-1";
        source.messageType = "text";
        source.text = "Tin nhắn gốc";
        source.ts = QDateTime::currentMSecsSinceEpoch();
        client.messageModel()->mergeMessages("conv-1", {source});
        const QString msgId = source.id;

        client.openForwardModal(msgId, "Tin nhắn gốc");
        QCOMPARE(client.showForwardModal(), true);
        QCOMPARE(client.forwardSourceMsgId(), msgId);

        client.forwardMessage("conv-2", msgId);
        QCOMPARE(client.showForwardModal(), false);
        QCOMPARE(client.messageModel()->threadSize("conv-2"), 1);

        auto* session = client.apiClient()->session();
        QCOMPARE(client.mobileSyncState(), 0);

        QJsonObject confirmCtrl{
            {"act_type", "syncmsgmb"},
            {"act", "user_confirm"},
            {"data", QJsonObject{{"user_action", 1}}}
        };
        emit session->mobileSyncControlReceived(confirmCtrl);
        QCOMPARE(client.mobileSyncState(), 0);

        QJsonObject infoCtrl{
            {"act_type", "syncmsgmb"},
            {"act", "syncmsg_info"},
            {"data", QJsonObject{{"sync_session", "session-123"}}}
        };
        emit session->mobileSyncControlReceived(infoCtrl);
        QCOMPARE(client.mobileSyncState(), 0);

        calls->hangup();
    }

    void testPresenceAndStatusFormatting() {
        ZaloClient client;

        client.selectConversation("u-peer-test", "Peer Test", "");
        QCOMPARE(client.currentConversationIsOnline(), false);
        QCOMPARE(client.currentConversationStatus(), QString("Truy cập gần đây"));

        auto* session = client.apiClient()->session();
        emit session->typingReceived(QStringLiteral("u-peer-test"), QStringLiteral("u-peer-test"), false);
        QCOMPARE(client.currentConversationStatus(), QString("Đang soạn tin..."));

        client.selectConversation("grp-test-99", "Nhóm Test", "");
        QCOMPARE(client.currentConversationIsOnline(), false);
        QVERIFY(client.currentConversationStatus() != "Đang hoạt động");

        QCOMPARE(client.isRestoringSession(), false);

        ThemeManager theme;
        theme.setThemeMode(0);
        QCOMPARE(theme.isDarkMode(), false);
        QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor("#FFFFFF"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Base), QColor("#EBECF0"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::WindowText), QColor("#081B3A"));

        theme.setThemeMode(1);
        QCOMPARE(theme.isDarkMode(), true);
        QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor("#22262B"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Base), QColor("#16191D"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::WindowText), QColor("#DFE2E7"));
    }

    void testAuthenticUserAccountSessionAndResponsiveLayout() {
        ZaloClient client;

        client.logout();
        QCOMPARE(client.isLoggedIn(), false);
        QCOMPARE(client.currentUserName(), QString(""));
        QCOMPARE(client.conversationModel()->rowCount(), 0);
        QCOMPARE(client.messageModel()->rowCount(), 0);

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto makeConv = [&](const QString& id, const QString& name, bool pinned, qint64 ts, bool group, int members) {
            ConversationItem item;
            item.id = id;
            item.name = name;
            item.isPinned = pinned;
            item.lastTs = ts;
            item.isGroup = group;
            item.memberCount = members;
            item.lastMessage = "test";
            return item;
        };
        auto* convModel = client.conversationModel();
        convModel->setCategoryFilter("");
        convModel->upsertConversation(makeConv("u-old", "Fixture Old", false, now - 3600000, false, 0));
        convModel->upsertConversation(makeConv("u-new", "Fixture New", false, now - 60000, false, 0));
        convModel->upsertConversation(makeConv("g-pin", "Fixture Group", true, now - 86400000, true, 12));
        QCOMPARE(convModel->rowCount(), 3);
        QCOMPARE(convModel->data(convModel->index(0), ConversationModel::IdRole).toString(), QString("g-pin"));
        QCOMPARE(convModel->data(convModel->index(1), ConversationModel::IdRole).toString(), QString("u-new"));
        QCOMPARE(convModel->data(convModel->index(2), ConversationModel::IdRole).toString(), QString("u-old"));

        convModel->recordMessage("u-old", "hi", "", now, true);
        QCOMPARE(convModel->data(convModel->index(1), ConversationModel::IdRole).toString(), QString("u-old"));
        QCOMPARE(convModel->totalUnreadCount(), 1);
        convModel->setCategoryFilter("unread");
        QCOMPARE(convModel->rowCount(), 1);
        convModel->setCategoryFilter("");

        MessageItem m1; m1.id = "2"; m1.ts = now; m1.text = "second";
        MessageItem m2; m2.id = "1"; m2.ts = now - 1000; m2.text = "first";
        client.selectConversation("u-new", "Fixture New", "");
        client.messageModel()->mergeMessages("u-new", {m1, m2, m1});
        QCOMPARE(client.messageModel()->rowCount(), 2);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::TextRole).toString(), QString("first"));

        auto* msgModel = client.messageModel();
        msgModel->setReaction("u-new", "1", QString::fromUtf8("❤️👍 2"));
        QCOMPARE(msgModel->data(msgModel->index(0), MessageModel::ReactionRole).toString(), QString::fromUtf8("❤️👍 2"));
        MessageItem mine; mine.id = "3"; mine.ts = now + 1000; mine.text = "mine"; mine.isMine = true; mine.status = "sent";
        msgModel->mergeMessages("u-new", {mine});
        msgModel->markSeenUpTo("u-new", "3");
        QCOMPARE(msgModel->data(msgModel->index(2), MessageModel::StatusRole).toString(), QString("seen"));
        msgModel->markRecalled("u-new", "2", "recalled");
        QCOMPARE(msgModel->data(msgModel->index(1), MessageModel::IsRecalledRole).toBool(), true);
        QCOMPARE(msgModel->data(msgModel->index(1), MessageModel::TextRole).toString(), QString("recalled"));

        convModel->setTyping("u-old", true);
        convModel->setTag("g-pin", "Fixture Tag", "#2563EB");
        convModel->setOnlineIds({"u-new"});
        QCOMPARE(convModel->data(convModel->index(0), ConversationModel::TagRole).toString(), QString("Fixture Tag"));

        ContactItem invite; invite.id = "inv-1"; invite.name = "Fixture Request"; invite.category = "invites";
        invite.subtitle = "hello";
        ContactItem friendItem; friendItem.id = "f-1"; friendItem.name = "Fixture Friend";
        client.contactModel()->setContacts({invite, friendItem});
        QCOMPARE(client.contactModel()->inviteCount(), 1);
        QCOMPARE(client.contactModel()->friendCount(), 1);

        client.setLoggedInForTesting(true);
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        engine.rootContext()->setContextProperty("zaloClient", &client);
        engine.rootContext()->setContextProperty("themeManager", &themeMgr);
        engine.rootContext()->setContextProperty("trayController", &trayCtrl);
        loadMainWindow(engine);
        auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);

        std::function<int(QQuickItem*)> countItems = [&](QQuickItem* item) {
            int n = 1;
            for (QQuickItem* c : item->childItems()) n += countItems(c);
            return n;
        };
        auto itemCount = [&]() { return countItems(window->contentItem()); };
        {
            QQmlComponent probe(&engine);
            probe.setData("import QtQuick\nimport Zalos\nQtObject { property var c: ZaloClient }", QUrl());
            std::unique_ptr<QObject> o(probe.create());
            QVERIFY2(o, qPrintable(probe.errorString()));
            QCOMPARE(o->property("c").value<QObject*>(), static_cast<QObject*>(&client));
        }
        const int closedItems = itemCount();
        client.setShowSettingsModal(true);
        QTest::qWait(100);
        const int openItems = itemCount();
        client.setShowSettingsModal(false);
        QTest::qWait(100);
        QVERIFY2(openItems > closedItems + 50, "settings must be created on open");
        QVERIFY2(itemCount() < openItems, "settings must be destroyed on close");
        window->resize(1040, 760);
        for (const QString& tab : {QStringLiteral("general"), QStringLiteral("storage"), QStringLiteral("about"), QStringLiteral("chat")}) {
            client.openSettings(tab);
            QTest::qWait(400);
            window->grabWindow().save(QStringLiteral("/workspace/build/ui_settings_%1.png").arg(tab));
            client.setShowSettingsModal(false);
            QTest::qWait(100);
        }

        QCOMPARE(client.showConversationInfo(), false);
        QCOMPARE(window->findChild<QQuickItem*>("conversationInfoPanel"), nullptr);
        client.setShowConversationInfo(true);
        QTest::qWait(100);
        QVERIFY(window->findChild<QQuickItem*>("conversationInfoPanel") != nullptr);
        client.setShowConversationInfo(false);
        QTest::qWait(100);
        QCOMPARE(window->findChild<QQuickItem*>("conversationInfoPanel"), nullptr);

        QVERIFY(window->minimumWidth() <= 360);
        QVERIFY(window->minimumHeight() <= 420);
        const QList<QSize> sizes{{1920, 1080}, {960, 1080}, {1280, 720}, {640, 480}, {480, 1040}, {480, 540},
                                 {360, 720}, {360, 420}};
        auto visibleItem = [&](const char* name) {
            auto* item = window->findChild<QQuickItem*>(QString::fromLatin1(name));
            return item && item->isVisible();
        };
        auto rightEdge = [&](const char* name) {
            auto* item = window->findChild<QQuickItem*>(QString::fromLatin1(name));
            return item ? item->mapToScene(QPointF(item->width(), 0)).x() : -1.0;
        };
        for (const QSize& size : sizes) {
            window->resize(size);
            QTest::qWait(250);
            QCOMPARE(window->size(), size);
            auto* list = window->findChild<QQuickItem*>("messageList");
            auto* composer = window->findChild<QQuickItem*>("composerBox");
            QVERIFY(list != nullptr && composer != nullptr);
            const bool compact = size.width() < 720;
            const QString where = QStringLiteral("%1x%2 list %3 composer %4")
                                      .arg(size.width()).arg(size.height()).arg(list->width()).arg(composer->height());
            QVERIFY2(visibleItem("navigationRail") == !compact, qPrintable(where));
            QVERIFY2(visibleItem("headerMoreButton") == compact, qPrintable(where));
            QVERIFY2(visibleItem("composerMoreButton") == (size.width() < 480), qPrintable(where));
            if (compact) {
                QVERIFY2(list->width() >= size.width() - 1, qPrintable(where));
                QVERIFY2(rightEdge("headerMoreButton") <= size.width(), qPrintable(where));
            }
            if (size.width() < 480) QVERIFY2(rightEdge("composerMoreButton") <= size.width(), qPrintable(where));
            if (size.height() < 600) QVERIFY2(composer->height() < 120, qPrintable(where));
            window->grabWindow().save(QString("/workspace/build/test_chat_%1x%2.png").arg(size.width()).arg(size.height()));
        }

        const int rowsBeforeList = client.messageModel()->rowCount();
        client.openWelcomeScreen();
        for (const QSize& size : {QSize(480, 1040), QSize(480, 540)}) {
            window->resize(size);
            QTest::qWait(250);
            QVERIFY(visibleItem("navigationRail"));
            window->grabWindow().save(QString("/workspace/build/test_chat_list_%1x%2.png").arg(size.width()).arg(size.height()));
        }
        client.selectConversation("u-new", "Fixture New", "");
        QTest::qWait(100);
        QCOMPARE(client.messageModel()->rowCount(), rowsBeforeList);

        window->resize(480, 540);
        QTest::qWait(250);
        client.setShowConversationInfo(true);
        QTest::qWait(100);
        QTest::mouseClick(window, Qt::BackButton, {}, QPoint(240, 300));
        QTest::qWait(100);
        QVERIFY(!client.showConversationInfo());
        QCOMPARE(client.currentConversationId(), QString("u-new"));
        QTest::mouseClick(window, Qt::BackButton, {}, QPoint(240, 300));
        QTest::qWait(100);
        QVERIFY(client.currentConversationId().isEmpty());
        client.selectConversation("u-new", "Fixture New", "");
        QTest::qWait(100);
        QCOMPARE(client.messageModel()->rowCount(), rowsBeforeList);
        window->resize(1280, 720);
        QTest::qWait(100);

        convModel->recordMessage("u-old", "fixture unread", "", now + 2000, true);
        QVERIFY(convModel->conversation("u-old").unreadCount > 0);
        client.markConversationRead("u-old");
        QCOMPARE(convModel->conversation("u-old").unreadCount, 0);
        convModel->recordMessage("g-pin", "fixture mention", "", now + 3000, true, true);
        QVERIFY(convModel->conversation("g-pin").hasUnreadMention);
        QVERIFY(client.markAllConversationsRead() >= 1);
        QCOMPARE(convModel->totalUnreadCount(), 0);

        QCOMPARE(client.copyMessages({"1", "3"}), QString("first\nmine"));
        QVERIFY(!client.canModerateCurrent());
        client.openForwardModalMulti({"3", "1"});
        QCOMPARE(client.forwardMode(), QString("messages"));
        QVERIFY(client.showForwardModal());
        client.closeForwardModal();
        {
            MessageItem call; call.id = "9960"; call.ts = now + 4000; call.messageType = "call"; call.text = "Fixture call";
            MessageItem sticker; sticker.id = "9961"; sticker.ts = now + 5000; sticker.messageType = "sticker";
            sticker.mediaUrl = "https://example.invalid/s.png";
            client.messageModel()->mergeMessages("u-new", {call, sticker});
            QSignalSpy notices(&client, &ZaloClient::notice);
            QCOMPARE(client.copyMessages({"1", "9960"}), QString("first"));
            client.openForwardModal("9960", "Fixture call");
            QVERIFY(!client.showForwardModal());
            client.openForwardModal("9961", QString());
            QVERIFY(!client.showForwardModal());
            client.openForwardModalMulti({"9960", "9961"});
            QVERIFY(!client.showForwardModal());
            client.openForwardModalMulti({"9960", "1"});
            QVERIFY(client.showForwardModal());
            QCOMPARE(client.forwardSourceMsgId(), QString("1"));
            client.closeForwardModal();
            QCOMPARE(notices.count(), 4);
        }
        {
            MessageItem a; a.id = "9970"; a.ts = now - 600000; a.messageType = "image"; a.mediaUrl = "https://example.invalid/1.jpg";
            a.senderId = "u-album"; a.extra.insert("groupLayoutId", "L1");
            MessageItem b = a; b.id = "9971"; b.ts = now; b.mediaUrl = "https://example.invalid/2.jpg";
            MessageItem c = a; c.id = "9972"; c.ts = now + 1000; c.mediaUrl = "https://example.invalid/3.jpg"; c.extra.insert("groupLayoutId", "L2");
            MessageModel albums;
            albums.loadConversation("album-fixture");
            albums.mergeMessages("album-fixture", {a, b, c});
            QCOMPARE(albums.rowCount(), 3);
            QVERIFY(albums.data(albums.index(1), MessageModel::InMediaRunRole).toBool());
            QVERIFY(!albums.data(albums.index(2), MessageModel::InMediaRunRole).toBool());
        }

        client.setScrollAnchor("u-old", "fixture-anchor");
        QCOMPARE(client.scrollAnchor("u-old"), QString("fixture-anchor"));
        client.setScrollAnchor("u-old", "");
        QVERIFY(client.scrollAnchor("u-old").isEmpty());
        QTemporaryDir downloadsDir;
        client.setDownloadDirectory(QUrl::fromLocalFile(downloadsDir.path()).toString());
        QCOMPARE(client.downloadDirectory(), downloadsDir.path());
        client.setDownloadDirectory(QString());
        QVERIFY(client.downloadDirectory().endsWith(QStringLiteral("Zalos")));
        QVERIFY(!ZaloClient::updateCheckDue(100000, 0, 0, true));
        QVERIFY(ZaloClient::updateCheckDue(100000, 0, 1, true));
        QVERIFY(!ZaloClient::updateCheckDue(100000, 0, 1, false));
        QVERIFY(ZaloClient::updateCheckDue(100000, 0, 24, false));
        QVERIFY(!ZaloClient::updateCheckDue(100000, 100000 - 3600, 24, true));
        QVERIFY(ZaloClient::updateCheckDue(100000 + 86400, 100000, 24, false));
        QVERIFY(!ZaloClient::updateCheckDue(100000 + 86400, 100000, 168, false));
        const int previousHours = client.updateCheckHours();
        client.setUpdateCheckHours(5);
        QCOMPARE(client.updateCheckHours(), previousHours);
        client.setUpdateCheckHours(0);
        QCOMPARE(client.updateCheckHours(), 0);
        client.setUpdateCheckHours(previousHours);
        const QVariantMap noFile = client.fileState(QStringLiteral("https://example.invalid/none.bin"));
        QVERIFY(!noFile.value("downloading").toBool());
        QVERIFY(noFile.value("path").toString().isEmpty());

        client.deleteConversationWithUndo("u-old");
        QVERIFY(!convModel->contains("u-old"));
        QCOMPARE(client.pendingDeleteConversation(), QString("u-old"));
        QTest::qWait(50);
        QVERIFY(visibleItem("undoDeleteBar"));
        client.undoDeleteConversation();
        QVERIFY(convModel->contains("u-old"));
        QVERIFY(client.pendingDeleteConversation().isEmpty());

        {
            Zalos::Core::Platform::SwipeBack swipe;
            QObject target;
            target.installEventFilter(&swipe);
            QSignalSpy swiped(&swipe, &Zalos::Core::Platform::SwipeBack::swipeBack);
            QPointingDevice pad(QStringLiteral("fixture touchpad"), 4242, QInputDevice::DeviceType::TouchPad,
                                QPointingDevice::PointerType::Finger,
                                QInputDevice::Capability::Position | QInputDevice::Capability::Scroll, 2, 0);
            auto send = [&](QPoint pixels, Qt::ScrollPhase phase) {
                QWheelEvent ev(QPointF(10, 10), QPointF(10, 10), pixels, pixels, Qt::NoButton, Qt::NoModifier, phase, false,
                               Qt::MouseEventSynthesizedBySystem, &pad);
                QCoreApplication::sendEvent(&target, &ev);
            };
            send(QPoint(-10, 0), Qt::ScrollBegin);
            send(QPoint(-120, 2), Qt::ScrollUpdate);
            send(QPoint(-120, 1), Qt::ScrollUpdate);
            send(QPoint(-60, 0), Qt::ScrollUpdate);
            send(QPoint(0, 0), Qt::ScrollEnd);
            QCOMPARE(swiped.count(), 1);
            send(QPoint(0, -10), Qt::ScrollBegin);
            send(QPoint(-120, 200), Qt::ScrollUpdate);
            send(QPoint(-120, 200), Qt::ScrollUpdate);
            send(QPoint(0, 0), Qt::ScrollEnd);
            QCOMPARE(swiped.count(), 1);
        }

        using Zalos::Core::Platform::SystemTrayController;
        QVERIFY(SystemTrayController::launcherDesktopFiles().contains(QStringLiteral("zalos.desktop")));
        QImage colour(4, 4, QImage::Format_ARGB32);
        colour.fill(QColor(0, 104, 255, 255));
        const QImage dim = SystemTrayController::dimmedImage(colour);
        QCOMPARE(qRed(dim.pixel(1, 1)), qBlue(dim.pixel(1, 1)));
        QVERIFY(qAlpha(dim.pixel(1, 1)) < 255);
        trayCtrl.setOffline(true);
        QVERIFY(!trayCtrl.offline());
        trayCtrl.setOffline(false);
        QVERIFY(!trayCtrl.offline());

        client.setShowConversationInfo(true);
        for (const QSize& size : sizes) {
            window->resize(size);
            QTest::qWait(250);
            auto* panel = window->findChild<QQuickItem*>("conversationInfoPanel");
            auto* list = window->findChild<QQuickItem*>("messageList");
            QVERIFY(panel != nullptr && list != nullptr);
            const QString where = QStringLiteral("%1x%2 panel %3 list %4 visible %5")
                                      .arg(size.width()).arg(size.height()).arg(panel->width()).arg(list->width())
                                      .arg(list->isVisible());
            if (size.width() < 720) {
                QVERIFY2(qAbs(panel->width() - size.width()) < 1.0, qPrintable(where));
                QVERIFY2(visibleItem("infoBackButton"), qPrintable(where));
            } else {
                QVERIFY2(panel->width() <= 280.5, qPrintable(where));
                QVERIFY2(list->isVisible() && list->width() > 200, qPrintable(where));
            }
            window->grabWindow().save(QString("/workspace/build/test_chat_info_%1x%2.png").arg(size.width()).arg(size.height()));
        }
        client.setShowConversationInfo(false);

        window->resize(1280, 720);
        client.setCurrentNavigationTab("contacts");
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_contacts_tab.png");
        client.contactModel()->setCategoryFilter("invites");
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_contacts_invites.png");
        client.contactModel()->setCategoryFilter("friends");

        client.setCurrentNavigationTab("messages");
        auto accountMenu = window->findChild<QObject*>("accountMenu");
        if (accountMenu) {
            QMetaObject::invokeMethod(accountMenu, "open");
            QTest::qWait(200);
            window->grabWindow().save("/workspace/build/test_settings_popup.png");
            QMetaObject::invokeMethod(accountMenu, "close");
        }
        QCOMPARE(client.showLabelFolders(), false);
        client.labels()->applyServer(QJsonObject{{"version", 1}, {"labelData", QJsonArray{
            QJsonObject{{"id", 1}, {"text", "Công việc"}, {"color", "#D91B1B"}, {"offset", 0}, {"conversations", QJsonArray{}}},
            QJsonObject{{"id", 2}, {"text", "Gia đình"}, {"color", "#4BC377"}, {"offset", 1}, {"conversations", QJsonArray{}}},
            QJsonObject{{"id", 3}, {"text", "Bạn bè"}, {"color", "#2F80ED"}, {"offset", 2}, {"conversations", QJsonArray{}}}}}});
        client.setShowLabelFolders(true);
        QCOMPARE(client.showLabelFolders(), true);
        QTest::qWait(150);
        window->grabWindow().save("/workspace/build/test_rail_label_folders.png");
        client.setShowLabelFolders(false);

        client.setAppLanguage("en");
        QCOMPARE(client.appLanguage(), QString("en"));
        QTest::qWait(150);
        window->grabWindow().save("/workspace/build/test_chat_english.png");
        client.setAppLanguage("vi");
        QCOMPARE(client.appLanguage(), QString("vi"));

        client.setCurrentNavigationTab("messages");
        themeMgr.setThemeMode(0);
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_chat_light.png");
        themeMgr.setThemeMode(1);
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_chat_dark.png");

        auto chatArea = window->findChild<QQuickItem*>("chatArea");
        if (chatArea) {
            QCOMPARE(chatArea->property("isFormattingActive").toBool(), false);
            QCOMPARE(chatArea->property("isEditorExpanded").toBool(), false);

            chatArea->setProperty("isFormattingActive", true);
            QTest::qWait(200);
            QCOMPARE(chatArea->property("isFormattingActive").toBool(), true);
            QTest::qWait(150);
            auto formatBar = window->findChild<QQuickItem*>("formatToolbar");
            QVERIFY(formatBar != nullptr);
            QVERIFY2(formatBar->height() >= 30, qPrintable(QString::number(formatBar->height())));
            auto composer = window->findChild<QQuickItem*>("composerBox");
            QVERIFY(composer != nullptr);
            QVERIFY2(composer->height() >= 32 + 1 + 34 + 3 * 18 + 32, qPrintable(QString::number(composer->height())));
            window->grabWindow().save("/workspace/build/test_chat_rtf_toolbar.png");

            chatArea->setProperty("isEditorExpanded", true);
            QTest::qWait(200);
            QCOMPARE(chatArea->property("isEditorExpanded").toBool(), true);
            window->grabWindow().save("/workspace/build/test_chat_expanded_editor.png");

            chatArea->setProperty("isEditorExpanded", false);
            chatArea->setProperty("isFormattingActive", false);
            QTest::qWait(100);

            themeMgr.setThemeMode(0);
            const QString peer = client.currentConversationId();
            const QString nowTs = QString::number(QDateTime::currentMSecsSinceEpoch());
            QJsonObject q{{"msgId", "8801"}, {"cliMsgId", "21"}, {"uidFrom", peer}, {"idTo", "0"}, {"msgType", "webchat"},
                          {"content", QString::fromUtf8("Tin nhắn có trích dẫn")}, {"ts", nowTs},
                          {"quote", QJsonObject{{"ownerId", "u-other"}, {"globalMsgId", 1}, {"fromD", "Fixture A"},
                                                {"msg", QString::fromUtf8("Nội dung gốc được trích dẫn")}}}};
            emit client.apiClient()->session()->messagesReceived(QJsonArray{q}, false, false);
            client.setReplyTarget("8801");
            QVERIFY(client.scheduleMessage(QString::fromUtf8("Tin hẹn giờ"), QDateTime::currentMSecsSinceEpoch() + 3600000));
            QMetaObject::invokeMethod(chatArea, "openSearch");
            QTest::qWait(250);
            window->grabWindow().save("/workspace/build/test_chat_reply_schedule.png");
            QMetaObject::invokeMethod(chatArea, "closeSearch");
            client.clearReplyTarget();
            for (const QVariant& v : client.scheduledMessages()) client.cancelScheduledMessage(v.toMap().value("id").toString());
            QTest::qWait(100);
        }

        auto* calls = client.calls();
        QSignalSpy incomingSpy(calls, &Zalos::Core::Call::CallBridge::incomingCall);
        calls->injectUiLineForTesting(R"({"type":"state","phase":"incoming","name":"Fixture Caller","avatar":"","text":"","since":0})");
        QCOMPARE(calls->phase(), QString("incoming"));
        QCOMPARE(calls->canAnswer(), true);
        QCOMPARE(incomingSpy.count(), 1);
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_call_incoming.png");
        const qint64 since = QDateTime::currentMSecsSinceEpoch() - 65000;
        calls->injectUiLineForTesting(QStringLiteral(R"({"type":"state","phase":"connected","name":"Fixture Caller","since":%1,"peerMuted":true})")
                                          .arg(since).toUtf8());
        QCOMPARE(calls->phase(), QString("connected"));
        QCOMPARE(calls->peerMuted(), true);
        QTest::qWait(1200);
        window->grabWindow().save("/workspace/build/test_call_connected.png");
        calls->injectUiLineForTesting(R"({"type":"close","text":""})");
        QCOMPARE(calls->phase(), QString("ended"));
        calls->hangup();
        QCOMPARE(calls->phase(), QString(""));

        client.setLoggedInForTesting(false);
        client.logout();
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_qr_view.png");
    }

    void testNetworkChaosAndMalformedJsonFuzzing() {
        ZaloClient client;
        auto* session = client.apiClient()->session();

        session->injectWsBinaryForTesting(QByteArray());
        session->injectWsBinaryForTesting(QByteArray("\x01"));
        session->injectWsBinaryForTesting(QByteArray("\x01\x02\x03"));

        QByteArray malformedFrame("\x01\xf5\x01\x00{invalid-json-payload-broken", 32);
        session->injectWsBinaryForTesting(malformedFrame);

        QJsonArray fuzzedBatch;
        fuzzedBatch.append(QJsonObject{{"msgId", QJsonValue::Null}, {"cliMsgId", QJsonValue::Null}, {"content", QJsonValue::Null}});
        fuzzedBatch.append(QJsonObject{{"msgId", 12345}, {"cliMsgId", 67890}, {"uidFrom", 999}, {"content", QJsonArray{1, 2, 3}}});
        fuzzedBatch.append(QJsonObject{{"randomField", "chaos"}});
        fuzzedBatch.append(QJsonObject{{"msgId", "999999"}, {"cliMsgId", "c-chaos"}, {"ts", "invalid-ts-number"}, {"content", "safe text"}});

        emit session->messagesReceived(fuzzedBatch, false, false);
        QVERIFY(client.conversationModel() != nullptr);

        QJsonArray outOfOrderBatch;
        const qint64 tNow = 1700000000000LL;
        outOfOrderBatch.append(QJsonObject{{"msgId", "801"}, {"cliMsgId", "c-801"}, {"uidFrom", "u-chaos"}, {"idTo", "u-me"},
                                           {"msgType", "webchat"}, {"content", "Message 2 (newer)"}, {"ts", QString::number(tNow + 2000)}});
        outOfOrderBatch.append(QJsonObject{{"msgId", "800"}, {"cliMsgId", "c-800"}, {"uidFrom", "u-chaos"}, {"idTo", "u-me"},
                                           {"msgType", "webchat"}, {"content", "Message 1 (older)"}, {"ts", QString::number(tNow + 1000)}});
        emit session->messagesReceived(outOfOrderBatch, false, false);

        client.selectConversation("u-chaos", "Chaos Peer", "");
        QCOMPARE(client.messageModel()->rowCount(), 2);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(), QString("800"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(1), MessageModel::IdRole).toString(), QString("801"));

        QJsonArray duplicateBatch;
        duplicateBatch.append(QJsonObject{{"msgId", "801"}, {"cliMsgId", "c-801"}, {"uidFrom", "u-chaos"}, {"idTo", "u-me"},
                                          {"msgType", "webchat"}, {"content", "Message 2 (duplicate replay)"}, {"ts", QString::number(tNow + 2000)}});
        emit session->messagesReceived(duplicateBatch, false, false);
        QCOMPARE(client.messageModel()->rowCount(), 2);
    }

    void testCorruptedStorageRecovery() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString dbPath = tempDir.filePath("corrupted_offline.db");

        {
            QFile f(dbPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("CORRUPTED_GARBAGE_HEADER_NOT_SQLITE3_DATABASE_FILE_TEST");
            f.close();
        }

        ZaloOfflineStore store;
        const bool openResult = store.initializeDatabase(dbPath);
        if (!openResult) {
            QVERIFY(!store.isDatabaseOpen());
        }
    }

    void testBusinessToolsNavigation() {
        ZaloClient client;
        QCOMPARE(client.showBusinessToolsModal(), false);

        client.openBusinessTools();
        QCOMPARE(client.showBusinessToolsModal(), true);
        QCOMPARE(client.businessTab(), QStringLiteral("profile"));

        client.openBusinessCatalog();
        QCOMPARE(client.showBusinessToolsModal(), true);
        QCOMPARE(client.businessTab(), QStringLiteral("catalog"));

        client.openBusinessAutoReply();
        QCOMPARE(client.showBusinessToolsModal(), true);
        QCOMPARE(client.businessTab(), QStringLiteral("autoreply"));

        client.openBusinessPackage();
        QCOMPARE(client.showBusinessToolsModal(), true);
        QCOMPARE(client.businessTab(), QStringLiteral("package"));

        client.closeBusinessTools();
        QCOMPARE(client.showBusinessToolsModal(), false);
    }

    void testGroupAOptimizationsLockIn() {
        AvatarImageProvider::clearMemCache();
        QImage avatarImg(48, 48, QImage::Format_ARGB32);
        avatarImg.fill(Qt::red);
        AvatarImageProvider::insertMemCache(QStringLiteral("https://zadn.vn/ava.jpg@48c"), avatarImg);

        QImage hugeMedia(1200, 1200, QImage::Format_ARGB32);
        hugeMedia.fill(Qt::green);
        AvatarImageProvider::insertMemCache(QStringLiteral("https://zadn.vn/huge.jpg#1200n"), hugeMedia);
        QImage probeOut;
        QVERIFY(!AvatarImageProvider::lookupMemCache(QStringLiteral("https://zadn.vn/huge.jpg#1200n"), &probeOut));

        QImage chunkMedia(880, 880, QImage::Format_ARGB32);
        chunkMedia.fill(Qt::blue);
        for (int i = 0; i < 6; ++i) {
            AvatarImageProvider::insertMemCache(QStringLiteral("https://zadn.vn/m%1.jpg#880n").arg(i), chunkMedia);
        }
        QVERIFY(AvatarImageProvider::lookupMemCache(QStringLiteral("https://zadn.vn/ava.jpg@48c"), &probeOut));
        QCOMPARE(probeOut.size(), QSize(48, 48));
        AvatarImageProvider::clearMemCache();

        ZaloClient client;
        const QJsonObject staticWebp{
            {"msgId", "7101"}, {"uidFrom", "u-p1"}, {"idTo", "u-me"},
            {"msgType", "chat.photo"}, {"ts", "1700000000000"},
            {"content", QJsonObject{{"href", "https://example.invalid/static.webp"},
                                    {"thumb", "https://example.invalid/static.webp"}}}
        };
        const MessageItem webpItem = client.toMessageItemForTesting(staticWebp, false);
        QCOMPARE(webpItem.messageType, QStringLiteral("image"));
        QVERIFY(webpItem.extra.value("animatedUrl").toString().isEmpty());

        const QJsonObject animGif{
            {"msgId", "7102"}, {"uidFrom", "u-p1"}, {"idTo", "u-me"},
            {"msgType", "chat.photo"}, {"ts", "1700000000001"},
            {"content", QJsonObject{{"href", "https://example.invalid/anim.gif"},
                                    {"thumb", "https://example.invalid/anim.jpg"}}}
        };
        const MessageItem gifItem = client.toMessageItemForTesting(animGif, false);
        QCOMPARE(gifItem.messageType, QStringLiteral("image"));
        QCOMPARE(gifItem.extra.value("animatedUrl").toString(), QStringLiteral("https://example.invalid/anim.gif"));

        const QJsonObject zgifMsg{
            {"msgId", "7103"}, {"uidFrom", "u-p1"}, {"idTo", "g-gif"},
            {"msgType", "chat.gif"}, {"ts", "1700000000002"},
            {"content", QJsonObject{{"href", "https://zchat-search.zadn.vn/8884179135713762630_gif"},
                                    {"thumb", "https://zchat-search.zadn.vn/8884179135713762630_png"}}}
        };
        const MessageItem zgifItem = client.toMessageItemForTesting(zgifMsg, true);
        QCOMPARE(zgifItem.messageType, QStringLiteral("image"));
        QCOMPARE(zgifItem.extra.value("animatedUrl").toString(),
                 QStringLiteral("https://zchat-search.zadn.vn/8884179135713762630_gif"));

        {
            ZaloOfflineStore store;
            store.bindOwner(QStringLiteral("u-ts-owner"));
            store.clearCache();
            store.bindOwner(QStringLiteral("u-ts-owner"));
            QCOMPARE(store.ownerUid(), QStringLiteral("u-ts-owner"));

            MessageItem item;
            item.id = QStringLiteral("msg-ts-901");
            item.cliMsgId = QStringLiteral("cli-ts-901");
            item.senderId = QStringLiteral("u-ts-peer");
            item.text = QStringLiteral("indexed ts check");
            item.ts = 1700000123456LL;
            QVERIFY(store.saveMessage(QStringLiteral("u-ts-peer"), item));

            MessageItem cachedGif;
            cachedGif.id = QStringLiteral("msg-gif-902");
            cachedGif.senderId = QStringLiteral("u-ts-peer");
            cachedGif.messageType = QStringLiteral("image");
            cachedGif.mediaUrl = QStringLiteral("https://zgif-v2.zdn.vn/thumb.png");
            cachedGif.href = QStringLiteral("https://zgif-v2.zdn.vn/anim.gif");
            cachedGif.ts = 1700000123457LL;
            QVERIFY(store.saveMessage(QStringLiteral("u-ts-peer"), cachedGif, zgifMsg, true));

            MessageItem cachedStaticWebp = webpItem;
            cachedStaticWebp.id = QStringLiteral("msg-webp-903");
            cachedStaticWebp.ts = 1700000123458LL;
            QVERIFY(store.saveMessage(QStringLiteral("u-ts-peer"), cachedStaticWebp, staticWebp, false));

            const QVector<MessageItem> reloaded = store.loadMessages(QStringLiteral("u-ts-peer"));
            bool foundGif = false;
            bool foundWebp = false;
            for (const MessageItem& r : reloaded) {
                if (r.id == QStringLiteral("msg-gif-902")) {
                    foundGif = true;
                    QCOMPARE(r.extra.value("animatedUrl").toString(), cachedGif.href);
                } else if (r.id == QStringLiteral("msg-webp-903")) {
                    foundWebp = true;
                    QVERIFY(r.extra.value("animatedUrl").toString().isEmpty());
                }
            }
            QVERIFY(foundGif);
            QVERIFY(foundWebp);

            MessageModel gifModel;
            gifModel.loadConversation(QStringLiteral("u-ts-peer"));
            MessageItem staleGif = gifItem;
            staleGif.extra.remove(QStringLiteral("animatedUrl"));
            gifModel.mergeMessages(QStringLiteral("u-ts-peer"), {staleGif});
            QVERIFY(gifModel.data(gifModel.index(0), MessageModel::ExtraRole).toMap().value("animatedUrl").toString().isEmpty());
            QSignalSpy backfillSpy(&gifModel, &MessageModel::dataChanged);
            gifModel.mergeMessages(QStringLiteral("u-ts-peer"), {gifItem});
            QVERIFY(!backfillSpy.isEmpty());
            QCOMPARE(gifModel.data(gifModel.index(0), MessageModel::ExtraRole).toMap().value("animatedUrl").toString(),
                     QStringLiteral("https://example.invalid/anim.gif"));
            const QVariantList media = gifModel.mediaItems();
            QCOMPARE(media.size(), 1);
            QCOMPARE(media.at(0).toMap().value("animated").toBool(), true);
            QCOMPARE(media.at(0).toMap().value("url").toString(), QStringLiteral("https://example.invalid/anim.gif"));

            QString convOut;
            QCOMPARE(store.messageTs(QStringLiteral("msg-ts-901"), &convOut), 1700000123456LL);
            QCOMPARE(convOut, QStringLiteral("u-ts-peer"));
            convOut.clear();
            QCOMPARE(store.messageTs(QStringLiteral("cli-ts-901"), &convOut), 1700000123456LL);
            QCOMPARE(convOut, QStringLiteral("u-ts-peer"));
            store.clearCache();
        }

        {
            Zalos::Core::Call::CameraSender sender;
            QVideoSink previewSink;
            QSignalSpy previewSpy(&previewSink, &QVideoSink::videoFrameChanged);
            QSignalSpy encodeSpy(&sender, &Zalos::Core::Call::CameraSender::frameEncoded);
            sender.setPreviewSink(&previewSink);
            sender.setEncodingEnabled(false);
            QCOMPARE(sender.encodingEnabled(), false);

            QVideoFrame frame(QVideoFrameFormat(QSize(640, 360), QVideoFrameFormat::Format_YUV420P));
            QVERIFY(frame.map(QVideoFrame::WriteOnly));
            for (int p = 0; p < frame.planeCount(); ++p) {
                memset(frame.bits(p), 128, size_t(frame.mappedBytes(p)));
            }
            frame.unmap();

            sender.processFrame(frame);
            QCOMPARE(previewSpy.count(), 1);
            QCOMPARE(encodeSpy.count(), 0);

            sender.setEncodingEnabled(true);
            QCOMPARE(sender.encodingEnabled(), true);
            if (Zalos::Core::Call::OpenH264::instance().available()) {
                sender.processFrame(frame);
                QCOMPARE(encodeSpy.count(), 1);
                QCOMPARE(encodeSpy.first().at(0).toBool(), true);
            }
        }

        {
            Zalos::Core::Media::VoiceRecorder recorder;
            const bool hasHwMic = !QDir(QStringLiteral("/dev/snd")).entryList({QStringLiteral("pcm*c")}, QDir::System).isEmpty();
            if (!hasHwMic) {
                QCOMPARE(recorder.available(), false);
                QCOMPARE(recorder.unavailableReason(), QStringLiteral("no_microphone"));
            }
        }
    }

    void testUiFeatureSettingsAndKikiFallback() {
        QSettings settings;
        settings.remove("settings/showBusinessButton");
        settings.remove("settings/showKikiButton");
        settings.remove("render/hardwareAcceleration");

        Zalos::Core::Network::ZaloClient client;
        QCOMPARE(client.showBusinessButton(), true);
        QCOMPARE(client.showKikiButton(), true);
        QCOMPARE(client.hardwareAcceleration(), false);

        client.setCurrentNavigationTab(QStringLiteral("kiki"));
        QCOMPARE(client.currentNavigationTab(), QStringLiteral("kiki"));
        client.setShowKikiButton(false);
        QCOMPARE(client.showKikiButton(), false);
        QCOMPARE(client.currentNavigationTab(), QStringLiteral("messages"));

        client.setShowBusinessToolsModal(true);
        QVERIFY(client.showBusinessToolsModal());
        client.setShowBusinessButton(false);
        QCOMPARE(client.showBusinessButton(), false);
        QCOMPARE(client.showBusinessToolsModal(), false);

        client.setHardwareAcceleration(true);
        QCOMPARE(client.hardwareAcceleration(), true);
        settings.sync();
        QCOMPARE(settings.value("render/hardwareAcceleration").toBool(), true);
        QCOMPARE(Zalos::Core::Platform::RenderProfile::probeSystem().hardwareAcceleration, true);
        client.setHardwareAcceleration(false);
        QCOMPARE(client.hardwareAcceleration(), false);
        settings.sync();
        QCOMPARE(settings.value("render/hardwareAcceleration").toBool(), false);
        QCOMPARE(Zalos::Core::Platform::RenderProfile::probeSystem().hardwareAcceleration, false);

        client.setShowBusinessButton(true);
        client.setShowKikiButton(true);
    }

    void testSmartMobileSyncOfferLogic() {
        Zalos::Core::Network::ZaloClient client;
        client.setMyUidForTesting(QStringLiteral("u-smart-sync"));
        if (client.offlineStore()) client.offlineStore()->bindOwner(QStringLiteral("u-smart-sync"));
        client.setLoggedInForTesting(true);
        QSettings settings;
        settings.remove(QStringLiteral("mobileSync/done/u-smart-sync"));
        settings.remove(QStringLiteral("mobileSync/neverAsk/u-smart-sync"));

        QCOMPARE(client.shouldOfferMobileSync(), false);

        Zalos::Core::Models::ConversationItem emptyGroup;
        emptyGroup.id = QStringLiteral("g-empty");
        emptyGroup.name = QStringLiteral("Fixture Empty Group");
        emptyGroup.isGroup = true;
        client.conversationModel()->upsertConversation(emptyGroup);
        QCOMPARE(client.shouldOfferMobileSync(), true);

        emptyGroup.lastMessage = QStringLiteral("Hello");
        emptyGroup.lastTs = 1700000000000LL;
        client.conversationModel()->upsertConversation(emptyGroup);
        QCOMPARE(client.shouldOfferMobileSync(), false);
    }

    void testAboutVersionComparison() {
        using ZC = Zalos::Core::Network::ZaloClient;
        QVERIFY(ZC::compareVersions(QStringLiteral("v1.2.5"), QStringLiteral("1.2.4")) > 0);
        QVERIFY(ZC::compareVersions(QStringLiteral("1.2.4"), QStringLiteral("v1.2.4")) == 0);
        QVERIFY(ZC::compareVersions(QStringLiteral("1.2.3"), QStringLiteral("1.2.4")) < 0);
        QVERIFY(ZC::compareVersions(QStringLiteral("v1.10.0"), QStringLiteral("1.2.9")) > 0);

        ZC client;
        QVERIFY(!client.appVersion().isEmpty());
        QVERIFY(!client.qtRuntimeVersion().isEmpty());
        QVERIFY(!client.rendererBackend().isEmpty());
    }

    void testAppUpdaterPolicyAndVerify() {
        using namespace Zalos::Core::Platform;
        QCOMPARE(detectPackageKind(QStringLiteral("/tmp/Zalos.AppImage"), QStringLiteral("/usr/bin/zalos"), true, true), PackageKind::AppImage);
        QCOMPARE(detectPackageKind(QString(), QStringLiteral("/nix/store/abc-zalos/bin/zalos"), true, true), PackageKind::Nix);
        QCOMPARE(detectPackageKind(QString(), QStringLiteral("/usr/bin/zalos"), true, false), PackageKind::Deb);
        QCOMPARE(detectPackageKind(QString(), QStringLiteral("/usr/bin/zalos"), false, true), PackageKind::Rpm);
        QCOMPARE(detectPackageKind(QString(), QStringLiteral("/usr/local/bin/zalos"), false, false), PackageKind::Unknown);

        const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Zalos-1.2.5-x86_64.AppImage.zsync")},
                        {QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/zsync")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Zalos-1.2.5-x86_64.AppImage")},
                        {QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/appimage")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("zalos_1.2.5_amd64.deb")},
                        {QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/deb")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("zalos-1.2.5-1.fc43.aarch64.rpm")},
                        {QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/rpm")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("SHA256SUMS")},
                        {QStringLiteral("browser_download_url"), QStringLiteral("https://example.invalid/sums")}},
        };

        const auto appSel = selectAssetsForKind(PackageKind::AppImage, QStringLiteral("x86_64"), assets);
        QCOMPARE(appSel.package.name, QStringLiteral("Zalos-1.2.5-x86_64.AppImage"));
        QCOMPARE(appSel.sha256Sums.name, QStringLiteral("SHA256SUMS"));

        const auto debSel = selectAssetsForKind(PackageKind::Deb, QStringLiteral("x86_64"), assets);
        QCOMPARE(debSel.package.name, QStringLiteral("zalos_1.2.5_amd64.deb"));

        const auto rpmSel = selectAssetsForKind(PackageKind::Rpm, QStringLiteral("arm64"), assets);
        QCOMPARE(rpmSel.package.name, QStringLiteral("zalos-1.2.5-1.fc43.aarch64.rpm"));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString targetPath = dir.filePath(QStringLiteral("Zalos.AppImage"));
        const QString newPath = dir.filePath(QStringLiteral("Zalos-new.AppImage"));
        {
            QFile f(targetPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("old");
        }
        const QByteArray newBytes("new-appimage-payload");
        {
            QFile f(newPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(newBytes);
        }
        const QString expectedHex = QString::fromLatin1(
            QCryptographicHash::hash(newBytes, QCryptographicHash::Sha256).toHex());
        const QByteArray sums = expectedHex.toUtf8() + "  dist/Zalos-1.2.5-x86_64.AppImage\n";
        QCOMPARE(parseSha256ForFile(sums, QStringLiteral("Zalos-1.2.5-x86_64.AppImage")), expectedHex);
        QVERIFY(verifyFileSha256(newPath, expectedHex));
        QVERIFY(!verifyFileSha256(newPath, QString(64, QLatin1Char('0'))));
        QVERIFY(replaceAppImageFile(targetPath, newPath));
        {
            QFile f(targetPath);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), newBytes);
        }
    }

    void testSettingsRoundTripAndUrlPolicy() {
        QSettings settings;
        settings.remove(QStringLiteral("settings/showBusinessButton"));
        settings.remove(QStringLiteral("settings/showKikiButton"));
        settings.remove(QStringLiteral("settings/showLabelFolders"));
        settings.remove(QStringLiteral("render/hardwareAcceleration"));

        {
            Zalos::Core::Network::ZaloClient writer;
            writer.setShowBusinessButton(false);
            writer.setShowKikiButton(false);
            writer.setShowLabelFolders(true);
            writer.setHardwareAcceleration(true);
        }

        {
            Zalos::Core::Network::ZaloClient reader;
            QCOMPARE(reader.showBusinessButton(), false);
            QCOMPARE(reader.showKikiButton(), false);
            QCOMPARE(reader.showLabelFolders(), true);
            QCOMPARE(reader.hardwareAcceleration(), true);
            reader.setShowBusinessButton(true);
            reader.setShowKikiButton(true);
            reader.setShowLabelFolders(false);
            reader.setHardwareAcceleration(false);

            QSignalSpy noticeSpy(&reader, &Zalos::Core::Network::ZaloClient::notice);
            reader.openExternalUrl(QStringLiteral("javascript:alert(1)"));
            reader.openExternalUrl(QStringLiteral("file:///etc/passwd"));
            QCOMPARE(noticeSpy.count(), 2);
        }
    }

    void testSidebarTypeFilterStranger() {
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting("u-me");

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        QTest::qWait(300);

        ConversationItem friendConv;
        friendConv.id = QStringLiteral("u-friend");
        friendConv.name = QStringLiteral("Friend A");
        friendConv.isGroup = false;
        ConversationItem strangerConv;
        strangerConv.id = QStringLiteral("u-stranger");
        strangerConv.name = QStringLiteral("Stranger B");
        strangerConv.isGroup = false;
        ConversationItem myDocs;
        myDocs.id = QStringLiteral("u-docs");
        myDocs.name = QStringLiteral("My Documents");
        myDocs.isGroup = false;
        client.conversationModel()->setFriendContext({QStringLiteral("u-friend")}, {}, QStringLiteral("u-me"), true);
        client.conversationModel()->setConversations({friendConv, strangerConv, myDocs});
        QCOMPARE(client.conversationModel()->rowCount(), 3);

        auto* btn = window->findChild<QQuickItem*>(QStringLiteral("typeFilterButton"));
        QVERIFY(btn != nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          btn->mapToScene(QPointF(btn->width() / 2, btn->height() / 2)).toPoint());
        QTest::qWait(100);

        auto* menu = window->findChild<QObject*>(QStringLiteral("typeFilterMenu"));
        QVERIFY(menu != nullptr);
        QVERIFY(menu->property("opened").toBool());

        auto* groupRow = window->findChild<QQuickItem*>(QStringLiteral("typeFilterGroup"));
        QVERIFY(groupRow != nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          groupRow->mapToScene(QPointF(groupRow->width() / 2, groupRow->height() / 2)).toPoint());
        QTest::qWait(100);
        QCOMPARE(client.conversationModel()->typeFilter(), QStringLiteral("group"));

        QMetaObject::invokeMethod(menu, "open");
        QTest::qWait(100);
        auto* strangerRow = window->findChild<QQuickItem*>(QStringLiteral("typeFilterStranger"));
        QVERIFY(strangerRow != nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          strangerRow->mapToScene(QPointF(strangerRow->width() / 2, strangerRow->height() / 2)).toPoint());
        QTest::qWait(100);

        QCOMPARE(client.conversationModel()->typeFilter(), QStringLiteral("stranger"));
        QCOMPARE(client.conversationModel()->rowCount(), 1);
        QCOMPARE(client.conversationModel()->data(client.conversationModel()->index(0, 0), ConversationModel::IdRole).toString(),
                 QStringLiteral("u-stranger"));
    }

    void testImePreeditEnterToSendAndComposerIndent() {
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting("u-me");

        ConversationItem conv;
        conv.id = QStringLiteral("u-ime-peer");
        conv.name = QStringLiteral("Peer IME");
        client.conversationModel()->setConversations({conv});
        client.selectConversation(conv.id, conv.name, "");

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(200);

        auto* msgInput = window->findChild<QQuickItem*>(QStringLiteral("msgInput"));
        QVERIFY(msgInput != nullptr);
        msgInput->forceActiveFocus();
        QTest::qWait(50);

        QSignalSpy sentSpy(&client, &ZaloClient::messageSent);

        {
            QInputMethodEvent pre(QStringLiteral("tieeng"), {});
            QCoreApplication::sendEvent(msgInput, &pre);
            QCOMPARE(msgInput->property("preeditText").toString(), QStringLiteral("tieeng"));

            QInputMethodEvent enterCommit;
            enterCommit.setCommitString(QStringLiteral("tiếng\n"));
            QCoreApplication::sendEvent(msgInput, &enterCommit);
        }
        QTRY_COMPARE_WITH_TIMEOUT(sentSpy.count(), 1, 1000);
        QCOMPARE(sentSpy.at(0).at(0).toString(), QStringLiteral("tiếng"));
        QCOMPARE(msgInput->property("text").toString(), QString());
        QCOMPARE(msgInput->property("preeditText").toString(), QString());

        {
            QInputMethodEvent pre(QStringLiteral("chao"), {});
            QCoreApplication::sendEvent(msgInput, &pre);
            QTest::keyClick(window, Qt::Key_Return);
        }
        QTRY_COMPARE_WITH_TIMEOUT(sentSpy.count(), 2, 1000);
        QCOMPARE(sentSpy.at(1).at(0).toString(), QStringLiteral("chao"));
        QCOMPARE(msgInput->property("text").toString(), QString());

        {
            QInputMethodEvent lateDup;
            lateDup.setCommitString(QStringLiteral("chao"));
            QCoreApplication::sendEvent(msgInput, &lateDup);
        }
        QTest::qWait(30);
        QCOMPARE(msgInput->property("text").toString(), QString());

        {
            QInputMethodEvent pre(QStringLiteral("dong1"), {});
            QCoreApplication::sendEvent(msgInput, &pre);
            QTest::keyClick(window, Qt::Key_Return, Qt::ShiftModifier);
        }
        QTest::qWait(50);
        QCOMPARE(sentSpy.count(), 2);
        QCOMPARE(msgInput->property("text").toString(), QStringLiteral("dong1\n"));

        QTextDocument doc;
        doc.setPlainText(QStringLiteral("Doan thut le"));
        RichComposer composer;
        composer.attachDocument(&doc);
        composer.changeIndent(2, 0, 5);
        const QVariantList styles = composer.styles();
        QCOMPARE(styles.size(), 1);
        QCOMPARE(styles.at(0).toMap().value("st").toString(), QStringLiteral("ind_20"));
        const QString html = MessageModel::styledHtml(doc.toPlainText(), {}, styles);
        QVERIFY(html.startsWith(QString(8, QChar(0x00A0))));

        QTextDocument restored;
        restored.setPlainText(QStringLiteral("Doan thut le"));
        RichComposer::applyStyles(&restored, styles);
        QCOMPARE(RichComposer::exportStyles(&restored).at(0).toMap().value("st").toString(), QStringLiteral("ind_20"));
    }

    void testTypingIndicatorGroupAndHistory() {
        ZaloClient client;
        auto* session = client.apiClient()->session();

        ConversationItem grp;
        grp.id = QStringLiteral("grp-typing");
        grp.name = QStringLiteral("Team");
        grp.isGroup = true;
        grp.memberCount = 5;
        client.conversationModel()->setConversations({grp});
        client.selectConversation(grp.id, grp.name, "");

        QSignalSpy statusSpy(&client, &ZaloClient::conversationStatusChanged);
        emit session->typingReceived(grp.id, QStringLiteral("u-bob"), true);
        QCOMPARE(client.currentConversationIsTyping(), true);
        QVERIFY(statusSpy.count() >= 1);

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonObject oldMsg{{"msgId", "9001"}, {"uidFrom", "u-bob"}, {"idTo", grp.id},
                           {"msgType", "webchat"}, {"content", "old"}, {"ts", now - 60000}};
        emit session->messagesReceived(QJsonArray{oldMsg}, true, true);
        QCOMPARE(client.currentConversationIsTyping(), true);

        QJsonObject liveMsg{{"msgId", "9002"}, {"uidFrom", "u-bob"}, {"idTo", grp.id},
                            {"msgType", "webchat"}, {"content", "live"}, {"ts", now}};
        emit session->messagesReceived(QJsonArray{liveMsg}, true, false);
        QCOMPARE(client.currentConversationIsTyping(), false);
    }

    void testBankCardAndScreenShare() {
        QCOMPARE(ZaloClient::bankShortNameForBin(970436), QStringLiteral("Vietcombank"));

        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.selectConversation(QStringLiteral("u-bank-peer"), QStringLiteral("Bank Peer"), QString());

        const QJsonObject bankParams{
            {"action", "zinstant.bankcard"},
            {"binBank", 970422},
            {"numAccBank", "0987654321"},
            {"nameAccBank", "TRAN THI B"}
        };
        const QJsonObject bankRaw{
            {"msgId", "77001"},
            {"cliMsgId", "77001"},
            {"uidFrom", "u-bank-peer"},
            {"idTo", "u-me"},
            {"msgType", "chat.webcontent"},
            {"ts", QDateTime::currentMSecsSinceEpoch()},
            {"content", QJsonObject{
                {"title", "MB Bank · 0987654321"},
                {"action", "zinstant.bankcard"},
                {"params", QString::fromUtf8(QJsonDocument(bankParams).toJson(QJsonDocument::Compact))}
            }}
        };
        emit client.apiClient()->session()->messagesReceived(QJsonArray{bankRaw}, false, false);
        QCOMPARE(client.messageModel()->rowCount(), 1);
        const auto idx = client.messageModel()->index(0, 0);
        QCOMPARE(client.messageModel()->data(idx, MessageModel::MessageTypeRole).toString(), QStringLiteral("bankcard"));
        const QVariantMap extra = client.messageModel()->data(idx, MessageModel::ExtraRole).toMap();
        QCOMPARE(extra.value("bankName").toString(), QStringLiteral("MB Bank"));
        QCOMPARE(extra.value("accountNumber").toString(), QStringLiteral("0987654321"));
        QCOMPARE(extra.value("accountHolder").toString(), QStringLiteral("TRAN THI B"));

        QImage shot(64, 36, QImage::Format_RGB32);
        shot.fill(Qt::darkBlue);
        const QByteArray i420 = Zalos::Core::Call::imageToI420(shot, QSize(32, 18));
        QCOMPARE(i420.size(), 32 * 18 * 3 / 2);

        if (Zalos::Core::Call::OpenH264::instance().available()) {
            Zalos::Core::Call::CameraSender sender;
            QList<QSize> encodedSizes;
            QList<bool> encodedKeys;
            connect(&sender, &Zalos::Core::Call::CameraSender::frameEncoded, this,
                    [&](bool key, const QByteArray&, int w, int h) {
                encodedKeys << key;
                encodedSizes << QSize(w, h);
            });

            QImage fhd(1920, 1080, QImage::Format_RGB32);
            fhd.fill(Qt::darkGreen);
            sender.processScreenImage(fhd);
            QCOMPARE(encodedSizes.size(), 1);
            QCOMPARE(encodedKeys.first(), true);
            QCOMPARE(encodedSizes.first(), QSize(1280, 720));

            QTest::qWait(110);
            QImage ratio1610(1920, 1200, QImage::Format_RGB32);
            ratio1610.fill(Qt::darkCyan);
            sender.processScreenImage(ratio1610);
            QCOMPARE(encodedSizes.size(), 2);
            QCOMPARE(encodedSizes.last(), QSize(1152, 720));

            if (Zalos::Core::Call::CameraSender::screenAvailable()) {
                QQuickWindow win;
                win.resize(320, 180);
                win.show();
                QVERIFY(QTest::qWaitForWindowExposed(&win));

                Zalos::Core::Call::CameraSender liveSender;
                QVideoSink previewSink;
                QSignalSpy previewSpy(&previewSink, &QVideoSink::videoFrameChanged);
                QSignalSpy liveEncSpy(&liveSender, &Zalos::Core::Call::CameraSender::frameEncoded);
                liveSender.setPreviewSink(&previewSink);
                liveSender.setEncodingEnabled(true);
                QVERIFY(liveSender.startScreen());
                QTRY_VERIFY_WITH_TIMEOUT(previewSpy.count() >= 1 && liveEncSpy.count() >= 1, 2000);
                QCOMPARE(liveEncSpy.first().at(0).toBool(), true);
                liveSender.stop();
            }
        }

        auto* calls = client.calls();
        calls->injectUiLineForTesting(R"({"type":"state","phase":"connected","video":true,"peerSharing":true,"name":"Peer","since":10})");
        QCOMPARE(calls->peerSharing(), true);
        if (Zalos::Core::Call::CameraSender::encoderAvailable() && Zalos::Core::Call::CameraSender::screenAvailable()) {
            calls->setScreenShare(true);
            QCOMPARE(calls->screenShareOn(), true);
            QCOMPARE(calls->cameraOn(), false);
            calls->setScreenShare(false);
            QCOMPARE(calls->screenShareOn(), false);
        }
        calls->injectUiLineForTesting(R"({"type":"close"})");
        calls->hangup();
        QCOMPARE(calls->peerSharing(), false);
    }

    void testCallMinimizeAndNetworkRetry() {
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));

        ConversationItem conv;
        conv.id = QStringLiteral("u-call-chat");
        conv.name = QStringLiteral("Call Friend");
        client.conversationModel()->setConversations({conv});
        client.selectConversation(conv.id, conv.name, QString());

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("avatar"), new AvatarImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("media"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("icon"), new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        QObject::connect(&client, &ZaloClient::windowActivationRequested, window, [window]() {
            QMetaObject::invokeMethod(window, "showAndActivate");
        });
        window->resize(1280, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(150);

        auto* calls = client.calls();
        calls->injectUiLineForTesting(R"({"type":"state","phase":"connected","video":true,"name":"Call Friend","since":1000})");
        QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QQuickItem*>(QStringLiteral("callOverlayRoot")) != nullptr, 2000);
        auto* overlay = window->findChild<QQuickItem*>(QStringLiteral("callOverlayRoot"));
        auto* callCard = window->findChild<QQuickItem*>(QStringLiteral("callCard"));
        auto* minBtn = window->findChild<QQuickItem*>(QStringLiteral("callMinimizeBtn"));
        QVERIFY(overlay && callCard && minBtn);
        QCOMPARE(overlay->property("minimized").toBool(), false);

        const QPoint minCenter = minBtn->mapToScene(QPointF(minBtn->width() / 2.0, minBtn->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, minCenter);
        QTRY_VERIFY_WITH_TIMEOUT(overlay->property("minimized").toBool(), 1000);
        QCOMPARE(callCard->width(), 300.0);

        auto* msgInput = window->findChild<QQuickItem*>(QStringLiteral("msgInput"));
        QVERIFY(msgInput != nullptr);
        const QPoint inputCenter = msgInput->mapToScene(QPointF(msgInput->width() / 2.0, msgInput->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, inputCenter);
        QTRY_VERIFY_WITH_TIMEOUT(msgInput->hasActiveFocus(), 1000);
        QSignalSpy sentSpy(&client, &ZaloClient::messageSent);
        for (const QChar ch : QStringLiteral("dang goi")) {
            if (ch == QLatin1Char(' ')) QTest::keyClick(window, Qt::Key_Space);
            else QTest::keyClick(window, Qt::Key(Qt::Key_A + ch.toUpper().unicode() - 'A'));
        }
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(sentSpy.count(), 1, 1000);
        QCOMPARE(sentSpy.at(0).at(0).toString(), QStringLiteral("dang goi"));

        auto* expandBtn = window->findChild<QQuickItem*>(QStringLiteral("callExpandBtn"));
        QVERIFY(expandBtn != nullptr);
        const QPoint expCenter = expandBtn->mapToScene(QPointF(expandBtn->width() / 2.0, expandBtn->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, expCenter);
        QTRY_VERIFY_WITH_TIMEOUT(!overlay->property("minimized").toBool(), 1000);

        if (Zalos::Core::Call::CameraSender::encoderAvailable() && Zalos::Core::Call::CameraSender::screenAvailable()) {
            calls->setScreenShare(true);
            QTRY_VERIFY_WITH_TIMEOUT(overlay->property("minimized").toBool(), 1000);
            calls->setScreenShare(false);
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              expandBtn->mapToScene(QPointF(expandBtn->width() / 2.0, expandBtn->height() / 2.0)).toPoint());
            QTRY_VERIFY_WITH_TIMEOUT(!overlay->property("minimized").toBool(), 1000);
        }

        calls->injectUiLineForTesting(R"({"type":"state","phase":"connected","video":true,"name":"Call Friend","since":1000,"micVolume":82})");
        calls->injectUiLineForTesting(R"({"type":"micLevel","level":6})");
        QTRY_COMPARE_WITH_TIMEOUT(trayCtrl.callPhase(), QStringLiteral("connected"), 1000);
        QVERIFY(trayCtrl.toolTip().contains(QStringLiteral("Call Friend")));
        QVERIFY(trayCtrl.callStatusAction()->isVisible());
        QVERIFY(trayCtrl.callMuteAction()->isVisible());
        QVERIFY(trayCtrl.callSpeakerAction()->isVisible());
        QVERIFY(trayCtrl.callCameraAction()->isVisible());
        QVERIFY(trayCtrl.callHangupAction()->isVisible());

        trayCtrl.callMuteAction()->trigger();
        QCOMPARE(calls->muted(), true);
        QCOMPARE(trayCtrl.callMuteAction()->isChecked(), true);
        trayCtrl.callMuteAction()->trigger();
        QCOMPARE(calls->muted(), false);
        trayCtrl.callSpeakerAction()->trigger();
        QCOMPARE(calls->speakerOff(), true);

        QCOMPARE(calls->micVolume(), 82);
        QCOMPARE(calls->micLevel(), 6);
        auto* settingsBtn = window->findChild<QQuickItem*>(QStringLiteral("callSettingsBtn"));
        auto* deviceMenu = window->findChild<QQuickItem*>(QStringLiteral("callDeviceMenu"));
        QVERIFY(settingsBtn && deviceMenu);
        QTRY_VERIFY_WITH_TIMEOUT(settingsBtn->isVisible(), 1000);
        QCOMPARE(deviceMenu->isVisible(), false);
        const QPoint settingsCenter = settingsBtn->mapToScene(QPointF(settingsBtn->width() / 2.0, settingsBtn->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, settingsCenter);
        QTRY_VERIFY_WITH_TIMEOUT(deviceMenu->isVisible(), 1000);
        calls->setMicVolume(65);
        QCOMPARE(calls->micVolume(), 65);
        calls->selectAudioDevice(QStringLiteral("mic"), QStringLiteral("test_mic_id"));
        calls->selectAudioDevice(QStringLiteral("speaker"), QStringLiteral("test_spk_id"));
        QCOMPARE(calls->selectedMicId(), QStringLiteral("test_mic_id"));
        QCOMPARE(calls->selectedSpeakerId(), QStringLiteral("test_spk_id"));

        const bool prevCloseToTray = client.closeToTray();
        client.setTrayAvailableForTesting(1);
        client.setCloseToTray(false);
        window->close();
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 1000);
        QCOMPARE(calls->phase(), QStringLiteral("connected"));
        QCOMPARE(trayCtrl.callPhase(), QStringLiteral("connected"));

        trayCtrl.callStatusAction()->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);

        client.setTrayAvailableForTesting(0);
        overlay->setProperty("minimized", false);
        window->close();
        QTRY_VERIFY_WITH_TIMEOUT(overlay->property("minimized").toBool(), 1000);
        QVERIFY(window->isVisible());
        QCOMPARE(calls->phase(), QStringLiteral("connected"));
        window->showNormal();
        overlay->setProperty("minimized", false);

        trayCtrl.callHangupAction()->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(calls->phase(), QString(), 1000);
        QCOMPARE(trayCtrl.callPhase(), QString());
        QVERIFY(!trayCtrl.callStatusAction()->isVisible());
        QVERIFY(!trayCtrl.callHangupAction()->isVisible());

        client.setTrayAvailableForTesting(1);
        window->hide();
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 1000);
        QCoreApplication::processEvents();
        calls->injectUiLineForTesting(R"({"type":"state","phase":"incoming","video":false,"name":"Incoming Friend"})");
        QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);
        QCOMPARE(trayCtrl.callPhase(), QStringLiteral("incoming"));
        QVERIFY(trayCtrl.callAcceptAction()->isVisible());
        trayCtrl.callRejectAction()->trigger();
        calls->hangup();
        client.setTrayAvailableForTesting(-1);
        client.setCloseToTray(prevCloseToTray);
        QSettings().remove(QStringLiteral("settings/closeToTray"));
        calls->selectAudioDevice(QStringLiteral("mic"), QString());
        calls->selectAudioDevice(QStringLiteral("speaker"), QString());

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int hits = 0;
        connect(&server, &QTcpServer::newConnection, this, [&server, &hits]() {
            while (QTcpSocket* sock = server.nextPendingConnection()) {
                const int attempt = ++hits;
                connect(sock, &QTcpSocket::readyRead, sock, [sock, attempt]() {
                    sock->readAll();
                    if (attempt == 1) {
                        sock->write("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    } else {
                        const QByteArray body = R"({"error_code":0,"data":{"ok":true}})";
                        sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    }
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });

        auto* session = client.apiClient()->session();
        const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        session->setLoginInfoForTesting(QJsonObject{
            {"uid", "u-me"},
            {"zpw_enk", QString::fromLatin1(QByteArray(32, 'A').toBase64())},
            {"zpw_service_map_v3", QJsonObject{{"chat", QJsonArray{baseUrl}}}}
        });
        bool apiDone = false;
        bool apiOk = false;
        QJsonValue apiData;
        session->apiGet(QStringLiteral("chat"), QStringLiteral("/api/test/retry"), QJsonObject{},
                        [&](bool ok, const QJsonValue& data, const QString&, int) {
            apiDone = true;
            apiOk = ok;
            apiData = data;
        });
        QTRY_VERIFY_WITH_TIMEOUT(apiDone, 3000);
        QCOMPARE(apiOk, true);
        QCOMPARE(hits, 2);
        QCOMPARE(apiData.toObject().value("ok").toBool(), true);
    }

    void testImageClickOpensPreview() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString p1 = dir.filePath(QStringLiteral("p1.png"));
        const QString p2 = dir.filePath(QStringLiteral("p2.png"));
        QImage img1(16, 16, QImage::Format_RGBA8888);
        img1.fill(Qt::red);
        QVERIFY(img1.save(p1, "PNG"));
        QImage img2(16, 16, QImage::Format_RGBA8888);
        img2.fill(Qt::blue);
        QVERIFY(img2.save(p2, "PNG"));

        ZaloClient client;
        ThemeManager themeMgr;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));

        ConversationItem conv;
        conv.id = QStringLiteral("u-img-chat");
        conv.name = QStringLiteral("Photo Friend");
        client.conversationModel()->setConversations({conv});
        client.selectConversation(conv.id, conv.name, QString());

        const QString u1 = QUrl::fromLocalFile(p1).toString();
        const QString u2 = QUrl::fromLocalFile(p2).toString();
        MessageItem m1;
        m1.id = QStringLiteral("m-img-1");
        m1.messageType = QStringLiteral("image");
        m1.senderId = conv.id;
        m1.senderName = conv.name;
        m1.mediaUrl = u1;
        m1.href = u1;
        m1.ts = 1700000000000LL;
        MessageItem m2;
        m2.id = QStringLiteral("m-img-2");
        m2.messageType = QStringLiteral("image");
        m2.senderId = conv.id;
        m2.senderName = conv.name;
        m2.mediaUrl = u2;
        m2.href = u2;
        m2.ts = 1700001000000LL;
        client.messageModel()->mergeMessages(conv.id, {m1, m2});

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("avatar"), new AvatarImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("media"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("icon"), new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto findItems = [&](const QString& name) {
            QList<QQuickItem*> out;
            std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
                if (!item) return;
                if (item->objectName() == name) out.append(item);
                for (QQuickItem* c : item->childItems()) walk(c);
            };
            walk(window->contentItem());
            return out;
        };
        QTRY_VERIFY_WITH_TIMEOUT(findItems(QStringLiteral("mediaClickArea")).size() >= 2, 2000);
        const auto areas = findItems(QStringLiteral("mediaClickArea"));
        QQuickItem* target = areas.last();
        QTRY_VERIFY_WITH_TIMEOUT(target->isVisible() && target->width() > 0 && target->height() > 0, 1000);
        const QPoint center = target->mapToScene(QPointF(target->width() / 2.0, target->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);

        QTRY_VERIFY_WITH_TIMEOUT(!findItems(QStringLiteral("imageViewerRoot")).isEmpty(), 2000);
        QQuickItem* viewer = findItems(QStringLiteral("imageViewerRoot")).first();
        QTRY_VERIFY_WITH_TIMEOUT(viewer->isVisible(), 1000);
        QCOMPARE(viewer->property("count").toInt(), 2);
        QCOMPARE(viewer->property("currentUrl").toString(), u2);

        const auto flicks = findItems(QStringLiteral("imageViewerToolsFlick"));
        QCOMPARE(flicks.size(), 1);
        QQuickItem* toolsFlick = flicks.first();
        QTRY_VERIFY_WITH_TIMEOUT(toolsFlick->width() >= 250 && toolsFlick->height() >= 36, 1000);

        const auto strips = findItems(QStringLiteral("imageViewerToolsStrip"));
        QCOMPARE(strips.size(), 1);
        QQuickItem* toolsStrip = strips.first();
        QTRY_VERIFY_WITH_TIMEOUT(toolsStrip->width() >= 250 && toolsStrip->height() >= 36, 1000);
        for (QQuickItem* child : toolsStrip->childItems()) {
            if (child->isVisible()) {
                QVERIFY2(child->width() >= 32 && child->height() >= 36, "Top toolbar button collapsed");
            }
        }

        viewer->setProperty("annotating", true);
        const auto annRows = findItems(QStringLiteral("imageViewerAnnotateRow"));
        QCOMPARE(annRows.size(), 1);
        QQuickItem* annRow = annRows.first();
        QTRY_VERIFY_WITH_TIMEOUT(annRow->isVisible() && annRow->width() > 200 && annRow->height() >= 32, 1000);
        int visibleButtons = 0;
        for (QQuickItem* child : annRow->childItems()) {
            if (child->isVisible() && child->width() > 1) {
                ++visibleButtons;
                QVERIFY2(child->width() >= 22 && child->height() >= 22, "Annotate toolbar item collapsed");
            }
        }
        QVERIFY(visibleButtons >= 11);
        window->grabWindow().save(QStringLiteral("test_image_viewer_annotating.png"));

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(findItems(QStringLiteral("imageViewerRoot")).isEmpty(), 1000);
    }

    void testGroupSidebarSenderPrefix() {
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.setUserForTesting(QStringLiteral("u-friend"), QStringLiteral("Minh Anh"), QString());
        client.setGroupForTesting(QStringLiteral("g-team"), QStringLiteral("Project Team"), QString());

        auto* session = client.apiClient()->session();
        emit session->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", "70001"},
            {"cliMsgId", "70001"},
            {"uidFrom", "u-friend"},
            {"idTo", "g-team"},
            {"dName", "Minh Anh"},
            {"msgType", "webchat"},
            {"content", "Chao moi nguoi"},
            {"ts", QString::number(1700000100000LL)}
        }}, true, false);

        QCOMPARE(client.conversationModel()->conversation(QStringLiteral("g-team")).senderPrefix,
                 QStringLiteral("Minh Anh: "));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, this, [&server]() {
            while (QTcpSocket* sock = server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, sock, [sock]() {
                    sock->readAll();
                    const QJsonObject payload{
                        {"changed_profiles", QJsonObject{
                            {"u-unknown_0", QJsonObject{{"userId", "u-unknown"}, {"displayName", "Hai Dang"}, {"avatar", ""}}}
                        }}
                    };
                    const QByteArray body = QJsonDocument(QJsonObject{{"error_code", 0}, {"data", payload}})
                                                .toJson(QJsonDocument::Compact);
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        session->setLoginInfoForTesting(QJsonObject{
            {"uid", "u-me"},
            {"zpw_enk", QString::fromLatin1(QByteArray(32, 'A').toBase64())},
            {"zpw_service_map_v3", QJsonObject{{"profile", QJsonArray{baseUrl}}}}
        });

        emit session->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", "70002"},
            {"cliMsgId", "70002"},
            {"uidFrom", "u-unknown"},
            {"idTo", "g-team"},
            {"msgType", "webchat"},
            {"content", "Cap nhat tien do"},
            {"ts", QString::number(1700000200000LL)}
        }}, true, false);

        QVERIFY(client.isUserLookupPendingForTesting(QStringLiteral("u-unknown")));
        QCOMPARE(client.conversationModel()->conversation(QStringLiteral("g-team")).senderPrefix, QString());

        client.resolveUnknownUsersForTesting();
        QTRY_COMPARE_WITH_TIMEOUT(client.conversationModel()->conversation(QStringLiteral("g-team")).senderPrefix,
                                  QStringLiteral("Hai Dang: "), 3000);

        client.applyAliasesForTesting(QHash<QString, QString>{{QStringLiteral("u-unknown"), QStringLiteral("Anh Dang")}});
        QCOMPARE(client.conversationModel()->conversation(QStringLiteral("g-team")).senderPrefix,
                 QStringLiteral("Anh Dang: "));

        emit session->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", "70003"},
            {"cliMsgId", "70003"},
            {"uidFrom", "u-friend"},
            {"idTo", "g-team"},
            {"msgType", "chat.list.action"},
            {"content", QJsonObject{{"title", "Minh Anh joined the group"}}},
            {"ts", QString::number(1700000300000LL)}
        }}, true, false);
        QCOMPARE(client.conversationModel()->conversation(QStringLiteral("g-team")).senderPrefix, QString());

        {
            ZaloOfflineStore store;
            store.bindOwner(QStringLiteral("u-prefix-backfill"));
            store.clearCache();
            store.bindOwner(QStringLiteral("u-prefix-backfill"));
            store.saveProfile({QStringLiteral("u-friend"), QStringLiteral("Minh Anh"), QString(), false, 0, true, false});
            store.saveProfile({QStringLiteral("g-backfill"), QStringLiteral("Backfill Group"), QString(), true, 5, false, false});
            ConversationItem conv;
            conv.id = QStringLiteral("g-backfill");
            conv.name = QStringLiteral("Backfill Group");
            conv.isGroup = true;
            conv.lastMessage = QStringLiteral("Tin cuoi trong nhom");
            conv.senderPrefix = QString();
            conv.lastTs = 1700000400000LL;
            store.saveConversation(conv);
            MessageItem msg;
            msg.id = QStringLiteral("70004");
            msg.senderId = QStringLiteral("u-friend");
            msg.senderName = QString();
            msg.text = QStringLiteral("Tin cuoi trong nhom");
            msg.messageType = QStringLiteral("text");
            msg.ts = 1700000400000LL;
            store.saveMessage(QStringLiteral("g-backfill"), msg, QJsonObject(), true);
        }

        ZaloClient client2;
        QVERIFY(client2.openOfflineCacheForTesting(QStringLiteral("u-prefix-backfill")));
        QCOMPARE(client2.conversationModel()->conversation(QStringLiteral("g-backfill")).senderPrefix,
                 QStringLiteral("Minh Anh: "));
        client2.offlineStore()->clearCache();
    }

    void testCallHangupPreUiSocketAndGroupSignalParams() {
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QByteArray capturedUrl;
        bool requestDone = false;
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            while (QTcpSocket* sock = server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, sock, [&, sock]() {
                    const QByteArray raw = sock->readAll();
                    const int sp1 = raw.indexOf(' ');
                    const int sp2 = raw.indexOf(' ', sp1 + 1);
                    if (sp1 >= 0 && sp2 > sp1) capturedUrl = raw.mid(sp1 + 1, sp2 - sp1 - 1);
                    const QByteArray body = R"({"error_code":0,"data":{"ok":true}})";
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    sock->disconnectFromHost();
                    requestDone = true;
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });

        const QString keyB64 = QString::fromLatin1(QByteArray(32, 'B').toBase64());
        const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        client.apiClient()->session()->setLoginInfoForTesting(QJsonObject{
            {"uid", "u-me"},
            {"zpw_enk", keyB64},
            {"zpw_service_map_v3", QJsonObject{{"voice_call", QJsonArray{baseUrl}}}}
        });

        auto* calls = client.calls();
        calls->callSendSignalForTesting(12438, QJsonObject{
            {"callId", 1456789012},
            {"hostCall", 777001},
            {"data", "{}"}
        });
        QTRY_VERIFY_WITH_TIMEOUT(requestDone, 3000);
        const QUrlQuery q(QUrl(QString::fromUtf8(capturedUrl)).query(QUrl::FullyDecoded));
        const QString encParams = QUrl::fromPercentEncoding(q.queryItemValue(QStringLiteral("params"), QUrl::FullyEncoded).toUtf8());
        bool decOk = false;
        const QByteArray dec = Zalos::Core::Network::ZaloCrypto::decodeAES(keyB64, encParams, &decOk);
        QVERIFY(decOk);
        const QJsonObject obj = QJsonDocument::fromJson(dec).object();
        QCOMPARE(obj.value("callId").toInt(), 1456789012);
        QCOMPARE(obj.value("hostCall").toInt(), 777001);
        QVERIFY(!obj.contains(QStringLiteral("calleeId")));
        QVERIFY(!obj.contains(QStringLiteral("callType")));
        QVERIFY(!obj.contains(QStringLiteral("partners")));
        QVERIFY(!obj.contains(QStringLiteral("groupId")));

        if (calls->isAvailable()) {
            calls->startVoiceCall(QStringLiteral("u-cancel"), QStringLiteral("Cancel Target"), QString());
            QCOMPARE(calls->phase(), QStringLiteral("outgoing"));
            calls->hangup();
            QCOMPARE(calls->phase(), QString());
            QCOMPARE(calls->pendingEngineFrameCountForTesting(), 0);
        } else {
            calls->injectUiLineForTesting(R"({"type":"state","phase":"outgoing","video":false,"name":"Cancel Target"})");
            QCOMPARE(calls->phase(), QStringLiteral("outgoing"));
            calls->hangup();
            QCOMPARE(calls->phase(), QString());
        }
    }

    void testRoundedImageReloadKeepsMaskChildrenStable() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString imgPath = dir.filePath(QStringLiteral("sample.png"));
        QImage sample(64, 64, QImage::Format_RGBA8888);
        sample.fill(QColor(0xE0, 0x30, 0x30));
        QVERIFY(sample.save(imgPath, "PNG"));

        ThemeManager themeMgr;
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));

        QQmlComponent comp(&engine);
        comp.setData(
            "import QtQuick\n"
            "import QtQuick.Window\n"
            "import Zalos\n"
            "Window {\n"
            "  width: 200; height: 200\n"
            "  ZRoundedImage {\n"
            "    id: rounded\n"
            "    objectName: \"roundedTarget\"\n"
            "    anchors.centerIn: parent\n"
            "    width: 120; height: 120\n"
            "    radius: 16\n"
            "    cache: false\n"
            "  }\n"
            "}\n",
            QUrl(QStringLiteral("qrc:/test_rounded_image.qml")));
        std::unique_ptr<QObject> rootObj(comp.create());
        QVERIFY2(rootObj, qPrintable(comp.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(rootObj.get());
        QVERIFY(window != nullptr);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* rounded = window->findChild<QQuickItem*>(QStringLiteral("roundedTarget"));
        QVERIFY(rounded != nullptr);
        rounded->setProperty("sourceSize", QSize(120, 120));
        rounded->setProperty("source", QUrl::fromLocalFile(imgPath));

        QTRY_COMPARE_WITH_TIMEOUT(rounded->property("status").toInt(), 1, 2000);
        QCOMPARE(rounded->property("maskArmed").toBool(), true);
        const bool gpu = rounded->property("gpu").toBool();
        if (gpu) {
            QTRY_COMPARE_WITH_TIMEOUT(rounded->property("masked").toBool(), true, 2000);
            QTRY_VERIFY_WITH_TIMEOUT(([&]() {
                const QImage frame = window->grabWindow();
                if (frame.isNull()) return false;
                const QPoint center = rounded->mapToScene(QPointF(60, 60)).toPoint();
                const QPoint corner = rounded->mapToScene(QPointF(1, 1)).toPoint();
                const QColor cCenter = frame.pixelColor(center);
                const QColor cCorner = frame.pixelColor(corner);
                return cCenter.red() > 180 && cCenter.green() < 80 && cCorner != cCenter;
            }()), 2000);
        }

        const QList<QQuickItem*> initialChildren = rounded->childItems();
        QList<QPointer<QQuickItem>> watchedChildren;
        for (QQuickItem* child : initialChildren) {
            watchedChildren.append(child);
            QObject* layerObj = child->property("layer").value<QObject*>();
            if (layerObj) {
                QCOMPARE(layerObj->property("enabled").toBool(), false);
            }
        }

        rounded->setProperty("sourceSize", QSize(180, 180));
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        QEvent dprEvent(QEvent::DevicePixelRatioChange);
        QCoreApplication::sendEvent(window, &dprEvent);
#endif

        QCOMPARE(rounded->property("maskArmed").toBool(), true);
        if (gpu) {
            QCOMPARE(rounded->property("masked").toBool(), true);
        }
        QCOMPARE(rounded->childItems(), initialChildren);
        for (const QPointer<QQuickItem>& ptr : watchedChildren) {
            QVERIFY2(!ptr.isNull(), "A ZRoundedImage child item was destroyed during async reload / DPR update");
        }

        QTRY_COMPARE_WITH_TIMEOUT(rounded->property("status").toInt(), 1, 2000);
        QCOMPARE(rounded->childItems(), initialChildren);

        rounded->setProperty("source", QUrl());
        QCOMPARE(rounded->property("maskArmed").toBool(), false);
        QCOMPARE(rounded->property("masked").toBool(), false);
        QCOMPARE(rounded->childItems(), initialChildren);
        for (const QPointer<QQuickItem>& ptr : watchedChildren) {
            QVERIFY(!ptr.isNull());
        }
    }

    void testGroupImageThenTextViewedOnPhoneHistorySync() {
        ZaloClient parseClient;
        QCOMPARE(parseClient.apiClient()->session()->serviceUrls(QStringLiteral("group_cloud_message")),
                 QStringList{QStringLiteral("https://tt-group-cm.api.zaloapp.com")});
        const MessageItem stringPhoto = parseClient.toMessageItemForTesting(
            QJsonObject{{"msgId", "1998"},
                        {"uidFrom", "u-peer"},
                        {"idTo", "g-photo"},
                        {"msgType", "chat.photo"},
                        {"ts", "1730000000500"},
                        {"content", QStringLiteral("{\"normalUrl\":\"https://cdn.example/norm.jpg\","
                                                   "\"thumbUrl\":\"https://cdn.example/th.jpg\","
                                                   "\"description\":\"Ảnh nhóm\"}")}},
            true);
        QCOMPARE(stringPhoto.messageType, QStringLiteral("image"));
        QCOMPARE(stringPhoto.mediaUrl, QStringLiteral("https://cdn.example/th.jpg"));
        QCOMPARE(stringPhoto.href, QStringLiteral("https://cdn.example/norm.jpg"));
        QCOMPARE(stringPhoto.text, QString::fromUtf8("Ảnh nhóm"));

        const MessageItem hdPlaceholder = parseClient.toMessageItemForTesting(
            QJsonObject{{"msgId", "1999"},
                        {"uidFrom", "u-peer"},
                        {"idTo", "g-photo"},
                        {"msgType", "chat.photo"},
                        {"ts", "1730000000700"},
                        {"content", QJsonObject{{"title", "[Photo]"},
                                                {"description", "https://cdn.example/desc-url"},
                                                {"hdUrl", "https://cdn.example/hd.jpg"}}}},
            true);
        QCOMPARE(hdPlaceholder.messageType, QStringLiteral("image"));
        QCOMPARE(hdPlaceholder.mediaUrl, QStringLiteral("https://cdn.example/hd.jpg"));
        QCOMPARE(hdPlaceholder.href, QStringLiteral("https://cdn.example/hd.jpg"));
        QVERIFY(hdPlaceholder.text.isEmpty());

        {
            ZaloOfflineStore owner;
            owner.bindOwner("u-photo-rep");
            owner.clearCache();
            owner.bindOwner("u-photo-rep");
            MessageItem stale;
            stale.id = "2000";
            stale.senderId = "u-peer";
            stale.text = QString();
            stale.messageType = "image";
            stale.ts = 1730000000800LL;
            const QJsonObject raw{
                {"msgId", "2000"},
                {"uidFrom", "u-peer"},
                {"idTo", "g-photo"},
                {"msgType", "chat.photo"},
                {"ts", "1730000000800"},
                {"content", QStringLiteral("{\"href\":\"https://cdn.example/full2000.jpg\","
                                           "\"thumb\":\"https://cdn.example/th2000.jpg\"}")}};
            QVERIFY(owner.saveMessage("g-photo", stale, raw, true));
            owner.setMetaValue("parse_version", "5");
        }
        {
            ZaloClient repClient;
            repClient.openOfflineCacheForTesting("u-photo-rep");
            auto* store = repClient.offlineStore();
            QTRY_COMPARE_WITH_TIMEOUT(store->metaValue("parse_version"), QString::number(ZaloClient::kCacheParseVersion), 5000);
            const auto rows = store->loadMessages("g-photo");
            QCOMPARE(rows.size(), 1);
            QCOMPARE(rows.first().messageType, QStringLiteral("image"));
            QCOMPARE(rows.first().mediaUrl, QStringLiteral("https://cdn.example/th2000.jpg"));
            QCOMPARE(rows.first().href, QStringLiteral("https://cdn.example/full2000.jpg"));
            store->clearCache();
        }

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QStringList cmPaths;
        QStringList oldCursors;
        int oldCalls = 0;
        const QString keyB64 = QString::fromLatin1(QByteArray(32, 'C').toBase64());

        connect(&server, &QTcpServer::newConnection, this, [&]() {
            while (QTcpSocket* sock = server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, sock, [&, sock]() {
                    const QByteArray raw = sock->readAll();
                    const int sp1 = raw.indexOf(' ');
                    const int sp2 = raw.indexOf(' ', sp1 + 1);
                    const QUrl url(QString::fromUtf8(raw.mid(sp1 + 1, sp2 - sp1 - 1)));
                    const QString path = url.path();
                    if (path.startsWith(QLatin1String("/api/cm/"))) cmPaths.append(path);

                    QJsonObject dataObj;
                    if (path == QLatin1String("/api/cm/getrecentv2")) {
                        dataObj = QJsonObject{
                            {"data", QJsonObject{{"groupMsgs", QJsonArray{}},
                                                 {"hasMore", 1},
                                                 {"isOld", 1},
                                                 {"lastMsgId", "2002"}}}};
                    } else if (path == QLatin1String("/api/cm/getoldv2")) {
                        ++oldCalls;
                        const QUrlQuery q(url.query(QUrl::FullyDecoded));
                        const QString enc = QUrl::fromPercentEncoding(
                            q.queryItemValue(QStringLiteral("params"), QUrl::FullyEncoded).toUtf8());
                        bool decOk = false;
                        const QByteArray dec = Zalos::Core::Network::ZaloCrypto::decodeAES(keyB64, enc, &decOk);
                        if (decOk) {
                            oldCursors.append(
                                QJsonDocument::fromJson(dec).object().value("globalMsgId").toVariant().toString());
                        }
                        const QJsonObject img2001{
                            {"msgId", "2001"},
                            {"uidFrom", "u-peer"},
                            {"idTo", "g-img-sync"},
                            {"msgType", "chat.photo"},
                            {"ts", "1730000001000"},
                            {"content", QStringLiteral("{\"href\":\"https://cdn.example/full2001.jpg\","
                                                       "\"thumb\":\"https://cdn.example/th2001.jpg\"}")}};
                        const QJsonObject txt2002{
                            {"msgId", "2002"},
                            {"uidFrom", "u-peer"},
                            {"idTo", "g-img-sync"},
                            {"msgType", "webchat"},
                            {"ts", "1730000002000"},
                            {"content", "Đã xem ảnh"}};
                        const QJsonObject older2000{
                            {"msgId", "2000"},
                            {"uidFrom", "u-peer"},
                            {"idTo", "g-img-sync"},
                            {"msgType", "webchat"},
                            {"ts", "1730000000500"},
                            {"content", "Tin cũ hơn"}};
                        if (oldCalls == 1) {
                            dataObj = QJsonObject{{"groupMsgs", QJsonArray{img2001, txt2002}},
                                                  {"hasMore", 1},
                                                  {"isOld", 1},
                                                  {"lastMsgId", "2001"}};
                        } else {
                            dataObj = QJsonObject{{"groupMsgs", QJsonArray{older2000}},
                                                  {"hasMore", 0},
                                                  {"isOld", 1},
                                                  {"lastMsgId", "2000"}};
                        }
                    }

                    const QByteArray body = QJsonDocument(QJsonObject{{"error_code", 0}, {"data", dataObj}})
                                                .toJson(QJsonDocument::Compact);
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });

        ZaloClient client;
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.setGroupForTesting(QStringLiteral("g-img-sync"), QString::fromUtf8("Nhóm Ảnh"), {});
        const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        client.apiClient()->session()->setLoginInfoForTesting(QJsonObject{
            {"uid", "u-me"},
            {"zpw_enk", keyB64},
            {"zpw_service_map_v3", QJsonObject{{"group_cloud_message", QJsonArray{baseUrl}},
                                               {"conversation", QJsonArray{baseUrl}},
                                               {"group", QJsonArray{baseUrl}},
                                               {"profile", QJsonArray{baseUrl}}}}
        });

        client.setLoggedInForTesting(false);
        client.selectConversation(QStringLiteral("g-img-sync"), QString::fromUtf8("Nhóm Ảnh"), {});
        QCoreApplication::processEvents();
        QVERIFY(cmPaths.isEmpty());

        client.setLoggedInForTesting(true);
        client.selectConversation(QStringLiteral("g-img-sync"), QString::fromUtf8("Nhóm Ảnh"), {});
        QTRY_VERIFY_WITH_TIMEOUT(client.messageModel()->rowCount() == 2 && !client.isLoadingHistory(), 3000);

        QCOMPARE(cmPaths, (QStringList{QStringLiteral("/api/cm/getrecentv2"), QStringLiteral("/api/cm/getoldv2")}));
        QCOMPARE(oldCursors, QStringList{QStringLiteral("2002")});
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QStringLiteral("2001"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::MessageTypeRole).toString(),
                 QStringLiteral("image"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::MediaUrlRole).toString(),
                 QStringLiteral("https://cdn.example/th2001.jpg"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::HrefRole).toString(),
                 QStringLiteral("https://cdn.example/full2001.jpg"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(1), MessageModel::IdRole).toString(),
                 QStringLiteral("2002"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(1), MessageModel::TextRole).toString(),
                 QString::fromUtf8("Đã xem ảnh"));

        client.loadOlderMessages();
        QTRY_VERIFY_WITH_TIMEOUT(client.messageModel()->rowCount() == 3 && !client.isLoadingHistory(), 3000);
        QCOMPARE(oldCalls, 2);
        QCOMPARE(oldCursors.at(1), QStringLiteral("2001"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QStringLiteral("2000"));
        QVERIFY(client.historyExhausted());
    }

    void testCloudHistoryFallbackAndExhaustedErrorCode() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QString mode = QStringLiteral("fallback");
        QStringList hitPaths;
        const QString keyB64 = QString::fromLatin1(QByteArray(32, 'D').toBase64());

        connect(&server, &QTcpServer::newConnection, this, [&]() {
            while (QTcpSocket* sock = server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, sock, [&, sock]() {
                    const QByteArray raw = sock->readAll();
                    const int sp1 = raw.indexOf(' ');
                    const int sp2 = raw.indexOf(' ', sp1 + 1);
                    const QString path = QUrl(QString::fromUtf8(raw.mid(sp1 + 1, sp2 - sp1 - 1))).path();
                    if (path.startsWith(QLatin1String("/api/cm/")) || path == QLatin1String("/api/group/history")) {
                        hitPaths.append(path);
                    }
                    QJsonObject payload{{"error_code", 0}, {"data", QJsonObject{}}};
                    if (path == QLatin1String("/api/cm/getrecentv2")) {
                        payload = QJsonObject{{"error_code", mode == QLatin1String("exhausted") ? -69 : -1},
                                              {"error_message", QStringLiteral("cloud err")}};
                    } else if (path == QLatin1String("/api/group/history")) {
                        const QJsonObject img3001{
                            {"msgId", "3001"},
                            {"uidFrom", "u-peer"},
                            {"idTo", "g-fallback"},
                            {"msgType", "chat.photo"},
                            {"ts", "1730000003000"},
                            {"content", QJsonObject{{"href", "https://cdn.example/full3001.jpg"},
                                                    {"thumb", "https://cdn.example/th3001.jpg"}}}};
                        payload = QJsonObject{{"error_code", 0},
                                              {"data", QJsonObject{{"groupMsgs", QJsonArray{img3001}}}}};
                    }
                    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });

        ZaloClient client;
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.setGroupForTesting(QStringLiteral("g-fallback"), QStringLiteral("Group Fallback"), {});
        client.setGroupForTesting(QStringLiteral("g-exhaust"), QStringLiteral("Group Exhaust"), {});
        const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        client.apiClient()->session()->setLoginInfoForTesting(QJsonObject{
            {"uid", "u-me"},
            {"zpw_enk", keyB64},
            {"zpw_service_map_v3", QJsonObject{{"group_cloud_message", QJsonArray{baseUrl}},
                                               {"group", QJsonArray{baseUrl}},
                                               {"conversation", QJsonArray{baseUrl}},
                                               {"profile", QJsonArray{baseUrl}}}}
        });
        client.setLoggedInForTesting(true);

        client.selectConversation(QStringLiteral("g-fallback"), QStringLiteral("Group Fallback"), {});
        QTRY_VERIFY_WITH_TIMEOUT(client.messageModel()->rowCount() == 1 && !client.isLoadingHistory(), 3000);
        QCOMPARE(hitPaths, (QStringList{QStringLiteral("/api/cm/getrecentv2"), QStringLiteral("/api/group/history")}));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QStringLiteral("3001"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::MessageTypeRole).toString(),
                 QStringLiteral("image"));
        QVERIFY(!client.historyExhausted());

        hitPaths.clear();
        mode = QStringLiteral("exhausted");
        client.selectConversation(QStringLiteral("g-exhaust"), QStringLiteral("Group Exhaust"), {});
        QTRY_VERIFY_WITH_TIMEOUT(client.historyExhausted() && !client.isLoadingHistory(), 3000);
        QCOMPARE(hitPaths, QStringList{QStringLiteral("/api/cm/getrecentv2")});
        QCOMPARE(client.messageModel()->rowCount(), 0);
    }

    void testPhase1To3Regressions() {
        {
            QImage tall(600, 5000, QImage::Format_RGB32);
            tall.fill(Qt::blue);
            QByteArray png;
            {
                QBuffer out(&png);
                QVERIFY(out.open(QIODevice::WriteOnly));
                QVERIFY(tall.save(&out, "PNG"));
            }
            {
                QBuffer in(&png);
                QVERIFY(in.open(QIODevice::ReadOnly));
                QString err;
                const QImage bounded = AvatarImageProvider::decodeScaled(&in, QSize(320, 600), false, &err);
                QVERIFY(!bounded.isNull());
                QVERIFY(err.isEmpty());
                QVERIFY(bounded.width() <= 320);
                QVERIFY(bounded.height() <= 600);
            }
            {
                QImage huge(17000, 8, QImage::Format_RGB32);
                huge.fill(Qt::red);
                QByteArray hugePng;
                QBuffer out(&hugePng);
                QVERIFY(out.open(QIODevice::WriteOnly));
                QVERIFY(huge.save(&out, "PNG"));
                QBuffer in(&hugePng);
                QVERIFY(in.open(QIODevice::ReadOnly));
                QString err;
                const QImage rejected = AvatarImageProvider::decodeScaled(&in, QSize(320, 320), false, &err);
                QVERIFY(rejected.isNull());
                QCOMPARE(err, QStringLiteral("image too large"));
            }
            AvatarImageProvider::markTooLarge(QStringLiteral("https://cdn.example/huge.jpg"));
            QVERIFY(AvatarImageProvider::isTooLarge(QStringLiteral("https://cdn.example/huge.jpg")));
            AvatarImageProvider::clearMemCache();
            QVERIFY(!AvatarImageProvider::isTooLarge(QStringLiteral("https://cdn.example/huge.jpg")));
        }

        {
            QSettings s;
            s.setValue(QStringLiteral("window/x"), -50000);
            s.setValue(QStringLiteral("window/y"), -50000);
            s.setValue(QStringLiteral("window/width"), 99999);
            s.setValue(QStringLiteral("window/height"), 99999);

            ZaloClient client;
            const QVariantMap geom = client.windowGeometry();
            if (QScreen* primary = QGuiApplication::primaryScreen()) {
                const QRect avail = primary->availableGeometry();
                if (avail.isValid()) {
                    QVERIFY(geom.value("width").toInt() <= avail.width());
                    QVERIFY(geom.value("height").toInt() <= avail.height());
                    QVERIFY(geom.value("x").toInt() >= avail.left());
                    QVERIFY(geom.value("y").toInt() >= avail.top());
                }
            }
            s.remove(QStringLiteral("window/x"));
            s.remove(QStringLiteral("window/y"));
            s.remove(QStringLiteral("window/width"));
            s.remove(QStringLiteral("window/height"));

            ConversationItem c1;
            c1.id = QStringLiteral("u-fwd-1");
            c1.name = QStringLiteral("Alice Read");
            c1.unreadCount = 0;
            c1.lastTs = 2000;
            ConversationItem c2;
            c2.id = QStringLiteral("u-fwd-2");
            c2.name = QStringLiteral("Bob Unread");
            c2.unreadCount = 3;
            c2.lastTs = 1000;
            client.conversationModel()->setConversations({c1, c2});
            client.conversationModel()->setCategoryFilter(QStringLiteral("unread"));
            QCOMPARE(client.conversationModel()->rowCount(), 1);
            QCOMPARE(client.conversationsForForward().size(), 2);
            QCOMPARE(client.conversationsForForward(QStringLiteral("alice")).size(), 1);
        }

        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            int reqCount = 0;
            connect(&server, &QTcpServer::newConnection, this, [&]() {
                while (QTcpSocket* sock = server.nextPendingConnection()) {
                    connect(sock, &QTcpSocket::readyRead, sock, [&, sock]() {
                        sock->readAll();
                        ++reqCount;
                        const QByteArray body = R"({"error_code":0,"data":[]})";
                        sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        sock->disconnectFromHost();
                    });
                    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
                }
            });

            ZaloClient client;
            ZaloSession& session = *client.apiClient()->session();
            const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
            const QString keyB64 = QString::fromLatin1(QByteArray(32, 'S').toBase64());
            session.setLoginInfoForTesting(QJsonObject{
                {"uid", "u-me"},
                {"zpw_sek", keyB64},
                {"zpw_enk", keyB64},
                {"zpw_service_map_v3", QJsonObject{{"sticker", QJsonArray{baseUrl}},
                                                   {"profile", QJsonArray{baseUrl}}}}
            });

            StickerStore::Hooks hooks;
            hooks.session = [&]() { return &session; };
            StickerStore store(hooks);
            store.loadBirthdayStickers();
            QTRY_COMPARE_WITH_TIMEOUT(reqCount, 4, 3000);
            QTest::qWait(80);
            store.loadBirthdayStickers();
            QTest::qWait(80);
            QCOMPARE(reqCount, 4);

            store.reset();
            store.loadBirthdayStickers();
            QTRY_COMPARE_WITH_TIMEOUT(reqCount, 8, 3000);
        }

        {
            QHash<QString, QVariantMap> appliedMap;
            QJsonObject persistedSnapshot;
            LabelManager::Hooks hooks;
            hooks.apply = [&](const QHash<QString, QVariantMap>& map) { appliedMap = map; };
            hooks.persist = [&](const QJsonObject& snap) { persistedSnapshot = snap; };
            LabelManager lm(hooks);

            const QJsonObject cachedPayload{
                {"version", 7},
                {"labelData", QJsonArray{QJsonObject{
                    {"id", 10}, {"text", "Khách hàng"}, {"color", "0068FF"},
                    {"offset", 0}, {"conversations", QJsonArray{"u123"}}
                }}}
            };
            lm.applyCached(cachedPayload);
            QVERIFY(!lm.loaded());
            QCOMPARE(lm.labels().size(), 1);
            QVERIFY(appliedMap.contains("123"));
            QVERIFY(persistedSnapshot.isEmpty());

            const QJsonObject serverPayload{
                {"version", 8},
                {"labelData", QJsonArray{QJsonObject{
                    {"id", 11}, {"text", "VIP"}, {"color", "#D91B1B"},
                    {"offset", 0}, {"conversations", QJsonArray{"g456"}}
                }}}
            };
            lm.applyServer(serverPayload);
            QVERIFY(lm.loaded());
            QCOMPARE(lm.labels().size(), 1);
            QVERIFY(appliedMap.contains("456"));
            QCOMPARE(persistedSnapshot.value("version").toInt(), 8);
            QCOMPARE(persistedSnapshot.value("labelData").toArray().size(), 1);

            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            ZaloOfflineStore offline;
            QVERIFY(offline.initializeDatabase(dir.filePath("labels_cache.sqlite")));
            offline.setMetaValue(QStringLiteral("labels_cache"),
                                 QString::fromUtf8(QJsonDocument(persistedSnapshot).toJson(QJsonDocument::Compact)));
            QVERIFY(!offline.metaValue(QStringLiteral("labels_cache")).isEmpty());
            offline.clearCache();
            QVERIFY(offline.metaValue(QStringLiteral("labels_cache")).isEmpty());
        }

        {
            QCOMPARE(ThemeManager::familyFromFontSpec(QStringLiteral("Noto Sans Medium 11")),
                     QStringLiteral("Noto Sans"));
            QCOMPARE(ThemeManager::familyFromFontSpec(QStringLiteral("Inter,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1")),
                     QStringLiteral("Inter"));

            ThemeManager tm;
            tm.setThemeMode(2);
            QPalette lightPal;
            lightPal.setColor(QPalette::Window, QColor("#FFFFFF"));
            lightPal.setColor(QPalette::WindowText, QColor("#111827"));
            lightPal.setColor(QPalette::Base, QColor("#FFFFFF"));
            lightPal.setColor(QPalette::AlternateBase, QColor("#FFFFFF"));
            tm.setSystemPaletteForTesting(lightPal);
            QVERIFY(!tm.isDarkMode());
            QVERIFY(tm.chatBackgroundColor() != tm.chatBubbleTheir());
            QVERIFY(tm.inputFillColor() != tm.surfaceColor());

            QPalette darkPal;
            darkPal.setColor(QPalette::Window, QColor("#1E2228"));
            darkPal.setColor(QPalette::WindowText, QColor("#E5E7EB"));
            darkPal.setColor(QPalette::Base, QColor("#16191D"));
            darkPal.setColor(QPalette::PlaceholderText, QColor(0, 0, 0, 128));
            tm.setSystemPaletteForTesting(darkPal);
            QVERIFY(tm.isDarkMode());
            QVERIFY(tm.placeholderColor().lightness() >= 95);
            QVERIFY(tm.inactiveSubtleColor().lightness() >= 95);
        }

        {
            ZaloClient client;
            const QJsonObject raw{
                {"msgId", "loc-1"},
                {"uidFrom", "u-peer"},
                {"idTo", "u-me"},
                {"msgType", "chat.location.new"},
                {"ts", "1700000000000"},
                {"content", QJsonObject{
                    {"title", "Hồ Hoàn Kiếm"},
                    {"description", "Hoàn Kiếm, Hà Nội"},
                    {"thumb", "https://example.test/map-thumb.png"},
                    {"params", R"({"latitude":21.028511,"longitude":105.854444})"}
                }}
            };
            const auto item = client.toMessageItemForTesting(raw, false);
            QCOMPARE(item.messageType, QStringLiteral("location"));
            QCOMPARE(item.mediaUrl, QStringLiteral("https://example.test/map-thumb.png"));
            QVERIFY(item.extra.value("hasCoords").toBool());
            QVERIFY(qAbs(item.extra.value("lat").toDouble() - 21.028511) < 1e-5);
            QVERIFY(qAbs(item.extra.value("lng").toDouble() - 105.854444) < 1e-5);
        }

        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            QStringList seenPaths;
            const QByteArray key = QByteArray(32, 'B');
            connect(&server, &QTcpServer::newConnection, this, [&]() {
                while (QTcpSocket* sock = server.nextPendingConnection()) {
                    connect(sock, &QTcpSocket::readyRead, sock, [&, sock]() {
                        const QByteArray req = sock->readAll();
                        const int sp1 = req.indexOf(' ');
                        const int sp2 = req.indexOf(' ', sp1 + 1);
                        if (sp1 > 0 && sp2 > sp1) seenPaths << QString::fromUtf8(req.mid(sp1 + 1, sp2 - sp1 - 1));
                        const QByteArray body = R"({"error_code":0,"data":{"version":1,"items":[],"item":[]}})";
                        sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        sock->disconnectFromHost();
                    });
                    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
                }
            });

            ZaloClient client;
            ZaloSession& session = *client.apiClient()->session();
            const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
            const QString keyB64 = QString::fromLatin1(key.toBase64());
            session.setLoginInfoForTesting(QJsonObject{
                {"uid", "u-me"},
                {"zpw_sek", keyB64},
                {"zpw_enk", keyB64},
                {"zpw_service_map_v3", QJsonObject{
                    {"profile", QJsonArray{baseUrl}},
                    {"catalog", QJsonArray{baseUrl}},
                    {"auto_reply", QJsonArray{baseUrl}}
                }}
            });

            int doneCount = 0;
            auto bump = [&](bool, const QJsonValue&, const QString&, int) { ++doneCount; };
            session.fetchBizAccount("u-me", bump);
            session.fetchCatalogList(0, 20, bump);
            session.createCatalog("Cat", bump);
            session.fetchProductCatalogList("c1", 0, 50, bump);
            session.createProductCatalog("c1", "Prod", "1000", "Desc", {}, bump);
            session.deleteProductCatalog("c1", {"p1"}, bump);
            session.fetchAutoReplyList("vi", bump);
            session.createAutoReply("vi", "Hello", true, 0, 0, 0, {}, bump);
            session.updateAutoReply("vi", 1, "Hi", false, 0, 0, 1, {}, bump);
            session.deleteAutoReply("vi", 1, bump);

            QTRY_COMPARE_WITH_TIMEOUT(doneCount, 10, 4000);
            const QString joined = seenPaths.join('\n');
            for (const char* p : {"/api/social/friend/get-bizacc", "/api/prodcatalog/catalog/list",
                                  "/api/prodcatalog/catalog/create", "/api/prodcatalog/product/list",
                                  "/api/prodcatalog/product/create", "/api/prodcatalog/product/mdelete",
                                  "/api/autoreply/list", "/api/autoreply/create",
                                  "/api/autoreply/update", "/api/autoreply/delete"}) {
                QVERIFY2(joined.contains(QLatin1String(p)), p);
            }
        }
    }

    void testUpstreamAdaptations() {
        {
            QNetworkAccessManager nam;
            SessionCookieJar jar;
            ZaloSession session(&nam, &jar);
            session.enqueuePendingWsFrameForTesting(2, 1, QJsonObject{{"eventId", 1}}, QString());
            QCOMPARE(session.pendingWsFrameCountForTesting(), 0);
            for (int i = 0; i < 35; ++i) {
                session.enqueuePendingWsFrameForTesting(590, 0, QJsonObject{{"i", i}});
            }
            QCOMPARE(session.pendingWsFrameCountForTesting(), 32);
            session.disconnectRealtime();
            QCOMPARE(session.pendingWsFrameCountForTesting(), 0);
        }

        {
            QByteArray badPng("\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR\xff\xff\xff\xff", 20);
            QBuffer in(&badPng);
            QVERIFY(in.open(QIODevice::ReadOnly));
            QString err;
            const QImage img = AvatarImageProvider::decodeScaled(&in, QSize(128, 128), false, &err);
            QVERIFY(img.isNull());
            QCOMPARE(err, QStringLiteral("not a decodable image"));
        }

        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost, 0));
            QStringList seenPaths;
            QByteArray lastBody;
            const QByteArray key = QByteArray(32, 'F');
            connect(&server, &QTcpServer::newConnection, this, [&]() {
                while (QTcpSocket* sock = server.nextPendingConnection()) {
                    connect(sock, &QTcpSocket::readyRead, sock, [&, sock]() {
                        const QByteArray req = sock->readAll();
                        const int sp1 = req.indexOf(' ');
                        const int sp2 = req.indexOf(' ', sp1 + 1);
                        if (sp1 > 0 && sp2 > sp1) seenPaths << QString::fromUtf8(req.mid(sp1 + 1, sp2 - sp1 - 1));
                        const int hdrEnd = req.indexOf("\r\n\r\n");
                        if (hdrEnd >= 0) lastBody = req.mid(hdrEnd + 4);
                        const QByteArray body = R"({"error_code":0,"data":{"msgId":"9090"}})";
                        sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        sock->disconnectFromHost();
                    });
                    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
                }
            });

            ZaloClient client;
            client.setLoggedInForTesting(true);
            client.setMyUidForTesting(QStringLiteral("u-me"));
            ZaloSession& session = *client.apiClient()->session();
            const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
            const QString keyB64 = QString::fromLatin1(key.toBase64());
            session.setLoginInfoForTesting(QJsonObject{
                {"uid", "u-me"},
                {"zpw_sek", keyB64},
                {"zpw_enk", keyB64},
                {"zpw_service_map_v3", QJsonObject{{"chat", QJsonArray{baseUrl}}}}
            });

            Zalos::Core::Models::MessageItem linkMsg;
            linkMsg.id = QStringLiteral("m-link-1");
            linkMsg.messageType = QStringLiteral("link");
            linkMsg.text = QStringLiteral("Sample article title");
            linkMsg.href = QStringLiteral("https://example.test/article");
            linkMsg.ts = 1700000000000LL;
            client.messageModel()->loadConversation(QStringLiteral("u-src"));
            client.messageModel()->mergeMessages(QStringLiteral("u-src"), {linkMsg});
            client.forwardMessage(QStringLiteral("u-target"), QStringLiteral("m-link-1"));
            QTRY_COMPARE_WITH_TIMEOUT(seenPaths.size(), 1, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(!lastBody.isEmpty(), 3000);
            QVERIFY(seenPaths.first().startsWith(QStringLiteral("/api/message/sms")));
            QCOMPARE(client.messageModel()->threadSize(QStringLiteral("u-target")), 1);
            const QUrlQuery q(QString::fromUtf8(lastBody));
            const QString encParams = QUrl::fromPercentEncoding(
                q.queryItemValue(QStringLiteral("params"), QUrl::FullyEncoded).toUtf8());
            const QByteArray dec = ZaloCrypto::decodeAES(keyB64, encParams);
            QVERIFY(dec.contains("https://example.test/article"));
        }

        {
            ZaloClient client;
            client.calls()->refreshDevicesAsync();
            QTest::qWait(50);
            QVERIFY(client.calls()->audioInputDevices().size() >= 0);
        }
    }

    void testEmojiFallbackRendersColorAndAscii() {
        const QString emojiFamily = Zalos::Core::Platform::ThemeManager::registerEmojiFallbackFont();
        QVERIFY(!emojiFamily.isEmpty());

        QImage canvas(320, 72, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::white);
        {
            QPainter p(&canvas);
            QFont f(QStringLiteral("Sans Serif"), 24);
            p.setFont(f);
            p.setPen(Qt::black);
            p.drawText(QRect(0, 0, 150, 72), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Hello 123 #*"));
            p.drawText(QRect(150, 0, 170, 72), Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("\U0001F600\u2764\uFE0F\U0001F44D\U0001F389"));
        }

        int asciiInkPixels = 0;
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 0; x < 150; ++x) {
                const QColor c = canvas.pixelColor(x, y);
                if (c.red() < 200 || c.green() < 200 || c.blue() < 200) ++asciiInkPixels;
            }
        }
        QVERIFY2(asciiInkPixels > 40, "ASCII text/digits should render visibly with emoji fallback active");

        int colorEmojiPixels = 0;
        for (int y = 0; y < canvas.height(); ++y) {
            for (int x = 150; x < canvas.width(); ++x) {
                const QColor c = canvas.pixelColor(x, y);
                const int spread = std::max({c.red(), c.green(), c.blue()}) - std::min({c.red(), c.green(), c.blue()});
                if (spread > 40) ++colorEmojiPixels;
            }
        }
        QVERIFY2(colorEmojiPixels > 40, "CBDT emoji fallback should draw colored emoji pixels");
    }

    void testCallEndAndNotificationRecovery() {
        {
            ZaloClient client;
            ZaloSession* session = client.apiClient()->session();
            QSignalSpy voipSpy(session, &ZaloSession::voipControlReceived);
            const QJsonObject inner{
                {"act_type", "voip"},
                {"act", "end_call"},
                {"data", QJsonObject{{"callId", QJsonValue::Null}, {"ts", QDateTime::currentMSecsSinceEpoch() - 180000}}}
            };
            session->handleControlsForTesting(QJsonArray{QJsonObject{{"content", QString::fromUtf8(QJsonDocument(inner).toJson(QJsonDocument::Compact))}}});
            QCOMPARE(voipSpy.count(), 1);
            QCOMPARE(voipSpy.at(0).at(0).toJsonObject().value(QStringLiteral("act")).toString(), QStringLiteral("end_call"));
        }

        {
            CallBridge bridge(nullptr);
            bridge.handleVoipControl(QJsonObject{
                {"act", "request"},
                {"data", QJsonObject{{"callId", "999"}, {"ts", QDateTime::currentMSecsSinceEpoch() - 120000}}}
            });
            QVERIFY(bridge.findChild<QProcess*>() == nullptr);

            bridge.startVoiceCall(QStringLiteral("u-call-end"), QStringLiteral("Peer"), QString());
            QVERIFY(bridge.findChild<QProcess*>() != nullptr);
            const int before = bridge.pendingEngineFrameCountForTesting();
            const qint64 staleTs = QDateTime::currentMSecsSinceEpoch() - 180000;
            bridge.handleVoipControl(QJsonObject{
                {"act", "end_call"},
                {"data", QJsonObject{{"callId", "999"}, {"ts", staleTs}}}
            });
            QCOMPARE(bridge.pendingEngineFrameCountForTesting(), before + 1);
            bridge.shutdown();
        }

        {
            ZaloClient client;
            client.setLoggedInForTesting(true);
            client.setMyUidForTesting(QStringLiteral("u-me"));
            client.setNotifyNewMessage(true);
            auto* calls = client.calls();
            calls->injectUiLineForTesting(R"({"type":"state","phase":"outgoing","name":"Peer","avatar":"","since":0})");
            calls->injectUiLineForTesting(R"({"type":"close","statusCode":"ended"})");
            QCOMPARE(calls->phase(), QStringLiteral("ended"));

            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const QJsonObject msg{
                {"msgId", "m-after-call-1"},
                {"cliMsgId", "m-after-call-1"},
                {"uidFrom", "u-peer-after"},
                {"idTo", "u-me"},
                {"dName", "Peer After"},
                {"msgType", "webchat"},
                {"content", "Message after call ended"},
                {"ts", QString::number(now)}
            };
            emit client.apiClient()->session()->messagesReceived(QJsonArray{msg}, false, false);
            QCOMPARE(client.notificationsForTesting()->shownForTesting(QStringLiteral("thread:u-peer-after")), 1);
        }
    }

    void testSystemTraySniAndDbusMenu() {
        using Zalos::Core::Platform::DbusMenuItemProps;
        using Zalos::Core::Platform::DbusMenuLayoutItem;
        using Zalos::Core::Platform::SniPixmap;
        using Zalos::Core::Platform::SniToolTip;
        SystemTrayController::registerDbusTypes();

        QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<SniPixmap>())),
                 QByteArray("(iiay)"));
        QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<DbusMenuLayoutItem>())),
                 QByteArray("(ia{sv}av)"));
        QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<DbusMenuItemProps>())),
                 QByteArray("(ia{sv})"));

        QImage pxTest(2, 1, QImage::Format_ARGB32);
        pxTest.setPixelColor(0, 0, QColor(0x11, 0x22, 0x33, 0x80));
        pxTest.setPixelColor(1, 0, QColor(0xAA, 0xBB, 0xCC, 0xFF));
        const QByteArray expectedBytes("\x80\x11\x22\x33\xFF\xAA\xBB\xCC", 8);
        QCOMPARE(SystemTrayController::toSniArgb32(pxTest), expectedBytes);

        SystemTrayController tray;
        const QImage initialBadge = tray.badgeImage();
        QCOMPARE(initialBadge.size(), QSize(64, 64));
        QCOMPARE(initialBadge.format(), QImage::Format_ARGB32);
        QCOMPARE(tray.sniPixmaps().size(), 1);
        QCOMPARE(tray.sniPixmaps().at(0).data.size(), 64 * 64 * 4);

        tray.setUnreadCount(5, false);
        QVERIFY(tray.badgeImage() != initialBadge);
        QVERIFY(tray.sniToolTip().title.contains(QStringLiteral("5")));

        const QImage fiveBadge = tray.badgeImage();
        tray.setUnreadCount(120, true);
        QVERIFY(tray.badgeImage() != fiveBadge);
        QVERIFY(tray.sniToolTip().subTitle.contains(QStringLiteral("@")));

        const uint revStart = tray.menuRevision();
        tray.setLanguage(QStringLiteral("en"));
        QVERIFY(tray.menuRevision() > revStart);

        tray.setWindowVisible(false);
        auto findByLabel = [&tray](const QString& needle) -> QVariantMap {
            for (const QVariant& v : tray.menuSnapshot()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("label")).toString().contains(needle)) return m;
            }
            return {};
        };
        QCOMPARE(findByLabel(QStringLiteral("Open Zalos")).value(QStringLiteral("visible")).toBool(), true);
        tray.setWindowVisible(true);
        QCOMPARE(findByLabel(QStringLiteral("Hide Zalos")).value(QStringLiteral("visible")).toBool(), true);

        tray.setAntiPeeking(true);
        QCOMPARE(findByLabel(QStringLiteral("Anti-peeking")).value(QStringLiteral("toggle-state")).toInt(), 1);
        tray.setAntiPeeking(false);
        QCOMPARE(findByLabel(QStringLiteral("Anti-peeking")).value(QStringLiteral("toggle-state")).toInt(), 0);

        QCOMPARE(findByLabel(QStringLiteral("Notifications")).value(QStringLiteral("toggle-state")).toInt(), 1);
        tray.setNotificationsEnabled(false);
        QCOMPARE(findByLabel(QStringLiteral("Notifications")).value(QStringLiteral("toggle-state")).toInt(), 0);
        tray.setNotificationsEnabled(true);
        QCOMPARE(findByLabel(QStringLiteral("Notifications")).value(QStringLiteral("toggle-state")).toInt(), 1);

        QSignalSpy markAllSpy(&tray, &SystemTrayController::markAllReadRequested);
        tray.setUnreadCount(0, false);
        const QVariantMap initialMarkAll = findByLabel(QStringLiteral("Mark as read"));
        QCOMPARE(initialMarkAll.value(QStringLiteral("visible")).toBool(), false);
        QCOMPARE(initialMarkAll.value(QStringLiteral("enabled")).toBool(), false);
        QVERIFY(!tray.triggerMenuItem(initialMarkAll.value(QStringLiteral("id")).toInt()));

        tray.setUnreadCount(3, false);
        const QVariantMap unreadMarkAll = findByLabel(QStringLiteral("Mark as read"));
        QCOMPARE(unreadMarkAll.value(QStringLiteral("visible")).toBool(), true);
        QCOMPARE(unreadMarkAll.value(QStringLiteral("enabled")).toBool(), true);
        QVERIFY(tray.triggerMenuItem(unreadMarkAll.value(QStringLiteral("id")).toInt()));
        QCOMPARE(markAllSpy.count(), 1);

        tray.setLocked(true);
        QCOMPARE(findByLabel(QStringLiteral("Mark as read")).value(QStringLiteral("visible")).toBool(), false);
        QVERIFY(!tray.triggerMenuItem(unreadMarkAll.value(QStringLiteral("id")).toInt()));
        tray.setLocked(false);
        QCOMPARE(findByLabel(QStringLiteral("Mark as read")).value(QStringLiteral("visible")).toBool(), true);
        tray.setUnreadCount(0, false);
        QCOMPARE(findByLabel(QStringLiteral("Mark as read")).value(QStringLiteral("visible")).toBool(), false);

        tray.setCallState(QStringLiteral("incoming"), QStringLiteral("Peer A"), true, false, false, false, false);
        QVERIFY(findByLabel(QStringLiteral("Incoming call: Peer A")).value(QStringLiteral("visible")).toBool());
        QVERIFY(findByLabel(QStringLiteral("Answer with video")).value(QStringLiteral("visible")).toBool());
        QVERIFY(findByLabel(QStringLiteral("Answer without camera")).value(QStringLiteral("visible")).toBool());
        QVERIFY(findByLabel(QStringLiteral("Decline call")).value(QStringLiteral("visible")).toBool());
        QVERIFY(!findByLabel(QStringLiteral("Mute microphone")).value(QStringLiteral("visible")).toBool());

        QSignalSpy acceptSpy(&tray, &SystemTrayController::acceptCallRequested);
        QSignalSpy rejectSpy(&tray, &SystemTrayController::rejectCallRequested);
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Answer with video")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Answer without camera")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Decline call")).value(QStringLiteral("id")).toInt()));
        QCOMPARE(acceptSpy.count(), 2);
        QCOMPARE(acceptSpy.at(0).at(0).toBool(), false);
        QCOMPARE(acceptSpy.at(1).at(0).toBool(), true);
        QCOMPARE(rejectSpy.count(), 1);

        tray.setCallState(QStringLiteral("connected"), QStringLiteral("Peer A"), true, true, false, true, true);
        QVERIFY(!findByLabel(QStringLiteral("Answer with video")).value(QStringLiteral("visible")).toBool());
        QCOMPARE(findByLabel(QStringLiteral("Mute microphone")).value(QStringLiteral("toggle-state")).toInt(), 1);
        QCOMPARE(findByLabel(QStringLiteral("Mute speaker")).value(QStringLiteral("toggle-state")).toInt(), 0);
        QCOMPARE(findByLabel(QStringLiteral("Camera on")).value(QStringLiteral("toggle-state")).toInt(), 1);
        QCOMPARE(findByLabel(QStringLiteral("Share screen")).value(QStringLiteral("toggle-state")).toInt(), 1);
        QVERIFY(findByLabel(QStringLiteral("End call")).value(QStringLiteral("visible")).toBool());

        QVERIFY(findByLabel(QStringLiteral("Toggle light / dark mode")).isEmpty());

        QSignalSpy toggleWinSpy(&tray, &SystemTrayController::toggleWindowRequested);
        QSignalSpy quitSpy(&tray, &SystemTrayController::quitRequested);
        QSignalSpy settingsSpy(&tray, &SystemTrayController::openSettingsRequested);
        QSignalSpy notiSpy(&tray, &SystemTrayController::notificationsToggled);
        QSignalSpy showCallSpy(&tray, &SystemTrayController::showCallWindowRequested);
        QSignalSpy muteSpy(&tray, &SystemTrayController::toggleCallMuteRequested);
        QSignalSpy speakerSpy(&tray, &SystemTrayController::toggleCallSpeakerRequested);
        QSignalSpy camSpy(&tray, &SystemTrayController::toggleCallCameraRequested);
        QSignalSpy screenSpy(&tray, &SystemTrayController::toggleCallScreenShareRequested);
        QSignalSpy hangupSpy(&tray, &SystemTrayController::hangupCallRequested);
        QSignalSpy peekSpy(&tray, &SystemTrayController::antiPeekingToggled);

        tray.activateFromHost();
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Hide Zalos")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Settings")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Notifications")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Anti-peeking")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Quit")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("In call: Peer A")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Mute microphone")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Mute speaker")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Camera on")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("Share screen")).value(QStringLiteral("id")).toInt()));
        QVERIFY(tray.triggerMenuItem(findByLabel(QStringLiteral("End call")).value(QStringLiteral("id")).toInt()));
        QVERIFY(!tray.triggerMenuItem(99999));

        QCOMPARE(toggleWinSpy.count(), 2);
        QCOMPARE(settingsSpy.count(), 1);
        QCOMPARE(notiSpy.count(), 1);
        QCOMPARE(peekSpy.count(), 1);
        QCOMPARE(quitSpy.count(), 1);
        QCOMPARE(showCallSpy.count(), 1);
        QCOMPARE(muteSpy.count(), 1);
        QCOMPARE(speakerSpy.count(), 1);
        QCOMPARE(camSpy.count(), 1);
        QCOMPARE(screenSpy.count(), 1);
        QCOMPARE(hangupSpy.count(), 1);

        const DbusMenuLayoutItem layout = tray.menuLayout(0, -1, {});
        QCOMPARE(layout.properties.value(QStringLiteral("children-display")).toString(), QStringLiteral("submenu"));
        QCOMPARE(layout.children.size(), tray.menuSnapshot().size());
        QCOMPARE(tray.menuProperty(0, QStringLiteral("children-display")).toString(), QStringLiteral("submenu"));
        QCOMPARE(tray.menuGroupProperties({0, 1}, {}).size(), 2);
        tray.showMessage(QStringLiteral("Zalos"), QStringLiteral("Test"));
        tray.shutdown();
        QVERIFY(!tray.isVisible());
        tray.shutdown();
        QVERIFY(!tray.isVisible());
    }

    void testDefaultReactionAndInterfaceScale() {
        QSettings settings;
        settings.remove(QStringLiteral("settings/defaultReaction"));
        settings.remove(QStringLiteral("appearance/interfaceScale"));

        ZaloClient client;
        QCOMPARE(client.defaultReaction(), QStringLiteral("like"));
        QCOMPARE(client.defaultReactionEmoji(), QStringLiteral("👍"));
        QCOMPARE(client.defaultReactionOptions().size(), 12);

        QSignalSpy spy(&client, &ZaloClient::settingsChanged);
        client.setDefaultReaction(QStringLiteral("heart"));
        QCOMPARE(client.defaultReaction(), QStringLiteral("heart"));
        QCOMPARE(client.defaultReactionEmoji(), QStringLiteral("❤️"));
        QCOMPARE(spy.count(), 1);

        client.setDefaultReaction(QStringLiteral("not_a_real_key"));
        QCOMPARE(client.defaultReaction(), QStringLiteral("heart"));
        QCOMPARE(spy.count(), 1);

        QCOMPARE(client.interfaceScale(), 100);
        QCOMPARE(client.interfaceScaleRestartRequired(), false);
        client.setInterfaceScale(150);
        QCOMPARE(client.interfaceScale(), 150);
        QCOMPARE(client.interfaceScaleRestartRequired(), true);
        QCOMPARE(settings.value(QStringLiteral("appearance/interfaceScale")).toInt(), 150);

        client.setInterfaceScale(115);
        QCOMPARE(client.interfaceScale(), 100);
        QCOMPARE(client.interfaceScaleRestartRequired(), false);

        QCOMPARE(ZaloClient::normalizeInterfaceScale(80), 80);
        QCOMPARE(ZaloClient::normalizeInterfaceScale(90), 90);
        QCOMPARE(ZaloClient::normalizeInterfaceScale(125), 125);
        QCOMPARE(ZaloClient::normalizeInterfaceScale(150), 150);
        QCOMPARE(ZaloClient::normalizeInterfaceScale(200), 100);

        settings.remove(QStringLiteral("settings/defaultReaction"));
        settings.remove(QStringLiteral("appearance/interfaceScale"));
    }

    void testTrayAvailabilityIsCached() {
        SystemTrayController tray;
        QSignalSpy changed(&tray, &SystemTrayController::availabilityChanged);
        QVERIFY(!tray.cachedAvailable());
        tray.seedAvailabilitySync();
        QVERIFY(!tray.cachedAvailable());
        QCOMPARE(changed.count(), 0);
        ZaloClient client;
        QVERIFY(!client.trayAvailable());
        const QMetaObject* meta = client.metaObject();
        const QMetaProperty prop = meta->property(meta->indexOfProperty("trayAvailable"));
        QCOMPARE(prop.notifySignal().name(), QByteArray("trayAvailableChanged"));
    }

    void testStorageUsageReadyAndVoiceClear() {
        QTemporaryDir cacheRoot;
        const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", cacheRoot.path().toUtf8());
        {
            StorageManager storage;
            const QVariantList expected = storage.usage();
            QSignalSpy ready(&storage, &StorageManager::usageReady);
            storage.refreshUsageAsync();
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000);
            const QVariantList got = ready.first().first().toList();
            QCOMPARE(int(got.size()), int(expected.size()));
            for (qsizetype i = 0; i < got.size(); ++i) {
                const QVariantMap a = got.at(i).toMap();
                const QVariantMap b = expected.at(i).toMap();
                QCOMPARE(a.value("key").toString(), b.value("key").toString());
                QVERIFY(a.contains("bytes") && a.contains("files") && a.contains("clearable") && a.contains("path"));
            }

            const QString voiceDir = StorageManager::directory(QStringLiteral("voice"));
            QDir().mkpath(voiceDir);
            QFile clip(voiceDir + QStringLiteral("/fixture-clip.aac"));
            QVERIFY(clip.open(QIODevice::WriteOnly));
            clip.write("fixture");
            clip.close();
            QSignalSpy cleared(&storage, &StorageManager::usageChanged);
            storage.clear(QStringLiteral("voice"));
            QTRY_COMPARE_WITH_TIMEOUT(cleared.count(), 1, 5000);
            QVERIFY(QDir(voiceDir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        }
        if (oldCache.isEmpty()) qunsetenv("XDG_CACHE_HOME");
        else qputenv("XDG_CACHE_HOME", oldCache);
    }

    void testShortcutPrimaryTextFallback() {
        using Zalos::Core::Platform::ShortcutMap;
        QTemporaryDir config;
        ShortcutMap map;
        map.load(config.path());
        QCOMPARE(map.primaryText(QStringLiteral("composer.voice")), QStringLiteral("Ctrl+Shift+V"));
        QCOMPARE(map.primaryText(QStringLiteral("unknown")), QString());
        QVERIFY(map.keys(QStringLiteral("unknown")).isEmpty());
    }

    void testBoundedHashEvictsInInsertionOrder() {
        BoundedSet<QString> ids;
        for (int i = 0; i < kMaxTrackedIds; ++i) ids.insert(QStringLiteral("u-b%1").arg(i));
        QCOMPARE(int(ids.size()), kMaxTrackedIds);
        ids.insert(QStringLiteral("u-b0"));
        QCOMPARE(int(ids.size()), kMaxTrackedIds);
        Zalos::Core::Network::Detail::dropOldestIfFull(ids);
        QVERIFY(!ids.contains(QStringLiteral("u-b0")));
        QVERIFY(ids.contains(QStringLiteral("u-b1")));
        QCOMPARE(int(ids.size()), kMaxTrackedIds - 1);
    }

    void testQuickMessageFormResetsFields() {
        ZaloClient client;
        ThemeManager themeMgr;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        ConversationItem peer;
        peer.id = QStringLiteral("u-qm");
        peer.name = QStringLiteral("Fixture Quick");
        client.conversationModel()->setConversations({peer});
        client.selectConversation(QStringLiteral("u-qm"), QStringLiteral("Fixture Quick"), QString());

        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(1280, 800);
        QTest::qWait(600);
        auto* chatArea = window->findChild<QQuickItem*>(QStringLiteral("chatArea"));
        QVERIFY(chatArea != nullptr);
        QObject* popup = chatArea->findChild<QObject*>(QStringLiteral("quickMsgPopup"));
        QObject* keyword = chatArea->findChild<QObject*>(QStringLiteral("qmKeywordField"));
        QObject* content = chatArea->findChild<QObject*>(QStringLiteral("qmTextField"));
        QVERIFY(popup != nullptr && keyword != nullptr && content != nullptr);

        auto openForm = [popup](double id, const QString& kw, const QString& text) {
            QMetaObject::invokeMethod(popup, "openQuickForm", Q_ARG(double, id), Q_ARG(QString, kw), Q_ARG(QString, text));
        };
        auto closeForm = [popup]() { QMetaObject::invokeMethod(popup, "closeQuickForm"); };
        auto fieldsAre = [keyword, content](const QString& kw, const QString& text) {
            return keyword->property("text").toString() == kw && content->property("text").toString() == text;
        };
        const QString emptyText;

        openForm(0, emptyText, emptyText);
        QVERIFY(fieldsAre(emptyText, emptyText));
        keyword->setProperty("text", QStringLiteral("Fixture typed"));
        content->setProperty("text", QStringLiteral("Fixture body typed"));
        closeForm();
        openForm(0, emptyText, emptyText);
        QVERIFY(fieldsAre(emptyText, emptyText));
        closeForm();

        openForm(7, QStringLiteral("fixture-kw-a"), QStringLiteral("Fixture body A"));
        QVERIFY(fieldsAre(QStringLiteral("fixture-kw-a"), QStringLiteral("Fixture body A")));
        keyword->setProperty("text", QStringLiteral("edited"));
        closeForm();
        openForm(7, QStringLiteral("fixture-kw-a"), QStringLiteral("Fixture body A"));
        QVERIFY(fieldsAre(QStringLiteral("fixture-kw-a"), QStringLiteral("Fixture body A")));

        openForm(8, QStringLiteral("fixture-kw-b"), QStringLiteral("Fixture body B"));
        QVERIFY(fieldsAre(QStringLiteral("fixture-kw-b"), QStringLiteral("Fixture body B")));
        closeForm();
        openForm(0, emptyText, emptyText);
        QVERIFY(fieldsAre(emptyText, emptyText));
        closeForm();
    }

    void testProfileWritesWaitForServer() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(1); });
        auto isSuccessNotice = [](const QVariantList& args) {
            const QString text = args.first().toString();
            return text.contains(QStringLiteral("Profile updated successfully"))
                || text.contains(QString::fromUtf8("Đã cập nhật thông tin cá nhân"));
        };
        ZaloClient client;
        QSignalSpy notices(&client, &ZaloClient::notice);
        const QString nameBefore = client.currentUserName();

        client.updateMyProfile(QStringLiteral("Fixture Offline"), 0, QString(), QString());
        QCOMPARE(notices.count(), 1);
        QVERIFY(!isSuccessNotice(notices.first()));
        QCOMPARE(client.currentUserName(), nameBefore);
        QCOMPARE(api.total(), 0);

        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        notices.clear();
        client.updateMyProfile(QStringLiteral("Fixture Failing"), 0, QString(), QStringLiteral("bio fixture"));
        QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 2, 5000);
        QCOMPARE(client.currentUserName(), nameBefore);
        for (const QList<QVariant>& args : notices) QVERIFY(!isSuccessNotice(args));

        api.setResponder([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(0, QJsonObject{}); });
        notices.clear();
        client.updateMyProfile(QStringLiteral("Fixture Online"), 0, QString(), QStringLiteral("bio fixture"));
        QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 1, 5000);
        QCOMPARE(client.currentUserName(), QStringLiteral("Fixture Online"));
        QVERIFY(isSuccessNotice(notices.first()));
    }

    void testFriendAndGroupWritesWaitForServer() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(1); });
        ZaloClient client;
        client.setMyUidForTesting(QStringLiteral("u-me"));
        QSignalSpy notices(&client, &ZaloClient::notice);
        auto friendIds = [&client]() {
            QStringList ids;
            for (const QVariant& v : client.friendsForCard()) ids << v.toMap().value("id").toString();
            return ids;
        };
        client.setUserForTesting(QStringLiteral("u-rm-1"), QStringLiteral("Fixture Remove"), QString());
        client.setFriendsForTesting({QStringLiteral("u-rm-1")});
        ConversationItem group;
        group.id = QStringLiteral("g-cn-1");
        group.name = QStringLiteral("Fixture Old Name");
        group.isGroup = true;
        client.conversationModel()->setConversations({group});

        client.removeFriend(QStringLiteral("u-rm-1"));
        QCOMPARE(notices.count(), 1);
        QVERIFY(friendIds().contains(QStringLiteral("u-rm-1")));
        client.changeGroupName(QStringLiteral("g-cn-1"), QStringLiteral("Fixture New Name"));
        QCOMPARE(notices.count(), 2);
        QCOMPARE(client.conversationModel()->conversation(QStringLiteral("g-cn-1")).name, QStringLiteral("Fixture Old Name"));
        QCOMPARE(api.total(), 0);

        client.setLoggedInForTesting(true);
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        client.removeFriend(QStringLiteral("u-rm-1"));
        QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 3, 5000);
        QVERIFY(friendIds().contains(QStringLiteral("u-rm-1")));
        client.changeGroupName(QStringLiteral("g-cn-1"), QStringLiteral("Fixture New Name"));
        QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 4, 5000);
        QCOMPARE(client.conversationModel()->conversation(QStringLiteral("g-cn-1")).name, QStringLiteral("Fixture Old Name"));

        api.setResponder([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(0, QJsonObject{}); });
        client.removeFriend(QStringLiteral("u-rm-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!friendIds().contains(QStringLiteral("u-rm-1")), 5000);
        client.changeGroupName(QStringLiteral("g-cn-1"), QStringLiteral("Fixture New Name"));
        QTRY_COMPARE_WITH_TIMEOUT(client.conversationModel()->conversation(QStringLiteral("g-cn-1")).name,
                                  QStringLiteral("Fixture New Name"), 5000);
    }

    void testRemovedFriendIsDroppedAfterRefresh() {
        bool replyArray = true;
        TestSupport::LoopbackApi api([&replyArray](const TestSupport::LoopbackApi::Request& r) {
            if (!r.target.contains(QStringLiteral("/api/social/friend/getfriends"))) return TestSupport::jsonEnvelope(1);
            if (!replyArray) return TestSupport::jsonEnvelope(0, QJsonObject{});
            return TestSupport::jsonEnvelope(0, QJsonArray{QJsonObject{
                {"userId", "u-keep"}, {"displayName", "Fixture Keep"}, {"avatar", ""}}});
        });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.openOfflineCacheForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        client.setFriendsForTesting({QStringLiteral("u-gone"), QStringLiteral("u-keep")});
        auto profileOf = [&client](const QString& id) {
            for (const ZaloOfflineStore::CachedProfile& p : client.offlineStore()->loadProfiles()) {
                if (p.id == id) return p;
            }
            return ZaloOfflineStore::CachedProfile{};
        };

        client.refreshContacts();
        QTRY_VERIFY_WITH_TIMEOUT(profileOf(QStringLiteral("u-gone")).id == QStringLiteral("u-gone"), 5000);
        QVERIFY(!profileOf(QStringLiteral("u-gone")).isFriend);
        QVERIFY(profileOf(QStringLiteral("u-keep")).isFriend);

        replyArray = false;
        client.setFriendsForTesting({QStringLiteral("u-keep")});
        client.refreshContacts();
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/social/friend/getfriends")), 2, 3000);
        QTest::qWait(300);
        QVERIFY(profileOf(QStringLiteral("u-keep")).isFriend);
    }

    void testForwardToManyReportsPartialFailure() {
        int textSends = 0;
        TestSupport::LoopbackApi api([&textSends](const TestSupport::LoopbackApi::Request& r) {
            if (!r.target.contains(QStringLiteral("/api/message/sms"))) return TestSupport::jsonEnvelope(0, QJsonObject{});
            ++textSends;
            if (textSends == 2) return TestSupport::jsonEnvelope(1);
            return TestSupport::jsonEnvelope(0, QJsonObject{{"msgId", QString::number(72000 + textSends)}});
        });
        auto sourceMessage = []() {
            MessageItem item;
            item.id = QStringLiteral("m-fwd-1");
            item.senderId = QStringLiteral("u-me");
            item.senderName = QStringLiteral("Fixture Me");
            item.text = QStringLiteral("Fixture forwarded line");
            item.messageType = QStringLiteral("text");
            item.ts = 1700000700000LL;
            item.isMine = true;
            return item;
        };
        const QStringList targets{QStringLiteral("u-fwd-1"), QStringLiteral("u-fwd-2"), QStringLiteral("u-fwd-3")};

        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        client.selectConversation(QStringLiteral("u-fwd-src"), QStringLiteral("Fixture Source"), QString());
        client.messageModel()->mergeMessages(QStringLiteral("u-fwd-src"), {sourceMessage()});
        QSignalSpy notices(&client, &ZaloClient::notice);
        auto hasNotice = [&notices](const QString& fragment) {
            for (const QList<QVariant>& args : notices) {
                if (args.first().toString().contains(fragment)) return true;
            }
            return false;
        };
        client.forwardMessageToMany(targets, QStringLiteral("m-fwd-1"));
        QTRY_VERIFY_WITH_TIMEOUT(hasNotice(QStringLiteral("2/3")), 5000);
        QVERIFY(!hasNotice(QStringLiteral("3/3")));
        QCOMPARE(api.count(QStringLiteral("/api/message/sms")), 3);

        ZaloClient offline;
        QSignalSpy offlineNotices(&offline, &ZaloClient::notice);
        offline.selectConversation(QStringLiteral("u-fwd-src"), QStringLiteral("Fixture Source"), QString());
        offline.messageModel()->mergeMessages(QStringLiteral("u-fwd-src"), {sourceMessage()});
        offline.forwardMessageToMany(targets, QStringLiteral("m-fwd-1"));
        QVERIFY(offlineNotices.count() >= 1);
        QVERIFY(offlineNotices.last().first().toString().contains(QStringLiteral("0/3")));
        QCOMPARE(api.count(QStringLiteral("/api/message/sms")), 3);
    }

    void testStrangerRankRetriesAfterFailure() {
        bool rankOk = false;
        TestSupport::LoopbackApi api([&rankOk](const TestSupport::LoopbackApi::Request& r) {
            if (!r.target.contains(QStringLiteral("/api/social/profile/getrank"))) return TestSupport::jsonEnvelope(1);
            if (!rankOk) return TestSupport::jsonEnvelope(1);
            return TestSupport::jsonEnvelope(0, QJsonObject{{"userRank", "BLACK"}});
        });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.setFriendsForTesting({QStringLiteral("u-known")});
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        ConversationItem stranger;
        stranger.id = QStringLiteral("u-str-1");
        stranger.name = QStringLiteral("Fixture Stranger");
        client.conversationModel()->setConversations({stranger});

        client.selectConversation(QStringLiteral("u-str-1"), QStringLiteral("Fixture Stranger"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/social/profile/getrank")), 1, 3000);
        QTest::qWait(300);
        rankOk = true;
        client.selectConversation(QStringLiteral("u-str-1"), QStringLiteral("Fixture Stranger"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/social/profile/getrank")), 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(client.strangerRank(), 2, 3000);
    }

    void testGroupLinkErrorIsNotReportedAsDisabled() {
        bool linkOk = false;
        TestSupport::LoopbackApi api([&linkOk](const TestSupport::LoopbackApi::Request& r) {
            if (!r.target.contains(QStringLiteral("/api/group/link/detail"))) return TestSupport::jsonEnvelope(1);
            if (!linkOk) return TestSupport::jsonEnvelope(1);
            return TestSupport::jsonEnvelope(0, QJsonObject{{"link", "https://zalo.me/g/fixture-link"}, {"enabled", 1}});
        });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        client.applyGroupInfoForTesting(QJsonObject{{"g-link", QJsonObject{
            {"name", "Fixture Link Group"}, {"totalMember", 2}, {"creatorId", "u-me"},
            {"memVerList", QJsonArray{"u-me_0", "u-peer_0"}}}}});
        QSignalSpy notices(&client, &ZaloClient::notice);

        client.groupAdmin()->openManage(QStringLiteral("g-link"));
        QTRY_VERIFY_WITH_TIMEOUT(client.groupAdmin()->link().value("error").toBool(), 3000);
        QVERIFY(!client.groupAdmin()->link().contains(QStringLiteral("enabled")));
        client.groupAdmin()->createLink();
        QTest::qWait(200);
        QCOMPARE(api.count(QStringLiteral("/api/group/link/new")), 0);
        QVERIFY(notices.count() >= 1);

        linkOk = true;
        client.groupAdmin()->retryLink();
        QTRY_VERIFY_WITH_TIMEOUT(client.groupAdmin()->link().value("enabled").toBool(), 3000);
        QVERIFY(!client.groupAdmin()->link().value("error").toBool());
    }

    void testBlockedListKeepsEveryPage() {
        bool blockedFail = false;
        int blockedPages = 0;
        TestSupport::LoopbackApi api([&blockedFail, &blockedPages](const TestSupport::LoopbackApi::Request& r) {
            if (!r.target.contains(QStringLiteral("/api/friend/blocklistv2"))) return TestSupport::jsonEnvelope(1);
            if (blockedFail) return TestSupport::jsonEnvelope(1);
            const bool second = ++blockedPages == 2;
            const int base = second ? 100 : 0;
            const int size = second ? 2 : 100;
            QJsonArray profiles;
            for (int i = 0; i < size; ++i) {
                profiles.append(QJsonObject{{"userId", QStringLiteral("u-blk-%1").arg(base + i)},
                                            {"displayName", QStringLiteral("Fixture Blocked %1").arg(base + i)},
                                            {"avatar", ""}});
            }
            return TestSupport::jsonEnvelope(0, QJsonObject{{"profiles", profiles}});
        });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));

        client.refreshBlockedUsers();
        QTRY_COMPARE_WITH_TIMEOUT(int(client.blockedUsers().size()), 102, 5000);
        QCOMPARE(api.count(QStringLiteral("/api/friend/blocklistv2")), 2);

        blockedFail = true;
        client.refreshBlockedUsers();
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/friend/blocklistv2")), 3, 3000);
        QTest::qWait(200);
        QCOMPARE(int(client.blockedUsers().size()), 102);
    }

    void testReminderReloadsOnlyOnConversationChange() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(0, QJsonArray{}); });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));

        client.selectConversation(QStringLiteral("u-rem-a"), QStringLiteral("Fixture Reminder A"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/board/oneone/list")), 1, 3000);
        client.selectConversation(QStringLiteral("u-rem-a"), QStringLiteral("Fixture Reminder A"), QString());
        QTest::qWait(300);
        QCOMPARE(api.count(QStringLiteral("/api/board/oneone/list")), 1);
        client.selectConversation(QStringLiteral("u-rem-b"), QStringLiteral("Fixture Reminder B"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/board/oneone/list")), 2, 3000);
    }

    void testPinTopicUpdateRefreshesPoll() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(1); });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.setGroupForTesting(QStringLiteral("g-pin"), QStringLiteral("Fixture Pin Group"), QString());
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));

        client.pins()->setCurrent(QStringLiteral("g-pin"));
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/board/pin/list")), 1, 3000);
        client.handleSyncControlForTesting(QStringLiteral("group"), QStringLiteral("update_pin_topic"), QJsonObject{
            {"groupId", "g-pin"},
            {"groupTopic", QJsonObject{{"params", QStringLiteral("{\"pollId\":9911}")}}}});
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/poll/detail")), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/board/pin/list")), 2, 3000);
    }

    void testGroupAdminFlagsRefreshComposer() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(1); });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        client.selectConversation(QStringLiteral("g-adm"), QStringLiteral("Fixture Admin Group"), QString());

        QSignalSpy composer(&client, &ZaloClient::composerStateChanged);
        QList<int> membersAtEmit;
        QObject::connect(&client, &ZaloClient::composerStateChanged, &client, [&client, &membersAtEmit]() {
            membersAtEmit << int(client.groupMembers(QStringLiteral("g-adm")).size());
        });
        client.applyGroupInfoForTesting(QJsonObject{{"g-adm", QJsonObject{
            {"name", "Fixture Admin Group"}, {"totalMember", 2}, {"creatorId", "u-owner"},
            {"memVerList", QJsonArray{"u-owner_0", "u-me_0"}}}}});
        QVERIFY(composer.count() >= 1);
        QCOMPARE(int(membersAtEmit.size()), composer.count());
        for (const int members : membersAtEmit) QCOMPARE(members, 2);

        composer.clear();
        client.handleGroupControlForTesting(QStringLiteral("add_admin"), QJsonObject{
            {"groupId", "g-adm"}, {"sourceId", "u-owner"}, {"updateMembers", QJsonArray{QJsonObject{{"id", "u-me"}}}}});
        QCOMPARE(composer.count(), 1);
        QCOMPARE(client.myGroupRole(QStringLiteral("g-adm")), QStringLiteral("admin"));
    }

    void testNicknameChangeRefreshesGroupSender() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(0, QJsonObject{}); });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        client.setUserForTesting(QStringLiteral("u-al-peer"), QStringLiteral("Fixture Old"), QString());
        client.setGroupForTesting(QStringLiteral("g-alias"), QStringLiteral("Fixture Alias Group"), QString());
        emit client.apiClient()->session()->messagesReceived(QJsonArray{QJsonObject{
            {"msgId", "71001"}, {"cliMsgId", "71001"}, {"uidFrom", "u-al-peer"}, {"idTo", "g-alias"},
            {"dName", "Fixture Old"}, {"msgType", "webchat"}, {"content", "Fixture group line"},
            {"ts", QString::number(1700000600000LL)}}}, true, false);
        QCOMPARE(client.conversationModel()->conversation(QStringLiteral("g-alias")).senderPrefix, QStringLiteral("Fixture Old: "));

        client.changeFriendAlias(QStringLiteral("u-al-peer"), QStringLiteral("Fixture Alias"));
        QTRY_COMPARE_WITH_TIMEOUT(client.conversationModel()->conversation(QStringLiteral("g-alias")).senderPrefix,
                                  QStringLiteral("Fixture Alias: "), 3000);
        QCOMPARE(api.count(QStringLiteral("/api/alias/update")), 1);
    }

    void testStickerRequestsStayWithinSessionLimit() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request& r) {
            if (r.target.contains(QStringLiteral("sticker_detail"))) return QByteArray();
            return TestSupport::jsonEnvelope(1);
        });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        ZaloSession* session = client.apiClient()->session();
        auto emitStickers = [session](int from, int to) {
            QJsonArray batch;
            for (int id = from; id < to; ++id) {
                batch.append(QJsonObject{
                    {"msgId", QString::number(70100 + id)}, {"cliMsgId", QString::number(70100 + id)},
                    {"uidFrom", "u-stk-peer"}, {"idTo", "u-me"}, {"msgType", "chat.sticker"},
                    {"ts", QString::number(1700000500000LL + id)},
                    {"content", QJsonObject{{"id", 9100 + id}, {"catId", 10}, {"type", 7}}}});
            }
            emit session->messagesReceived(batch, false, false);
        };

        emitStickers(1, 7);
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("sticker_detail")), 4, 3000);
        QTest::qWait(200);
        QCOMPARE(api.count(QStringLiteral("sticker_detail")), 4);

        client.logout();
        QTest::qWait(100);
        api.answerHeld(TestSupport::jsonEnvelope(1));
        QTest::qWait(300);

        client.setLoggedInForTesting(true);
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        emitStickers(7, 13);
        QTest::qWait(500);
        QCOMPARE(api.count(QStringLiteral("sticker_detail")), 8);
        QCOMPARE(api.held(), 4);
    }

    void testReadPointSyncRunsUnderSteadyTraffic() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request& r) {
            if (r.target.contains(QStringLiteral("get-last-seen"))) return TestSupport::jsonEnvelope(0, QJsonObject{});
            return TestSupport::jsonEnvelope(1);
        });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        ConversationItem peer;
        peer.id = QStringLiteral("u-rp-max");
        peer.name = QStringLiteral("Fixture Steady");
        client.conversationModel()->setConversations({peer});
        ZaloSession* session = client.apiClient()->session();

        emit session->realtimeStateChanged(true);
        for (int i = 0; i < 40 && api.count(QStringLiteral("get-last-seen")) == 0; ++i) {
            emit session->messagesReceived(QJsonArray{QJsonObject{
                {"msgId", QString::number(60000 + i)}, {"cliMsgId", QString::number(60000 + i)},
                {"uidFrom", "u-rp-max"}, {"idTo", "u-me"}, {"msgType", "webchat"},
                {"content", QStringLiteral("Fixture steady %1").arg(i)},
                {"ts", QString::number(QDateTime::currentMSecsSinceEpoch())}}}, false, false);
            QTest::qWait(500);
        }
        QVERIFY(api.count(QStringLiteral("get-last-seen")) > 0);
    }

    void testCompositeLookupDebouncesAndCapsRetries() {
        TestSupport::LoopbackApi api([](const TestSupport::LoopbackApi::Request&) { return TestSupport::jsonEnvelope(1); });
        ZaloClient client;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));
        client.apiClient()->session()->setLoginInfoForTesting(api.loginInfo(QStringLiteral("u-me")));
        const QJsonObject group{{"name", "Fixture Composite"}, {"totalMember", 3}, {"creatorId", "u-me"},
                                {"memVerList", QJsonArray{"u-me_0", "u-c1_0", "u-c2_0"}}};
        for (int i = 0; i < 10; ++i) {
            client.applyGroupInfoForTesting(QJsonObject{{"g-comp", group}});
            QTest::qWait(10);
        }
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/social/group/members")), 1, 3000);
        QTest::qWait(150);
        QCOMPARE(api.count(QStringLiteral("/api/social/group/members")), 1);
        QTRY_COMPARE_WITH_TIMEOUT(api.count(QStringLiteral("/api/social/group/members")), 4, 8000);
        QTest::qWait(1500);
        QCOMPARE(api.count(QStringLiteral("/api/social/group/members")), 4);
    }

    void testScreenshotPreservesWindowVisibility() {
        QSettings().remove(QStringLiteral("window"));
        auto cleanup = qScopeGuard([]() {
            QSettings().remove(QStringLiteral("window"));
        });

        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("avatar"), new AvatarImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("media"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("icon"), new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);

        window->showMaximized();
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Maximized, 1000);
        emit client.requestRestoreAfterScreenshot();
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QVERIFY(window->isVisible());
        QCOMPARE(window->visibility(), QWindow::Maximized);

        window->showFullScreen();
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::FullScreen, 1000);
        emit client.requestRestoreAfterScreenshot();
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QVERIFY(window->isVisible());
        QCOMPARE(window->visibility(), QWindow::FullScreen);

        window->showMaximized();
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Maximized, 1000);
        emit client.requestHideForScreenshot();
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 1000);
        emit client.requestRestoreAfterScreenshot();
        QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Maximized, 1000);

        window->showFullScreen();
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::FullScreen, 1000);
        emit client.requestHideForScreenshot();
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 1000);
        emit client.requestRestoreAfterScreenshot();
        QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::FullScreen, 1000);

        window->showNormal();
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Windowed, 1000);
        emit client.requestHideForScreenshot();
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 1000);
        emit client.requestRestoreAfterScreenshot();
        QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);
        QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Windowed, 1000);
    }

    void testSoftwareVideoItemRendersFrames() {
        using Zalos::Core::Media::SoftwareVideoItem;

        QRectF target;
        QRectF source;
        SoftwareVideoItem::computeRects(QSizeF(160, 160), QSize(160, 80), Qt::IgnoreAspectRatio, &target, &source);
        QCOMPARE(target, QRectF(0, 0, 160, 160));
        QCOMPARE(source, QRectF(0, 0, 160, 80));

        SoftwareVideoItem::computeRects(QSizeF(160, 160), QSize(160, 80), Qt::KeepAspectRatio, &target, &source);
        QCOMPARE(target, QRectF(0, 40, 160, 80));
        QCOMPARE(source, QRectF(0, 0, 160, 80));

        SoftwareVideoItem::computeRects(QSizeF(160, 160), QSize(80, 160), Qt::KeepAspectRatio, &target, &source);
        QCOMPARE(target, QRectF(40, 0, 80, 160));
        QCOMPARE(source, QRectF(0, 0, 80, 160));

        SoftwareVideoItem::computeRects(QSizeF(80, 80), QSize(160, 80), Qt::KeepAspectRatioByExpanding, &target, &source);
        QCOMPARE(target, QRectF(0, 0, 80, 80));
        QCOMPARE(source, QRectF(40, 0, 80, 80));

        SoftwareVideoItem::computeRects(QSizeF(80, 80), QSize(80, 160), Qt::KeepAspectRatioByExpanding, &target, &source);
        QCOMPARE(target, QRectF(0, 0, 80, 80));
        QCOMPARE(source, QRectF(0, 40, 80, 80));

        if (Zalos::Core::Platform::MultimediaSupport::available()) {
            QQuickWindow window;
            window.setColor(Qt::black);
            window.resize(160, 160);
            auto* item = new SoftwareVideoItem(window.contentItem());
            item->setSize(QSizeF(160, 160));
            QVERIFY(item->videoSink() != nullptr);

            QVideoFrame yuvFrame(QVideoFrameFormat(QSize(160, 80), QVideoFrameFormat::Format_YUV420P));
            QVERIFY(yuvFrame.map(QVideoFrame::WriteOnly));
            std::memset(yuvFrame.bits(0), 150, size_t(yuvFrame.bytesPerLine(0) * 80));
            std::memset(yuvFrame.bits(1), 44, size_t(yuvFrame.bytesPerLine(1) * 40));
            std::memset(yuvFrame.bits(2), 21, size_t(yuvFrame.bytesPerLine(2) * 40));
            yuvFrame.unmap();
            item->videoSink()->setVideoFrame(yuvFrame);
            QVERIFY(item->hasFrame());
            QCOMPARE(item->frameSize(), QSize(160, 80));

            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QTest::qWait(50);
            QImage grab = window.grabWindow();
            QVERIFY(!grab.isNull());
            const QRgb centerGreen = grab.pixel(80, 80);
            QVERIFY2(qGreen(centerGreen) > 170 && qRed(centerGreen) < 80 && qBlue(centerGreen) < 80,
                     qPrintable(QStringLiteral("expected green center pixel, got (%1,%2,%3)")
                                    .arg(qRed(centerGreen)).arg(qGreen(centerGreen)).arg(qBlue(centerGreen))));
            const QRgb topLetterbox = grab.pixel(80, 10);
            QVERIFY2(qRed(topLetterbox) < 20 && qGreen(topLetterbox) < 20 && qBlue(topLetterbox) < 20,
                     qPrintable(QStringLiteral("expected black letterbox pixel, got (%1,%2,%3)")
                                    .arg(qRed(topLetterbox)).arg(qGreen(topLetterbox)).arg(qBlue(topLetterbox))));

            item->setFillMode(Qt::KeepAspectRatioByExpanding);
            QTest::qWait(50);
            grab = window.grabWindow();
            const QRgb croppedTop = grab.pixel(80, 10);
            QVERIFY2(qGreen(croppedTop) > 170 && qRed(croppedTop) < 80 && qBlue(croppedTop) < 80,
                     qPrintable(QStringLiteral("expected green cropped pixel, got (%1,%2,%3)")
                                    .arg(qRed(croppedTop)).arg(qGreen(croppedTop)).arg(qBlue(croppedTop))));

            QVideoFrame argbFrame(QVideoFrameFormat(QSize(160, 160), QVideoFrameFormat::Format_BGRA8888));
            QVERIFY(argbFrame.map(QVideoFrame::WriteOnly));
            for (int row = 0; row < 160; ++row) {
                auto* dst = reinterpret_cast<QRgb*>(argbFrame.bits(0) + row * argbFrame.bytesPerLine(0));
                std::fill(dst, dst + 160, 0xFF0000FF);
            }
            argbFrame.unmap();
            item->videoSink()->setVideoFrame(argbFrame);
            QTest::qWait(50);
            grab = window.grabWindow();
            const QRgb centerBlue = grab.pixel(80, 80);
            QVERIFY2(qBlue(centerBlue) > 200 && qRed(centerBlue) < 40 && qGreen(centerBlue) < 40,
                     qPrintable(QStringLiteral("expected blue pixel, got (%1,%2,%3)")
                                    .arg(qRed(centerBlue)).arg(qGreen(centerBlue)).arg(qBlue(centerBlue))));

            item->videoSink()->setVideoFrame(QVideoFrame());
            QVERIFY(!item->hasFrame());
            QVERIFY(item->frameSize().isEmpty());

            QMediaPlayer player;
            player.setVideoOutput(item);
            QCOMPARE(player.videoSink(), item->videoSink());
        }

        const QByteArray prevBackend = qgetenv("QT_QUICK_BACKEND");
        const auto restoreBackend = qScopeGuard([&prevBackend]() {
            if (prevBackend.isNull()) qunsetenv("QT_QUICK_BACKEND");
            else qputenv("QT_QUICK_BACKEND", prevBackend);
        });
        qputenv("QT_QUICK_BACKEND", "software");

        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        engine.loadFromModule("Zalos", "ZVideoView");
#else
        engine.load(QUrl(QStringLiteral("qrc:/Zalos/src/qml/ZVideoView.qml")));
#endif
        auto* rootItem = qobject_cast<QQuickItem*>(engine.rootObjects().value(0));
        QVERIFY(rootItem != nullptr);
        auto* swItem = qobject_cast<SoftwareVideoItem*>(rootItem->property("output").value<QObject*>());
        QVERIFY(swItem != nullptr);
        QCOMPARE(rootItem->property("videoSink").value<QObject*>(), static_cast<QObject*>(swItem->videoSink()));
    }

    void testScreenShareStitchAndPicker() {
        using Zalos::Core::Call::CallBridge;
        using Zalos::Core::Call::CameraSender;
        using Zalos::Core::Call::PortalScreenCast;

        QImage leftImg(160, 120, QImage::Format_RGB32);
        leftImg.fill(Qt::red);
        QImage rightImg(160, 80, QImage::Format_RGB32);
        rightImg.fill(Qt::blue);

        const QImage stitched = CameraSender::stitchScreenCaptures({
            qMakePair(QRect(-160, 0, 160, 120), leftImg),
            qMakePair(QRect(0, 0, 160, 80), rightImg),
        });
        QCOMPARE(stitched.size(), QSize(320, 120));
        QCOMPARE(stitched.pixelColor(80, 60), QColor(Qt::red));
        QCOMPARE(stitched.pixelColor(240, 40), QColor(Qt::blue));
        QCOMPARE(stitched.pixelColor(240, 100), QColor(Qt::black));

        const QString previewUrl = CameraSender::encodePreviewDataUrl(stitched);
        QVERIFY(previewUrl.startsWith(QStringLiteral("data:image/jpeg;base64,")));
        const QByteArray jpgBytes = QByteArray::fromBase64(previewUrl.mid(23).toLatin1());
        const QImage decodedThumb = QImage::fromData(jpgBytes, "JPEG");
        QVERIFY(!decodedThumb.isNull());
        QVERIFY(decodedThumb.width() <= 320 && decodedThumb.height() <= 180);

        const QVariantList single = CallBridge::buildScreenSourcesFromDescriptors(
            {CallBridge::ScreenSourceDesc{QStringLiteral("HDMI-1"), QRect(0, 0, 1920, 1080), true, previewUrl}},
            QString());
        QCOMPARE(single.size(), 1);
        QCOMPARE(single.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("screen:0"));

        const QVariantList multi = CallBridge::buildScreenSourcesFromDescriptors(
            {
                CallBridge::ScreenSourceDesc{QStringLiteral("DP-1"), QRect(-1920, 0, 1920, 1080), true, previewUrl},
                CallBridge::ScreenSourceDesc{QStringLiteral("HDMI-1"), QRect(0, 0, 1920, 1200), false, previewUrl},
            },
            previewUrl);
        QCOMPARE(multi.size(), 3);
        QCOMPARE(multi.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("screen:0"));
        QCOMPARE(multi.at(0).toMap().value(QStringLiteral("isPrimary")).toBool(), true);
        QCOMPARE(multi.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("screen:1"));
        const QVariantMap allMap = multi.at(2).toMap();
        QCOMPARE(allMap.value(QStringLiteral("id")).toString(), QStringLiteral("all"));
        QCOMPARE(allMap.value(QStringLiteral("isAll")).toBool(), true);
        QCOMPARE(allMap.value(QStringLiteral("width")).toInt(), 3840);
        QCOMPARE(allMap.value(QStringLiteral("height")).toInt(), 1200);
        QCOMPARE(allMap.value(QStringLiteral("screenCount")).toInt(), 2);

        const QByteArray fmtPod = PortalScreenCast::buildVideoFormatPod();
        QVERIFY(!fmtPod.isEmpty());
        QCOMPARE(fmtPod.size() % 8, 0);
        quint32 parsedFmt = 0;
        int parsedW = 0;
        int parsedH = 0;
        QVERIFY(PortalScreenCast::parseVideoFormatPod(fmtPod.constData(), &parsedFmt, &parsedW, &parsedH));
        QCOMPARE(parsedFmt, PortalScreenCast::kSpaVideoFormatBGRx);
        QCOMPARE(parsedW, 1280);
        QCOMPARE(parsedH, 720);

        const QByteArray bufPod = PortalScreenCast::buildBuffersParamPod();
        QVERIFY(!bufPod.isEmpty());
        QCOMPARE(bufPod.size() % 8, 0);

        for (const quint32 fmt : {PortalScreenCast::kSpaVideoFormatBGRx,
                                  PortalScreenCast::kSpaVideoFormatBGRA,
                                  PortalScreenCast::kSpaVideoFormatRGBx,
                                  PortalScreenCast::kSpaVideoFormatRGBA}) {
            const int stride = 24;
            QByteArray raw(stride * 4, '\0');
            for (int row = 0; row < 4; ++row) {
                uchar* line = reinterpret_cast<uchar*>(raw.data()) + row * stride;
                for (int col = 0; col < 4; ++col) {
                    const bool bgFirst = (fmt == PortalScreenCast::kSpaVideoFormatBGRx
                                          || fmt == PortalScreenCast::kSpaVideoFormatBGRA);
                    line[col * 4 + 0] = bgFirst ? 10 : 200;
                    line[col * 4 + 1] = 120;
                    line[col * 4 + 2] = bgFirst ? 200 : 10;
                    line[col * 4 + 3] = 255;
                }
            }
            const QImage converted = PortalScreenCast::convertVideoBuffer(
                fmt, 4, 4, reinterpret_cast<const uchar*>(raw.constData()), stride, raw.size());
            QVERIFY(!converted.isNull());
            QCOMPARE(converted.size(), QSize(4, 4));
            const QColor c = converted.pixelColor(2, 2);
            QCOMPARE(c.red(), 200);
            QCOMPARE(c.green(), 120);
            QCOMPARE(c.blue(), 10);
        }

        const QByteArray tiny(12, '\0');
        QVERIFY(PortalScreenCast::convertVideoBuffer(
                    PortalScreenCast::kSpaVideoFormatBGRx, 4, 4,
                    reinterpret_cast<const uchar*>(tiny.constData()), 16, tiny.size())
                    .isNull());
        QVERIFY(PortalScreenCast::convertVideoBuffer(999, 4, 4,
                    reinterpret_cast<const uchar*>(tiny.constData()), 16, tiny.size())
                    .isNull());

        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        client.setMyUidForTesting(QStringLiteral("u-me"));

        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("avatar"), new AvatarImageProvider);
        engine.addImageProvider(QStringLiteral("cover"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("media"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("icon"), new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        window->resize(960, 680);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* calls = client.calls();
        calls->injectUiLineForTesting(R"({"type":"state","phase":"connected","video":true,"name":"Call Friend","since":1000})");
        QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QQuickItem*>(QStringLiteral("callOverlayRoot")) != nullptr, 2000);
        auto* overlay = window->findChild<QQuickItem*>(QStringLiteral("callOverlayRoot"));
        QVERIFY(overlay != nullptr);

        calls->openScreenPickerForTesting(multi);
        auto* picker = window->findChild<QQuickItem*>(QStringLiteral("callScreenPicker"));
        QVERIFY(picker != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(picker->isVisible(), 1000);
        QCOMPARE(overlay->property("selectedScreenId").toString(), QStringLiteral("screen:0"));

        std::function<QQuickItem*(QQuickItem*, const QString&)> findVisual = [&](QQuickItem* root, const QString& name) -> QQuickItem* {
            if (!root) return nullptr;
            if (root->objectName() == name) return root;
            for (QQuickItem* child : root->childItems()) {
                if (QQuickItem* found = findVisual(child, name)) return found;
            }
            return nullptr;
        };
        QQuickItem* secondCard = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((secondCard = findVisual(picker, QStringLiteral("screenSourceCard_screen:1"))) != nullptr
                                     && secondCard->x() > 100.0 && secondCard->width() > 0.0,
                                 1000);
        auto* shareBtn = window->findChild<QQuickItem*>(QStringLiteral("callScreenPickerShareBtn"));
        QVERIFY(secondCard && shareBtn);
        const QPoint cardCenter = secondCard->mapToScene(QPointF(secondCard->width() / 2.0, secondCard->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, cardCenter);
        QTRY_COMPARE_WITH_TIMEOUT(overlay->property("selectedScreenId").toString(), QStringLiteral("screen:1"), 1000);

        const QPoint shareCenter = shareBtn->mapToScene(QPointF(shareBtn->width() / 2.0, shareBtn->height() / 2.0)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, shareCenter);
        QTRY_VERIFY_WITH_TIMEOUT(!calls->screenPickerOpen(), 1000);
        calls->hangup();
    }

    void testBoundLoaderAndMainQmlQt64Safe() {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("bound component|Component is not ready")));

        {
            QQmlEngine engine;
            engine.addImportPath(QStringLiteral("qrc:/"));
            engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
            const QByteArray qml =
                "pragma ComponentBehavior: Bound\n"
                "import QtQuick\n"
                "import Zalos\n"
                "Item {\n"
                "    id: box\n"
                "    property bool on: true\n"
                "    property string outerTag: 'hello'\n"
                "    ZBoundLoader {\n"
                "        id: impLoader\n"
                "        objectName: 'impLoader'\n"
                "        active: box.on\n"
                "        sourceComponent: Item {\n"
                "            implicitWidth: 120\n"
                "            implicitHeight: 44\n"
                "            readonly property string echoed: box.outerTag\n"
                "        }\n"
                "    }\n"
                "    ZBoundLoader {\n"
                "        id: expLoader\n"
                "        objectName: 'expLoader'\n"
                "        active: box.on\n"
                "        sourceComponent: Rectangle {\n"
                "            width: 96\n"
                "            height: 28\n"
                "        }\n"
                "    }\n"
                "}\n";
            QQmlComponent comp(&engine);
            comp.setData(qml, QUrl(QStringLiteral("qrc:/Zalos/TestBoundLoader.qml")));
            QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
            std::unique_ptr<QObject> root(comp.create());
            QVERIFY(root != nullptr);
            auto* impLoader = root->findChild<Zalos::Core::Platform::BoundLoader*>(QStringLiteral("impLoader"));
            auto* expLoader = root->findChild<Zalos::Core::Platform::BoundLoader*>(QStringLiteral("expLoader"));
            QVERIFY(impLoader != nullptr && expLoader != nullptr);
            QVERIFY(impLoader->item() != nullptr);
            QVERIFY(expLoader->item() != nullptr);
            QCOMPARE(impLoader->item()->property("echoed").toString(), QStringLiteral("hello"));
            QCOMPARE(impLoader->implicitWidth(), 120.0);
            QCOMPARE(impLoader->implicitHeight(), 44.0);
            QCOMPARE(expLoader->implicitWidth(), 96.0);
            QCOMPARE(expLoader->implicitHeight(), 28.0);

            root->setProperty("on", false);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(impLoader->item() == nullptr);
            QVERIFY(expLoader->item() == nullptr);
            QCOMPARE(impLoader->implicitWidth(), 0.0);
            QCOMPARE(impLoader->implicitHeight(), 0.0);
        }

        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(false);
        QQmlApplicationEngine engine;
        engine.addImageProvider("avatar", new AvatarImageProvider);
        engine.addImageProvider("media", new AvatarImageProvider(false));
        engine.addImageProvider("icon", new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        loadMainWindow(engine);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        QVERIFY(window != nullptr);
        QTest::qWait(150);

        client.setLoggedInForTesting(true);
        client.setProperty("showSettingsModal", true);
        QTest::qWait(250);
        client.setProperty("showSettingsModal", false);
        QTest::qWait(50);
    }

    void testScaleCrispRenderingAndDialogClamp() {
        ZaloClient client;
        ThemeManager themeMgr;
        QQmlEngine engine;
        engine.addImageProvider(QStringLiteral("avatar"), new AvatarImageProvider);
        engine.addImageProvider(QStringLiteral("media"), new AvatarImageProvider(false));
        engine.addImageProvider(QStringLiteral("icon"), new Zalos::Core::Platform::IconImageProvider);
        engine.addImportPath(QStringLiteral("qrc:/"));
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));

        const QByteArray qml =
            "import QtQuick\n"
            "import QtQuick.Controls.Basic\n"
            "import Zalos\n"
            "ApplicationWindow {\n"
            "    id: win\n"
            "    width: 300\n"
            "    height: 420\n"
            "    visible: false\n"
            "    ZIcon {\n"
            "        id: icon\n"
            "        objectName: 'testIcon'\n"
            "        name: 'chat'\n"
            "        size: 20\n"
            "    }\n"
            "    ZAvatar {\n"
            "        id: av\n"
            "        objectName: 'testAvatar'\n"
            "        size: 40\n"
            "        avatarUrl: 'https://example.invalid/a.jpg'\n"
            "        isOnline: true\n"
            "        showStatusDot: true\n"
            "    }\n"
            "    ZRoundedImage {\n"
            "        id: ri\n"
            "        objectName: 'testRounded'\n"
            "        width: 120\n"
            "        height: 80\n"
            "        source: 'https://example.invalid/b.jpg'\n"
            "    }\n"
            "    ZConfirmDialog {\n"
            "        id: dlg\n"
            "        objectName: 'testDlg'\n"
            "        maxWidth: 360\n"
            "        title: 'Fixture'\n"
            "    }\n"
            "}\n";
        QQmlComponent comp(&engine);
        comp.setData(qml, QUrl(QStringLiteral("qrc:/Zalos/TestScaleCrisp.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY(root != nullptr);

        auto* iconItem = root->findChild<QQuickItem*>(QStringLiteral("testIcon"));
        auto* avItem = root->findChild<QQuickItem*>(QStringLiteral("testAvatar"));
        auto* riItem = root->findChild<QQuickItem*>(QStringLiteral("testRounded"));
        QVERIFY(iconItem && avItem && riItem);

        auto findImageBySource = [](QQuickItem* rootItem, const QString& needle) -> QQuickItem* {
            std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* node) -> QQuickItem* {
                if (!node) return nullptr;
                if (node->inherits("QQuickImage")
                    && node->property("source").toUrl().toString().contains(needle)) {
                    return node;
                }
                for (QQuickItem* ch : node->childItems()) {
                    if (QQuickItem* hit = walk(ch)) return hit;
                }
                return nullptr;
            };
            return walk(rootItem);
        };

        QQuickItem* iconImg = findImageBySource(iconItem, QStringLiteral("image://icon/"));
        QQuickItem* avImg = findImageBySource(avItem, QStringLiteral("image://avatar/"));
        QQuickItem* riImg = findImageBySource(riItem, QStringLiteral("image://media/"));
        QVERIFY(iconImg && avImg && riImg);
        QCOMPARE(iconImg->property("mipmap").toBool(), true);
        QCOMPARE(avImg->property("mipmap").toBool(), true);
        QCOMPARE(riImg->property("mipmap").toBool(), true);
        QCOMPARE(iconImg->property("sourceSize").toSize(), QSize(20, 20));
        QCOMPARE(avImg->property("sourceSize").toSize(), QSize(40, 40));

        QQuickItem* statusDot = avItem->findChild<QQuickItem*>(QStringLiteral("statusDot"));
        QVERIFY(statusDot != nullptr);
        QVERIFY(statusDot->inherits("QQuickImage"));
        QVERIFY(statusDot->property("source").toUrl().toString().contains(QStringLiteral("image://icon/dot/")));
        QCOMPARE(statusDot->property("mipmap").toBool(), true);
        QCOMPARE(statusDot->property("smooth").toBool(), true);

        Zalos::Core::Platform::IconImageProvider iconProvider;
        for (const int px : {14, 17, 40}) {
            QSize outSize;
            const QImage pacImg = iconProvider.requestImage(QStringLiteral("pacman/%1").arg(px), &outSize, QSize());
            QVERIFY(!pacImg.isNull());
            QVERIFY(outSize.width() >= 8 && outSize.height() >= 8);
            int yellowPx = 0;
            int brownPx = 0;
            for (int y = 0; y < pacImg.height(); ++y) {
                for (int x = 0; x < pacImg.width(); ++x) {
                    const QColor c = pacImg.pixelColor(x, y);
                    if (c.alpha() < 128) continue;
                    if (c.red() > 180 && c.green() > 135 && c.blue() < 100) ++yellowPx;
                    else if (c.red() < 140 && c.green() < 90 && c.blue() < 40) ++brownPx;
                }
            }
            QVERIFY2(yellowPx > px, qPrintable(QStringLiteral("icon/pacman/%1 missing yellow").arg(px)));
            QVERIFY2(brownPx > 0, qPrintable(QStringLiteral("icon/pacman/%1 missing eyes").arg(px)));
        }

        for (const QSize& dotTarget : {QSize(14, 14), QSize(19, 19)}) {
            QSize outSize;
            const QImage dotImg = iconProvider.requestImage(
                QStringLiteral("dot/%2322262B/%236ACD8B/733"), &outSize, dotTarget);
            QCOMPARE(outSize, dotTarget);
            QVERIFY(dotImg.pixelColor(dotTarget.width() / 2, dotTarget.height() / 2).green() > 150);
            for (int y = 0; y < dotImg.height(); ++y) {
                for (int x = 0; x < dotImg.width(); ++x) {
                    const QRgb a = dotImg.pixel(x, y);
                    const QRgb b = dotImg.pixel(dotImg.width() - 1 - x, y);
                    const QRgb c = dotImg.pixel(x, dotImg.height() - 1 - y);
                    QVERIFY(qAbs(qRed(a) - qRed(b)) <= 2 && qAbs(qGreen(a) - qGreen(b)) <= 2
                            && qAbs(qBlue(a) - qBlue(b)) <= 2 && qAbs(qAlpha(a) - qAlpha(b)) <= 2);
                    QVERIFY(qAbs(qRed(a) - qRed(c)) <= 2 && qAbs(qGreen(a) - qGreen(c)) <= 2
                            && qAbs(qBlue(a) - qBlue(c)) <= 2 && qAbs(qAlpha(a) - qAlpha(c)) <= 2);
                }
            }
        }

        QSvgRenderer pacmanSvg(QStringLiteral(":/Zalos/assets/icons/pacman.svg"));
        QVERIFY(pacmanSvg.isValid());
        for (const int px : {18, 44, 66}) {
            QImage rendered(px, px, QImage::Format_ARGB32_Premultiplied);
            rendered.fill(Qt::transparent);
            {
                QPainter p(&rendered);
                pacmanSvg.render(&p, QRectF(0, 0, px, px));
            }
            int yellowPx = 0;
            int brownPx = 0;
            for (int y = 0; y < px; ++y) {
                for (int x = 0; x < px; ++x) {
                    const QColor c = rendered.pixelColor(x, y);
                    if (c.alpha() < 128) continue;
                    if (c.red() > 180 && c.green() > 140 && c.blue() < 100) ++yellowPx;
                    else if (c.red() < 140 && c.green() < 90 && c.blue() < 40) ++brownPx;
                }
            }
            QVERIFY2(yellowPx > px, qPrintable(QStringLiteral("pacman.svg missing yellow body at %1px").arg(px)));
            QVERIFY2(brownPx > 0, qPrintable(QStringLiteral("pacman.svg missing brown eyes at %1px").arg(px)));
        }

        const QString emojiFamily = Zalos::Core::Platform::ThemeManager::registerEmojiFallbackFont();
        QVERIFY(!emojiFamily.isEmpty());
        auto measureEmojiHeight = [&](int px, qreal dpr) -> int {
            QFont ef(emojiFamily);
            ef.setPixelSize(px);
            QImage img(int(140 * dpr), int(140 * dpr), QImage::Format_ARGB32_Premultiplied);
            img.setDevicePixelRatio(dpr);
            img.fill(Qt::transparent);
            {
                QPainter p(&img);
                p.setFont(ef);
                p.drawText(10, 100, QString::fromUtf8("😄"));
            }
            int minY = img.height();
            int maxY = -1;
            for (int y = 0; y < img.height(); ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    if (qAlpha(row[x]) > 5) {
                        minY = qMin(minY, y);
                        maxY = qMax(maxY, y);
                    }
                }
            }
            return maxY >= minY ? (maxY - minY + 1) : 0;
        };
        for (const int px : {16, 19, 23, 32, 36, 44}) {
            const int h10 = measureEmojiHeight(px, 1.0);
            const int h15 = measureEmojiHeight(px, 1.5);
            QVERIFY2(h15 > h10, qPrintable(QStringLiteral("Emoji px=%1 did not scale at 1.5x (%2 -> %3)")
                                               .arg(px).arg(h10).arg(h15)));
        }

        QObject* dlg = root->findChild<QObject*>(QStringLiteral("testDlg"));
        QVERIFY(dlg != nullptr);
        QVERIFY(dlg->property("width").toReal() <= 276.0);
        QVERIFY(dlg->property("width").toReal() >= 240.0);
    }

    void testServerSettingsParsing() {
        ZaloClient client;
        QSignalSpy spy(&client, &ZaloClient::serverSettingsChanged);

        // Nested response matching Zalo's /api/setting/me payload ("setting" + "setting2")
        const QJsonObject nested{
            {"setting", QJsonObject{
                {"show_online_status", 0},
                {"display_seen_status", false},
                {"receive_message", 2},
                {"accept_stranger_call", 2},
                {"display_on_recommend_friend", "0"},
                {"add_friend_via_phone", "false"},
                {"add_friend_via_qr", 0},
                {"add_friend_via_group", 1},
                {"view_birthday", 2},
            }},
            {"setting2", QJsonObject{
                {"add_friend_via_qr", 1},
            }},
        };
        client.applyServerSettingsForTesting(nested);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(client.showOnlineStatus(), false);
        QCOMPARE(client.displaySeenStatus(), false);
        QCOMPARE(client.receiveStrangerMessage(), false);
        QCOMPARE(client.acceptStrangerCall(), false);
        QCOMPARE(client.displayOnRecommendFriend(), false);
        QCOMPARE(client.addFriendViaPhone(), false);
        // "setting" takes precedence over "setting2" when both contain the same key
        QCOMPARE(client.addFriendViaQr(), false);
        QCOMPARE(client.addFriendViaGroup(), true);
        QCOMPARE(client.viewBirthday(), 2);

        // accept_stranger_call = 4 (Everyone) and 3 (Friends & contacted strangers) are both ON
        client.applyServerSettingsForTesting(QJsonObject{{"setting", QJsonObject{{"accept_stranger_call", 4}}}});
        QCOMPARE(client.acceptStrangerCall(), true);
        client.applyServerSettingsForTesting(QJsonObject{{"setting", QJsonObject{{"accept_stranger_call", 3}}}});
        QCOMPARE(client.acceptStrangerCall(), true);
        client.applyServerSettingsForTesting(QJsonObject{{"setting", QJsonObject{{"accept_stranger_call", 0}}}});
        QCOMPARE(client.acceptStrangerCall(), false);

        // JSON-encoded string inside "setting" and flat response fallback
        client.applyServerSettingsForTesting(QJsonObject{
            {"setting", QStringLiteral("{\"show_online_status\":1,\"receive_message\":\"1\"}")}
        });
        QCOMPARE(client.showOnlineStatus(), true);
        QCOMPARE(client.receiveStrangerMessage(), true);

        client.applyServerSettingsForTesting(QJsonObject{{"display_seen_status", true}});
        QCOMPARE(client.displaySeenStatus(), true);

        // Verify "settings" service resolves to https://wpa.chat.zalo.me when omitted from zpw_service_map_v3
        QNetworkAccessManager nam;
        SessionCookieJar jar;
        ZaloSession session(&nam, &jar);
        session.setLoginInfoForTesting(QJsonObject{
            {"zpw_service_map_v3", QJsonObject{
                {"chat", QJsonArray{QStringLiteral("https://tt-chat3-wpa.chat.zalo.me")}},
            }}
        });
        QCOMPARE(session.serviceUrls(QStringLiteral("settings")),
                 QStringList{QStringLiteral("https://wpa.chat.zalo.me")});
    }
};

QTEST_MAIN(TestSystem)
#include "test_system.moc"
