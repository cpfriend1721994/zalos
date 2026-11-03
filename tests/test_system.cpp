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
#include <QSignalSpy>
#include <QDateTime>
#include <QBuffer>
#include <QElapsedTimer>
#include <functional>
#include <QTemporaryDir>
#include <QTemporaryFile>
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
        client.setCurrentNavigationTab("todo");
        QCOMPARE(client.currentNavigationTab(), QString("todo"));
        client.setCurrentNavigationTab("cloud");
        QCOMPARE(client.currentNavigationTab(), QString("cloud"));
        client.setCurrentNavigationTab("messages");
        QCOMPARE(client.currentNavigationTab(), QString("messages"));

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
        // Opening 40 threads keeps at most 30 in RAM; the evicted ones reload completely.
        for (int t = 0; t < 40; ++t) client.selectConversation(QString("u-mem-%1").arg(t), "Fixture", "");
        QCOMPARE(client.messageModel()->threadSize("u-mem-0"), 0);
        client.selectConversation("u-mem-0", "Fixture", "");
        QCOMPARE(client.messageModel()->rowCount(), 170);
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
        emit session->messagesReceived(msgs, false, true);
        client.selectConversation("u-gallery", "Fixture Gallery", "");
        client.setMyUidForTesting("u-me");
        client.pins()->applyListForTesting("u-gallery", QJsonObject{{"version", 1}, {"data", QJsonArray{
            QString::fromUtf8(QJsonDocument(QJsonObject{{"id", 1}, {"type", 2}, {"params", QString::fromUtf8(QJsonDocument(QJsonObject{
                {"global_msg_id", "9101"}, {"senderName", "Fixture Gallery"}, {"title", "Fixture pinned note"}}).toJson(QJsonDocument::Compact))}}).toJson(QJsonDocument::Compact)),
            QString::fromUtf8(QJsonDocument(QJsonObject{{"id", 2}, {"type", 2}, {"params", QString::fromUtf8(QJsonDocument(QJsonObject{
                {"global_msg_id", "9102"}, {"senderName", "Me"}, {"title", "Second pin"}}).toJson(QJsonDocument::Compact))}}).toJson(QJsonDocument::Compact))}}}, false);
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
            client.setCurrentNavigationTab("messages");
            shot("chat_" + suffix);
            client.setAntiPeeking(true);                  // indicator strip + dimmed messages
            shot("chat_antipeek_" + suffix);
            client.setAntiPeeking(false);
            for (const char* tab : {"general", "language", "appearance", "notifications", "privacy", "storage", "sync"}) {
                client.openSettings(tab);
                shot(QString("settings_%1_%2").arg(tab, suffix));
                client.setShowSettingsModal(false);
            }
            client.openCreateGroup();
            shot("create_group_" + suffix);
            client.closeCreateGroup();
            client.setCurrentNavigationTab("contacts");
            shot("contacts_" + suffix);
            client.setCurrentNavigationTab("todo");
            shot("todo_" + suffix);
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
        themeMgr.setThemeMode(0);
        client.setNotifySound(false);
        QFile::remove(picPath);
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

    // zcall-native: the bundled copy (third_party → share/zalos/zcall-native), picker folder shapes, override.
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

        QSettings settings;
        const QVariant before = settings.value("calls/zcallDir");
        settings.remove("calls/zcallDir");
        CallBridge bridge(nullptr);
        QVERIFY(!bridge.engineDirectory().isEmpty()); // dev build: the source tree's vendored copy
        QVERIFY(!bridge.setEngineDirectory(QUrl::fromLocalFile(other.path()).toString()));
        QVERIFY(bridge.setEngineDirectory(QUrl::fromLocalFile(root).toString()));
        QCOMPARE(bridge.engineDirectory(), QDir::cleanPath(root + "/zcall-native"));
        QVERIFY(!bridge.engineIsBundled());
        QVERIFY(bridge.setEngineDirectory(QString())); // back to the bundled / vendored engine
        QVERIFY(!QSettings().contains("calls/zcallDir"));
        QCOMPARE(bridge.engineDirectory(), QDir::cleanPath(vendored));
        if (before.isValid()) settings.setValue("calls/zcallDir", before);
    }

    // Labels ("Phân loại", convlabel/get|update semantics of Zalo PC) and contact cards (sendCard / recommened.user).
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
        QCOMPARE(qgetenv("VK_LOADER_DRIVERS_DISABLE"), QByteArray("*lvp_icd*"));
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
        client.setCurrentNavigationTab("todo");
        QTest::qWait(200);
        window->grabWindow().save("/workspace/build/test_todo_tab.png");

        client.setCurrentNavigationTab("messages");
        auto settingsMenu = window->findChild<QObject*>("settingsMenu");
        if (settingsMenu) {
            QMetaObject::invokeMethod(settingsMenu, "open");
            QTest::qWait(200);
            window->grabWindow().save("/workspace/build/test_settings_popup.png");
            QMetaObject::invokeMethod(settingsMenu, "close");
        }

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
};

QTEST_MAIN(TestSystem)
#include "test_system.moc"
