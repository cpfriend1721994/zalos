pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    objectName: "shortcutsDialog"
    anchors.fill: parent

    signal closeRequested()

    function labelFor(command: string): string {
        switch (command) {
        case "chat.next": return I18n.t("Hội thoại tiếp theo", "Next conversation");
        case "chat.previous": return I18n.t("Hội thoại trước", "Previous conversation");
        case "chat.pinned1": return I18n.t("Mở hội thoại ghim thứ 1", "Open pinned chat 1");
        case "chat.pinned2": return I18n.t("Mở hội thoại ghim thứ 2", "Open pinned chat 2");
        case "chat.pinned3": return I18n.t("Mở hội thoại ghim thứ 3", "Open pinned chat 3");
        case "chat.pinned4": return I18n.t("Mở hội thoại ghim thứ 4", "Open pinned chat 4");
        case "chat.pinned5": return I18n.t("Mở hội thoại ghim thứ 5", "Open pinned chat 5");
        case "chat.search": return I18n.t("Tìm kiếm", "Search");
        case "chat.find": return I18n.t("Tìm trong hội thoại", "Find in conversation");
        case "composer.voice": return I18n.t("Ghi âm tin nhắn thoại", "Record a voice message");
        case "privacy.antiPeeking": return I18n.t("Bật/tắt chống nhìn trộm", "Toggle anti-peeking");
        case "app.lock": return I18n.t("Khóa Zalos", "Lock Zalos");
        case "app.screenshot": return I18n.t("Chụp màn hình", "Take a screenshot");
        case "app.hide": return I18n.t("Ẩn cửa sổ", "Hide window");
        case "app.quit": return I18n.t("Thoát Zalos", "Quit Zalos");
        case "app.shortcuts": return I18n.t("Xem phím tắt", "Show keyboard shortcuts");
        }
        return command;
    }

    ZModalScrim {
        onDismissed: root.closeRequested()
    }

    component KeyChip: Rectangle {
        id: chip
        property string label: ""
        implicitWidth: Math.max(24, chipText.implicitWidth + 12)
        implicitHeight: 24
        radius: 5
        color: ThemeManager.inputFillColor
        border.color: ThemeManager.borderColor
        border.width: 1

        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }
    }

    component KeyCombo: Row {
        id: combo
        property string keysText: ""
        spacing: 3

        Repeater {
            model: combo.keysText.split("+")
            delegate: KeyChip {
                required property string modelData
                label: modelData
            }
        }
    }

    component ShortcutRow: RowLayout {
        id: shortcutRow
        property string label: ""
        property list<string> combos: []
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: shortcutRow.label
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: ThemeManager.textColor
        }

        Row {
            spacing: 8

            Repeater {
                model: shortcutRow.combos
                delegate: KeyCombo {
                    required property string modelData
                    keysText: modelData
                }
            }
        }
    }

    Rectangle {
        objectName: "shortcutsDialogCard"
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 520)
        height: Math.min(root.height - 40, 660)
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ZIcon { name: "info"; size: 20; color: Theme.zaloBlue }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Phím tắt", "Keyboard shortcuts")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                }
                ZIconButton {
                    iconName: "close"
                    iconSize: 16
                    tooltip: I18n.t("Đóng", "Close")
                    onClicked: root.closeRequested()
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("Có thể đổi phím tắt trong tệp shortcuts-custom.json ở thư mục cấu hình Zalos.",
                             "Shortcuts can be changed in shortcuts-custom.json in the Zalos config folder.")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: ThemeManager.secondaryTextColor
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: shortcutColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { width: 6 }

                Column {
                    id: shortcutColumn
                    width: parent.width
                    spacing: 6

                    Repeater {
                        model: ZaloClient.shortcuts.commands()
                        delegate: ShortcutRow {
                            id: shortcutEntry
                            required property string modelData
                            width: shortcutColumn.width
                            visible: shortcutEntry.combos.length > 0
                            label: root.labelFor(shortcutEntry.modelData)
                            combos: ZaloClient.shortcuts.keys(shortcutEntry.modelData)
                        }
                    }

                    ShortcutRow {
                        width: shortcutColumn.width
                        label: I18n.t("Đóng cửa sổ đang mở", "Close the open dialog")
                        combos: ["Esc"]
                    }
                }
            }
        }
    }
}
