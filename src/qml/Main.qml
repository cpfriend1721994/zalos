pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import Zalos

ApplicationWindow {
    id: mainWindow
    visible: !ZaloClient.launchHidden
    width: 1040
    height: 680
    minimumWidth: 480
    minimumHeight: 440
    title: "Zalos"
    color: ThemeManager.backgroundColor
    font.family: Theme.fontFamily

    readonly property bool isCompactMode: width < 720
    readonly property bool anyModalOpen: ZaloClient.showUserProfileModal
    readonly property real callBarInset: callOverlayLoader.item && !callOverlayLoader.item.minimized ? callOverlayLoader.item.controlBarHeight : 0
                                      || ZaloClient.pollDialogId > 0
                                      || ZaloClient.showSettingsModal
                                      || ZaloClient.showAddFriendModal
                                      || ZaloClient.showCreateGroupModal
                                      || ZaloClient.showForwardModal
                                      || ZaloClient.showCreatePollModal
                                      || ZaloClient.groupAdmin.manageOpen
                                      || ZaloClient.groupAdmin.joinOpen
                                      || ZaloClient.labels.manageOpen
                                      || ZaloClient.showBusinessToolsModal
    function closeTopModal(): void {
        if (ZaloClient.showUserProfileModal) ZaloClient.closeUserProfile();
        else if (ZaloClient.pollDialogId > 0) ZaloClient.closePollDialog();
        else if (ZaloClient.showSettingsModal) ZaloClient.closeSettings();
        else if (ZaloClient.showAddFriendModal) ZaloClient.closeAddFriend();
        else if (ZaloClient.showCreateGroupModal) ZaloClient.closeCreateGroup();
        else if (ZaloClient.showForwardModal) ZaloClient.closeForwardModal();
        else if (ZaloClient.showCreatePollModal) ZaloClient.closeCreatePollModal();
        else if (ZaloClient.groupAdmin.manageOpen) ZaloClient.groupAdmin.closeManage();
        else if (ZaloClient.groupAdmin.joinOpen) ZaloClient.groupAdmin.closeJoin();
        else if (ZaloClient.labels.manageOpen) ZaloClient.labels.manageOpen = false;
        else if (ZaloClient.showBusinessToolsModal) ZaloClient.closeBusinessTools();
    }
    readonly property bool inActiveCall: !!(ZaloClient.calls && ZaloClient.calls.phase.length > 0 && ZaloClient.calls.phase !== "ended")
    property bool forceQuit: false
    property int preHideVisibility: Window.AutomaticVisibility
    property int preScreenshotVisibility: Window.AutomaticVisibility
    property int lastShownVisibility: Window.Windowed

    flags: Qt.Window

    Component.onCompleted: {
        I18n.setLanguage(ZaloClient.appLanguage)
        var geom = ZaloClient.windowGeometry()
        if (geom.width && geom.width >= minimumWidth) mainWindow.width = geom.width
        if (geom.height && geom.height >= minimumHeight) mainWindow.height = geom.height
        if (geom.hasPosition) {
            mainWindow.x = geom.x
            mainWindow.y = geom.y
        }
        if (geom.maximized) mainWindow.visibility = Window.Maximized
        ZaloClient.notifyWindowVisibility(mainWindow.visible && mainWindow.visibility !== Window.Hidden)
        mainWindow.ensureLoginWindowHeight()
    }

    function ensureLoginWindowHeight(): void {
        if (!loginLoader.active || !loginLoader.item) return;
        if (mainWindow.visibility !== Window.Windowed && mainWindow.visibility !== Window.AutomaticVisibility) return;
        var needed = loginLoader.item.requiredContentHeight || 0;
        var avail = Screen.desktopAvailableHeight > 0 ? Screen.desktopAvailableHeight - 40 : needed;
        var target = Math.min(needed, avail);
        if (target > mainWindow.height) {
            mainWindow.height = target;
        }
    }

    function recordGeometry(): void {
        if (mainWindow.visibility === Window.Maximized) {
            ZaloClient.saveWindowGeometry(mainWindow.x, mainWindow.y, mainWindow.width, mainWindow.height, true)
        } else if (mainWindow.visibility === Window.Windowed || mainWindow.visibility === Window.AutomaticVisibility) {
            ZaloClient.saveWindowGeometry(mainWindow.x, mainWindow.y, mainWindow.width, mainWindow.height, false)
        }
    }

    onXChanged: geomSaveTimer.restart()
    onYChanged: geomSaveTimer.restart()
    onWidthChanged: geomSaveTimer.restart()
    onHeightChanged: geomSaveTimer.restart()
    onVisibilityChanged: {
        recordGeometry()
        if (visibility === Window.Windowed || visibility === Window.Maximized || visibility === Window.FullScreen)
            lastShownVisibility = visibility
        ZaloClient.notifyWindowVisibility(mainWindow.visible && mainWindow.visibility !== Window.Hidden)
    }
    onVisibleChanged: {
        ZaloClient.notifyWindowVisibility(mainWindow.visible && mainWindow.visibility !== Window.Hidden)
    }

    Timer {
        id: geomSaveTimer
        interval: 400
        repeat: false
        onTriggered: mainWindow.recordGeometry()
    }

    Connections {
        target: ZaloClient
        function onAppLanguageChanged(): void {
            if (I18n.currentLanguage !== ZaloClient.appLanguage) I18n.setLanguage(ZaloClient.appLanguage);
        }
        function onRequestHideForScreenshot(): void {
            if (mainWindow.visibility === Window.FullScreen
                    || mainWindow.visibility === Window.Maximized
                    || mainWindow.visibility === Window.Windowed) {
                mainWindow.preScreenshotVisibility = mainWindow.visibility;
            } else {
                mainWindow.preScreenshotVisibility = mainWindow.lastShownVisibility;
            }
            mainWindow.hide();
        }
        function onHideWindowRequested(): void {
            mainWindow.minimizeOrHideForCall();
        }
        function onRequestRestoreAfterScreenshot(): void {
            var target = mainWindow.preScreenshotVisibility;
            mainWindow.preScreenshotVisibility = Window.AutomaticVisibility;
            if (target === Window.AutomaticVisibility) {
                if (!mainWindow.visible
                        || mainWindow.visibility === Window.Hidden
                        || mainWindow.visibility === Window.Minimized) {
                    target = mainWindow.lastShownVisibility;
                }
            }
            if (target === Window.FullScreen) {
                if (!mainWindow.visible || mainWindow.visibility !== Window.FullScreen)
                    mainWindow.showFullScreen();
            } else if (target === Window.Maximized) {
                if (!mainWindow.visible || mainWindow.visibility !== Window.Maximized)
                    mainWindow.showMaximized();
            } else if (target !== Window.AutomaticVisibility) {
                if (!mainWindow.visible || mainWindow.visibility !== Window.Windowed)
                    mainWindow.showNormal();
            }
            mainWindow.raise();
            mainWindow.requestActivate();
        }
    }
    Connections {
        target: I18n
        function onLanguageChanged(lang: string): void {
            if (ZaloClient.appLanguage !== lang) ZaloClient.setAppLanguage(lang);
        }
    }

    function hideToTray(): void {
        mainWindow.recordGeometry();
        if (mainWindow.visibility === Window.Maximized || mainWindow.visibility === Window.FullScreen) {
            mainWindow.preHideVisibility = mainWindow.visibility;
        } else {
            mainWindow.preHideVisibility = Window.AutomaticVisibility;
        }
        mainWindow.hide();
    }

    function minimizeOrHideForCall(): void {
        if (ZaloClient.trayAvailable) {
            mainWindow.hideToTray();
        } else {
            mainWindow.recordGeometry();
            if (callOverlayLoader.item) callOverlayLoader.item.minimized = true;
            mainWindow.showMinimized();
        }
    }

    function toggleVisibility(): void {
        if (mainWindow.visible && mainWindow.visibility !== Window.Minimized) {
            mainWindow.hideToTray();
        } else {
            mainWindow.showAndActivate();
        }
    }

    onClosing: (close) => {
        if (mainWindow.forceQuit) return;
        mainWindow.recordGeometry();
        if (mainWindow.inActiveCall) {
            close.accepted = false;
            mainWindow.minimizeOrHideForCall();
        } else if (ZaloClient.closeToTray && ZaloClient.trayAvailable) {
            close.accepted = false;
            mainWindow.hideToTray();
        }
    }

    function showAndActivate(): void {
        if (mainWindow.preHideVisibility === Window.FullScreen) {
            mainWindow.showFullScreen();
        } else if (mainWindow.preHideVisibility === Window.Maximized) {
            mainWindow.showMaximized();
        } else if (mainWindow.visibility === Window.Minimized) {
            if (mainWindow.lastShownVisibility === Window.FullScreen)
                mainWindow.showFullScreen();
            else if (mainWindow.lastShownVisibility === Window.Maximized)
                mainWindow.showMaximized();
            else
                mainWindow.showNormal();
        } else {
            mainWindow.show();
        }
        mainWindow.preHideVisibility = Window.AutomaticVisibility;
        mainWindow.raise();
        mainWindow.requestActivate();
    }

    function toggleTheme(): void {
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
                    running: startupSplash.visible && mainWindow.visible && mainWindow.visibility !== Window.Minimized
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
                font.weight: Font.DemiBold
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
                            id: splashDot
                            required property int index
                            width: 6
                            height: 6
                            radius: 3
                            color: Theme.zaloBlue
                            SequentialAnimation on y {
                                running: startupSplash.visible && mainWindow.visible && mainWindow.visibility !== Window.Minimized
                                loops: Animation.Infinite
                                PauseAnimation { duration: splashDot.index * 140 }
                                PropertyAnimation { to: -5; duration: 280; easing.type: Easing.OutQuad }
                                PropertyAnimation { to: 0; duration: 280; easing.type: Easing.InQuad }
                                PauseAnimation { duration: (2 - splashDot.index) * 140 }
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
        id: loginLoader
        active: !ZaloClient.isLoggedIn && !ZaloClient.isRestoringSession && !ZaloClient.isOfflineMode
        anchors.fill: parent
        sourceComponent: LoginView {}
        onLoaded: mainWindow.ensureLoginWindowHeight()
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
                Layout.preferredWidth: mainWindow.isCompactMode
                                       ? -1
                                       : Math.max(260, Math.min(Theme.sidebarWidth, Math.round(mainWindow.width * 0.34)))
                Layout.fillWidth: mainWindow.isCompactMode
                Layout.minimumWidth: mainWindow.isCompactMode ? 200 : 260
                Layout.maximumWidth: mainWindow.isCompactMode ? -1 : 390
                Layout.fillHeight: true
            }

            Loader {
                active: !mainWindow.isCompactMode && ZaloClient.currentConversationId === ""
                visible: active
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
                sourceComponent: WelcomeArea {}
            }

            Loader {
                active: ZaloClient.currentConversationId !== ""
                visible: active
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 260
                sourceComponent: ChatArea {}
            }
        }

        Loader {
            active: ZaloClient.currentNavigationTab === "contacts"
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: ContactsView {}
        }

        Loader {
            active: ZaloClient.currentNavigationTab === "cloud"
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: CloudView {}
        }

        Loader {
            active: ZaloClient.currentNavigationTab === "kiki"
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: KikiView {}
        }
    }

    readonly property bool shortcutsLive: (ZaloClient.isLoggedIn || ZaloClient.isOfflineMode)
                                       && !ZaloClient.appLock.locked
                                       && !mainWindow.anyModalOpen

    Shortcut {
        sequence: "Esc"
        enabled: mainWindow.anyModalOpen && !ZaloClient.appLock.locked && !ImageViewerState.open
        onActivated: mainWindow.closeTopModal()
    }

    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("privacy.antiPeeking")
        enabled: mainWindow.shortcutsLive
        onActivated: ZaloClient.setAntiPeeking(!ZaloClient.antiPeeking)
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("chat.next")
        enabled: mainWindow.shortcutsLive
        onActivated: ZaloClient.selectAdjacentConversation(1)
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("chat.previous")
        enabled: mainWindow.shortcutsLive
        onActivated: ZaloClient.selectAdjacentConversation(-1)
    }
    Repeater {
        model: 5
        delegate: Item {
            id: pinnedShortcut
            required property int index
            Shortcut {
                sequences: ZaloClient.shortcuts.keySequences("chat.pinned" + (pinnedShortcut.index + 1))
                enabled: mainWindow.shortcutsLive
                onActivated: ZaloClient.openPinnedConversation(pinnedShortcut.index + 1)
            }
        }
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("composer.voice")
        enabled: mainWindow.shortcutsLive && ZaloClient.currentConversationId.length > 0
        onActivated: {
            if (ZaloClient.voiceRecorder.recording) ZaloClient.voiceRecorder.stop();
            else ZaloClient.voiceRecorder.start();
        }
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("app.lock")
        enabled: ZaloClient.appLock.hasPasscode && !ZaloClient.appLock.locked
        onActivated: ZaloClient.appLock.lock()
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("app.hide")
        onActivated: {
            if (ZaloClient.trayAvailable) mainWindow.hideToTray();
            else mainWindow.showMinimized();
        }
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("app.screenshot")
        enabled: mainWindow.shortcutsLive
        onActivated: ZaloClient.requestScreenshot(false)
    }
    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("app.quit")
        onActivated: {
            mainWindow.forceQuit = true;
            Qt.quit();
        }
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
        z: 2500
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24 + mainWindow.callBarInset
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
            color: Theme.textOnColor
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
    LazyOverlay { z: 920; active: ZaloClient.showUserProfileModal; sourceComponent: UserProfileModal {} }
    LazyOverlay { active: ZaloClient.showAddFriendModal; sourceComponent: AddFriendModal {} }
    LazyOverlay { active: ZaloClient.showCreateGroupModal; sourceComponent: CreateGroupModal {} }
    LazyOverlay { active: ZaloClient.showForwardModal; sourceComponent: ForwardMessageModal {} }
    LazyOverlay { active: ZaloClient.showCreatePollModal; sourceComponent: CreatePollModal {} }
    LazyOverlay { z: 905; active: ZaloClient.pollDialogId > 0; sourceComponent: PollVoteDialog {} }
    LazyOverlay { active: ZaloClient.groupAdmin.manageOpen; sourceComponent: GroupManageModal {} }
    LazyOverlay { active: ZaloClient.groupAdmin.joinOpen; sourceComponent: GroupJoinModal {} }
    LazyOverlay { active: ZaloClient.labels.manageOpen; sourceComponent: LabelManageModal {} }
    LazyOverlay { active: ZaloClient.showBusinessToolsModal; sourceComponent: BusinessToolsModal {} }

    Loader {
        id: appLinkPopupLoader
        active: false
        sourceComponent: AppLinkPopup {}
    }
    Loader {
        id: hiddenPinPopupLoader
        active: false
        sourceComponent: HiddenPinPopup {}
    }
    Connections {
        target: ZaloClient
        function onAppOnlyLinkRequested(url: string, title: string, thumb: string) {
            appLinkPopupLoader.active = true;
            appLinkPopupLoader.item.show(url, title, thumb);
        }
        function onHiddenFlowRequested(flow: string, convId: string, convName: string, bypass: bool) {
            hiddenPinPopupLoader.active = true;
            hiddenPinPopupLoader.item.start(flow, convId, convName, bypass);
        }
    }

    LazyOverlay {
        z: 1500
        active: ImageViewerState.open
        source: active ? Qt.resolvedUrl("ImageViewer.qml") : ""
        onActiveChanged: if (!active) ZaloClient.requestMemoryTrim()
    }

    LazyOverlay {
        id: callOverlayLoader
        z: 2000
        active: !!(ZaloClient.calls && ZaloClient.calls.phase.length > 0)
        source: active ? Qt.resolvedUrl("CallOverlay.qml") : ""
    }

    LazyOverlay {
        z: 950
        active: ZaloClient.showSyncPrompt && !ZaloClient.appLock.locked
        sourceComponent: SyncPromptDialog {}
    }

    Connections {
        target: ZaloClient.appLock
        function onLockedChanged(): void {
            if (!ZaloClient.appLock.locked) return;
            if (appLinkPopupLoader.item && typeof appLinkPopupLoader.item.close === "function") appLinkPopupLoader.item.close();
            if (hiddenPinPopupLoader.item && typeof hiddenPinPopupLoader.item.close === "function") hiddenPinPopupLoader.item.close();
            const overlay = mainWindow.overlay;
            if (overlay) {
                for (let i = 0; i < overlay.children.length; ++i) {
                    const ch = overlay.children[i];
                    if (ch && typeof ch.close === "function") ch.close();
                }
            }
        }
    }

    LazyOverlay {
        z: 3000
        active: ZaloClient.appLock.locked
        sourceComponent: LockScreen {}
    }
}
