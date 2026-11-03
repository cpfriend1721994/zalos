import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

// Left navigation rail mirroring chat.zalo.me: account avatar, Messages, Contacts, To-Do,
// then My Cloud and the Settings menu pinned to the bottom.
Rectangle {
    id: root
    color: ThemeManager.railBackgroundColor

    readonly property string currentTab: ZaloClient.currentNavigationTab
    readonly property bool compact: width < Theme.railWidth
    readonly property int buttonSize: compact ? 40 : 48 // 64px rail minus 2x8px, like Zalo PC
    readonly property int iconSize: compact ? 22 : 24

    // A single rail button: circular round button matching modern Zalo PC / Web.
    component RailButton: Rectangle {
        id: btn
        property string icon: ""
        property string tip: ""
        property bool active: false
        property int badge: 0
        signal clicked()

        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: root.compact ? 40 : 44
        Layout.preferredHeight: root.compact ? 40 : 44
        radius: width / 2
        color: active ? (ThemeManager.railSelectedColor)
                      : (btnMouse.containsMouse ? (ThemeManager.railHoverColor) : ThemeManager.railHoverClearColor)
        Behavior on color { ColorAnimation { duration: 140 } }
        scale: btnMouse.pressed ? 0.93 : (btnMouse.containsMouse ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

        ZIcon {
            anchors.centerIn: parent
            name: btn.icon
            size: root.iconSize
            color: "#FFFFFF" // --icon-navbar-normal
            opacity: ThemeManager.isDarkMode && !btn.active && !btnMouse.containsMouse ? 0.75 : 1.0
        }

        Rectangle {
            visible: btn.badge > 0
            scale: btn.badge > 0 ? 1.0 : 0.0
            Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 4
            anchors.rightMargin: 2
            height: 16
            width: Math.max(16, badgeText.implicitWidth + 8)
            radius: 8
            color: Theme.badgeRed
            border.color: root.color
            border.width: 1

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: btn.badge > 99 ? "99+" : btn.badge
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.bold: true
            }
        }

        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
        }

        ToolTip.visible: btnMouse.containsMouse && btn.tip.length > 0
        ToolTip.delay: 400
        ToolTip.text: btn.tip
    }

    // Menu row used by the account and settings popups.
    component MenuRow: Rectangle {
        id: row
        property string icon: ""
        property string label: ""
        property bool danger: false
        property bool hasSubmenu: false
        signal triggered()

        Layout.fillWidth: true
        Layout.preferredHeight: 38
        radius: 6
        color: rowMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor
        Behavior on color { ColorAnimation { duration: 140 } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 10

            ZIcon {
                visible: row.icon.length > 0
                name: row.icon
                size: 16
                color: row.danger ? Theme.badgeRed : ThemeManager.textColor
            }
            Text {
                Layout.fillWidth: true
                text: row.label
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 13
                color: row.danger ? Theme.badgeRed : ThemeManager.textColor
            }
            ZIcon {
                visible: row.hasSubmenu
                name: "chevron_right_small"
                size: 16
                color: ThemeManager.secondaryTextColor
            }
        }

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.triggered()
        }
    }

    component MenuDivider: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        Layout.topMargin: 4
        Layout.bottomMargin: 4
        color: ThemeManager.borderColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 20
        anchors.bottomMargin: 12
        spacing: 4

        // Account avatar
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: root.buttonSize
            Layout.preferredHeight: root.buttonSize
            Layout.bottomMargin: 16

            ZAvatar {
                anchors.centerIn: parent
                size: root.compact ? 36 : 44
                avatarUrl: ZaloClient.currentUserAvatar
                name: ZaloClient.currentUserName
                isOnline: ZaloClient.isConnected
                showStatusDot: true
            }

            MouseArea {
                id: avatarMouse
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                onClicked: accountMenu.opened ? accountMenu.close() : accountMenu.open()
            }
            ToolTip.visible: avatarMouse.containsMouse && !accountMenu.opened && ZaloClient.currentUserName.length > 0
            ToolTip.delay: 400
            ToolTip.text: ZaloClient.currentUserName
        }

        RailButton {
            icon: "chat"
            tip: I18n.t("Tin nhắn", "Messages")
            active: root.currentTab === "messages"
            badge: ZaloClient.conversationModel.totalUnreadCount
            onClicked: ZaloClient.setCurrentNavigationTab("messages")
        }

        RailButton {
            icon: "contacts"
            tip: I18n.t("Danh bạ", "Contacts")
            active: root.currentTab === "contacts"
            onClicked: {
                ZaloClient.setCurrentNavigationTab("contacts");
                ZaloClient.contactModel.setCategoryFilter("friends");
            }
        }

        RailButton {
            icon: "todo"
            tip: I18n.t("To-Do", "To-Do")
            active: root.currentTab === "todo"
            onClicked: ZaloClient.setCurrentNavigationTab("todo")
        }

        Item { Layout.fillHeight: true }

        // "Cloud của tôi" is not offered in the rail (removed at the user's request, 2026-11-03).

        RailButton {
            objectName: "btnSettings"
            icon: "settings"
            tip: settingsMenu.opened ? "" : I18n.t("Cài đặt", "Settings")
            active: settingsMenu.opened
            onClicked: settingsMenu.opened ? settingsMenu.close() : settingsMenu.open()
        }
    }

    // Account popup (avatar click), like the web client's profile menu.
    Popup {
        id: accountMenu
        x: root.width + 6
        y: 12
        width: 260
        padding: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: 10
            border.color: ThemeManager.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 2

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: 6
                Layout.bottomMargin: 6
                text: ZaloClient.currentUserName
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 16
                font.bold: true
                color: ThemeManager.textColor
            }

            MenuDivider {}

            MenuRow {
                icon: "user"
                label: I18n.t("Hồ sơ của bạn", "Profile")
                onTriggered: { accountMenu.close(); ZaloClient.openUserProfile(); }
            }
            MenuRow {
                icon: "settings"
                label: I18n.t("Cài đặt", "Settings")
                onTriggered: { accountMenu.close(); ZaloClient.openSettings("general"); }
            }

            MenuDivider {}

            MenuRow {
                icon: "logout"
                label: I18n.t("Đăng xuất", "Log out")
                danger: true
                onTriggered: { accountMenu.close(); ZaloClient.logout(); }
            }
        }
    }

    // Settings popup (gear), anchored to the bottom of the rail.
    Popup {
        id: settingsMenu
        objectName: "settingsMenu"
        x: root.width + 6
        y: Math.max(8, root.height - height - 12)
        width: 248
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: languageMenu.close()
        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: 10
            border.color: ThemeManager.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 2

            MenuRow {
                icon: "user"
                label: I18n.t("Thông tin tài khoản", "Account information")
                onTriggered: { settingsMenu.close(); ZaloClient.openUserProfile(); }
            }
            MenuRow {
                icon: "settings"
                label: I18n.t("Cài đặt", "Settings")
                onTriggered: { settingsMenu.close(); ZaloClient.openSettings("general"); }
            }

            MenuDivider {}

            MenuRow {
                id: languageRow
                icon: "globe"
                label: I18n.t("Ngôn ngữ", "Language")
                hasSubmenu: true
                onTriggered: languageMenu.opened ? languageMenu.close() : languageMenu.open()
            }
            MenuRow {
                icon: "help"
                label: I18n.t("Hỗ trợ", "Help")
                onTriggered: { settingsMenu.close(); Qt.openUrlExternally("https://help.zalo.me/"); }
            }

            MenuDivider {}

            MenuRow {
                icon: "logout"
                label: I18n.t("Đăng xuất", "Log out")
                danger: true
                onTriggered: { settingsMenu.close(); ZaloClient.logout(); }
            }
        }

        // Language submenu opens beside the settings menu.
        Popup {
            id: languageMenu
            x: settingsMenu.width - 4
            y: languageRow.y
            width: 180
            padding: 6
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
            background: Rectangle {
                color: ThemeManager.surfaceColor
                radius: 10
                border.color: ThemeManager.borderColor
            }

            contentItem: ColumnLayout {
                spacing: 2

                Repeater {
                    model: [
                        { code: "vi", label: "Tiếng Việt" },
                        { code: "en", label: "English" }
                    ]

                    MenuRow {
                        required property var modelData
                        label: modelData.label
                        icon: I18n.currentLanguage === modelData.code ? "check" : ""
                        onTriggered: {
                            ZaloClient.setAppLanguage(modelData.code);
                            I18n.setLanguage(modelData.code);
                            languageMenu.close();
                            settingsMenu.close();
                        }
                    }
                }
            }
        }
    }
}
