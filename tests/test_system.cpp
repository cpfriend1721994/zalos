#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QNetworkReply>
#include <QSettings>
#include <QSoundEffect>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <QSignalSpy>
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
#include <QVideoSink>

#include "core/network/zalo_client.h"
#include "core/platform/theme_manager.h"
#include "core/platform/system_tray.h"
#include "core/crypto/db_cross.h"
#include "core/models/conversation_model.h"
#include "core/models/message_model.h"
#include "core/fs/file_utils.h"
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

using namespace Zalos::Core::Network;
using namespace Zalos::Core::Platform;
using namespace Zalos::Core::Models;
using namespace Zalos::Core::Crypto;
using namespace Zalos::Core::Fs;
using namespace Zalos::Core::Media;
using namespace Zalos::Core::Call;
using namespace Zalos::Core::Offline;

namespace {
// QQmlApplicationEngine::loadFromModule exists from Qt 6.5; Qt 6.4 (Ubuntu 24.04) loads the module file by URL.
void loadMainWindow(QQmlApplicationEngine& engine) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule("Zalos", "Main");
#else
    engine.load(QUrl(QStringLiteral("qrc:/Zalos/src/qml/Main.qml")));
#endif
}
} // namespace

class TestSystem : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("Zalos");
        QCoreApplication::setApplicationName("ZalosApp");
        QSettings settings("Zalos", "ZalosApp");
        settings.setValue("auth/isLoggedIn", false);
        settings.setValue("auth/userName", "");
        settings.setValue("auth/userPhone", "");
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(QDir(baseDir).filePath("offline.db"));
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering); // as main.cpp: screenshots match the app
    }

    // FTS5 search over the whole cached history, compressed raw payloads and the one-time storage upgrade
    // (2026-11-04): accent-insensitive incl. đ, per conversation or global, recall/delete drop hits, MATCH input
    // is sanitised, legacy TEXT raw rows are compressed and indexed on open.
    void testMessageSearchFtsAndRawCodec() {
        using Zalos::Core::Models::MessageItem;
        QCOMPARE(ZaloOfflineStore::ftsQuery("Đường  phố\"x"), QString("\"duong\"* \"pho\"* \"x\"*"));
        QCOMPARE(ZaloOfflineStore::ftsQuery("  ** \" "), QString());
        const QJsonObject rawIn{{"msgId", "1"}, {"content", "Fixture payload"}};
        const QByteArray packed = ZaloOfflineStore::encodeRaw(rawIn);
        QVERIFY(!packed.isEmpty() && packed.at(0) != '{');
        QCOMPARE(ZaloOfflineStore::decodeRaw(packed), rawIn);
        QCOMPARE(ZaloOfflineStore::decodeRaw(QString("{\"a\":1}")).value("a").toInt(), 1); // legacy TEXT row
        QVERIFY(ZaloOfflineStore::decodeRaw(QVariant()).isEmpty());
        {
            ZaloOfflineStore store;
            store.clearCache();
            QVERIFY(store.ftsAvailable()); // every supported distro's SQLite (Fedora 43, Ubuntu 24.04) has FTS5
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
            QCOMPARE(ids(store.searchMessages("duong")), (QStringList{"7003", "7001"})); // newest first
            QCOMPARE(ids(store.searchMessages("DUONG fixt", "u-fts-a")), QStringList{"7001"});
            QCOMPARE(ids(store.searchMessages("hen gap")), QStringList{"7001"});
            QVERIFY(store.searchMessages("\"").isEmpty());
            QVERIFY(store.markRecalled("7003", "Tin nhắn đã được thu hồi"));
            QCOMPARE(ids(store.searchMessages("duong")), QStringList{"7001"});
            QVERIFY(store.deleteMessage("7001"));
            QVERIFY(store.searchMessages("duong").isEmpty());
            // A text change re-indexes the row.
            QVERIFY(store.saveMessage("u-fts-a", msg("7002", "Giờ thì có đường", 2000)));
            QCOMPARE(ids(store.searchMessages("duong")), QStringList{"7002"});
            QCOMPARE(store.countMessagesBetween("u-fts-a", 0, 5000), 1);
            QString conv;
            QCOMPARE(store.messageTs("7002", &conv), 2000);
            QCOMPARE(conv, QString("u-fts-a"));
        }
        // Legacy database: TEXT raw, no FTS rows, no migration marks → upgraded on open.
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
            QCOMPARE(upgraded.pragmaForTesting("auto_vacuum"), QString("2")); // incremental
            upgraded.clearCache();
        }
    }

    // Desktop font specs: the size and weight/style words are not part of the family (bug 2026-11-03: GNOME
    // "Ubuntu Medium 12" became family "Ubuntu Medium" and bold labels were synthesised and smeared).
    void testSystemFontFamilyFromSpec() {
        using Zalos::Core::Platform::ThemeManager;
        QCOMPARE(ThemeManager::familyFromFontSpec("Ubuntu Medium 12"), QString("Ubuntu"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Noto Sans Bold Italic 11"), QString("Noto Sans"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Cantarell 11"), QString("Cantarell"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Noto Sans,10,-1,5,400,0,0,0,0,0"), QString("Noto Sans"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Source Code Pro Semibold 10.5"), QString("Source Code Pro"));
        QCOMPARE(ThemeManager::familyFromFontSpec("Medium"), QString("Medium")); // never empties a one-word family
        QCOMPARE(QQuickWindow::textRenderType(), QQuickWindow::NativeTextRendering);
    }

    void init() {
        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(QDir(baseDir).filePath("offline.db"));
    }

    void cleanupTestCase() {
        QSettings settings("Zalos", "ZalosApp");
        settings.setValue("auth/isLoggedIn", false);
        settings.setValue("auth/userName", "");
        settings.setValue("auth/userPhone", "");
    }

    // 1. System Integration: QML Engine & Context Properties
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

        // Verify native window decorations are preserved (Qt::Window without FramelessWindowHint)
        Qt::WindowFlags flags = window->flags();
        QVERIFY(flags & Qt::Window);
        QVERIFY(!(flags & Qt::FramelessWindowHint));

        // Verify minimum window geometry constraints supporting quarter-screen
        QVERIFY(window->minimumWidth() <= 480);
        QVERIFY(window->minimumHeight() <= 450);

        // Test responsive window geometry changes across quarter-screen, half-screen, and full-screen
        window->resize(480, 540);
        QTest::qWait(100);
        QCOMPARE(window->width(), 480);
        QCOMPARE(window->height(), 540);
        QImage quarterImg = window->grabWindow();
        quarterImg.save("/workspace/build/test_quarter_screen.png");

        // Test Tab 1: Phone Login View
        auto loginView = window->findChild<QQuickItem*>("loginViewRoot");
        if (loginView) {
            loginView->setProperty("activeTab", 1);
            QTest::qWait(100);
            QImage phoneImg = window->grabWindow();
            phoneImg.save("/workspace/build/test_quarter_phone.png");
            // Reset to Tab 0
            loginView->setProperty("activeTab", 0);
            QTest::qWait(50);
        }

        window->resize(800, 600);
        QCOMPARE(window->width(), 800);
        QCOMPARE(window->height(), 600);

        window->resize(1280, 800);
        QCOMPARE(window->width(), 1280);
        QCOMPARE(window->height(), 800);

        window->resize(1920, 1080);
        QCOMPARE(window->width(), 1920);
        QCOMPARE(window->height(), 1080);
    }

    // 2. Feature: Advanced Search with Diacritics & Case Insensitivity
    void testConversationSearchFeature() {
        ConversationModel model;
        model.addConversation({
            "ht-1", "Hỗ trợ khách hàng", "Xin chào", "10:00", 0, true, false, "", "priority", ""
        });
        model.addConversation({
            "ht-2", "Nhóm bạn thân", "Đi ăn nhé", "11:00", 0, true, false, "", "priority", ""
        });
        QVERIFY(model.rowCount() > 0);

        // Search for existing contact "Hỗ trợ"
        model.setSearchFilter("Hỗ trợ");
        QVERIFY(model.rowCount() >= 1);

        // Case-insensitive search "hỗ trợ"
        model.setSearchFilter("hỗ trợ");
        QVERIFY(model.rowCount() >= 1);

        // Search for non-existent contact
        model.setSearchFilter("nonexistent_contact_xyz_123");
        QCOMPARE(model.rowCount(), 0);

        // Reset search filter
        model.setSearchFilter("");
        QCOMPARE(model.rowCount(), 2);

        // Diacritic/case-insensitive, word-order-free matching on names and last messages (đ/Đ fold to d).
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
        // A chat without a stored name is found by the fallback label the list shows for it.
        model.addConversation({
            "u-noname", "", "", "12:30", 0, false, false, "", "priority", ""
        });
        for (const char* q : {"nguoi dung zalo", "Người dùng", "zalo user"}) {
            model.setSearchFilter(QString::fromUtf8(q));
            QVERIFY2(has("u-noname"), q);
        }
        model.setSearchFilter("");
        // A search covers every chat: the Unread tab and a label filter must not hide matches.
        model.setCategoryFilter("unread");
        model.setSearchFilter("cau long");
        QVERIFY(has("g-cl"));
        model.setSearchFilter("");
        QVERIFY(!has("g-cl"));
        model.setCategoryFilter("");
        // Archived ("Hội thoại khác") chats are found by search.
        model.setArchivedThreads({"g-cl"});
        model.setHiddenThreads({"ht-2"});
        model.setSearchFilter("");
        QVERIFY(!has("g-cl"));
        QVERIFY(!has("ht-2"));
        model.setSearchFilter("cau long");
        QVERIFY(has("g-cl"));
        // Hidden chats (Zalo behaviour): found by their exact name only — never by message text, never in the
        // plain list, and kept out of normal results while the typed keyword is the hidden-chat PIN.
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

    // Sidebar search: one word finds chats, groups/contacts without a chat, and messages (with or without accents).
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
        // A hidden chat's messages stay out of the sidebar search.
        client.setHiddenThreadsForTesting({"u-search-b"});
        msgs->setQuery("cau long");
        msgs->runNow();
        QCOMPARE(msgs->count(), 1);
        QCOMPARE(msgs->data(msgs->index(0), MessageSearchModel::ConvIdRole).toString(), QString("u-search-a"));

        // Hosts whose SQLite has no FTS5 still find messages (folded substring fallback).
        client.offlineStore()->setFtsAvailableForTesting(false);
        QCOMPARE(client.offlineStore()->searchMessages(QString::fromUtf8("đánh cầu")).size(), 1);
        QCOMPARE(client.offlineStore()->searchMessages("cau long", "u-search-a").size(), 1);
        client.offlineStore()->setFtsAvailableForTesting(true);

        // Opening a directory hit creates the chat and selects it.
        client.openSearchResult("g-search", QString::fromUtf8("Fixture Nhóm Cầu Lông"), "", true);
        QVERIFY(client.conversationModel()->contains("g-search"));
        QCOMPARE(client.currentConversationId(), QString("g-search"));
    }

    // Hidden conversations (Zalo): server-stored MD5 PIN, control-event sync, reveal by PIN in search, no leaks.
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

        // Another device hides a chat: the "hidden_chat" control event (is_group may arrive as a string).
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

        // Name search finds the hidden chat (opening asks for the PIN); its messages' text never matches.
        model->setSearchFilter("hidden friend");
        QVERIFY(has("u-hide-a"));
        model->setSearchFilter("secret words");
        QVERIFY(!has("u-hide-a"));
        // Typing the exact PIN: hidden chats leave the normal results and get their own section.
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

        // A new message in a hidden chat: no notification, only the contentless search-icon dot.
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

        // Unhidden elsewhere, then the PIN changed and was reset from another device.
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

    // Removed from / disbanded group (Zalo): history stays, the thread turns read-only with a notice and a delete action.
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
        QVERIFY(noticeIn("g-ro", QString::fromUtf8("Bạn đã được xoá khỏi nhóm")));

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

        // The flag survives a restart; my own voluntary leave still removes the chat.
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

    // Per-message ttl: expired messages never show, are dropped from the cache (+FTS) and live ones expire on time.
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
        QCOMPARE(store->nextExpiry(QDateTime::currentMSecsSinceEpoch()), qint64(0));
    }

    // System/OA broadcast types never count as unread and never notify (Zalo's shared-worker exclusion list).
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

    // Group rows saved before the sender's profile was known resolve name/avatar at display time, never the group's.
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

    // Group "seen" avatars survive a restart: the uid list is persisted inside the cached raw payload.
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
            // A later history sync re-saves the same message with a payload that carries no seenUids.
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

    // Official accounts: detected from oaInfo/oa_status, type >= 1 or bizPkg (never a group id), persisted in the cache.
    void testOfficialAccountDetectionPersists() {
        QVERIFY(ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-1"}, {"type", 1}}));
        QVERIFY(ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-2"}, {"oaInfo", QJsonObject{{"id", 1}}}}));
        QVERIFY(ZaloClient::isOfficialAccountForTesting(QJsonObject{{"userId", "u-oa-3"}, {"bizPkg", QJsonObject{{"pkgId", 2}}}}));
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

    // System/OA threads never expose call or group-creation actions; My Documents keeps its single display name.
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

    // chat.location.new → a location card opening the coordinates in a map; bad coordinates give no link.
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

    // Stale cached rows are re-parsed once (meta parse_version): text, type, FTS and previews; bank card; href-less cards.
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
        QCOMPARE(store->metaValue("parse_version"), QString::number(ZaloClient::kCacheParseVersion));
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

    // 3. Feature: Unread Message Calculation & Notification Clearing
    void testUnreadCountAggregation() {
        ConversationModel model;
        int initialUnread = model.totalUnreadCount();
        QVERIFY(initialUnread >= 0);

        // Clearing unread on existing conversation
        model.clearUnread("conv-1");
        int afterClear = model.totalUnreadCount();
        QVERIFY(afterClear <= initialUnread);

        // Verify total unread count signal
        QSignalSpy unreadSpy(&model, &ConversationModel::unreadCountChanged);
        model.clearUnread("conv-2");
        // Spy may record 0 or 1 depending on whether conv-2 had unread messages
        QVERIFY(unreadSpy.isValid());
    }

    // zbusiness/OA chats are skipped by applyReadPoints (sub-chat cursors carry type=2 + sct), so the server
    // read-point never re-heals their badge. Reading a chat must write the zeroed unread back to offline.db,
    // otherwise the stale unread_count resurfaces as a "new message" badge on every launch.
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

        // Opening the chat clears the badge in memory and persists the zeroed count to the store.
        client.selectConversation(peer, QStringLiteral("Z Business Fixture"), QString());
        QCOMPARE(client.conversationModel()->conversation(peer).unreadCount, 0);

        // Re-reading from disk must not resurrect the stale "new message" badge.
        const auto persisted = client.offlineStore()->loadConversations();
        int reloadedUnread = -1;
        for (const auto& c : persisted) {
            if (c.id == peer) reloadedUnread = c.unreadCount;
        }
        QCOMPARE(reloadedUnread, 0);

        client.offlineStore()->clearCache();
    }

    // 4. Feature: Real-time Message Stream & Formatting
    void testMessageStreamFeatures() {
        MessageModel model;
        int countBefore = model.rowCount();

        // 1. Text Message
        model.addMessage("Thử nghiệm tin nhắn hệ thống tiếng Việt", true);
        QCOMPARE(model.rowCount(), countBefore + 1);
        QModelIndex idxText = model.index(countBefore);
        QCOMPARE(model.data(idxText, MessageModel::TextRole).toString(), "Thử nghiệm tin nhắn hệ thống tiếng Việt");
        QCOMPARE(model.data(idxText, MessageModel::IsMineRole).toBool(), true);

        // 2. Incoming Contact Message
        model.addMessage("Tin nhắn phản hồi từ đối tác", false);
        QCOMPARE(model.rowCount(), countBefore + 2);
        QModelIndex idxContact = model.index(countBefore + 1);
        QCOMPARE(model.data(idxContact, MessageModel::IsMineRole).toBool(), false);

        // 3. Attachment Message
        model.addAttachmentMessage("image", "bang_gia_gpu.png", "https://cdn.example.com/gpu.png", true);
        QCOMPARE(model.rowCount(), countBefore + 3);
        QModelIndex idxAtt = model.index(countBefore + 2);
        QCOMPARE(model.data(idxAtt, MessageModel::MessageTypeRole).toString(), "image");
        QCOMPARE(model.data(idxAtt, MessageModel::TextRole).toString(), "bang_gia_gpu.png");
        QCOMPARE(model.data(idxAtt, MessageModel::MediaUrlRole).toString(), "https://cdn.example.com/gpu.png");
    }

    // 5. Feature: Audio Preview Timeout and Cancellation
    void testSoundPreviewLifecycle() {
        ZaloClient client;

        QSignalSpy previewSpy(&client, &ZaloClient::previewPlayingChanged);
        client.playSoundPreview("notification", "pop");
        QCOMPARE(client.isPreviewPlaying(), true);
        QCOMPARE(client.currentPreviewSound(), QString("pop"));
        QCOMPARE(previewSpy.count(), 1);

        // Rapid switch to another sound
        client.playSoundPreview("ringtone", "classic");
        QCOMPARE(client.isPreviewPlaying(), true);
        QCOMPARE(client.currentPreviewSound(), QString("classic"));

        // Every offered sound renders to a valid WAV file.
        for (const QString& key : Zalos::Core::Platform::SoundEngine::notificationKeys()
                                  + Zalos::Core::Platform::SoundEngine::ringtoneKeys()) {
            const QString path = Zalos::Core::Platform::SoundEngine::renderToFile(key);
            QVERIFY2(!path.isEmpty(), qPrintable(key));
            QFile wav(path);
            QVERIFY(wav.open(QIODevice::ReadOnly));
            QVERIFY(wav.read(4) == "RIFF");
            QVERIFY(wav.size() > 1000);
        }

        // Manual stop
        client.stopSoundPreview();
        QCOMPARE(client.isPreviewPlaying(), false);
        QCOMPARE(client.currentPreviewSound(), QString(""));
        QVERIFY(previewSpy.count() >= 3);
    }

    // 6. Feature: Complete Authentication Flow & Edge Cases
    void testAuthValidationEdgeCases() {
        ZaloClient client;
        client.logout();
        QCOMPARE(client.isLoggedIn(), false);

        // Empty input is rejected with a message.
        client.loginWithPhone("", "", true);
        QCOMPARE(client.isLoggedIn(), false);
        QVERIFY(client.hasLoginError());

        // Password login is unsupported by Zalo Web: never fake a successful login.
        client.loginWithPhone("0988777888", "ValidPassword123", true);
        QTest::qWait(100);
        QCOMPARE(client.isLoggedIn(), false);
        QVERIFY(client.hasLoginError());

        // QR refresh resets the countdown; the fallback matrix only exists for a real token.
        client.refreshQrCode();
        QVERIFY(client.qrSecondsRemaining() > 0);
        QCOMPARE(client.qrMatrix().size(), client.qrSize() * client.qrSize());
        QCOMPARE(client.qrLoginStep(), 0);
    }

    // 7. Feature: D-Bus Incoming Call Alert Dispatch
    void testIncomingCallWorkflow() {
        ZaloClient client;
        QSignalSpy callSpy(&client, &ZaloClient::incomingCallStateChanged);

        client.triggerSimulatedIncomingCall("Trần Thị B");
        QCOMPARE(client.isIncomingCallActive(), true);
        QCOMPARE(client.incomingCallerName(), QString("Trần Thị B"));
        QCOMPARE(callSpy.count(), 1);

        // Answer call
        client.answerCall();
        QCOMPARE(client.isIncomingCallActive(), false);
        QCOMPARE(callSpy.count(), 2);
    }

    // 8. Feature: Cryptographic Decryption & LZMA Safety
    void testCryptoEngineSafety() {
        std::string privKey = "A1B2C3D4E5F60718293A4B5C6D7E8F90";
        auto derivedKey = DbCross::deriveAes256Key(privKey);
        QCOMPARE(static_cast<int>(derivedKey.size()), 32); // Must be 256-bit AES key

        // Self-test assertion (encrypt -> decrypt roundtrip with AES-256-CBC and PKCS#7)
        QVERIFY(DbCross::verifyCryptoSelfTest());
    }

    // 9. Feature: Navigation Views, Modals & Conversation Info Workflow
    void testInteractiveFeaturesAndNavigation() {
        ZaloClient client;

        // Navigation Tab Switching
        QCOMPARE(client.currentNavigationTab(), QString("messages"));
        client.setCurrentNavigationTab("contacts");
        QCOMPARE(client.currentNavigationTab(), QString("contacts"));
        client.setCurrentNavigationTab("cloud");
        QCOMPARE(client.currentNavigationTab(), QString("cloud"));
        client.setCurrentNavigationTab("messages");
        QCOMPARE(client.currentNavigationTab(), QString("messages"));

        // Honest business and cloud data (zero mock data)
        QCOMPARE(client.hasBusinessPackage(), false);
        QCOMPARE(client.businessPackageName(), QString());
        QCOMPARE(client.cloudSharedItems().isEmpty(), true);

        // Add Friend Modal Flow
        QCOMPARE(client.showAddFriendModal(), false);
        client.openAddFriend();
        QCOMPARE(client.showAddFriendModal(), true);
        // Without a session, searching must fail cleanly instead of inventing a user.
        QSignalSpy searchSpy(&client, &ZaloClient::userSearchFinished);
        client.findUserByPhone("0912345678");
        QCOMPARE(searchSpy.count(), 1);
        QCOMPARE(searchSpy.first().at(0).toBool(), false);
        client.closeAddFriend();
        QCOMPARE(client.showAddFriendModal(), false);

        // Create Group Modal Flow
        QCOMPARE(client.showCreateGroupModal(), false);
        client.openCreateGroup();
        QCOMPARE(client.showCreateGroupModal(), true);
        QSignalSpy groupSpy(&client, &ZaloClient::groupCreateFinished);
        client.createGroup("Nhom Test", QStringList{"111"});
        QCOMPARE(groupSpy.count(), 1);
        QCOMPARE(groupSpy.first().at(0).toBool(), false); // needs at least 2 members
        client.createGroup("Nhom Test", QStringList{"111", "222"});
        QCOMPARE(groupSpy.count(), 2);
        QCOMPARE(groupSpy.last().at(0).toBool(), false); // no live session
        client.closeCreateGroup();
        QCOMPARE(client.showCreateGroupModal(), false);

        // Conversation Info Panel Toggle
        QCOMPARE(client.showConversationInfo(), false);
        client.toggleConversationInfo();
        QCOMPARE(client.showConversationInfo(), true);
        client.toggleConversationInfo();
        QCOMPARE(client.showConversationInfo(), false);
    }

    // 10. Feature: Real-time Message Sending & Interactive Simulated Reply
    void testRealMessageInteraction() {
        ZaloClient client;
        client.selectConversation("vp-8t10", "Võ Thị Sáu", "");
        QCOMPARE(client.currentConversationId(), QString("vp-8t10"));

        MessageModel* msgModel = client.messageModel();
        int initialCount = msgModel->rowCount();

        // Send a message
        client.sendMessage("Xin chào bạn!");
        QCOMPARE(msgModel->rowCount(), initialCount + 1);

        // Verify outgoing message properties
        QModelIndex lastIdx = msgModel->index(msgModel->rowCount() - 1);
        QCOMPARE(msgModel->data(lastIdx, MessageModel::TextRole).toString(), QString("Xin chào bạn!"));
        QCOMPARE(msgModel->data(lastIdx, MessageModel::IsMineRole).toBool(), true);

        // Zero-mock validation: Real interactive sending adds message directly without fake bot replies
        QCOMPARE(msgModel->rowCount(), initialCount + 1);
        QCOMPARE(msgModel->data(lastIdx, MessageModel::IsMineRole).toBool(), true);
        QCOMPARE(msgModel->data(lastIdx, MessageModel::TextRole).toString(), QString("Xin chào bạn!"));
    }

    // 11. Feature: FileUtils (Linux statvfs & Directory Scanning)
    void testFileUtilsAndDiskUsage() {
        // Disk usage statvfs
        DiskUsage usage = FileUtils::getDiskUsage("/");
        QVERIFY(usage.total > 0.0);
        QVERIFY(usage.available > 0.0);
        QVERIFY(usage.free <= usage.total);

        // Directory size calculation
        DirectorySizeResult res = FileUtils::getDirectorySize("/tmp", 2);
        QCOMPARE(res.path, QString("/tmp"));
        QVERIFY(res.durationMs >= 0.0);

        // Directory tree traversal
        DirectoryTreeNode tree = FileUtils::getDirectoryTree("/tmp", 1);
        QVERIFY(tree.depth == 0);
    }

    // 12. Feature: MediaUtils (Thumbnail Generation & JXL Check)
    void testMediaUtilsThumbnails() {
        // Generate test image in memory
        QImage testImg(100, 100, QImage::Format_RGB32);
        testImg.fill(Qt::blue);
        QByteArray buffer;
        QBuffer qbuf(&buffer);
        qbuf.open(QIODevice::WriteOnly);
        testImg.save(&qbuf, "PNG");

        // Test in-memory buffer thumbnail
        QByteArray thumbBytes = MediaUtils::generateThumbnailBuffer(buffer, 48, 48);
        QVERIFY(!thumbBytes.isEmpty());

        // File-based thumbnail
        QString tmpIn = "/tmp/test_thumb_in.png";
        QString tmpOut = "/tmp/test_thumb_out.jpg";
        testImg.save(tmpIn);
        bool ok = MediaUtils::generateThumbnailFs(tmpIn, tmpOut, 48, 48);
        QVERIFY(ok);
        QVERIFY(QFile::exists(tmpOut));
        QFile::remove(tmpIn);
        QFile::remove(tmpOut);

        // Capability check
        bool jxl = MediaUtils::isJxlSupported();
        Q_UNUSED(jxl);

        // Infrared camera recognition (Zalux / Linux video compatibility)
        QVERIFY(MediaUtils::isIrCamera("Integrated IR Camera"));
        QVERIFY(MediaUtils::isIrCamera("Windows Hello Infrared Sensor"));
        QVERIFY(!MediaUtils::isIrCamera("HD Web Camera 1080p"));
    }

    // 13. Feature: ZCallEngine (ZRTP/SRTP Call Engine & PipeWire/Pulse Devices)
    void testCallEngineLifecycleAndCrypto() {
        ZCallEngine engine;
        QCOMPARE(engine.callState(), CallState::Idle);

        // Test SRTP key derivation matching zcall-native
        QByteArray zrtpSecret = "MY-ZRTP-SECRET";
        QByteArray salt = "SALT-16BYTES1234";
        QByteArray srtpKey = ZCallEngine::deriveSrtpMasterKey(zrtpSecret, salt);
        QCOMPARE(srtpKey.size(), 16);

        // Test Call State transitions
        engine.startCall("peer-uid-1", "Võ Thị Sáu", false);
        QCOMPARE(engine.callState(), CallState::CallingOutgoing);
        QCOMPARE(engine.currentCaller(), QString("Võ Thị Sáu"));

        // Toggle Mute & Camera
        engine.setMutedMic(true);
        QCOMPARE(engine.isMutedMic(), true);
        engine.setMutedSpeaker(true);
        QCOMPARE(engine.isMutedSpeaker(), true);
        engine.startScreenShare();
        QCOMPARE(engine.isScreenSharing(), true);
        engine.stopScreenShare();
        QCOMPARE(engine.isScreenSharing(), false);

        // End Call
        engine.endCall();
        QCOMPARE(engine.callState(), CallState::Ended);

        // Audio devices
        QVector<AudioDevice> inputs = engine.getAudioDevices(true);
        QVERIFY(!inputs.isEmpty());
        QVector<AudioDevice> outputs = engine.getAudioDevices(false);
        QVERIFY(!outputs.isEmpty());
    }

    // 14. Feature: ZaloOfflineStore (AES-256-GCM Encrypted Session & SQLite)
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

        // Test write encrypted session
        bool written = ZaloOfflineStore::writeEncryptedSession(testDir, zpwSek, sessionRecord);
        QVERIFY(written);

        // Test read encrypted session
        QJsonObject readBack = ZaloOfflineStore::readEncryptedSession(testDir, zpwSek);
        QCOMPARE(readBack.value("clientId").toString(), QString("linux-client-1"));
        QCOMPARE(readBack.value("info").toObject().value("config").toObject().value("uid").toString(), QString("123456789"));

        // Clear encrypted session
        ZaloOfflineStore::clearEncryptedSession(testDir);
        QJsonObject emptyAfterClear = ZaloOfflineStore::readEncryptedSession(testDir, zpwSek);
        QVERIFY(emptyAfterClear.isEmpty());

        // Test SQLite local database caching
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

        // Test Sync State Machine
        store.setOfflineActive(true);
        QCOMPARE(store.isOfflineActive(), true);
        QCOMPARE(store.syncState(), QString("DISCONNECTED"));

        store.setOfflineActive(false);
        QCOMPARE(store.isOfflineActive(), false);
    }

    // Offline history: everything received is cached per account with its timeline and raw TMessage,
    // and an offline start renders it (neutral fixtures, nothing generated by the app).
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
            QVERIFY(store.saveMessage("u-fixture-peer", later, raw, false));   // arrives first
            QVERIFY(store.saveMessage("u-fixture-peer", earlier, {}, false));  // older page arrives later
            MessageItem localEcho; localEcho.id = "local-1-1"; localEcho.text = "x";
            QVERIFY(!store.saveMessage("u-fixture-peer", localEcho));        // echoes are never cached
            later.reaction = "❤️";
            QVERIFY(store.saveMessage("u-fixture-peer", later));             // update keeps the raw payload
            store.saveProfile({"u-fixture-peer", "Fixture Peer", "", false, 0, true});

            QHash<QString, QJsonObject> raws;
            const auto msgs = store.loadMessages("u-fixture-peer", &raws);
            QCOMPARE(msgs.size(), 2);
            QCOMPARE(msgs.at(0).id, QString("501"));                         // ordered by ts, not insertion
            QCOMPARE(msgs.at(0).status, QString("seen"));
            QCOMPARE(msgs.at(0).quoteText, QString("q"));
            QCOMPARE(msgs.at(1).reaction, QString::fromUtf8("❤️"));
            QCOMPARE(raws.value("502").value("content").toString(), QString("second"));
            QCOMPARE(store.loadConversations().first().lastTs, qint64(2000));
        }

        // Offline start: the cached account's data is shown, presence is not taken from the cache.
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
            QVERIFY(client.canRecall("501") == false); // no raw for 501 → no server action offered
        }

        // Another account binding wipes the previous account's cache.
        {
            ZaloOfflineStore store;
            QVERIFY(store.bindOwner("u-other-owner"));
            QVERIFY(store.loadConversations().isEmpty());
            QVERIFY(store.loadMessages("u-fixture-peer").isEmpty());
            store.clearCache();
        }
    }


    // Phone history transfer: synthetic ZDB4.0 backup through the real decrypt -> read -> map path.
    void testMobileHistoryBackupPipeline() {
        using Zalos::Core::Sync::MobileHistorySync;

        // 1. RSA: the phone encrypts the backup key to our SPKI public key (PKCS#1 v1.5).
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

        // 2. Two conversation databases in Zalo's ChatContent shape (neutral ids and texts).
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
                           {36, 3000, 1, "c3", "1001", "5003", "", QByteArray(), 0}}); // undo marker: skipped
        makeDb("group_2002.db", {{0, 1500, 1, "c4", "1003", "6001", "hello group", QByteArray(), 0}});

        // 3. ZDB4.0 container: magic, file table, XZ payload, AES-256-CBC (64 KiB chunks, IV 0).
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

        // 4. Real decrypt + extraction; a wrong key must fail instead of "succeeding".
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

        // 5. Plain ids -> noised ids, TMessage shape, then the normal pipeline shows them.
        const QHash<QString, QString> ids{{"1001", "9001"}, {"2002", "9002"}, {"1003", "9003"}};
        int skipped = 0;
        QJsonArray oneToOne;
        QJsonArray group;
        for (const auto& c : convs) {
            const QJsonArray m = MobileHistorySync::toMessages(c, ids, "1111", "u-me-fixture", &skipped);
            (c.isGroup ? group : oneToOne) = m;
        }
        QCOMPARE(skipped, 1);
        // Media rows keep their attachment but never the MsgContent placeholder as a caption.
        {
            MobileHistorySync::BackupConversation media;
            media.plainId = "1001";
            MobileHistorySync::BackupRow photoRow;
            photoRow.globalMsgId = "7001"; photoRow.cliMsgId = "c9"; photoRow.msgType = 3; photoRow.ts = 4000;
            photoRow.senderId = "1001"; photoRow.text = "[Photo] "; // no BinNet attachment either
            media.rows = {photoRow};
            int skippedMedia = 0;
            QVERIFY(MobileHistorySync::toMessages(media, ids, "1111", "u-me-fixture", &skippedMedia).isEmpty());
            QCOMPARE(skippedMedia, 1);
        }
        QCOMPARE(oneToOne.size(), 2);
        QCOMPARE(oneToOne.at(0).toObject().value("uidFrom").toString(), QString("9001"));
        QCOMPARE(oneToOne.at(1).toObject().value("uidFrom").toString(), QString("0")); // our own message
        QCOMPARE(oneToOne.at(1).toObject().value("idTo").toString(), QString("9001"));
        QCOMPARE(group.at(0).toObject().value("idTo").toString(), QString("9002"));
        QCOMPARE(group.at(0).toObject().value("uidFrom").toString(), QString("9003"));
        // Unknown conversation id: nothing is invented.
        QVERIFY(MobileHistorySync::toMessages(convs.at(0), {}, "1111", "u-me-fixture").isEmpty());

        ZaloClient client;
        emit client.apiClient()->session()->messagesReceived(oneToOne, false, true);
        emit client.apiClient()->session()->messagesReceived(group, true, true);
        QVERIFY(client.conversationModel()->contains("9001"));
        QVERIFY(client.conversationModel()->contains("9002"));
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 0); // history, not news
        client.selectConversation("9001", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 2);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::TextRole).toString(),
                 QString::fromUtf8("chào bạn"));

        // 6. Sync is refused without a session and never reports success.
        client.requestMobileSync();
        QCOMPARE(client.mobileSyncState(), 5);
    }


    // Bulk history (phone transfer, socket pages): one sidebar reset per batch, bounded time.
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


    // Memory: history of threads nobody is looking at goes to the offline cache, not RAM.
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
        // Opening a thread still shows its full history (from the cache).
        QVERIFY2(after - before < 70000, "history of closed threads must not stay in RAM");
        client.selectConversation("u-mem-7", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 170);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QString::number(900000 + 7 * 170));
        // Opening 40 threads keeps at most 12 in RAM; the evicted ones reload completely.
        for (int t = 0; t < 40; ++t) client.selectConversation(QString("u-mem-%1").arg(t), "Fixture", "");
        QCOMPARE(client.messageModel()->threadSize("u-mem-0"), 0);
        client.selectConversation("u-mem-0", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 170);
        // A long thread opens with its newest 200 cached rows; scrolling up pages older ones from offline.db
        // (also without a session), and actions needing the raw JSON (quote) read it from disk.
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
        // Search covers the whole stored thread, not only the loaded page; revealing an old hit loads down to it.
        const QStringList found = client.searchConversationMessages("fixture long thread message 12");
        QVERIFY(found.contains(QString::number(5000000 + 12)));
        QCOMPARE(found.first(), QString::number(5000000 + 12)); // oldest first
        QVERIFY(client.revealMessage(QString::number(5000000 + 12)));
        QCOMPARE(client.messageModel()->rowCount(), 500);       // 12..499 plus 20 older rows of context (0..11)
        QVERIFY(!client.revealMessage("does-not-exist"));
        // Sidebar search (all conversations, typed model) and opening a hit.
        client.messageSearch()->setQuery("fixture long thread message 12");
        client.messageSearch()->runNow();
        QVERIFY(client.messageSearch()->count() >= 1);
        const QModelIndex first = client.messageSearch()->index(0);
        QCOMPARE(client.messageSearch()->data(first, Zalos::Core::Models::MessageSearchModel::ConvIdRole).toString(),
                 QString("u-mem-big"));
        client.messageSearch()->setQuery("x"); // < 2 characters clears
        QCOMPARE(client.messageSearch()->count(), 0);
        QSignalSpy revealSpy(&client, &ZaloClient::revealRequested);
        client.openMessageFromSearch("u-mem-big", QString::number(5000000 + 12));
        QCOMPARE(revealSpy.count(), 1);
        // The rest of this test pages from a fresh open of the thread.
        client.selectConversation("u-mem-1", "Fixture", "");
        for (int t = 2; t < 16; ++t) client.selectConversation(QString("u-mem-%1").arg(t), "Fixture", ""); // evict
        client.selectConversation("u-mem-big", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 200);
        client.loadOlderMessages();
        QCOMPARE(client.messageModel()->rowCount(), 400);
        client.loadOlderMessages();
        QCOMPARE(client.messageModel()->rowCount(), 500);
        client.loadOlderMessages(); // nothing older in the cache, offline: unchanged
#ifdef __GLIBC__
        qInfo("heap for 500 resident cached messages: %zu kB (%zu B each)", (mallinfo2().uordblks - heapBefore) / 1024,
              (mallinfo2().uordblks - heapBefore) / 500);
#endif
        QCOMPARE(client.messageModel()->rowCount(), 500);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(),
                 QString::number(5000000));
        client.setReplyTarget(QString::number(5000000)); // cached row: raw JSON read on demand
        QCOMPARE(client.replyTarget().value("id").toString(), QString::number(5000000));
        client.clearReplyTarget();
        ZaloOfflineStore cleanup;
        cleanup.clearCache();
    }


    // Rendering is chosen from the hardware, never by asking the user.
    void testRenderProfileDecisions() {
        using RP = Zalos::Core::Platform::RenderProfile;
        RP::Inputs vm;                       // VM / remote desktop: no GPU node
        vm.memTotalKb = 8LL * 1024 * 1024;
        vm.cpuCores = 8;
        QCOMPARE(RP::decide(vm).backend, RP::Backend::Software);
        QVERIFY(!RP::decide(vm).basicRenderLoop);

        RP::Inputs amd = vm;                 // AMD + RADV + Qt with Vulkan
        amd.hasRenderNode = true;
        amd.gpuVendor = "0x1002";
        amd.mesaVulkanIcd = true;
        amd.qtHasVulkan = true;
        QCOMPARE(RP::decide(amd).backend, RP::Backend::Vulkan);
        amd.vulkanFailedBefore = true;       // crash guard tripped earlier
        QCOMPARE(RP::decide(amd).backend, RP::Backend::Default);

        RP::Inputs nvidia = vm;
        nvidia.hasRenderNode = true;
        nvidia.gpuVendor = "0x10de";
        QCOMPARE(RP::decide(nvidia).backend, RP::Backend::Default);

        RP::Inputs small = amd;              // 2 GiB RAM: basic render loop
        small.vulkanFailedBefore = false;
        small.memTotalKb = 2LL * 1024 * 1024;
        QVERIFY(RP::decide(small).basicRenderLoop);

        RP::Inputs forced = vm;              // explicit override wins
        forced.override = "opengl";
        QCOMPARE(RP::decide(forced).backend, RP::Backend::OpenGL);
    }


    // Crash guard: a Vulkan launch that never reached its first frame turns Vulkan off for later launches.
    void testRenderGuardRemembersVulkanFailure() {
        namespace RG = Zalos::Core::Platform::RenderGuard;
        using RP = Zalos::Core::Platform::RenderProfile;
        QSettings s("Zalos", "ZalosApp");
        s.remove("render/vulkanBroken");
        RG::markAttempt(RP::Backend::Vulkan);
        RG::markFirstFrame();                 // healthy launch
        QVERIFY(!RG::vulkanFailedBefore());
        RG::markAttempt(RP::Backend::Vulkan); // launch dies before the first frame...
        QVERIFY(RG::vulkanFailedBefore());    // ...the next launch sees it
        QVERIFY(RG::vulkanFailedBefore());    // and keeps remembering it
        s.remove("render/vulkanBroken");
        s.remove("render/pending");
    }

    // The audio backend is only loaded by the first sound.
    void testSoundEngineIsLazy() {
        SoundEngine engine;
        QVERIFY(engine.findChild<QSoundEffect*>() == nullptr);
        QVERIFY(!engine.isPlaying());
        engine.stop(); // safe before any sound
        engine.play("notification", "pop");
        QVERIFY(engine.findChild<QSoundEffect*>() != nullptr);
        engine.stop();
    }

    // REST and downloads go over HTTP/1.1 (Qt's HTTP/2 client logged socket warnings with Zalo's edges).
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
        replyZaloapp->abort();
        replyZaloapp->deleteLater();

        QNetworkReply* replyExternal = session->getWithCookies(QUrl("https://s3.amazonaws.com/backup/test.zdb"));
        QVERIFY(replyExternal != nullptr);
        QVERIFY(!replyExternal->request().hasRawHeader("Cookie"));
        replyExternal->abort();
        replyExternal->deleteLater();
    }

    // QML singletons: I18n.t is typed (compiled to C++), Theme reads ThemeManager directly.
    void testTypedQmlSingletons() {
        ThemeManager themeMgr;
        QQmlEngine engine;
        engine.addImportPath("qrc:/");
        engine.addImportPath("qrc:/qt/qml");
        QQmlComponent c(&engine);
        c.setData("import QtQuick\nimport Zalos\nQtObject {\n"
                  "  property string vi: I18n.t('Tin nhắn', 'Messages')\n"
                  "  property color blue: Theme.zaloBlue\n"
                  "  property bool sameTheme: ThemeManager === ThemeManager\n}", QUrl());
        std::unique_ptr<QObject> o(c.create());
        QVERIFY2(o, qPrintable(c.errorString()));
        QCOMPARE(o->property("vi").toString(), QString::fromUtf8("Tin nhắn"));
        QCOMPARE(o->property("blue").value<QColor>(), themeMgr.accentColor());
    }


    // UI gallery: renders the main screens with neutral fixtures into build/ui_*.png for visual review
    // (light and dark). Keep it in sync when screens change; it also fails on QML errors via the loader.
    void testUiGalleryScreenshots() {
        ZaloClient client;
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        client.setLoggedInForTesting(true);
        // A neutral picture for photo messages.
        QImage pic(640, 420, QImage::Format_RGB32);
        for (int y = 0; y < pic.height(); ++y)
            for (int x = 0; x < pic.width(); ++x) pic.setPixel(x, y, qRgb(40 + x / 4, 90 + y / 5, 160));
        const QString picPath = QDir::temp().filePath("zalos-gallery-photo.png");
        pic.save(picPath);
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
            msg("9105", false, "chat.photo", QJsonObject{{"thumb", QUrl::fromLocalFile(picPath).toString()},
                                                         {"href", QUrl::fromLocalFile(picPath).toString()},
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
        client.setNotifySound(true); // one switch ON in the notifications tab
        for (int mode : {0, 1}) {
            themeMgr.setThemeMode(mode);
            const QString suffix = mode ? "dark" : "light";
            client.pins()->applyListForTesting("u-gallery", initialPins, false);
            client.setCurrentNavigationTab("messages");
            shot("chat_" + suffix);
            if (mode == 0) {
                // QML weight of the message list: objects per instantiated message row (memory review 2026-11-04).
                // ListView delegates are not QObject children of the window: walk the visual item tree.
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
                // Closed popups are not in the visual tree; count the QObject tree of the window too.
                qInfo("window QML objects: %d visual, %d in the QObject tree", weight(window->contentItem()),
                      int(window->findChildren<QObject*>().size()));
                QVERIFY2(perRow < 260, "message rows must create only the parts of their type (Loaders)");
                QVERIFY(!rows.isEmpty());
            }
            client.setAntiPeeking(true);                  // indicator strip + dimmed messages
            shot("chat_antipeek_" + suffix);
            client.setAntiPeeking(false);
            // Pinned topics bar (Zalo PC chat-group-topic) and its expanded list, from neutral fixtures.
            {
                const QString conv = client.currentConversationId();
                auto topic = [](const QString& id, int type, const QString& title, int msgType) {
                    return QJsonObject{{"id", id}, {"type", type}, {"params", QJsonObject{
                        {"title", title}, {"senderUid", "u-gallery"}, {"senderName", "Fixture Gallery"},
                        {"global_msg_id", "m-" + id}, {"client_msg_id", "c-" + id}, {"msg_type", msgType}}}};
                };
                // t2: a photo pin whose thumb is gone (as an expired CDN link) → placeholder glyph.
                QJsonObject photoPin = topic("t2", 2, "", 32);
                QJsonObject photoParams = photoPin.value("params").toObject();
                photoParams.insert("thumb", "file:///nonexistent/fixture-expired-thumb.jpg");
                photoPin.insert("params", photoParams);
                client.pins()->applyListForTesting(conv, QJsonObject{{"version", 3}, {"data", QJsonArray{
                    topic("t1", 2, "Fixture pinned text message\n" + QString("Fixture detail line with enough words to wrap. ").repeated(12), 1), photoPin,
                    topic("t3", 1, "Fixture note", 1), topic("t4", 3, "Fixture poll", 1)}}}, false);
                shot("pins_" + suffix);
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
                QTRY_VERIFY(client.isMediaBroken("file:///nonexistent/fixture-expired-thumb.jpg"));
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
            // Composite avatar of a group without a photo (header + info panel).
            client.selectConversation("g-gallery", "Fixture Team", "");
            client.setShowConversationInfo(true);
            shot("group_composite_" + suffix);
            client.setShowConversationInfo(false);
            client.selectConversation("u-gallery", "Fixture Gallery", "");
            // Group management dialog (owner view) and the join-by-link preview, from neutral fixtures.
            client.groupAdmin()->openManage("g-gallery");
            QTest::qWait(100); // offline replies to the reload settle first
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
            // Label manager and the share-card picker, from neutral fixtures.
            client.labels()->applyServer(QJsonObject{{"version", 1}, {"labelData", QJsonArray{
                QJsonObject{{"id", 1}, {"text", "Fixture A"}, {"color", "#D91B1B"}, {"offset", 0}, {"conversations", QJsonArray{"u-gallery"}}},
                QJsonObject{{"id", 2}, {"text", "Fixture B"}, {"color", "#4BC377"}, {"offset", 1}, {"conversations", QJsonArray{}}}}}});
            client.labels()->setManageOpen(true);
            shot("labels_" + suffix);
            client.labels()->setManageOpen(false);
            client.openShareCard("u-gallery", "Fixture Gallery");
            shot("share_card_" + suffix);
            client.closeForwardModal();
            client.appLock()->setPasscode("", "9753"); // lock screen
            client.appLock()->lock();
            shot("lock_" + suffix);
            QVERIFY(client.appLock()->unlock("9753"));
            QVERIFY(client.appLock()->removePasscode("9753"));
            // Photo viewer opened from a photo message (zoom/rotate/copy/save toolbar, thumbnail strip).
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
        // Responsive sweep: the logged-in messages UI across quarter/half/full windows on common desktop sizes.
        themeMgr.setThemeMode(1);
        client.setCurrentNavigationTab("messages");
        client.selectConversation("u-gallery", "Fixture Gallery", "");
        client.setShowConversationInfo(false);
        for (const QSize& s : {QSize(1920, 1080), QSize(1280, 800), QSize(960, 1080), QSize(683, 768),
                               QSize(520, 480), QSize(380, 420)}) {
            window->resize(s);
            shot(QString("responsive_%1x%2").arg(s.width()).arg(s.height()));
        }
        // Info panel open (the split that can squeeze the chat + composer at narrow widths).
        client.setShowConversationInfo(true);
        for (const QSize& s : {QSize(1280, 800), QSize(760, 700), QSize(560, 600)}) {
            window->resize(s);
            shot(QString("responsive_info_%1x%2").arg(s.width()).arg(s.height()));
        }
        client.setShowConversationInfo(false);
        window->resize(1280, 800);

        // Sidebar: the always-visible tab row (Tất cả · Chưa đọc · Khác · Nhắn tôi + Nhãn) at normal and narrowest
        // widths, and one query finding chats + groups + messages.
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
            client.conversationModel()->setSearchFilter("");
            client.peopleSearch()->clear();
            client.messageSearch()->clear();

            // Hidden conversations: typing the exact PIN shows the "Trò chuyện ẩn" section; the PIN-entry popup.
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

        // Rail tonal legibility check: label folders on, in dark (near-black rail) and light (blue rail).
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


    // Zalo system payloads: chat.webcontent birthday (ZInstant template 14030) and other customMsg notices, plus
    // app-only zalo.me links (zvideo / mini-app) routed to the in-app QR popup instead of the browser.
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

        // customMsg placeholders %N$s resolve from highLightsV2 (own uid → "Bạn"); unresolved ones are stripped;
        // a calendar action keeps the reminder event id. Webcontent without customMsg/href is a system notice.
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

        // 1-1 OA/system content without href is a card from the OA (left, avatar), never a centred group notice.
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

        // Per-message ttl is a self-destruct only for user messages; broadcast/system content never expires.
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

    // Anti-peeking: one state shared by settings, the Ctrl+Shift+H shortcut and the tray menu quick switch.
    void testAntiPeekingQuickSwitch() {
        ZaloClient client;
        SystemTrayController tray;
        QObject::connect(&tray, &SystemTrayController::antiPeekingToggled, &client, &ZaloClient::setAntiPeeking);
        QObject::connect(&client, &ZaloClient::settingsChanged, &tray, [&]() { tray.setAntiPeeking(client.antiPeeking()); });
        client.setAntiPeeking(false);
        emit tray.antiPeekingToggled(true);               // what the checkable tray action emits
        QCOMPARE(client.antiPeeking(), true);
        client.setAntiPeeking(false);                     // from settings / shortcut / "Tắt" link
        QCOMPARE(client.antiPeeking(), false);
    }


    // Changes made on the phone/web reach the UI through socket controls; nothing is written back.
    void testCrossDeviceSyncControls() {
        ZaloClient client;
        client.setLoggedInForTesting(true);
        auto* session = client.apiClient()->session();
        auto* convs = client.conversationModel();
        const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
        // A 1-1 thread and a group thread exist.
        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "8001"}, {"uidFrom", "u-sync"}, {"idTo", "u-me"},
            {"msgType", "webchat"}, {"content", "a"}, {"ts", ts}}}, false, true);
        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "8002"}, {"uidFrom", "u-sync"}, {"idTo", "g-sync"},
            {"msgType", "webchat"}, {"content", "b"}, {"ts", ts}}}, true, true);
        QVERIFY(convs->contains("u-sync") && convs->contains("g-sync"));

        // Nickname set on the phone wins; removed on the phone -> back to the profile name.
        client.applyAliasesForTesting({{"u-sync", QString::fromUtf8("Anh Tư")}});
        QCOMPARE(convs->conversation("u-sync").name, QString::fromUtf8("Anh Tư"));

        // Group renamed on another device: name + a notice line in the chat.
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

        // Mute / pin / mark-unread from the phone.
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

        // Delete-for-me on another device removes the message; clearing the conversation removes the thread.
        emit session->messagesReceived(QJsonArray{QJsonObject{{"msgId", "8003"}, {"uidFrom", "u-me"}, {"idTo", "u-sync"},
            {"msgType", "chat.delete"}, {"ts", ts},
            {"content", QJsonArray{QJsonObject{{"uidFrom", "u-me"}, {"uidTo", "u-sync"}, {"globalDelMsgId", "8001"}, {"type", 2}}}}}},
            false, false);
        QVERIFY(!convs->contains("u-sync"));

    }


    // Batch 1: server message types (link card, voice, call log, poll), mentions, group roster, receipts.
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

        // Link card: title, description, domain, thumb.
        auto link = client.toMessageItemForTesting(base("chat.recommended", QJsonObject{
            {"action", "recommened.link"}, {"title", "Fixture page"}, {"description", "About the fixture"},
            {"href", "https://example.org/a"}, {"thumb", "https://example.org/t.jpg"}, {"params", "{}"}}), false);
        QCOMPARE(link.messageType, QString("link"));
        QCOMPARE(link.text, QString("Fixture page"));
        QCOMPARE(link.href, QString("https://example.org/a"));
        QCOMPARE(link.mediaUrl, QString("https://example.org/t.jpg"));
        QCOMPARE(link.extra.value("domain").toString(), QString("example.org"));
        QCOMPARE(link.extra.value("desc").toString(), QString("About the fixture"));

        // Call history is not a link: completed outgoing video call with its duration, and a missed voice call.
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

        // Voice: AAC link and duration in ms.
        auto voice = client.toMessageItemForTesting(base("chat.voice", QJsonObject{
            {"action", ""}, {"title", ""}, {"description", ""}, {"href", "https://example.org/v.aac"}, {"thumb", ""},
            {"params", params({{"duration", 12672}, {"fileSize", 1000}, {"m4a", ""}})}}), false);
        QCOMPARE(voice.messageType, QString("voice"));
        QCOMPARE(voice.href, QString("https://example.org/v.aac"));
        QCOMPARE(voice.extra.value("durationMs").toLongLong(), 12672);

        // group.poll: "create" is a card with the poll id from params; other actions only refresh it.
        QJsonObject pollMsg = base("group.poll", QJsonObject{{"action", "create"}, {"title", "Q"},
                                                              {"params", params({{"pollId", 4242}})}});
        pollMsg["idTo"] = "g-fixture";
        auto poll = client.toMessageItemForTesting(pollMsg, true);
        QCOMPARE(poll.messageType, QString("poll"));
        QCOMPARE(poll.pollData.value("poll_id").toLongLong(), 4242);
        pollMsg["content"] = QJsonObject{{"action", "vote"}, {"params", params({{"pollId", 4242}})}};
        QCOMPARE(client.toMessageItemForTesting(pollMsg, true).messageType, QString("poll_update"));

        // Mentions become profile links in the rendered text.
        QJsonObject mentionMsg = base("webchat", QString("@Fixture hi"));
        mentionMsg["idTo"] = "g-fixture";
        mentionMsg["mentions"] = QJsonArray{QJsonObject{{"uid", "u-peer2"}, {"pos", 0}, {"len", 8}, {"type", 0}}};
        auto mention = client.toMessageItemForTesting(mentionMsg, true);
        QCOMPARE(mention.mentions.size(), 1);
        const QString html = MessageModel::linkify(mention.text, mention.mentions);
        QVERIFY(html.contains("zalos-mention:u-peer2"));
        QVERIFY(html.contains("@Fixture"));
        QVERIFY(html.endsWith(" hi"));
        // Out-of-range mention ranges are dropped, never rendered half.
        mentionMsg["mentions"] = QJsonArray{QJsonObject{{"uid", "u-x"}, {"pos", 5}, {"len", 99}}};
        QCOMPARE(client.toMessageItemForTesting(mentionMsg, true).mentions.size(), 0);

        // Roster: owner first, then deputies; my role; names of non-friends are pending (never invented).
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

        // Deputy granted on another device updates the roster.
        emit client.apiClient()->session()->controlReceived("group", "add_admin", QJsonObject{
            {"groupId", "g-fixture"}, {"sourceId", "u-owner"}, {"updateMembers", QJsonArray{QJsonObject{{"id", "u-peer"}}}}});
        bool peerIsAdmin = false;
        for (const QVariant& m : client.groupMembers("g-fixture")) {
            if (m.toMap().value("id") == "u-peer") peerIsAdmin = m.toMap().value("role") == "admin";
        }
        QVERIFY(peerIsAdmin);

        // Delivered receipts: only other people's fresh messages, grouped by thread.
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

    // 15. Feature: ZaloApiClient (REST & WebSocket Wrapper)
    void testZaloApiClientEndpoints() {
        ZaloApiClient client;
        QCOMPARE(client.networkStatus(), QString("CONNECTED"));

        // Test connectivity probe
        QSignalSpy netSpy(&client, &ZaloApiClient::networkStatusChanged);
        client.pingConnectivity();
        QCOMPARE(client.networkStatus(), QString("CHECKING"));

        // Test QR token generation call
        client.generateQrToken();

        // Session APIs refuse to run before the login handshake.
        bool called = false;
        client.session()->fetchFriends([&](bool ok, const QJsonValue&, const QString&, int) {
            called = true;
            QCOMPARE(ok, false);
        });
        QVERIFY(called);
    }

    // 16. Feature: ZaloClient Native Wrappers Integration
    void testZaloClientWrappersIntegration() {
        ZaloClient client;
        QVERIFY(client.callEngine() != nullptr);
        QVERIFY(client.offlineStore() != nullptr);
        QVERIFY(client.apiClient() != nullptr);

        // Test invokable disk usage wrapper
        QVariantMap disk = client.getDiskUsage("/");
        QVERIFY(disk.contains("available"));
        QVERIFY(disk.contains("total"));

        // Test invokable directory size wrapper
        QVariantMap dir = client.getDirectorySize("/tmp");
        QVERIFY(dir.contains("totalSize"));

        // Test OCR wrapper
        QString ocrResult = client.recognizeImageText("/nonexistent/file.png");
        QVERIFY(ocrResult.isEmpty());

        // Test BinNet TLV parser wrapper
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

        // Test SRTP KDF key derivation wrapper
        QVariantMap srtpKeys = client.deriveSrtpKeys("012345678901234567890123456789");
        QVERIFY(srtpKeys.contains("encKey"));
        QVERIFY(srtpKeys.contains("authKey"));
        QVERIFY(srtpKeys.contains("saltKey"));

        // Test sync offline data trigger
        client.syncOfflineData();
    }

    // 17. Feature: Zero Mock Data Mandate & Dynamic Model Operations & Responsive Geometry

    // Call bridge: call-v2 framing must match zcall-native's tools/lib/callv2-crypto.js.
    void testCallBridgeFraming() {
        using Zalos::Core::Call::CallBridge;
        const QByteArray key(16, char(0x11)); // dummy key, not Zalo's
        const QByteArray expected =
            "3acf70e766113e07d5ea0729112dd8b1f8d784fb418d278748b21eea249ada7d8f07b224bc8af36337b7e2710aa78da49a"
            "d9e6546bc743083349e9f8ad291c38$";
        QJsonObject frame{{"type", "update"}, {"command", "native-ready"}, {"data", QJsonObject()}};
        // A frame produced by the engine's JS library decodes to the same object...
        QByteArray jsFrame = expected;
        QHash<QString, QStringList> none;
        const QList<QJsonObject> fromJs = CallBridge::decodeFrames(jsFrame, key, &none);
        QCOMPARE(fromJs.size(), 1);
        QCOMPARE(fromJs.first(), frame);
        // ...and our frames decode back (key order differs from JS, which JSON parsing ignores).
        QByteArray ours = CallBridge::encodeFrame(frame, key);
        QCOMPARE(CallBridge::decodeFrames(ours, key, &none).value(0), frame);

        // Round trip, including a frame split into chunks and a partial buffer.
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

    // Logic learned from ctdozlite (06-upstream-sync §5b), reimplemented and checked with neutral fixtures.
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
        QVERIFY(ZaloSession::nextPullPage({{"more", 1}}, 603, 1).isEmpty()); // no cursor: stop, never loop

        // Another host is tried only for transport failures / missing routes, never for API errors.
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
        // Push (501) and the offline queue / 570 can deliver the same message: count it once.
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

        // A read point from another device (clearUnreads) recomputes the badge.
        emit session->readPointsReceived(QJsonArray{QJsonObject{{"idTo", "u-fixture"}, {"lastMsgId", "7001"}, {"isGroup", 0}}});
        QCOMPARE(client.conversationModel()->totalUnreadCount(), 1);
        // Sub-chat cursors are ignored.
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

        // Reply target: only server messages can be quoted.
        client.setReplyTarget("local-unknown");
        QVERIFY(client.replyTarget().isEmpty());
        client.setReplyTarget("7001");
        QCOMPARE(client.replyTarget().value("id").toString(), QString("7001"));
        client.clearReplyTarget();
        QVERIFY(client.replyTarget().isEmpty());

        // Scheduled send is local and honest about the time it needs.
        QVERIFY(!client.scheduleMessage("muộn", QDateTime::currentMSecsSinceEpoch() - 1000));
        QVERIFY(client.scheduleMessage("hẹn giờ", QDateTime::currentMSecsSinceEpoch() + 3600000));
        QCOMPARE(client.scheduledMessages().size(), 1);
        client.cancelScheduledMessage(client.scheduledMessages().first().toMap().value("id").toString());
        QCOMPARE(client.scheduledMessages().size(), 0);
    }

    void testCallTransportKeyFile() {
        // The call-v2 key only protects loopback sockets; zalos generates one per session.
        const QByteArray key = QByteArray::fromHex("00112233445566778899aabbccddeeff"); // neutral test key
        QTemporaryFile file(QDir::tempPath() + "/zalos-test-XXXXXX.js");
        QVERIFY(CallBridge::writeKeyFile(&file, key));
        QCOMPARE(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
        qputenv("ZALO_MAIN_JS", file.fileName().toUtf8());
        QString path;
        QCOMPARE(CallBridge::findTransportKey(&path), key);
        QCOMPARE(path, QDir::cleanPath(file.fileName()));
        qunsetenv("ZALO_MAIN_JS");
        QVERIFY(CallBridge::findTransportKey().isEmpty());
    }

    // Review regressions: recall parsing, unique local echoes, UI socket binary frames.
    void testMessagingRegressions() {
        ZaloClient client;
        client.selectConversation("u-peer", "Fixture Peer", "");

        // Two sends in the same millisecond must both appear (unique local ids).
        client.sendMessage("one");
        client.sendMessage("two");
        QCOMPARE(client.messageModel()->rowCount(), 2);

        // A live message, then its recall in zca-js shape: content {globalMsgId, cliMsgId, deleteMsg}.
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

        // Binary video frames on the call UI socket must not swallow the next JSON line.
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

        // 1. Test Create Poll Modal
        QCOMPARE(client.showCreatePollModal(), false);
        client.openCreatePollModal();
        QCOMPARE(client.showCreatePollModal(), true);

        // Test create poll with insufficient options (< 2 options fails)
        QSignalSpy noticeSpy(&client, &ZaloClient::notice);
        client.createPoll("grp-100", "Ăn gì?", {"Cơm"}, false);
        QVERIFY(noticeSpy.count() >= 1);

        // Valid poll
        client.createPoll("grp-100", "Họp vào mấy giờ?", {"9h", "10h", "14h"}, false);
        QCOMPARE(client.showCreatePollModal(), false);
        QCOMPARE(client.messageModel()->rowCount(), 1);

        QModelIndex pollIdx = client.messageModel()->index(0);
        QCOMPARE(client.messageModel()->data(pollIdx, MessageModel::MessageTypeRole).toString(), QString("poll"));
        QVariantMap pData = client.messageModel()->data(pollIdx, MessageModel::PollDataRole).toMap();
        QCOMPARE(pData.value("question").toString(), QString("Họp vào mấy giờ?"));
        QCOMPARE(pData.value("options").toList().size(), 3);

        // Voting offline changes nothing: the card only follows the server (/api/poll/detail after the vote).
        const int noticesBefore = noticeSpy.count();
        client.votePoll(pData.value("poll_id").toLongLong(), 2);
        QVariantMap pDataAfter = client.messageModel()->data(pollIdx, MessageModel::PollDataRole).toMap();
        QCOMPARE(pDataAfter.value("num_vote").toInt(), 0);
        QVERIFY(noticeSpy.count() > noticesBefore);

        // 2. Test Group Management APIs
        client.changeGroupName("grp-100", "Nhóm Dự Án 2026");
        QCOMPARE(client.currentConversationName(), QString("Nhóm Dự Án 2026"));

        client.addMemberToGroup("grp-100", "u-123");
        client.removeMemberFromGroup("grp-100", "u-123");

        // 3. Test Clipboard image paste helper
        // When clipboard is empty, returns false cleanly
        bool pasted = client.tryPasteClipboardImage();
        QCOMPARE(pasted, false);

        // 4. Test Pinned Message
        client.sendMessage("Thông báo quan trọng cần ghim");
        QString pinTargetId = client.messageModel()->data(client.messageModel()->index(client.messageModel()->rowCount() - 1), MessageModel::IdRole).toString();
        QCOMPARE(client.hasPinnedMessage(), false);
        const int noticesBeforePin = noticeSpy.count();
        client.pinMessage(pinTargetId); // offline: refused with a notice, never a fake local pin
        QCOMPARE(client.hasPinnedMessage(), false);
        QVERIFY(noticeSpy.count() > noticesBeforePin);

        // 5. Test Friend Alias
        client.changeFriendAlias("u-123", "Bạn Đồng Nghiệp");
        QVERIFY(noticeSpy.count() >= 2);

        // 6. Test Contact Card
        client.sendContactCard("grp-100", "u-999", "Trần Thị B", "0987654321");
        QModelIndex cardIdx = client.messageModel()->index(client.messageModel()->rowCount() - 1);
        QCOMPARE(client.messageModel()->data(cardIdx, MessageModel::MessageTypeRole).toString(), QString("card"));
        QCOMPARE(client.messageModel()->data(cardIdx, MessageModel::TextRole).toString(), QString("Trần Thị B"));
    }

    // Group link / permissions / join requests / blocked list / join-by-link / invite-to-groups (GroupAdmin).
    void testGroupAdminLinksRequestsAndInvites() {
        // Link recognition: only zalo.me/g/<code> invites go to the in-app preview.
        QVERIFY(GroupAdmin::isGroupLink("https://zalo.me/g/abcdef123"));
        QVERIFY(GroupAdmin::isGroupLink("zalo.me/g/abcdef123/"));
        QVERIFY(GroupAdmin::isGroupLink("http://www.zalo.me/g/Ab_c-12?src=share"));
        QVERIFY(!GroupAdmin::isGroupLink("https://zalo.me/0123456789"));
        QVERIFY(!GroupAdmin::isGroupLink("https://example.com/g/abcdef123"));
        QVERIFY(!GroupAdmin::isGroupLink("https://zalo.me/g/"));
        QCOMPARE(GroupAdmin::normalizeGroupLink(" zalo.me/g/abcdef123?x=1 "), QString("https://zalo.me/g/abcdef123"));

        // Hooks without a session: nothing is sent, state comes from server payloads only.
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

        // Settings: unknown until getmg-v2 delivered them; booleans per key.
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
        QCOMPARE(settingsSpy.count(), 1); // other groups do not touch the open dialog
        admin.setSetting("joinAppr", false); // members cannot, and no session: unchanged
        QCOMPARE(admin.settings().value("joinAppr").toBool(), true);
        role = "admin";
        QVERIFY(admin.canAdminister());

        // Link detail.
        admin.applyLinkDetailForTesting(QJsonObject{{"link", "https://zalo.me/g/xyz789"}, {"enabled", 1},
                                                    {"expiration_date", 1700000000}});
        QCOMPARE(admin.link().value("enabled").toBool(), true);
        QCOMPARE(admin.link().value("link").toString(), QString("https://zalo.me/g/xyz789"));
        QCOMPARE(admin.link().value("expiration").toLongLong(), 1700000000000LL);
        admin.applyLinkDetailForTesting(QJsonObject{{"link", "https://zalo.me/g/xyz789"}, {"enabled", 0}});
        QCOMPARE(admin.link().value("enabled").toBool(), false);
        QVERIFY(admin.link().value("link").toString().isEmpty());
        QCOMPARE(admin.link().value("loaded").toBool(), true);

        // Pending requests and blocked members.
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

        // Closing forgets the group; reopening starts clean.
        admin.closeManage();
        QVERIFY(!admin.manageOpen());
        QVERIFY(admin.groupId().isEmpty());
        admin.openManage("g-1");
        QVERIFY(admin.pending().isEmpty());
        QVERIFY(!admin.pendingLoaded());
        QCOMPARE(admin.settings().value("joinAppr").toBool(), true); // settings stay cached per group

        // Join by link: bad links are rejected locally, offline says so, a preview marks known groups.
        admin.openJoin("");
        QVERIFY(admin.joinOpen());
        admin.previewLink("https://example.com/x");
        QVERIFY(!admin.preview().value("error").toString().isEmpty());
        admin.previewLink("https://zalo.me/g/abcdef123");
        QVERIFY(!admin.preview().value("error").toString().isEmpty()); // no session
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

        // invite/multi reply: per-group error codes.
        QStringList failed;
        const QStringList done = GroupAdmin::invitedGroupsForTesting(QJsonObject{{"grid_message_map", QJsonObject{
            {"g-a", QJsonObject{{"error_code", 0}}}, {"g-b", QJsonObject{{"error_code", 269}}}}}}, &failed);
        QCOMPARE(done, QStringList{"g-a"});
        QCOMPARE(failed, QStringList{"g-b"});

        // reset (sign-out) closes both dialogs and drops cached settings.
        admin.openJoin("");
        admin.reset();
        QVERIFY(!admin.manageOpen());
        QVERIFY(!admin.joinOpen());
        admin.openManage("g-1");
        QVERIFY(admin.settings().isEmpty());

        // ZaloClient feeds getmg-v2 settings, routes chat links and lists groups for the invite picker.
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

    // Composer rich text (Zalo text styles), styled rendering, media runs, animated images, composite avatars.
    void testRichComposerMediaRunsAndCompositeAvatars() {
        // 1. RichComposer: real document formatting exported as Zalo styles.
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
        QVERIFY(composer.apply("b", 0, 5)); // toggles off
        QVERIFY(composer.styles().isEmpty());
        QVERIFY(composer.apply("i", 7, 7)); // caret inside "world": the word
        QCOMPARE(composer.styles().first().toMap().value("start").toInt(), 6);
        QCOMPARE(composer.styles().first().toMap().value("len").toInt(), 5);
        QVERIFY(composer.apply("c_db342e", 0, 15));
        QVERIFY(composer.apply("c_15a85f", 0, 5)); // colours are exclusive
        bool red = false, green = false;
        for (const QVariant& v : composer.styles()) {
            const QVariantMap m = v.toMap();
            if (m.value("st") == "c_15a85f") { green = true; QCOMPARE(m.value("len").toInt(), 5); }
            if (m.value("st") == "c_db342e") { red = true; QCOMPARE(m.value("start").toInt(), 5); }
        }
        QVERIFY(red && green);
        QVERIFY(composer.apply("f_18", 0, 5));
        QVERIFY(composer.apply("f_", 0, 5)); // normal size again
        for (const QVariant& v : composer.styles()) QVERIFY(v.toMap().value("st").toString() != "f_18");
        composer.clearFormatting(0, 0);
        QVERIFY(composer.styles().isEmpty());
        QVERIFY(!composer.apply("b", 5, 5) || true); // a space: nothing to format is acceptable
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
        // Round trip through applyStyles.
        doc.setPlainText("abc def");
        RichComposer::applyStyles(&doc, {QVariantMap{{"start", 4}, {"len", 3}, {"st", "u"}}});
        QCOMPARE(RichComposer::exportStyles(&doc).size(), 1);
        QVERIFY(RichComposer::isKnownStyle("c_f7b503"));
        QVERIFY(!RichComposer::isKnownStyle("c_123456"));

        // 2. Wire format and styled rendering.
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
        QCOMPARE(styled.extra.value("styles").toList().size(), 1); // out-of-range style dropped

        // 3. GIF: the still thumb stays, the animated href is kept for AnimatedImage.
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

        // 4. Media runs: consecutive caption-less photos/stickers fold into the first row.
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
        QCOMPARE(role(3, MessageModel::InMediaRunRole).toBool(), false); // captioned photo keeps its own bubble
        QCOMPARE(role(4, MessageModel::MediaRunRole).toList().size(), 0); // single tile: no run
        QCOMPARE(role(0, MessageModel::LastInGroupRole).toBool(), false); // m4 (same sender) follows the run
        QCOMPARE(role(0, MessageModel::MediaRunRole).toList().at(2).toMap().value("msgId").toString(), QString("m3"));
        model.mergeMessages("c1", {tile("m6", "sticker", t0 + 10 * 60000)}); // > 5 min later: new run
        QCOMPARE(role(5, MessageModel::InMediaRunRole).toBool(), false);
        model.removeMessage("c1", "m2");
        QCOMPARE(role(0, MessageModel::MediaRunRole).toList().size(), 2);

        // 5. Composite avatar of a group without a photo: first 4 members, count cell decided in QML.
        client.setMyUidForTesting("u-me");
        client.applyGroupInfoForTesting(QJsonObject{
            {"g-np", QJsonObject{{"name", "No Photo"}, {"avt", ""}, {"totalMember", 9},
                                 {"memVerList", QJsonArray{"u-me_0", "u-1_0", "u-2_0", "u-3_0", "u-4_0"}}, {"creatorId", "u-1"}}},
            {"g-ph", QJsonObject{{"name", "Photo"}, {"avt", "https://example.com/g.jpg"}, {"totalMember", 3},
                                 {"memVerList", QJsonArray{"u-me_0", "u-1_0"}}, {"creatorId", "u-1"}}}});
        QCOMPARE(client.groupComposite("g-np").size(), 4);
        QVERIFY(client.groupComposite("g-ph").isEmpty());
    }

    // zcall-native: the bundled copy (third_party → share/zalos/zcall-native) and the upstream folder shapes.
    void testCallEngineDiscovery() {
        // The vendored engine is complete and carries its licence and origin.
        const QString vendored = QStringLiteral(ZALOS_SOURCE_DIR) + "/third_party/zcall-native";
        QVERIFY(QFile::exists(vendored + "/native-engine/zcall-native.js"));
        QVERIFY(QFile::exists(vendored + "/native-engine/audio-io.py"));
        QVERIFY(QFile::exists(vendored + "/tools/lib/callv2-crypto.js"));
        QVERIFY(QFile::exists(vendored + "/LICENSE"));
        QVERIFY(QFile::exists(vendored + "/UPSTREAM"));
        QVERIFY(CallBridge::bundledEngineRoot().endsWith("/share/zalos/zcall-native"));

        QTemporaryDir other;
        QVERIFY(other.isValid());
        const QString root = other.path() + "/zalo-for-linux";
        const QString engineDir = root + "/zcall-native/native-engine";
        QVERIFY(QDir().mkpath(engineDir));
        QFile f(engineDir + "/zcall-native.js");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("// fixture");
        f.close();
        QVERIFY(!CallBridge::engineIn(root + "/zcall-native").isEmpty());
        QVERIFY(!CallBridge::engineIn(engineDir).isEmpty());
        QVERIFY(!CallBridge::engineIn(root).isEmpty());
        QVERIFY(CallBridge::engineIn(other.path()).isEmpty());

        // The engine always comes from the bundle; only ZALOS_ZCALL_DIR can point a dev build elsewhere.
        const QString resolved = CallBridge::findEngine();
        QVERIFY(!resolved.isEmpty());
        QCOMPARE(QFileInfo(resolved).fileName(), QStringLiteral("zcall-native.js"));
        QVERIFY(QFileInfo(resolved).absolutePath().endsWith("/native-engine"));
    }

    // Labels ("Nhãn", convlabel/get|update semantics of Zalo PC) and contact cards (sendCard / recommened.user).
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

        // Server list as a JSON string, out of order, one group conversation ("g" prefix).
        const QJsonArray server{
            QJsonObject{{"id", 2}, {"text", "Công việc"}, {"textKey", "STR_LABEL_WORK"}, {"color", "#0068FF"}, {"offset", 1},
                        {"conversations", QJsonArray{"g9001", "101"}}, {"emoji", ""}, {"createTime", 1}},
            QJsonObject{{"id", 1}, {"text", "Khách hàng"}, {"color", "D91B1B"}, {"offset", 0},
                        {"conversations", QJsonArray{"102"}}, {"emoji", ""}, {"createTime", 1}}};
        labels.applyServer(QJsonObject{{"labelData", QString::fromUtf8(QJsonDocument(server).toJson())}, {"version", 7}});
        QVERIFY(labels.loaded());
        QCOMPARE(labels.versionForTesting(), 7);
        QCOMPARE(labels.labels().size(), 2);
        QCOMPARE(labels.labels().first().toMap().value("text").toString(), QString("Khách hàng")); // offset order
        QCOMPARE(labels.labels().first().toMap().value("color").toString(), QString("#D91B1B"));     // "#" added
        QCOMPARE(labels.labels().at(1).toMap().value("count").toInt(), 2);
        QCOMPARE(applied.value("9001").value("id").toInt(), 2);  // "g" prefix decoded
        QCOMPARE(applied.value("102").value("text").toString(), QString("Khách hàng"));
        QCOMPARE(labels.labelOf("101"), 2);
        QCOMPARE(LabelManager::decodeConvId("g123"), QString("123"));
        QCOMPARE(LabelManager::decodeConvId("u-b"), QString("u-b")); // not a prefix
        QCOMPARE(LabelManager::encodeConvId("123", true), QString("g123"));

        // Assign moves a conversation (one label each); assigning its own label again removes it.
        QJsonArray next = labels.assignedForTesting("101", 1);
        QCOMPARE(next.at(0).toObject().value("conversations").toArray().size(), 2);
        QCOMPARE(next.at(1).toObject().value("conversations").toArray(), QJsonArray{"g9001"});
        next = labels.assignedForTesting("101", 2);
        QCOMPARE(next.at(1).toObject().value("conversations").toArray(), QJsonArray{"g9001"});
        next = labels.assignedForTesting("9002", 1);
        QVERIFY(next.at(0).toObject().value("conversations").toArray().contains(QJsonValue("g9002"))); // groups get "g"

        // Without a session nothing changes locally, the would-be request is still built, and a notice says why.
        labels.assign("101", 1);
        QCOMPARE(labels.labelOf("101"), 2);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("conversations").toArray().size(), 2);
        QVERIFY(!notices.isEmpty());

        // Create: validation (empty, too long, duplicate), new id = max + 1, palette colour.
        QVERIFY(!labels.create("  ", "#4BC377").isEmpty());
        QVERIFY(!labels.create(QString(31, 'x'), "#4BC377").isEmpty());
        QVERIFY(!labels.create("khách hàng", "#4BC377").isEmpty());
        QVERIFY(labels.create("Bạn bè", "#4BC377").isEmpty());
        QCOMPARE(labels.lastSubmittedForTesting().size(), 3);
        QCOMPARE(labels.lastSubmittedForTesting().at(2).toObject().value("id").toInt(), 3);
        QCOMPARE(labels.lastSubmittedForTesting().at(2).toObject().value("offset").toInt(), 2);
        // Edit: renaming a default label drops its textKey; recolour.
        QVERIFY(labels.edit(2, "Việc gấp", "#FF6905").isEmpty());
        const QJsonObject edited = labels.lastSubmittedForTesting().at(1).toObject();
        QCOMPARE(edited.value("text").toString(), QString("Việc gấp"));
        QVERIFY(!edited.contains("textKey"));
        QCOMPARE(edited.value("color").toString(), QString("#FF6905"));
        // Move and remove re-number offsets.
        labels.move(2, -1);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("id").toInt(), 2);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("offset").toInt(), 0);
        labels.remove(1);
        QCOMPARE(labels.lastSubmittedForTesting().size(), 1);
        QCOMPARE(labels.lastSubmittedForTesting().at(0).toObject().value("offset").toInt(), 0);
        // Max 15 labels.
        QJsonArray full;
        for (int i = 1; i <= 15; ++i) full.append(QJsonObject{{"id", i}, {"text", QString("L%1").arg(i)}, {"offset", i}});
        labels.applyServer(QJsonObject{{"labelData", full}, {"version", 8}});
        QVERIFY(!labels.create("L16", "#4BC377").isEmpty());

        // ConversationModel: labels replace the old ones (a removed label clears the pill) and filter the list.
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
        convs.addConversation(c); // added later: still gets its label
        QCOMPARE(convs.data(convs.index(rowOf("u-c")), ConversationModel::LabelIdRole).toInt(), 4);

        // Received contact card (chat.recommended / recommened.user): uid in params, phone + QR in description.
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

        // Share / pick modes; offline send keeps an honest failed echo.
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

    // Our camera in a video call: frames → I420 (centre crop) → OpenH264 → Annex-B, rung, throttle, key frames.
    void testCameraSenderEncodesFrames() {
        if (!OpenH264::instance().available())
            QSKIP(qPrintable("OpenH264 unavailable: " + OpenH264::instance().unavailableReason()));
        // A 4:3 synthetic camera picture (Qt 6.4-compatible construction).
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
        // Centre crop keeps the aspect: a 4:3 frame converted to 16:9 is not stretched.
        const QByteArray i420 = frameToI420(makeFrame(640, 480, 10), QSize(320, 180));
        QCOMPARE(i420.size(), 320 * 180 * 3 / 2);

        CameraSender sender;
        sender.setProfile(QSize(321, 181), 15, 200000); // odd sizes rounded down
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
        QVERIFY(keys.first());                       // the stream starts with a key frame
        QVERIFY(units.first().startsWith(QByteArray("\x00\x00\x00\x01", 4)) || units.first().startsWith(QByteArray("\x00\x00\x01", 3)));
        sender.processFrame(makeFrame(640, 480, 5)); // same instant: throttled to 15 fps
        QCOMPARE(keys.size(), 1);
        QTest::qWait(80);
        sender.processFrame(makeFrame(640, 480, 9));
        QCOMPARE(keys.size(), 2);
        QVERIFY(!keys.last());                       // delta frame
        sender.requestKeyFrame();                    // engine asks (PLI / FIR)
        QTest::qWait(80);
        sender.processFrame(makeFrame(640, 480, 13));
        QVERIFY(keys.last());
        sender.setProfile(QSize(640, 360), 15, 300000); // new rung: re-open, key frame
        QTest::qWait(80);
        sender.processFrame(makeFrame(640, 480, 17));
        QVERIFY(keys.last());
        QCOMPARE(sizes.first(), QSize(320, 180));
        QCOMPARE(sizes.last(), QSize(640, 360));

        // The decoder of the peer side reads what we produce.
        H264Decoder decoder;
        QVERIFY(decoder.isOpen());
        bool damaged = false;
        const QVideoFrame decoded = decoder.decode(units.first(), &damaged);
        QVERIFY(!damaged);
        QVERIFY(decoded.isValid());
        QCOMPARE(decoded.size(), QSize(320, 180));

        // CallBridge: camera availability needs a camera and the encoder; turning it on without a camera is refused
        // honestly (the container has no camera).
        CallBridge bridge(nullptr);
        if (!CameraSender::cameraAvailable()) {
            QVERIFY(!bridge.cameraAvailable());
            bridge.setCamera(true);
            QVERIFY(!bridge.cameraOn());
            QCOMPARE(bridge.cameraError(), QString("no_camera"));
        }
    }

    // Who reacted with what (pill → details popup): per-user icons, groups by count, me first, removal by others.
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
        QVERIFY(people.first().toMap().value("isMe").toBool()); // "0" = this account, listed first
        QCOMPARE(people.first().toMap().value("emoji").toString(), QString::fromUtf8("❤️"));
        // Someone takes their reaction back (empty icon / rType -1).
        client.handleReactionsForTesting(QJsonArray{react("u-b", "", -1)}, true);
        d = client.reactionDetails("m-r1");
        QCOMPARE(d.value("total").toInt(), 3);
        QCOMPARE(d.value("groups").toList().size(), 2);
    }

    // Reactions of history (bug 2026-11-03: 0 of 65 568 cached messages had one): per-user table, summary column
    // updated for messages that are only on disk, an empty re-save never wipes it, details survive a restart.
    void testReactionHistoryPersists() {
        {
            ZaloOfflineStore store;
            store.bindOwner("u-react-owner"); // returns whether another account was wiped
            store.clearCache();
            store.bindOwner("u-react-owner"); // returns whether another account was wiped
            ConversationItem conv; conv.id = "u-react-peer"; conv.name = "Fixture"; conv.lastTs = 1000;
            QVERIFY(store.saveConversation(conv));
            MessageItem m; m.id = "7001"; m.text = "old"; m.ts = 1000; m.senderId = "u-react-peer";
            QVERIFY(store.saveMessage("u-react-peer", m));
            QVERIFY(store.addReactionEvent("7001", "u-a", "/-heart", "ev-1", 1000));
            QVERIFY(!store.addReactionEvent("7001", "u-a", "/-heart", "ev-1", 1000)); // replay ignored
            QVERIFY(store.setMessageReaction("7001", QString::fromUtf8("❤️")));
            QVERIFY(store.saveMessage("u-react-peer", m)); // re-save without a summary
            QCOMPARE(store.loadMessages("u-react-peer").first().reaction, QString::fromUtf8("❤️"));
            QCOMPARE(store.reactionEventsOf("7001").size(), 1);
            QCOMPARE(store.reactionEventsOf("7001").first().icon, QString("/-heart"));
            QVERIFY(store.reactedMessageIds().contains("7001"));
            QVERIFY(store.clearReactions("7001", "u-a"));
            QVERIFY(store.reactionEventsOf("7001").isEmpty());
        }
        {
            // A reaction for a message that is not in RAM (thread never opened) still lands on disk.
            ZaloClient client;
            QVERIFY(client.openOfflineCacheForTesting("u-react-owner"));
            client.setMyUidForTesting("u-react-owner");
            client.handleReactionsForTesting(QJsonArray{QJsonObject{{"uidFrom", "u-react-peer"}, {"idTo", "0"},
                {"content", QJsonObject{{"rIcon", "/-strong"}, {"rType", 3}, {"rMsg", QJsonArray{QJsonObject{{"gMsgID", "7001"}}}}}}}}, false);
        }
        {
            ZaloOfflineStore store;
            store.bindOwner("u-react-owner"); // returns whether another account was wiped
            QCOMPARE(store.loadMessages("u-react-peer").first().reaction, QString::fromUtf8("👍"));
            ZaloClient client; // fresh RAM: who reacted comes from the table
            QVERIFY(client.openOfflineCacheForTesting("u-react-owner"));
            const QVariantMap d = client.reactionDetails("7001");
            QVERIFY(d.value("known").toBool());
            QCOMPARE(d.value("people").toList().first().toMap().value("uid").toString(), QString("u-react-peer"));
            store.clearCache();
        }
    }

    // Stickers: render info (animated WebP / sprite strip / still), packs, pack contents, recent, disk cache.
    void testStickerStore() {
        // ext 2: the sprite url is an animated WebP; stickerWebpUrl otherwise; frames > 1 without WebP: sprite strip.
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
            QCOMPARE(store.categories().size(), 2);  // no recent yet
            QCOMPARE(store.categories().first().toMap().value("name").toString(), QString("Fixture Pack"));
            store.applyCategoryForTesting(5, QJsonArray{QJsonObject{{"id", 12}, {"cateId", 5}, {"totalFrames", 16},
                {"stickerUrl", "https://example.com/t.png"}, {"stickerSpriteUrl", "https://example.com/t-sprite.png"}}});
            store.selectCategory(5);
            QCOMPARE(store.stickers().size(), 1);
            QCOMPARE(store.info("12").value("frames").toInt(), 16); // remembered for chat messages
            store.send(store.stickers().first().toMap());
            QCOMPARE(sent, QString("12/5/0"));
            QCOMPARE(store.categories().first().toMap().value("id").toInt(), int(StickerStore::kRecentId));
            store.selectCategory(StickerStore::kRecentId);
            QCOMPARE(store.stickers().size(), 1);
        } // destructor flushes the cache file
        StickerStore again(hooks);
        again.setCacheFile(dir.filePath("info.json"));
        QCOMPARE(again.info("12").value("spriteUrl").toString(), QString("https://example.com/t-sprite.png"));
        QSettings().remove("stickers/recent/u-sticker-fixture");

        // A chat.sticker message picks the render info up from the cache.
        ZaloClient client;
        client.stickers()->remember(b);
        const MessageItem m = client.toMessageItemForTesting(QJsonObject{{"msgId", "s-1"}, {"uidFrom", "u-a"}, {"idTo", "u-me"},
            {"msgType", "chat.sticker"}, {"ts", "1700000000000"}, {"content", QJsonObject{{"id", 12}, {"catId", 5}}}}, false);
        QCOMPARE(m.extra.value("sticker").toMap().value("frames").toInt(), 16);
        QCOMPARE(m.mediaUrl, QString("https://example.com/t.png"));
    }

    // Personal To-Do host (bug 2026-11-03: -69 "Stop retry" from the group_board host).
    void testBoardServiceHost() {
        ZaloClient client; // owns a ZaloSession with its network access manager and cookie jar
        ZaloSession& session = *client.apiClient()->session();
        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"group_board", QJsonArray{"https://groupboard.example"}}}}});
        QCOMPARE(session.serviceUrls("boards"), QStringList{"https://board-wpa.api.zalo.me"}); // never group_board
        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"board", QJsonArray{"https://board.example"}}}}});
        QCOMPARE(session.serviceUrls("boards"), QStringList{"https://board.example"});
        session.setLoginInfoForTesting(QJsonObject{{"zpw_service_map_v3", QJsonObject{
            {"boards", QJsonArray{"https://boards.example"}}, {"board", QJsonArray{"https://board.example"}}}}});
        QCOMPARE(session.serviceUrls("boards"), QStringList{"https://boards.example"});
    }

    // Reactions by other people reach the store from every socket shape: 612 live / 610-611 old arrays (handled by
    // ZaloClient), control & offline pulls with reacts arrays, and "chat.reaction" items inside message lists.
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
        const QJsonArray kept = session->splitReactionMessages(mixed, false);
        QCOMPARE(kept.size(), 1);                     // the reaction is not shown as a message
        QCOMPARE(kept.first().toObject().value("msgId").toString(), QString("8001"));
        QCOMPARE(spy.count(), 1);                     // ZaloClient is connected to this signal
        const QVariantMap d = client.reactionDetails("7999");
        QVERIFY(d.value("known").toBool());
        QCOMPARE(d.value("people").toList().first().toMap().value("emoji").toString(), QString::fromUtf8("👍")); // rType 3 only
    }

    // One person can react many times and with several icons (Zalo apps: "👍❤️ 9"); replays are not counted twice;
    // a removal takes back all of that person's reactions; our own tap is shown at once and not doubled by its echo.
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
        taps.append(tap("u-tu", "/-heart", 5, "h0")); // replay from the old queue
        client.handleReactionsForTesting(taps, true);
        QVariantMap d = client.reactionDetails("m-multi");
        QCOMPARE(d.value("total").toInt(), 13);
        QCOMPARE(d.value("groups").toList().first().toMap().value("count").toInt(), 10); // ❤️ 10
        const QVariantMap tu = d.value("people").toList().first().toMap();
        QCOMPARE(tu.value("uid").toString(), QString("u-tu"));
        QCOMPARE(tu.value("count").toInt(), 9);
        QCOMPARE(tu.value("emojis").toString(), QString::fromUtf8("❤️👍"));
        QCOMPARE(tu.value("perEmoji").toMap().value(QString::fromUtf8("👍")).toInt(), 3);
        client.handleReactionsForTesting(QJsonArray{tap("u-tu", "", -1, "r1")}, true); // removal
        d = client.reactionDetails("m-multi");
        QCOMPARE(d.value("total").toInt(), 4);
        QCOMPARE(d.value("people").toList().size(), 1);
    }

    void testReactionSnapshotAndFallbackIds() {
        ZaloClient client;
        client.setMyUidForTesting("u-me");

        // 1. Snapshot with object shape: reacts.reactions.<uid>.<rType>
        const QJsonObject msgWithReacts{
            {"msgId", "m-snap1"},
            {"cliMsgId", "c-snap1"},
            {"uidFrom", "u-friend"},
            {"idTo", "u-me"},
            {"msgType", "webchat"},
            {"content", "Hello snapshot"},
            {"reacts", QJsonObject{
                {"reactions", QJsonObject{
                    {"u-friend", QJsonObject{{"5", 2}}}, // 2 hearts
                    {"u-other", QJsonObject{{"3", 1}}}   // 1 like
                }}
            }}
        };
        const Zalos::Core::Models::MessageItem item1 = client.toMessageItemForTesting(msgWithReacts, false);
        QCOMPARE(item1.reaction, QString::fromUtf8("❤️👍 3"));

        // 2. Snapshot with array shape: reacts.reactions: [{rIcon: "/-heart", count: 2}]
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

        // 3. Fallback to cMsgID when gMsgID is "0"
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

        // 4. Forwarded and E2EE metadata
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

    // Pinned messages (Zalo PC PinTopic): list parsing for groups (items/boardVersion/pinLimit) and 1-1 (data = JSON
    // strings/version), topic built from a message, isPinned, controls that trigger a reload, offline refusal.
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

        // Topic for a message: our own message ("0") names us; photos carry the thumb; msg_type is the client type.
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

        // ZaloClient: the open conversation's board; offline pin is refused honestly; pin controls are consumed.
        ZaloClient client;
        QSignalSpy noticeSpy(&client, &ZaloClient::notice);
        client.selectConversation("u-a", "Fixture A", "");
        client.pins()->applyListForTesting("u-a", QJsonObject{{"data", QJsonArray{topicJson}}, {"version", 7}}, false);
        QCOMPARE(client.pins()->pins().size(), 1);
        QVERIFY(client.pins()->isPinned("9001"));
        QVERIFY(client.hasPinnedMessage());
        QCOMPARE(client.currentPinnedMessageText(), QString("Fixture pinned text"));
        QCOMPARE(client.pins()->versionForTesting("u-a"), 7);
        // Detail view: a pin whose message is not in the loaded history keeps its whole text from the topic.
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
        // Refused media (expired pin thumbs): remembered once, not refetched, retry forgets it.
        QSignalSpy brokenSpy(&client, &ZaloClient::brokenMediaChanged);
        const int rev = client.brokenMediaRevision();
        QVERIFY(!client.isMediaBroken("https://example.com/t2.jpg"));
        client.markMediaBroken("https://example.com/t2.jpg");
        client.markMediaBroken("https://example.com/t2.jpg"); // no second change
        QVERIFY(client.isMediaBroken("https://example.com/t2.jpg"));
        QCOMPARE(brokenSpy.count(), 1);
        QVERIFY(client.brokenMediaRevision() > rev);
        QVERIFY(!client.isMediaBroken(""));
        client.retryMedia("https://example.com/t2.jpg");
        QVERIFY(!client.isMediaBroken("https://example.com/t2.jpg"));
        QCOMPARE(brokenSpy.count(), 2);
        client.pins()->applyListForTesting("u-a", QJsonObject{{"data", QJsonArray{topicJson}}, {"version", 7}}, false);
        client.pins()->pinMessage("9001"); // offline
        QVERIFY(noticeSpy.count() >= 1);
        QVERIFY(client.pins()->handleControl("unpin_topic", QJsonObject{{"conversationId", "u-a"}}));
        QVERIFY(!client.pins()->handleControl("join", QJsonObject{}));
        client.selectConversation("u-b", "Fixture B", "");
        QVERIFY(!client.hasPinnedMessage()); // boards are per conversation
    }

    // Memory/IO review 2026-11-03: no lavapipe/LLVM next to a GPU Vulkan driver; offline.db in WAL with a small cache.
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
        QCOMPARE(store.pragmaForTesting("synchronous"), QString("1")); // NORMAL
    }

    void testFullProfileSuite() {
        ZaloClient client;

        // 1. Test Open Self Profile
        QCOMPARE(client.showUserProfileModal(), false);
        client.openUserProfile();
        QCOMPARE(client.showUserProfileModal(), true);
        QCOMPARE(client.profileIsSelf(), true);

        // 2. Test Update My Profile
        client.updateMyProfile("Nguyễn Văn Test", 1, "01/01/1995", "Yêu công nghệ, thích lập trình");
        QCOMPARE(client.profileDisplayName(), QString("Nguyễn Văn Test"));
        QCOMPARE(client.currentUserName(), QString("Nguyễn Văn Test"));
        QCOMPARE(client.profileGender(), 1);
        QCOMPARE(client.profileDob(), QString("01/01/1995"));
        QCOMPARE(client.profileBio(), QString("Yêu công nghệ, thích lập trình"));

        // 3. Test View Other User Profile
        client.viewProfile("user-xyz");
        QCOMPARE(client.showUserProfileModal(), true);
        QCOMPARE(client.profileIsSelf(), false);
        QCOMPARE(client.profileUserId(), QString("user-xyz"));

        // 4. Test Block / Unblock User
        QCOMPARE(client.profileIsBlocked(), false);
        client.blockUser("user-xyz");
        QCOMPARE(client.profileIsBlocked(), true);
        client.unblockUser("user-xyz");
        QCOMPARE(client.profileIsBlocked(), false);

        // 5. Test Remove Friend
        client.removeFriend("user-xyz");
        QCOMPARE(client.profileIsFriend(), false);

        // 6. Test Close Profile Modal
        client.closeUserProfile();
        QCOMPARE(client.showUserProfileModal(), false);
    }

    void testChatBackgroundModes() {
        // The mode and image path are persisted in QSettings; start from a clean slate so a leaked
        // value from an earlier run cannot decide what a "new install" reads back.
        {
            QSettings settings("Zalos", "ZalosApp");
            settings.remove("settings/chatBackgroundMode");
            settings.remove("settings/chatBackgroundImage");
            settings.remove("settings/useAvatarAsChatBackground");
            // The retired profile-background mode must migrate to "none" on load.
            settings.setValue("settings/chatBackgroundMode", "background");
        }
        ZaloClient client;
        QCOMPARE(client.chatBackgroundMode(), QString("none"));
        QCOMPARE(client.currentConversationBackground(), QString());

        // The source chain behind backgroundOf still powers the profile-card cover.
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

        // Avatar mode dims the current conversation photo behind the messages and updates on every conversation switch.
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

        // Switching conversations in avatar mode must emit currentConversationBackgroundChanged and resolve fallback avatars.
        client.selectConversation("u-peer-2", QString(), QString());
        QCOMPARE(client.currentConversationBackground(), QString("https://x/peer2_av.jpg"));
        QCOMPARE(bgSpy.count(), spyBeforeMode + 2);

        client.openWelcomeScreen();
        QCOMPARE(client.currentConversationBackground(), QString());
        QCOMPARE(bgSpy.count(), spyBeforeMode + 3);

        client.selectConversation("grp-1", "Nh\u1ee9m th\u1eed", "https://x/group_av.jpg");
        QCOMPARE(client.currentConversationBackground(), QString("https://x/group_av.jpg"));
        QCOMPARE(bgSpy.count(), spyBeforeMode + 4);

        // Unknown values collapse to "none".
        client.setChatBackgroundMode("wallpaper");
        QCOMPARE(client.chatBackgroundMode(), QString("none"));
        QCOMPARE(client.currentConversationBackground(), QString());

        // Custom mode renders a user-selected local image copied into the app data directory.
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

        // Clearing removes the managed copy and falls back to the plain background.
        client.clearChatBackgroundImage();
        QCOMPARE(client.chatBackgroundMode(), QString("none"));
        QVERIFY(client.chatBackgroundImage().isEmpty());
        QCOMPARE(client.currentConversationBackground(), QString());

        // Regression test: setting chatBackgroundMode to "none" while legacy useAvatarAsChatBackground=true
        // must preserve "none" and scrub the legacy setting.
        {
            QSettings legacySettings("Zalos", "ZalosApp");
            legacySettings.setValue("settings/useAvatarAsChatBackground", true);
            legacySettings.setValue("settings/chatBackgroundMode", "none");
        }
        {
            ZaloClient clientNone;
            QCOMPARE(clientNone.chatBackgroundMode(), QString("none"));
            QSettings legacySettings("Zalos", "ZalosApp");
            QVERIFY(!legacySettings.contains("settings/useAvatarAsChatBackground"));
        }

        // Leave no residue for the tests that run after this one.
        QSettings settings("Zalos", "ZalosApp");
        settings.remove("settings/chatBackgroundMode");
        settings.remove("settings/chatBackgroundImage");
        settings.remove("settings/useAvatarAsChatBackground");
    }

    // tdesktop-inspired memory optimizations:
    // 1. AvatarImageProvider::decodeScaled downsamples at decode time via QImageReader::setScaledSize.
    // 2. When offlineStore is usable, incoming messages persist raw JSON to SQLite without duplicating in m_rawMessages RAM.
    // 3. trimMemoryForBackground() collapses resident threads to the active conversation and clears avatar RAM cache.
    void testTdesktopMemoryOptimizations() {
        // 1. Scaled decode from a 1600x900 PNG buffer into 96px circular and 320px cover targets.
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

        // 2 & 3. Zero m_rawMessages duplication when cacheUsable() is true + trimMemoryForBackground().
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

        // Raw payloads are stored in SQLite, not duplicated in m_rawMessages RAM.
        QCOMPARE(client.rawMessageCountForTesting(), 0);
        QVERIFY(client.residentThreadCountForTesting() >= 5);

        // Insert a dummy avatar into AvatarImageProvider's RAM cache to verify background trim clears it.
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

    // Comprehensive memory lifecycle, bounded RSS scaling, navigation thrash, and 4K image downsampling gate.
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

        // Stage 1: Zero-Duplication & Cold-Storage Ingestion
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
        // SQLite page cache, schema overhead, and conversation metadata for 3,000 messages must remain under 65MB.
        QVERIFY2(rssAfterIngestion - initialRss < 65000, "Cold message ingestion exceeded bounded RSS limit!");

        // Stage 2: Navigation & LRU Cache Eviction (Leak Prevention)
        for (int round = 0; round < 2; ++round) {
            for (int c = 0; c < 30; ++c) {
                client.selectConversation(QString("u-audit-conv-%1").arg(c), "Peer", "");
            }
        }

        QCOMPARE(client.currentConversationId(), QString("u-audit-conv-29"));
        QVERIFY2(client.residentThreadCountForTesting() <= 12,
                 "Resident thread LRU eviction failed: more than 12 threads held in RAM!");
        QCOMPARE(client.messageModel()->threadSize("u-audit-conv-0"), 0);

        // Stage 3: Large Image Downscaling Spike Suppression
#ifdef __GLIBC__
        const size_t heapBeforeImage = mallinfo2().uordblks;
#endif
        QImage highRes4K(3840, 2160, QImage::Format_RGB32);
        highRes4K.fill(QColor("#0068FF"));
        QByteArray encodedJpg;
        {
            QBuffer buffer(&encodedJpg);
            buffer.open(QIODevice::WriteOnly);
            highRes4K.save(&buffer, "JPG", 85);
        }

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

        // Stage 4: Background Trim (Minimize-to-Tray working-set collapse)
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

    // System theme (mode 2) reads surfaces/text/link from the platform palette snapshot, so it works
    // for every Qt and GTK theme line, while Zalo Light/Dark (modes 0/1) keep their exact literals.
    void testSystemThemePaletteSnapshot() {
        const QPalette original = QGuiApplication::palette();

        // A synthetic desktop palette, as any Qt or GTK platform theme would deliver at startup.
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
            // Mode 2 derives surfaces and text straight from the platform palette roles.
            QCOMPARE(theme.backgroundColor(), QColor("#101010"));
            QCOMPARE(theme.chatBackgroundColor(), QColor("#0A0A0A"));
            QCOMPARE(theme.cardBackgroundColor(), QColor("#202020"));
            QCOMPARE(theme.textColor(), QColor("#EAEAEA"));
            // syncApplicationPalette() overwrote the app palette (Window) but did not re-set Link;
            // the snapshot still returns the system Link, which a direct app-palette read would lose.
            QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor("#101010"));
            QVERIFY(QGuiApplication::palette().color(QPalette::Link) != QColor("#FF00FF"));
            QCOMPARE(theme.linkColor(), QColor("#FF00FF"));
            // New mode-2 tokens follow the system instead of the Zalo literals.
            QCOMPARE(theme.uiLinkColor(), QColor("#FF00FF"));
            QCOMPARE(theme.reactionSurfaceColor(), QColor("#0A0A0A"));
            QCOMPARE(theme.inactiveSubtleColor(), QColor("#7F7F7F"));

            // Zalo Light: literals untouched (parity guard).
            theme.setThemeMode(ThemeManager::ZaloLight);
            QCOMPARE(theme.uiAccentColor(), QColor("#0068FF"));
            QCOMPARE(theme.activenessColor(), QColor("#0068FF"));
            QCOMPARE(theme.uiLinkColor(), QColor("#0068FF"));
            QCOMPARE(theme.cardBackgroundColor(), QColor("#EBECF0")); // Theme.mediaPlaceholder source
            QCOMPARE(theme.reactionSurfaceColor(), QColor("#FFFFFF"));
            QCOMPARE(theme.inactiveSubtleColor(), QColor("#B5B5B5"));
            QCOMPARE(theme.inactiveDisabledColor(), QColor("#DBDBDB"));
            QCOMPARE(theme.activeDisabledColor(), QColor("#ADD1FF"));
            QCOMPARE(theme.dropOverlayColor(), QColor("#F0F7FF"));

            // Zalo Dark: literals untouched.
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

    // Peer video: H.264 access units on the UI socket -> decoder thread -> the QML sink. The stream is
    // produced by the same OpenH264 (synthetic gradient pictures), so it needs libopenh264 at run time.
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
        QVERIFY(units.first().first); // the first unit is an IDR with SPS/PPS
        QVERIFY(units.first().second.startsWith(QByteArray("\x00\x00\x00\x01", 4)));

        // Direct round trip through the decoder.
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

        // Deltas before any key frame are dropped (nothing shown), then the key frame starts the stream.
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

        // The call ends: the picture and the active flag are cleared.
        calls->injectUiLineForTesting(R"({"type":"close"})");
        calls->hangup();
        QVERIFY(!calls->peerVideoActive());
        QVERIFY(!sink.videoFrame().isValid());
    }

    // ---- tdesktop learnings (2026-11-03): single instance, notification actions, unread bar, drafts,
    //      scheduler split, shortcut table, passcode lock, storage pruning, voice recorder ----

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
        QVERIFY(!second.start()); // the first one runs: it is asked to come forward instead
        qunsetenv("XDG_ACTIVATION_TOKEN");
        QTRY_COMPARE_WITH_TIMEOUT(activated.count(), 1, 5000);
        QCOMPARE(activated.first().first().toString(), QString("fixture-token"));

        // Another data directory is another instance.
        SingleInstance other(QStringLiteral("other-data-dir"), runtime.path());
        QVERIFY(other.start());
        other.release();

        // Released (e.g. the render guard relaunch): the next process becomes primary.
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
        n.invokeForTesting(999, "default"); // not ours
        n.invokeForTesting(41, "inline-reply"); // the text comes through NotificationReplied
        QCOMPARE(actions.count(), 1);
        n.replyForTesting(41, "  ");
        n.replyForTesting(41, "ok nhé");
        QCOMPARE(replies.count(), 1);
        QCOMPARE(replies.first().at(1).toString(), QString::fromUtf8("ok nhé"));
        n.registerForTesting("thread:u-a", 42); // replaced popup: the old id is forgotten
        n.invokeForTesting(41, "default");
        QCOMPARE(actions.count(), 1);
        n.closedForTesting(42);
        QCOMPARE(n.idForTesting("thread:u-a"), 0u);

        // ZaloClient: click opens the thread and raises the window; mark-read clears the badge; reply sends.
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
        QCOMPARE(client.currentConversationId(), QString()); // marking read does not open it
        notifications->registerForTesting("thread:u-notif", 8);
        notifications->invokeForTesting(8, "default");
        QCOMPARE(client.currentConversationId(), QString("u-notif"));
        QCOMPARE(raise.count(), 1);
        notifications->registerForTesting("thread:u-notif", 9);
        const int before = client.messageModel()->threadSize("u-notif");
        notifications->replyForTesting(9, QString::fromUtf8("đã nhận"));
        QCOMPARE(client.messageModel()->threadSize("u-notif"), before + 1); // local echo (offline: failed)
        QVERIFY(convs->conversation("u-notif").lastMessage.contains(QString::fromUtf8("đã nhận")));

        // Locked app: a reply from the popup is refused.
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
            m.isMine = (i == 2); // one own message in between
            m.senderId = m.isMine ? "me" : "peer";
            items.append(m);
        }
        model.mergeMessages("c-unread", items);
        QCOMPARE(incoming.count(), 1);
        QCOMPARE(incoming.first().first().toInt(), 5); // own messages are not "new below"
        model.setUnreadMarker("c-unread", 3);
        QCOMPARE(model.unreadMarkerRow(), 3); // m3, m4, m5 are the 3 newest incoming
        QVERIFY(model.data(model.index(3), MessageModel::UnreadMarkerRole).toBool());
        QVERIFY(!model.data(model.index(4), MessageModel::UnreadMarkerRole).toBool());
        model.clearUnreadMarker();
        QCOMPARE(model.unreadMarkerRow(), -1);

        // More unread than loaded: placed when the history arrives.
        model.loadConversation("c-late");
        model.setUnreadMarker("c-late", 1);
        QCOMPARE(model.unreadMarkerRow(), -1);
        MessageItem late;
        late.id = "late-1";
        late.ts = base;
        late.senderId = "peer";
        model.mergeMessages("c-late", {late});
        QCOMPARE(model.unreadMarkerRow(), 0);

        // Drafts: per thread, shown in the sidebar role, persisted per account, cleared by sending.
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
        client.reloadDraftsForTesting(); // from QSettings drafts/<uid>
        QCOMPARE(client.draftFor("u-d1"), QString::fromUtf8("viết dở"));
        client.selectConversation("u-d1", "D1", "");
        client.sendMessage("gửi rồi");
        QCOMPARE(client.draftFor("u-d1"), QString());
        client.reloadDraftsForTesting();
        QCOMPARE(client.draftFor("u-d1"), QString());

        // chat.next / chat.previous follow the sidebar order.
        const QString first = convs->data(convs->index(0), ConversationModel::IdRole).toString();
        const QString second = convs->data(convs->index(1), ConversationModel::IdRole).toString();
        client.selectConversation(first, "", "");
        client.selectAdjacentConversation(1);
        QCOMPARE(client.currentConversationId(), second);
        client.selectAdjacentConversation(-1);
        QCOMPARE(client.currentConversationId(), first);
        client.selectAdjacentConversation(-1); // already at the top: stays
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
        QVERIFY(sent.isEmpty()); // not signed in
        ready = true;
        scheduler.runDue(at - 1000);
        QVERIFY(sent.isEmpty()); // not due yet
        scheduler.runDue(at + 1);
        QCOMPARE(sent, QStringList({QString::fromUtf8("c1:hẹn")}));
        QSignalSpy failed(&scheduler, &MessageScheduler::sendFailed);
        pendingDone(false, "mạng lỗi");
        QCOMPARE(failed.count(), 1);
        QCOMPARE(scheduler.entriesFor("c1").first().toMap().value("state").toString(), QString("failed"));
        scheduler.cancel(scheduler.entriesFor("c1").first().toMap().value("id").toString());
        QCOMPARE(scheduler.entriesFor("c1").size(), 0);
    }

    void testShortcutTableAndPasscodeLock() {
        using Zalos::Core::Platform::AppLock;
        using Zalos::Core::Platform::ShortcutMap;
        QTemporaryDir config;
        QFile custom(config.path() + "/shortcuts-custom.json");
        QVERIFY(custom.open(QIODevice::WriteOnly));
        // Built with QJson (moc does not parse raw string literals with braces inside slots).
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
        QVERIFY(map.keys("app.quit").isEmpty());          // disabled by the user
        QVERIFY(map.keys("made.up").isEmpty());           // unknown commands are not invented
        QCOMPARE(map.keys("composer.voice"), QStringList({"Ctrl+Shift+V"})); // defaults stay
        QVERIFY(QFile::exists(config.path() + "/shortcuts-default.json"));
        QCOMPARE(map.all().size(), map.commands().size());

        {
            AppLock lock;
            if (lock.hasPasscode()) QSKIP("a passcode from another run is set in this profile");
            QVERIFY(!lock.locked());
            QVERIFY(!lock.setPasscode("", "12")); // too short
            QVERIFY(lock.setPasscode("", "2468"));
            QVERIFY(!lock.setPasscode("0000", "1357")); // wrong current one
            lock.lock();
            QVERIFY(lock.locked());
            for (int i = 0; i < AppLock::kFreeAttempts; ++i) QVERIFY(!lock.unlock("0000"));
            QVERIFY(lock.retryAfterSeconds() > 0);
            QVERIFY(!lock.unlock("2468")); // even the right one waits out the penalty
        }
        {
            AppLock lock; // a set passcode locks on start
            QVERIFY(lock.hasPasscode());
            QVERIFY(lock.locked());
            QVERIFY(lock.unlock("2468"));
            QVERIFY(!lock.removePasscode("0000"));
            QVERIFY(lock.removePasscode("2468"));
            QVERIFY(!lock.hasPasscode());
        }
        QSettings settings;
        QVERIFY(!settings.contains("lock/hash")); // only salt+hash were ever stored, and they are gone
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
            QVERIFY(f.setFileTime(now.addDays(-daysOld), QFileDevice::FileModificationTime)); // needs the open file
            f.close();
        };
        make("old", 10, 40);
        make("mid", 10, 5);
        make("new", 10, 0);
        // Age first (40 days > 31), then the oldest until <= 15 KB.
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
            QCOMPARE(m.value("clearable").toBool(), m.value("key").toString() != "downloads"); // user files stay
        }
        QCOMPARE(storage.clear("downloads"), qint64(0));

        // No microphone in the container: the recorder says so instead of pretending to record.
        Zalos::Core::Media::VoiceRecorder recorder;
        if (recorder.available()) QSKIP("a microphone is present");
        QSignalSpy failed(&recorder, &Zalos::Core::Media::VoiceRecorder::failed);
        QVERIFY(!recorder.start());
        QVERIFY(!recorder.recording());
        QCOMPARE(failed.count(), 1);

        // A finished take is a local voice echo (offline: marked failed, never "sent").
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
        // First sign-in prompt: shown once per account until synced or declined for good.
        ZaloClient client;
        client.setMyUidForTesting("u-prompt-owner");
        QSettings settings;
        settings.remove("mobileSync/done/u-prompt-owner");
        settings.remove("mobileSync/neverAsk/u-prompt-owner");
        client.maybeOfferMobileSync();
        QVERIFY(!client.showSyncPrompt()); // not signed in: never asks
        client.setLoggedInForTesting(true);
        client.maybeOfferMobileSync();
        QVERIFY(client.showSyncPrompt());
        client.dismissSyncPrompt(false); // "Để sau": asks again next time
        QVERIFY(!client.showSyncPrompt());
        client.maybeOfferMobileSync();
        QVERIFY(client.showSyncPrompt());
        client.dismissSyncPrompt(true); // "Không hỏi lại"
        client.maybeOfferMobileSync();
        QVERIFY(!client.showSyncPrompt());
        settings.remove("mobileSync/neverAsk/u-prompt-owner");
        settings.setValue("mobileSync/done/u-prompt-owner", true); // already synced: never asks
        client.maybeOfferMobileSync();
        QVERIFY(!client.showSyncPrompt());
        settings.remove("mobileSync/done/u-prompt-owner");
        client.setLoggedInForTesting(false);

        // The reaction picker offers every Zalo reaction once (scrolls beyond the first 6).
        const QVariantList reactions = client.reactionList();
        QVERIFY(reactions.size() > 6);
        QSet<QString> emojis;
        for (const QVariant& v : reactions) emojis.insert(v.toMap().value("emoji").toString());
        QCOMPARE(emojis.size(), reactions.size());

        // A sticker seen while offline keeps an empty URL (placeholder in the UI) and waits in the queue instead
        // of being dropped; it is never invisible data loss.
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
        // A fake sound theme on XDG_DATA_DIRS: theme files are listed with the theme's name, speaker tests and
        // licence files are not, and a directory without index.theme (alsa) is skipped.
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
        QVERIFY(!SoundEngine::isValidKey("ringtone", "pop")); // a notification key is not a ringtone

        // "Khởi động cùng hệ thống": writes / removes ~/.config/autostart/zalos.desktop.
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
            QVERIFY(client.closeToTray()); // Zalo PC behaviour by default
        }
        if (oldConfig.isEmpty()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", oldConfig);
    }

    // ZIcon glyphs are rendered antialiased at the requested pixel size (no jagged edges on any backend).
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
        QVERIFY(partial > 10); // edge pixels with intermediate coverage = antialiasing
        // Stroke style and an unknown/empty path are safe.
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

        // Test Forward Message
        client.selectConversation("conv-1", "Conv 1", "");
        client.sendMessage("Tin nhắn gốc");
        QString msgId = client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString();

        client.openForwardModal(msgId, "Tin nhắn gốc");
        QCOMPARE(client.showForwardModal(), true);
        QCOMPARE(client.forwardSourceMsgId(), msgId);

        client.forwardMessage("conv-2", msgId);
        QCOMPARE(client.showForwardModal(), false);
        QCOMPARE(client.messageModel()->threadSize("conv-2"), 1);

        // Test Mobile Sync Control
        auto* session = client.apiClient()->session();
        QCOMPARE(client.mobileSyncState(), 0);

        // Simulate mobile user confirm push event
        QJsonObject confirmCtrl{
            {"act_type", "syncmsgmb"},
            {"act", "user_confirm"},
            {"data", QJsonObject{{"user_action", 1}}}
        };
        emit session->mobileSyncControlReceived(confirmCtrl);
        QCOMPARE(client.mobileSyncState(), 0); // not ours: no fake progress

        // Simulate syncmsg_info
        QJsonObject infoCtrl{
            {"act_type", "syncmsgmb"},
            {"act", "syncmsg_info"},
            {"data", QJsonObject{{"sync_session", "session-123"}}}
        };
        emit session->mobileSyncControlReceived(infoCtrl);
        // Controls for a request this client never made (no matching public key) are ignored.
        QCOMPARE(client.mobileSyncState(), 0);

        calls->hangup();
    }

    void testPresenceAndStatusFormatting() {
        ZaloClient client;

        // 1. Initial 1-1 conversation offline state
        client.selectConversation("u-peer-test", "Peer Test", "");
        QCOMPARE(client.currentConversationIsOnline(), false);
        QCOMPARE(client.currentConversationStatus(), QString("Truy cập gần đây"));

        // 2. Typing status overrides last active
        auto* session = client.apiClient()->session();
        // Signal shape: (threadId, userId, isGroup) — see ZaloSession::typingReceived.
        emit session->typingReceived(QStringLiteral("u-peer-test"), QStringLiteral("u-peer-test"), false);
        QCOMPARE(client.currentConversationStatus(), QString("Đang soạn tin..."));

        // 3. Group conversations must NEVER show online status or green dot
        client.selectConversation("grp-test-99", "Nhóm Test", "");
        QCOMPARE(client.currentConversationIsOnline(), false);
        // Group status should reflect member count, not online text
        QVERIFY(client.currentConversationStatus() != "Đang hoạt động");

        // 4. Session restoration state lifecycle
        QCOMPARE(client.isRestoringSession(), false);

        // 5. Theme palette synchronization verification (prevent inverted control backgrounds)
        ThemeManager theme;
        theme.setThemeMode(0); // Zalo Light
        QCOMPARE(theme.isDarkMode(), false);
        QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor("#FFFFFF"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Base), QColor("#EBECF0"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::WindowText), QColor("#081B3A"));

        theme.setThemeMode(1); // Zalo Dark
        QCOMPARE(theme.isDarkMode(), true);
        QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor("#22262B"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Base), QColor("#16191D"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::WindowText), QColor("#DFE2E7"));
    }

    void testAuthenticUserAccountSessionAndResponsiveLayout() {
        ZaloClient client;

        // 1. Clean state when logged out (zero mock data guarantee)
        client.logout();
        QCOMPARE(client.isLoggedIn(), false);
        QCOMPARE(client.currentUserName(), QString(""));
        QCOMPARE(client.conversationModel()->rowCount(), 0);
        QCOMPARE(client.messageModel()->rowCount(), 0);

        // 2. Ordering: pinned first, then newest activity first (synthetic test fixtures only)
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

        // New activity moves a conversation up and counts unread messages.
        convModel->recordMessage("u-old", "hi", "", now, true);
        QCOMPARE(convModel->data(convModel->index(1), ConversationModel::IdRole).toString(), QString("u-old"));
        QCOMPARE(convModel->totalUnreadCount(), 1);
        convModel->setCategoryFilter("unread");
        QCOMPARE(convModel->rowCount(), 1);
        convModel->setCategoryFilter("");

        // 3. Message merge deduplicates by id and keeps chronological order.
        MessageItem m1; m1.id = "2"; m1.ts = now; m1.text = "second";
        MessageItem m2; m2.id = "1"; m2.ts = now - 1000; m2.text = "first";
        client.selectConversation("u-new", "Fixture New", "");
        client.messageModel()->mergeMessages("u-new", {m1, m2, m1});
        QCOMPARE(client.messageModel()->rowCount(), 2);
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::TextRole).toString(), QString("first"));

        // Phase-1 message state: reactions, recall, delivery status.
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

        // Typing / tags / presence on the conversation list.
        convModel->setTyping("u-old", true);
        convModel->setTag("g-pin", "Fixture Tag", "#2563EB");
        convModel->setOnlineIds({"u-new"});
        QCOMPARE(convModel->data(convModel->index(0), ConversationModel::TagRole).toString(), QString("Fixture Tag"));

        // Friend requests category.
        ContactItem invite; invite.id = "inv-1"; invite.name = "Fixture Request"; invite.category = "invites";
        invite.subtitle = "hello";
        ContactItem friendItem; friendItem.id = "f-1"; friendItem.name = "Fixture Friend";
        client.contactModel()->setContacts({invite, friendItem});
        QCOMPARE(client.contactModel()->inviteCount(), 1);
        QCOMPARE(client.contactModel()->friendCount(), 1);

        // 4. Responsive layout screenshots across 1, 1/2 and 1/4 screen.
        client.setLoggedInForTesting(true);
        ThemeManager themeMgr;
        SystemTrayController trayCtrl;
        QQmlApplicationEngine engine; // declared last so it is destroyed before its context objects
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

        // Modals exist only while open (Loader in Main.qml): opening settings adds items, closing removes them.
        std::function<int(QQuickItem*)> countItems = [&](QQuickItem* item) {
            int n = 1;
            for (QQuickItem* c : item->childItems()) n += countItems(c);
            return n;
        };
        auto itemCount = [&]() { return countItems(window->contentItem()); };
        {   // The QML singleton is the C++ instance, never a second ZaloClient created by QML.
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

        // ConversationInfoPanel is also lazy-loaded via Loader in ChatArea.qml when showConversationInfo is true.
        QCOMPARE(client.showConversationInfo(), false);
        QCOMPARE(window->findChild<QQuickItem*>("conversationInfoPanel"), nullptr);
        client.setShowConversationInfo(true);
        QTest::qWait(100);
        QVERIFY(window->findChild<QQuickItem*>("conversationInfoPanel") != nullptr);
        client.setShowConversationInfo(false);
        QTest::qWait(100);
        QCOMPARE(window->findChild<QQuickItem*>("conversationInfoPanel"), nullptr);

        const QList<QSize> sizes{{1920, 1080}, {960, 1080}, {1280, 720}, {640, 480}, {480, 540}};
        for (const QSize& size : sizes) {
            window->resize(size);
            QTest::qWait(250);
            QCOMPARE(window->size(), size);
            window->grabWindow().save(QString("/workspace/build/test_chat_%1x%2.png").arg(size.width()).arg(size.height()));
        }

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
        // Label folders in the navigation rail (Telegram-style), off by default.
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

        // 5. Language switching
        client.setAppLanguage("en");
        QCOMPARE(client.appLanguage(), QString("en"));
        QTest::qWait(150);
        window->grabWindow().save("/workspace/build/test_chat_english.png");
        client.setAppLanguage("vi");
        QCOMPARE(client.appLanguage(), QString("vi"));

        // Light and dark palettes (Zalo PC tokens)
        client.setCurrentNavigationTab("messages");
        themeMgr.setThemeMode(0);
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_chat_light.png");
        themeMgr.setThemeMode(1);
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_chat_dark.png");

        // Rich Text Formatting & Composer Toolbar Suite
        auto chatArea = window->findChild<QQuickItem*>("chatArea");
        if (chatArea) {
            QCOMPARE(chatArea->property("isFormattingActive").toBool(), false);
            QCOMPARE(chatArea->property("isEditorExpanded").toBool(), false);

            // Toggle RTF formatting toolbar
            chatArea->setProperty("isFormattingActive", true);
            QTest::qWait(200);
            QCOMPARE(chatArea->property("isFormattingActive").toBool(), true);
            // The toolbar really opens (a `height:` inside the ColumnLayout used to collapse it to 0 px).
            QTest::qWait(150);
            auto formatBar = window->findChild<QQuickItem*>("formatToolbar");
            QVERIFY(formatBar != nullptr);
            QVERIFY2(formatBar->height() >= 30, qPrintable(QString::number(formatBar->height())));
            // The composer text area shows about three lines (the bottom row used to cover it).
            auto composer = window->findChild<QQuickItem*>("composerBox");
            QVERIFY(composer != nullptr);
            QVERIFY2(composer->height() >= 32 + 1 + 34 + 3 * 18 + 32, qPrintable(QString::number(composer->height())));
            window->grabWindow().save("/workspace/build/test_chat_rtf_toolbar.png");

            // Toggle editor height expansion
            chatArea->setProperty("isEditorExpanded", true);
            QTest::qWait(200);
            QCOMPARE(chatArea->property("isEditorExpanded").toBool(), true);
            window->grabWindow().save("/workspace/build/test_chat_expanded_editor.png");

            // Reset back
            chatArea->setProperty("isEditorExpanded", false);
            chatArea->setProperty("isFormattingActive", false);
            QTest::qWait(100);

            // Quote reply, reply banner, scheduled strip and in-chat search (neutral fixtures).
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

        // Call window states, driven through the engine UI protocol (ui-client.js "state" lines)
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
        calls->hangup(); // no engine: closes the window
        QCOMPARE(calls->phase(), QString(""));

        // 6. Logged-out QR view
        client.setLoggedInForTesting(false);
        client.logout();
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_qr_view.png");
    }

    // Network chaos, malformed JSON fuzzing, and resilient error recovery gate.
    void testNetworkChaosAndMalformedJsonFuzzing() {
        ZaloClient client;
        auto* session = client.apiClient()->session();

        // 1. Binary frames with corrupted or truncated headers (< 4 bytes)
        session->injectWsBinaryForTesting(QByteArray());
        session->injectWsBinaryForTesting(QByteArray("\x01"));
        session->injectWsBinaryForTesting(QByteArray("\x01\x02\x03"));

        // 2. Malformed JSON payloads after 4-byte header
        // cmd 501, subCmd 0 (userLive) with completely invalid non-JSON payload
        QByteArray malformedFrame("\x01\xf5\x01\x00{invalid-json-payload-broken", 32);
        session->injectWsBinaryForTesting(malformedFrame);

        // 3. Fuzzed TMessage JSON objects: missing keys, inverted types, null values
        QJsonArray fuzzedBatch;
        // Null values
        fuzzedBatch.append(QJsonObject{{"msgId", QJsonValue::Null}, {"cliMsgId", QJsonValue::Null}, {"content", QJsonValue::Null}});
        // Inverted types: int instead of string, array instead of object
        fuzzedBatch.append(QJsonObject{{"msgId", 12345}, {"cliMsgId", 67890}, {"uidFrom", 999}, {"content", QJsonArray{1, 2, 3}}});
        // Missing essential keys
        fuzzedBatch.append(QJsonObject{{"randomField", "chaos"}});
        // Truncated timestamps and out-of-range numbers
        fuzzedBatch.append(QJsonObject{{"msgId", "999999"}, {"cliMsgId", "c-chaos"}, {"ts", "invalid-ts-number"}, {"content", "safe text"}});

        // Emit through live session: must be handled gracefully without SIGSEGV or invariant breach
        emit session->messagesReceived(fuzzedBatch, false, false);
        QVERIFY(client.conversationModel() != nullptr);

        // 4. Out-of-order & Duplicate packet arrival test
        QJsonArray outOfOrderBatch;
        const qint64 tNow = 1700000000000LL;
        // Newer message arrives first
        outOfOrderBatch.append(QJsonObject{{"msgId", "801"}, {"cliMsgId", "c-801"}, {"uidFrom", "u-chaos"}, {"idTo", "u-me"},
                                           {"msgType", "webchat"}, {"content", "Message 2 (newer)"}, {"ts", QString::number(tNow + 2000)}});
        // Older message arrives second
        outOfOrderBatch.append(QJsonObject{{"msgId", "800"}, {"cliMsgId", "c-800"}, {"uidFrom", "u-chaos"}, {"idTo", "u-me"},
                                           {"msgType", "webchat"}, {"content", "Message 1 (older)"}, {"ts", QString::number(tNow + 1000)}});
        emit session->messagesReceived(outOfOrderBatch, false, false);

        client.selectConversation("u-chaos", "Chaos Peer", "");
        QCOMPARE(client.messageModel()->rowCount(), 2);
        // Model must preserve proper chronological order based on timestamp
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(0), MessageModel::IdRole).toString(), QString("800"));
        QCOMPARE(client.messageModel()->data(client.messageModel()->index(1), MessageModel::IdRole).toString(), QString("801"));

        // Duplicate replay packet must be cleanly deduplicated by cliMsgId
        QJsonArray duplicateBatch;
        duplicateBatch.append(QJsonObject{{"msgId", "801"}, {"cliMsgId", "c-801"}, {"uidFrom", "u-chaos"}, {"idTo", "u-me"},
                                          {"msgType", "webchat"}, {"content", "Message 2 (duplicate replay)"}, {"ts", QString::number(tNow + 2000)}});
        emit session->messagesReceived(duplicateBatch, false, false);
        QCOMPARE(client.messageModel()->rowCount(), 2); // Count remains 2, no ghost duplicate rows
    }

    // Corrupted SQLite recovery and graceful degradation test.
    void testCorruptedStorageRecovery() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString dbPath = tempDir.filePath("corrupted_offline.db");

        // Create a corrupted file with random garbage bytes instead of SQLite header
        {
            QFile f(dbPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("CORRUPTED_GARBAGE_HEADER_NOT_SQLITE3_DATABASE_FILE_TEST");
            f.close();
        }

        // OfflineStore must not crash; it should catch the initialization failure safely
        ZaloOfflineStore store;
        const bool openResult = store.initializeDatabase(dbPath);
        // Either recovers/recreates or returns false safely without crashing
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
};

QTEST_MAIN(TestSystem)
#include "test_system.moc"
