import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

// Emoji + sticker picker. Stickers: ZaloClient.stickers (the account's packs, recent, keyword search), drawn with
// ZSticker (animated on hover); a click sends through ZaloClient.stickers.send (recent list kept per account).
Rectangle {
    id: root
    width: parent ? Math.min(380, parent.width - 32) : 380
    height: Math.min(parent ? parent.height - 32 : 420, 420)
    radius: 12
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    clip: true

    signal emojiSelected(string emoji)
    signal stickerSelected(var sticker)

    property int tab: 0 // 0 = emoji, 1 = stickers

    onVisibleChanged: if (visible && root.tab === 1) ZaloClient.stickers.load()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // Tabs
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Repeater {
                model: [I18n.t("Biểu tượng cảm xúc", "Emoji"), I18n.t("Nhãn dán", "Stickers")]
                Item {
                    required property int index
                    required property string modelData
                    implicitWidth: tabLabel.implicitWidth
                    implicitHeight: 28

                    Text {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: modelData
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: root.tab === index
                        color: root.tab === index ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: root.tab === index ? Theme.zaloBlue : "transparent"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.tab = index;
                            if (index === 1) {
                                ZaloClient.stickers.load();
                                stickerSearch.forceActiveFocus();
                            }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: ThemeManager.borderColor
        }

        // Emoji grid
        GridView {
            visible: root.tab === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(width / Math.max(4, Math.floor(width / 44)))
            cellHeight: 44
            model: [
                "👍", "❤️", "😂", "😮", "😢", "😡", "😆", "😍",
                "🥰", "😘", "😊", "😉", "🤔", "😴", "😭", "😅",
                "🎉", "🔥", "👏", "🙏", "🚀", "💯", "👌", "🤝",
                "☕", "💼", "✅", "🎁", "⭐", "💡", "🎂", "🥳",
                "✨", "💪", "😎", "🌹", "💔", "👎", "🙌", "🤗"
            ]
            delegate: Rectangle {
                required property string modelData
                width: GridView.view.cellWidth
                height: 44
                radius: 8
                color: emojiMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.pixelSize: 22
                }
                MouseArea {
                    id: emojiMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.emojiSelected(modelData)
                }
            }
        }

        // Sticker search (Enter searches; clearing the field goes back to the pack)
        ZTextField {
            id: stickerSearch
            visible: root.tab === 1
            Layout.fillWidth: true
            placeholderText: I18n.t("Tìm nhãn dán (ví dụ: chào, cảm ơn, haha)", "Search stickers (e.g. hello, thanks)")
            onAccepted: ZaloClient.stickers.search(text)
            onTextChanged: if (text.length === 0 && ZaloClient.stickers.searchQuery.length > 0) ZaloClient.stickers.search("")
        }

        Item {
            visible: root.tab === 1
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                id: stickerGrid
                anchors.fill: parent
                clip: true
                cellWidth: Math.floor(width / Math.max(3, Math.floor(width / 84)))
                cellHeight: cellWidth
                model: ZaloClient.stickers.stickers
                delegate: Rectangle {
                    id: cell
                    required property var modelData
                    width: GridView.view.cellWidth
                    height: GridView.view.cellHeight
                    radius: 8
                    color: stickerMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZSticker {
                        anchors.fill: parent
                        anchors.margins: 6
                        info: cell.modelData
                        playing: stickerMouse.containsMouse
                    }
                    MouseArea {
                        id: stickerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.stickerSelected(cell.modelData)
                    }
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                visible: ZaloClient.stickers.loading && ZaloClient.stickers.stickers.length === 0
                running: visible
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: !ZaloClient.stickers.loading && ZaloClient.stickers.stickers.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: !ZaloClient.isLoggedIn ? I18n.t("Cần kết nối tới Zalo để dùng nhãn dán.", "Connect to Zalo to use stickers.")
                      : ZaloClient.stickers.searchQuery.length > 0 ? I18n.t("Không tìm thấy nhãn dán phù hợp.", "No matching stickers.")
                      : ZaloClient.stickers.categories.length === 0 ? I18n.t("Nhập từ khóa rồi nhấn Enter để tìm nhãn dán.", "Type a keyword and press Enter.")
                      : I18n.t("Bộ nhãn dán này chưa có gì.", "This pack is empty.")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }
        }

        // Packs: recent first, then the account's sticker packs (Zalo PC sticker bar).
        ListView {
            id: packBar
            visible: root.tab === 1 && ZaloClient.stickers.categories.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            orientation: ListView.Horizontal
            spacing: 4
            clip: true
            model: ZaloClient.stickers.categories
            delegate: Rectangle {
                id: pack
                required property var modelData
                readonly property bool active: ZaloClient.stickers.searchQuery.length === 0 && ZaloClient.stickers.currentCategory === pack.modelData.id
                width: 40
                height: 40
                radius: 8
                color: pack.active ? ThemeManager.selectedColor : (packMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                ZIcon {
                    visible: pack.modelData.id === -1
                    anchors.centerIn: parent
                    name: "clock"
                    size: 20
                    color: pack.active ? Theme.zaloBlue : ThemeManager.iconColor
                }
                Image {
                    visible: pack.modelData.id !== -1
                    anchors.fill: parent
                    anchors.margins: 5
                    source: pack.modelData.iconUrl || ""
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 64
                }
                MouseArea {
                    id: packMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        stickerSearch.text = "";
                        ZaloClient.stickers.selectCategory(pack.modelData.id);
                    }
                }
                ToolTip.visible: packMouse.containsMouse && (pack.modelData.name || pack.modelData.id === -1)
                ToolTip.text: pack.modelData.id === -1 ? I18n.t("Gần đây", "Recent") : (pack.modelData.name || "")
            }
        }
    }
}
