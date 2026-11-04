import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

ApplicationWindow {
    id: mainWindow
    visible: !ZaloClient.launchHidden
    width: 1040
    height: 680
    minimumWidth: 380
    minimumHeight: 420
    title: "Zalo"
    color: ThemeManager.backgroundColor

    readonly property bool isCompactMode: width < 680

    flags: Qt.Window

    Component.onCompleted: I18n.setLanguage(ZaloClient.appLanguage)

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

    onClosing: (close) => {
        if (ZaloClient.closeToTray && ZaloClient.trayAvailable) {
            close.accepted = false;
            mainWindow.hide();
        }
    }

    function showAndActivate(): void {
        if (mainWindow.visibility === Window.Minimized) mainWindow.showNormal();
        else mainWindow.show();
        mainWindow.raise();
        mainWindow.requestActivate();
    }

    function toggleTheme() {
        ThemeManager.toggleTheme();
    }

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

    Loader {
        active: !ZaloClient.isLoggedIn && !ZaloClient.isRestoringSession && !ZaloClient.isOfflineMode
        anchors.fill: parent
        sourceComponent: LoginView {}
    }

    RowLayout {
        visible: ZaloClient.isLoggedIn || ZaloClient.isOfflineMode
        anchors.fill: parent
        spacing: 0

        NavigationRail {
            id: navigationRail
            Layout.preferredWidth: mainWindow.isCompactMode ? 52 : Theme.railWidth
            z: 10
            Layout.minimumWidth: mainWindow.isCompactMode ? 52 : Theme.railWidth
            Layout.maximumWidth: mainWindow.isCompactMode ? 52 : Theme.railWidth
            Layout.fillHeight: true
        }

        RowLayout {
            visible: ZaloClient.currentNavigationTab === "messages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar
                visible: !mainWindow.isCompactMode || ZaloClient.currentConversationId === ""
                Layout.preferredWidth: mainWindow.isCompactMode ? -1 : Theme.sidebarWidth
                Layout.fillWidth: mainWindow.isCompactMode
                Layout.minimumWidth: mainWindow.isCompactMode ? 200 : 280
                Layout.maximumWidth: mainWindow.isCompactMode ? -1 : 360
                Layout.fillHeight: true
            }

            WelcomeArea {
                visible: !mainWindow.isCompactMode && ZaloClient.currentConversationId === ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
            }

            ChatArea {
                visible: ZaloClient.currentConversationId !== ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
            }
        }

        Loader {
            active: ZaloClient.currentNavigationTab === "contacts"
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: ContactsView {}
        }
    }

    readonly property bool shortcutsLive: (ZaloClient.isLoggedIn || ZaloClient.isOfflineMode) && !ZaloClient.appLock.locked

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
        color: ThemeManager.isDarkMode ? "#38404A" : "#E6081B3A"

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

    LazyOverlay {
        z: 1500
        active: ImageViewerState.open
        sourceComponent: ImageViewer {}
    }

    LazyOverlay {
        z: 2000
        active: !!(ZaloClient.calls && ZaloClient.calls.phase.length > 0)
        sourceComponent: CallOverlay {}
    }

    LazyOverlay {
        z: 950
        active: ZaloClient.showSyncPrompt && !ZaloClient.appLock.locked
        sourceComponent: SyncPromptDialog {}
    }

    LazyOverlay {
        z: 3000
        active: ZaloClient.appLock.locked
        sourceComponent: LockScreen {}
    }
}
