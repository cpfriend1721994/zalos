import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

ApplicationWindow {
    id: mainWindow
    visible: !ZaloClient.launchHidden // autostart minimized to the tray (main.cpp)
    width: 1040
    height: 680
    minimumWidth: 380
    minimumHeight: 420
    title: "Zalo"
    color: ThemeManager.backgroundColor

    // Responsive State Standard across 1 màn, 1/2 màn, and 1/4 màn
    readonly property bool isCompactMode: width < 680

    // Native Window Framing: Use standard system window decorations (decorations, titlebar, borders)
    flags: Qt.Window

    Component.onCompleted: I18n.setLanguage(ZaloClient.appLanguage)

    // Keep the backend (persisted) language and the QML singleton in sync both ways.
    Connections {
        target: ZaloClient
        function onAppLanguageChanged() {
            if (I18n.currentLanguage !== ZaloClient.appLanguage) I18n.setLanguage(ZaloClient.appLanguage);
        }
    }
    Connections {
        target: I18n
        function onLanguageChanged(lang) {
            if (ZaloClient.appLanguage !== lang) ZaloClient.setAppLanguage(lang);
        }
    }

    function toggleVisibility() {
        if (mainWindow.visible) {
            mainWindow.hide();
        } else {
            mainWindow.show();
            mainWindow.raise();
            mainWindow.requestActivate();
        }
    }

    // Closing the window keeps Zalos running in the tray (Zalo PC behaviour) unless turned off in Settings.
    onClosing: (close) => {
        if (ZaloClient.closeToTray && ZaloClient.trayAvailable) {
            close.accepted = false;
            mainWindow.hide();
        }
    }

    // Second launch (SingleInstance) or a click on a desktop notification.
    function showAndActivate(): void {
        if (mainWindow.visibility === Window.Minimized) mainWindow.showNormal();
        else mainWindow.show();
        mainWindow.raise();
        mainWindow.requestActivate();
    }

    function toggleTheme() {
        ThemeManager.toggleTheme();
    }

    // View 1: Startup Session Restoration Splash (seamless startup without QR screen flash)
    Rectangle {
        id: startupSplash
        anchors.fill: parent
        color: ThemeManager.backgroundColor
        visible: ZaloClient.isRestoringSession && !ZaloClient.isLoggedIn && !ZaloClient.isOfflineMode
        z: 999

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 16

            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/Zalos/assets/icons/zalos.svg"
                // Rasterised at the screen's pixel density (crisp on HiDPI).
                sourceSize.width: Math.ceil(80 * Screen.devicePixelRatio)
                sourceSize.height: Math.ceil(80 * Screen.devicePixelRatio)
                width: 80
                height: 80
                smooth: true
                mipmap: true

                SequentialAnimation on scale {
                    running: startupSplash.visible
                    loops: Animation.Infinite
                    PropertyAnimation { to: 1.05; duration: 800; easing.type: Easing.InOutQuad }
                    PropertyAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Zalo"
                font.family: Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
                color: Theme.zaloBlue
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8

                Row {
                    spacing: 4
                    Repeater {
                        model: 3
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: Theme.zaloBlue
                            SequentialAnimation on y {
                                running: startupSplash.visible
                                loops: Animation.Infinite
                                PauseAnimation { duration: index * 140 }
                                PropertyAnimation { to: -5; duration: 280; easing.type: Easing.OutQuad }
                                PropertyAnimation { to: 0; duration: 280; easing.type: Easing.InQuad }
                                PauseAnimation { duration: (2 - index) * 140 }
                            }
                        }
                    }
                }

                Text {
                    text: ZaloClient.loginStatusMessage.length > 0 ? ZaloClient.loginStatusMessage
                                                                 : I18n.t("Đang kết nối tới máy chủ...", "Connecting to server...")
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    color: ThemeManager.secondaryTextColor
                }
            }

            // Offline at startup keeps the saved session and retries; this is the way out.
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: ZaloClient.hasLoginError
                text: I18n.t("Đăng nhập bằng tài khoản khác", "Sign in with another account")
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.underline: switchAccountMouse.containsMouse
                color: Theme.zaloBlue
                MouseArea {
                    id: switchAccountMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ZaloClient.logout()
                }
            }
        }
    }

    // View 2: Login View (shown ONLY when no session exists or session expired)
    // Views and modals below are created only while shown (Loader) and destroyed afterwards, so a
    // session never pays for screens it does not open (QR login, settings, profile, calls, ...).
    Loader {
        active: !ZaloClient.isLoggedIn && !ZaloClient.isRestoringSession && !ZaloClient.isOfflineMode
        anchors.fill: parent
        sourceComponent: LoginView {}
    }

    // View 3: Authentic Zalo Desktop Layout (signed in, or offline showing the account's cached history)
    RowLayout {
        visible: ZaloClient.isLoggedIn || ZaloClient.isOfflineMode
        anchors.fill: parent
        spacing: 0

        // 1. Navigation Rail (Far left, spans 100% full height)
        NavigationRail {
            id: navigationRail
            Layout.preferredWidth: mainWindow.isCompactMode ? 52 : Theme.railWidth
            z: 10 // keep rail popups above the panes
            Layout.minimumWidth: mainWindow.isCompactMode ? 52 : Theme.railWidth
            Layout.maximumWidth: mainWindow.isCompactMode ? 52 : Theme.railWidth
            Layout.fillHeight: true
        }

        // 2. View Mode: Messages (Chat)
        RowLayout {
            visible: ZaloClient.currentNavigationTab === "messages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 2a. Conversation List (Sidebar)
            Sidebar {
                id: sidebar
                visible: !mainWindow.isCompactMode || ZaloClient.currentConversationId === ""
                Layout.preferredWidth: mainWindow.isCompactMode ? -1 : Theme.sidebarWidth
                Layout.fillWidth: mainWindow.isCompactMode
                Layout.minimumWidth: mainWindow.isCompactMode ? 200 : 280
                Layout.maximumWidth: mainWindow.isCompactMode ? -1 : 360
                Layout.fillHeight: true
            }

            // 2b. Welcome Area (when no conversation is active)
            WelcomeArea {
                visible: !mainWindow.isCompactMode && ZaloClient.currentConversationId === ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
            }

            // 2c. Chat Area (when a conversation is open)
            ChatArea {
                visible: ZaloClient.currentConversationId !== ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
            }
        }

        // 3. View Mode: Contacts (Danh bạ)
        Loader {
            active: ZaloClient.currentNavigationTab === "contacts"
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: ContactsView {}
        }

        // 4. View Mode: To-Do
        Loader {
            active: ZaloClient.currentNavigationTab === "todo"
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: ToDoView {}
        }
    }

    // ---- Keyboard shortcuts: one table (ZaloClient.shortcuts, user overrides in shortcuts-custom.json) ----
    readonly property bool shortcutsLive: (ZaloClient.isLoggedIn || ZaloClient.isOfflineMode) && !ZaloClient.appLock.locked

    // Quick switch for anti-peeking (also in the tray icon menu and Settings > Privacy).
    Shortcut {
        sequences: ZaloClient.shortcuts.keys("privacy.antiPeeking")
        enabled: mainWindow.shortcutsLive
        onActivated: ZaloClient.setAntiPeeking(!ZaloClient.antiPeeking)
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keys("chat.next")
        enabled: mainWindow.shortcutsLive
        onActivated: ZaloClient.selectAdjacentConversation(1)
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keys("chat.previous")
        enabled: mainWindow.shortcutsLive
        onActivated: ZaloClient.selectAdjacentConversation(-1)
    }
    Repeater {
        model: 5
        delegate: Item {
            id: pinnedShortcut
            required property int index
            Shortcut {
                sequences: ZaloClient.shortcuts.keys("chat.pinned" + (pinnedShortcut.index + 1))
                enabled: mainWindow.shortcutsLive
                onActivated: ZaloClient.openPinnedConversation(pinnedShortcut.index + 1)
            }
        }
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keys("composer.voice")
        enabled: mainWindow.shortcutsLive && ZaloClient.currentConversationId.length > 0
        onActivated: {
            if (ZaloClient.voiceRecorder.recording) ZaloClient.voiceRecorder.stop();
            else ZaloClient.voiceRecorder.start();
        }
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keys("app.lock")
        enabled: ZaloClient.appLock.hasPasscode && !ZaloClient.appLock.locked
        onActivated: ZaloClient.appLock.lock()
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keys("app.hide")
        onActivated: mainWindow.hide()
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keys("app.quit")
        onActivated: Qt.quit()
    }

    // Global notices raised by the backend (send failures, unsupported actions, ...)
    Connections {
        target: ZaloClient
        function onNotice(message) {
            noticeText.text = message;
            noticeBox.visible = true;
            noticeTimer.restart();
        }
    }

    Rectangle {
        id: noticeBox
        visible: false
        z: 1000
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: Math.min(parent.width - 32, noticeText.implicitWidth + 32)
        height: noticeText.implicitHeight + 20
        radius: 8
        color: ThemeManager.isDarkMode ? "#38404A" : "#E6081B3A" // inverse layer: readable in both themes

        Text {
            id: noticeText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 32)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: 12
            color: "#FFFFFF"
        }
        Timer {
            id: noticeTimer
            interval: 4000
            onTriggered: noticeBox.visible = false
        }
    }

    // Modal Overlays (each one exists only while open)
    component LazyOverlay: Loader {
        anchors.fill: parent
        z: 900
    }
    LazyOverlay { active: ZaloClient.showSettingsModal; sourceComponent: SettingsModal {} }
    LazyOverlay { active: ZaloClient.showUserProfileModal; sourceComponent: UserProfileModal {} }
    LazyOverlay { active: ZaloClient.showAddFriendModal; sourceComponent: AddFriendModal {} }
    LazyOverlay { active: ZaloClient.showCreateGroupModal; sourceComponent: CreateGroupModal {} }
    LazyOverlay { active: ZaloClient.showForwardModal; sourceComponent: ForwardMessageModal {} }
    LazyOverlay { active: ZaloClient.showCreatePollModal; sourceComponent: CreatePollModal {} }
    LazyOverlay { active: ZaloClient.groupAdmin.manageOpen; sourceComponent: GroupManageModal {} }
    LazyOverlay { active: ZaloClient.groupAdmin.joinOpen; sourceComponent: GroupJoinModal {} }
    LazyOverlay { active: ZaloClient.labels.manageOpen; sourceComponent: LabelManageModal {} }

    // Photo viewer (opened from photo messages)
    LazyOverlay {
        z: 1500
        active: ImageViewerState.open
        sourceComponent: ImageViewer {}
    }

    // Voice and Video call window (zcall-native engine via CallBridge)
    LazyOverlay {
        z: 2000 // above notices, like the call window always was
        active: !!(ZaloClient.calls && ZaloClient.calls.phase.length > 0)
        sourceComponent: CallOverlay {}
    }

    // First sign-in without phone history: offer the sync once (ZaloClient.showSyncPrompt).
    LazyOverlay {
        z: 950
        active: ZaloClient.showSyncPrompt && !ZaloClient.appLock.locked
        sourceComponent: SyncPromptDialog {}
    }

    // Passcode lock (ZaloClient.appLock): above everything, calls included.
    LazyOverlay {
        z: 3000
        active: ZaloClient.appLock.locked
        sourceComponent: LockScreen {}
    }
}
