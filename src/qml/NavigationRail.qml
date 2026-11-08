pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    color: ThemeManager.railBackgroundColor

    readonly property string currentTab: ZaloClient.currentNavigationTab
    readonly property bool compact: width < Theme.railWidth
    readonly property int buttonSize: compact ? 40 : 48
    readonly property int iconSize: compact ? 22 : 24
    readonly property bool foldersShown: ZaloClient.showLabelFolders && ZaloClient.labels.labels.length > 0

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
        // Theme-aware: the rail is near-black in dark mode (a faint accent tint reads well) but a saturated
        // blue bar in light mode (an accent tint would vanish — a bright white highlight stands out instead).
        color: active
               ? (ThemeManager.isDarkMode ? Qt.rgba(Theme.zaloBlue.r, Theme.zaloBlue.g, Theme.zaloBlue.b, 0.26)
                                          : Qt.rgba(1, 1, 1, 0.95))
               : (btnMouse.containsMouse ? Qt.rgba(1, 1, 1, ThemeManager.isDarkMode ? 0.10 : 0.20)
                                         : ThemeManager.railHoverClearColor)
        Behavior on color { ColorAnimation { duration: 140 } }
        scale: btnMouse.pressed ? 0.93 : (btnMouse.containsMouse ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

        ZIcon {
            anchors.centerIn: parent
            name: btn.icon
            size: root.iconSize
            // Dark: white glyph on the faint accent tint. Light: accent-blue glyph on the white highlight.
            color: btn.active ? (ThemeManager.isDarkMode ? "#FFFFFF" : Theme.zaloBlue) : "#FFFFFF"
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

    component LabelFolder: Item {
        id: folder
        property int labelId: 0
        property string labelName: ""
        property color dot: "transparent"
        property bool active: false
        signal clicked()

        function initials(text: string): string {
            var t = text.trim();
            if (t.length === 0) return "#";
            if (t.indexOf(" ") < 0) return t.substring(0, 2).toUpperCase();
            var last = t.substring(t.lastIndexOf(" ") + 1);
            if (last.length === 0) return t.charAt(0).toUpperCase();
            return (t.charAt(0) + last.charAt(0)).toUpperCase();
        }

        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: root.compact ? 40 : 44
        Layout.preferredHeight: root.compact ? 40 : 44
        scale: folderMouse.pressed ? 0.93 : (folderMouse.containsMouse ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

        Rectangle {
            id: folderDisc
            anchors.centerIn: parent
            width: root.compact ? 40 : 44
            height: width
            radius: width / 2
            // Same principle as the nav buttons: faint colored tint on the dark rail; a solid label-colour disc
            // on the saturated-blue light rail (a faint tint would vanish there), so colour identity stays clear.
            color: ThemeManager.isDarkMode
                   ? (folder.active ? Qt.rgba(folder.dot.r, folder.dot.g, folder.dot.b, 0.42)
                      : (folderMouse.containsMouse ? Qt.rgba(folder.dot.r, folder.dot.g, folder.dot.b, 0.28)
                                                   : "transparent"))
                   : (folderMouse.containsMouse ? Qt.lighter(folder.dot, 1.12) : folder.dot)
            Behavior on color { ColorAnimation { duration: 140 } }

            Text {
                anchors.centerIn: parent
                text: folder.initials(folder.labelName)
                // Dark: colored glyph on the faint/transparent tint. Light: white glyph on the solid disc.
                color: ThemeManager.isDarkMode ? folder.dot : "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: Math.max(11, Math.floor(folderDisc.width * 0.36))
                font.bold: true
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: folderDisc.width
            height: width
            radius: width / 2
            color: "transparent"
            visible: folder.active
            border.color: "#FFFFFF"
            border.width: 2
        }

        MouseArea {
            id: folderMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: folder.clicked()
        }

        ToolTip.visible: folderMouse.containsMouse
        ToolTip.delay: 400
        ToolTip.text: folder.labelName
    }

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
            active: root.currentTab === "messages" && (!root.foldersShown || ZaloClient.conversationModel.labelFilter === 0)
            badge: ZaloClient.conversationModel.totalUnreadCount
            onClicked: {
                ZaloClient.setCurrentNavigationTab("messages");
                if (root.foldersShown) ZaloClient.conversationModel.setLabelFilter(0);
            }
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

        MenuDivider { visible: root.foldersShown; Layout.leftMargin: 10; Layout.rightMargin: 10 }

        Flickable {
            id: folderFlick
            visible: root.foldersShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: folderColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickDeceleration: 6000
            maximumFlickVelocity: 2500
            ScrollBar.vertical: ScrollBar {
                id: folderScroll
                policy: ScrollBar.AsNeeded
                width: 4
                contentItem: Rectangle {
                    radius: 2
                    color: ThemeManager.railSelectedColor
                    opacity: folderScroll.pressed ? 0.9 : 0.5
                }
            }

            ColumnLayout {
                id: folderColumn
                width: parent.width
                spacing: 4
                Repeater {
                    model: ZaloClient.labels.labels
                    LabelFolder {
                        required property var modelData
                        labelId: modelData.id
                        labelName: modelData.text
                        dot: modelData.color
                        active: ZaloClient.currentNavigationTab === "messages" && ZaloClient.conversationModel.labelFilter === labelId
                        onClicked: {
                            ZaloClient.setCurrentNavigationTab("messages");
                            ZaloClient.conversationModel.setLabelFilter(labelId);
                        }
                    }
                }
            }
        }

        Item { visible: !root.foldersShown; Layout.fillHeight: true }
    }

    Popup {
        id: accountMenu
        objectName: "accountMenu"
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
            MenuRow {
                icon: "help"
                label: I18n.t("Hỗ trợ", "Help")
                onTriggered: { accountMenu.close(); Qt.openUrlExternally("https://help.zalo.me/"); }
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
}
