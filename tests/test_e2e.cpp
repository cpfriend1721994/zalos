#include <QtTest>
#include <QGuiApplication>
#include <QFont>
#include <QSignalSpy>

#include "core/network/zalo_client.h"
#include "core/platform/theme_manager.h"
#include "core/platform/system_tray.h"
#include "core/crypto/db_cross.h"
#include "core/models/conversation_model.h"
#include "core/models/message_model.h"

using namespace Zalos::Core::Network;
using namespace Zalos::Core::Platform;
using namespace Zalos::Core::Models;

class TestE2E : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("Zalos");
        QCoreApplication::setApplicationName("ZalosApp");
        QSettings settings("Zalos", "ZalosApp");
        settings.setValue("auth/isLoggedIn", false);
        settings.setValue("auth/userName", "");
        settings.setValue("auth/userPhone", "");
    }

    void cleanupTestCase() {
        QSettings settings("Zalos", "ZalosApp");
        settings.setValue("auth/isLoggedIn", false);
        settings.setValue("auth/userName", "");
        settings.setValue("auth/userPhone", "");
    }

    // 1. System Theme & Colors
    void testThemeManagerModesAndColors() {
        ThemeManager theme;

        // Test Light Mode
        theme.setThemeMode(0);
        QCOMPARE(theme.themeMode(), 0);
        QCOMPARE(theme.isDarkMode(), false);
        QCOMPARE(theme.railBackgroundColor(), QColor("#005AE0")); // Zalo PC --layer-background-navbar-normal (light)
        QCOMPARE(theme.backgroundColor(), QColor("#FFFFFF"));

        // Test Dark Mode
        theme.setThemeMode(1);
        QCOMPARE(theme.themeMode(), 1);
        QCOMPARE(theme.isDarkMode(), true);
        QCOMPARE(theme.railBackgroundColor(), QColor("#121416")); // Zalo PC navbar (dark)
        QCOMPARE(theme.backgroundColor(), QColor("#22262B")); // --layer-background (dark)
        QCOMPARE(theme.chatBubbleMine(), QColor("#1F344D")); // --blue-message (dark)

        // Test Toggle Theme
        theme.toggleTheme();
        QCOMPARE(theme.isDarkMode(), false);
        theme.toggleTheme();
        QCOMPARE(theme.isDarkMode(), true);

        // Test System Theme Mode (Inherits from desktop environment)
        theme.setThemeMode(2);
        QCOMPARE(theme.themeMode(), 2);
        QVERIFY(theme.accentColor().isValid());
        QVERIFY(theme.backgroundColor().isValid());
        QVERIFY(theme.chatBackgroundColor().isValid());
        QVERIFY(theme.textColor().isValid());
        QVERIFY(theme.railBackgroundColor().isValid());
    }

    // 2. System Font Detection & Inheritance
    void testSystemFontInheritance() {
        ThemeManager theme;
        QSignalSpy spy(&theme, &ThemeManager::systemFontFamilyChanged);

        // System font should be non-empty
        QString initialFont = theme.systemFontFamily();
        QVERIFY(!initialFont.isEmpty());

        // Test setting custom system font
        theme.setSystemFontFamily("Cantarell");
        QCOMPARE(theme.systemFontFamily(), QString("Cantarell"));
        QCOMPARE(QGuiApplication::font().family(), QString("Cantarell"));
        QCOMPARE(spy.count(), 1);

        theme.setSystemFontFamily("Inter");
        QCOMPARE(theme.systemFontFamily(), QString("Inter"));
        QCOMPARE(QGuiApplication::font().family(), QString("Inter"));
        QCOMPARE(spy.count(), 2);
    }

    // 3. Authentication & Session Lifecycle
    void testAuthLifecycle() {
        ZaloClient client;
        QCOMPARE(client.isLoggedIn(), false);

        // Password sign-in is not offered by the Zalo Web protocol: it must fail honestly.
        client.loginWithPhone("0988123456", "SecurePass123", true);
        QCOMPARE(client.isLoggedIn(), false);
        QVERIFY(client.hasLoginError());
        QVERIFY(!client.loginStatusMessage().isEmpty());

        client.clearLoginError();
        QCOMPARE(client.hasLoginError(), false);

        // Logout always returns to a clean state.
        QSignalSpy logoutSpy(&client, &ZaloClient::loginStateChanged);
        client.logout();
        QCOMPARE(client.isLoggedIn(), false);
        QCOMPARE(client.currentConversationId(), QString(""));
        QCOMPARE(client.conversationModel()->rowCount(), 0);
        QCOMPARE(logoutSpy.count(), 1);
    }

    // 4. Conversation Navigation & Filtering
    void testConversationNavigation() {
        ZaloClient client;
        auto convModel = client.conversationModel();
        QVERIFY(convModel != nullptr);

        convModel->setCategoryFilter("");
        convModel->addConversation({
            "test-1", "Hỗ trợ khách hàng", "Xin chào", "10:00", 0, true, false, "", "priority", ""
        });
        convModel->addConversation({
            "test-2", "Thông báo tiện ích", "Tin mới", "09:00", 0, true, false, "", "other", ""
        });
        QCOMPARE(convModel->rowCount(), 2);

        // Priority filter
        convModel->setCategoryFilter("priority");
        QCOMPARE(convModel->activeCategory(), QString("priority"));
        int priorityCount = convModel->rowCount();
        QCOMPARE(priorityCount, 1);

        // Other filter
        convModel->setCategoryFilter("other");
        QCOMPARE(convModel->activeCategory(), QString("other"));
        int otherCount = convModel->rowCount();
        QCOMPARE(otherCount, 1);

        // Reset filter
        convModel->setCategoryFilter("");
        QCOMPARE(convModel->activeCategory(), QString(""));
        QCOMPARE(convModel->rowCount(), priorityCount + otherCount);

        // Search filter
        convModel->setSearchFilter("Hỗ trợ");
        QCOMPARE(convModel->rowCount(), 1);
        convModel->setSearchFilter("");
    }

    // 5. Chat Interaction & Message Sending
    void testChatAndMessageSending() {
        ZaloClient client;
        auto msgModel = client.messageModel();
        QVERIFY(msgModel != nullptr);

        // Select conversation
        client.selectConversation("cloud-me", "My Documents", "qrc:/Zalos/assets/icons/zalos.svg");
        QCOMPARE(client.currentConversationId(), QString("cloud-me"));
        QCOMPARE(client.currentConversationName(), QString("My Documents"));

        int initialMsgCount = msgModel->rowCount();
        QVERIFY(initialMsgCount >= 0);

        // Send a text message
        QSignalSpy sendSpy(&client, &ZaloClient::messageSent);
        client.sendMessage("Ghi chú tài liệu cá nhân trên Cloud");
        QCOMPARE(sendSpy.count(), 1);
        QCOMPARE(msgModel->rowCount(), initialMsgCount + 1);

        // Verify sent message in model
        QModelIndex lastIdx = msgModel->index(initialMsgCount);
        QCOMPARE(msgModel->data(lastIdx, MessageModel::TextRole).toString(),
                 QString("Ghi chú tài liệu cá nhân trên Cloud"));
        QCOMPARE(msgModel->data(lastIdx, MessageModel::IsMineRole).toBool(), true);

        // Attachments are not supported yet: the client says so instead of faking a message.
        QSignalSpy noticeSpy(&client, &ZaloClient::notice);
        client.sendAttachment("image", "photo.png", "");
        QCOMPARE(noticeSpy.count(), 1);
        QCOMPARE(msgModel->rowCount(), initialMsgCount + 1);

        // Return to welcome screen
        client.openWelcomeScreen();
        QCOMPARE(client.currentConversationId(), QString(""));
    }

    // 6. ZaPrivacy Settings Persistence
    void testZaPrivacyFeatures() {
        ZaloClient client;

        client.setBlockTyping(true);
        QCOMPARE(client.blockTyping(), true);
        client.setBlockTyping(false);
        QCOMPARE(client.blockTyping(), false);

        client.setBlockDelivered(true);
        QCOMPARE(client.blockDelivered(), true);
        client.setBlockDelivered(false);
        QCOMPARE(client.blockDelivered(), false);

        client.setBlockSeen(true);
        QCOMPARE(client.blockSeen(), true);
        client.setBlockSeen(false);
        QCOMPARE(client.blockSeen(), false);

    }

    // 7. ZaSounds & Preview Playback
    void testZaSoundsFeatures() {
        ZaloClient client;

        client.setSelectedRingtone("ring1");
        QCOMPARE(client.selectedRingtone(), QString("ring1"));

        client.setSelectedNotiSound("ding");
        QCOMPARE(client.selectedNotiSound(), QString("ding"));

        // Sound preview simulation
        client.playSoundPreview("ringtone", "ring1");
        QCOMPARE(client.isPreviewPlaying(), true);
        QCOMPARE(client.currentPreviewSound(), QString("ring1"));

        client.stopSoundPreview();
        QCOMPARE(client.isPreviewPlaying(), false);
    }

    // 8. Modals & Incoming Call Flows
    void testModalsAndIncomingCalls() {
        ZaloClient client;

        // Settings modal
        client.openSettings("appearance");
        QCOMPARE(client.showSettingsModal(), true);
        QCOMPARE(client.settingsInitialTab(), QString("appearance"));
        client.closeSettings();
        QCOMPARE(client.showSettingsModal(), false);

        // User profile modal
        client.openUserProfile();
        QCOMPARE(client.showUserProfileModal(), true);
        client.closeUserProfile();
        QCOMPARE(client.showUserProfileModal(), false);

        // Incoming call simulation
        client.triggerSimulatedIncomingCall("Người gọi Zalo");
        QCOMPARE(client.isIncomingCallActive(), true);
        QCOMPARE(client.incomingCallerName(), QString("Người gọi Zalo"));
        client.declineCall();
        QCOMPARE(client.isIncomingCallActive(), false);
    }

    // 9. Crypto E2EE Verification
    void testE2EEDecryptionVerification() {
        ZaloClient client;
        bool verified = client.testE2EEDecryption();
        QVERIFY(verified);
    }

    // 10. System Tray Synchronization
    void testSystemTraySync() {
        SystemTrayController tray;
        tray.setUnreadCount(5);
        tray.setUnreadCount(99);
        tray.setUnreadCount(100);
        tray.setUnreadCount(150, true);
        tray.setUnreadCount(0);
    }
};

QTEST_MAIN(TestE2E)
#include "test_e2e.moc"
