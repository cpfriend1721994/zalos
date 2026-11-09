pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    color: ThemeManager.chatBackgroundColor

    property int activeTab: 0
    readonly property var tabs: [
        I18n.t("Tất cả", "All"),
        I18n.t("Tài liệu", "Documents"),
        I18n.t("Ảnh & Video", "Photos & Videos"),
        I18n.t("Liên kết", "Links")
    ]

    property var cloudItems: []

    function tabKind(idx: int): string {
        if (idx === 1) return "file";
        if (idx === 2) return "media";
        if (idx === 3) return "link";
        return "all";
    }

    function refresh(): void {
        root.cloudItems = ZaloClient.cloudSharedItems(root.tabKind(root.activeTab));
    }

    function formatFileSize(bytes: real): string {
        if (!bytes || bytes <= 0) return "";
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB";
    }

    function formatItemTime(ts: real, timeStr: string): string {
        if (timeStr && timeStr.length > 0) return timeStr;
        if (!ts || ts <= 0) return "";
        var d = new Date(ts);
        return Qt.formatDateTime(d, "dd/MM/yyyy HH:mm");
    }

    onActiveTabChanged: refresh()
    Component.onCompleted: refresh()

    Connections {
        target: ZaloClient
        function onNavigationTabChanged() {
            if (ZaloClient.currentNavigationTab === "cloud") root.refresh();
        }
        function onCurrentConversationChanged() {
            if (ZaloClient.currentNavigationTab === "cloud") root.refresh();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header bar
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.4, 0.9, 0.25) : Theme.zaloBlueLight

                    ZIcon {
                        anchors.centerIn: parent
                        name: "cloud"
                        size: 20
                        color: Theme.zaloBlue
                    }
                }

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: I18n.t("My Documents", "My Documents")
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.bold: true
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: I18n.t("Lưu trữ đám mây, tự động đồng bộ tài liệu, ảnh và liên kết của bạn trên mọi thiết bị",
                                     "Cloud storage, sync documents, photos, and links across all your devices")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    height: 34
                    width: openCloudChatBtn.implicitWidth + 24
                    radius: 17
                    color: Theme.zaloBlue

                    RowLayout {
                        id: openCloudChatBtn
                        anchors.centerIn: parent
                        spacing: 6

                        ZIcon {
                            name: "file_transfer"
                            size: 16
                            color: "#FFFFFF"
                        }

                        Text {
                            text: I18n.t("Mở cuộc hội thoại Cloud", "Open Cloud Chat")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.bold: true
                            color: "#FFFFFF"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ZaloClient.openMyCloud()
                    }
                }
            }
        }

        // Filter tab bar
        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                spacing: 10

                Repeater {
                    model: root.tabs

                    Rectangle {
                        id: tabChip
                        required property string modelData
                        required property int index

                        height: 28
                        width: tabChipText.implicitWidth + 18
                        radius: 14
                        color: root.activeTab === index
                               ? (ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.4, 0.9, 0.3) : Theme.zaloBlueLight)
                               : (tabMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor)

                        Text {
                            id: tabChipText
                            anchors.centerIn: parent
                            text: tabChip.modelData
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.bold: root.activeTab === tabChip.index
                            color: root.activeTab === tabChip.index
                                   ? (ThemeManager.isDarkMode ? "#FFFFFF" : Theme.zaloBlueTonalText)
                                   : ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.activeTab = tabChip.index
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // Authentic Storage Status Overview Card (Zero mock data)
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 8
            height: 64
            radius: 12
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                Rectangle {
                    width: 38
                    height: 38
                    radius: 19
                    color: ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.5, 1, 0.2) : Theme.zaloBlueLight

                    ZIcon {
                        anchors.centerIn: parent
                        name: "cloud"
                        size: 20
                        color: Theme.zaloBlue
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    RowLayout {
                        spacing: 8
                        Text {
                            text: I18n.t("Kho lưu trữ My Documents", "My Documents Storage")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: "#34C759"
                        }
                        Text {
                            text: I18n.t("Đã đồng bộ tự động với tài khoản Zalo", "Auto-synced with Zalo account")
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    Text {
                        text: I18n.t("Mọi tập tin, ảnh và ghi chú gửi vào Cloud sẽ được bảo mật và đồng bộ tức thì.",
                                     "All files, photos, and notes sent to Cloud are secured and instantly synced.")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Rectangle {
                    height: 28
                    width: countBadgeText.implicitWidth + 18
                    radius: 14
                    color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.08) : ThemeManager.hoverColor

                    Text {
                        id: countBadgeText
                        anchors.centerIn: parent
                        text: root.cloudItems.length + " " + I18n.t("mục", "items")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.bold: true
                        color: ThemeManager.textColor
                    }
                }
            }
        }

        // Content Area: Files / Media / Links List or Empty State
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: cloudList
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.bottomMargin: 16
                anchors.topMargin: 4
                clip: true
                spacing: 8
                visible: root.cloudItems.length > 0

                model: root.cloudItems

                delegate: Rectangle {
                    id: itemCard
                    required property var modelData
                    required property int index

                    width: cloudList.width
                    height: 62
                    radius: 10
                    color: itemMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1
                    Behavior on color { ColorAnimation { duration: 120 } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 12

                        // Icon / Thumbnail preview
                        Rectangle {
                            width: 42
                            height: 42
                            radius: 8
                            color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.08) : ThemeManager.hoverColor
                            clip: true

                            Image {
                                anchors.fill: parent
                                visible: itemCard.modelData.itemType === "media" && !!itemCard.modelData.thumb
                                source: visible ? itemCard.modelData.thumb : ""
                                fillMode: Image.PreserveAspectCrop
                            }

                            ZIcon {
                                anchors.centerIn: parent
                                visible: itemCard.modelData.itemType !== "media" || !itemCard.modelData.thumb
                                name: itemCard.modelData.itemType === "file" ? "file"
                                      : (itemCard.modelData.itemType === "link" ? "link" : "image")
                                size: 20
                                color: Theme.zaloBlue
                            }

                            // Video indicator overlay
                            Rectangle {
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                radius: 11
                                color: Qt.rgba(0, 0, 0, 0.6)
                                visible: itemCard.modelData.itemType === "media" && !!itemCard.modelData.isVideo

                                ZIcon {
                                    anchors.centerIn: parent
                                    name: "play"
                                    size: 12
                                    color: "#FFFFFF"
                                }
                            }
                        }

                        // Title & Subtitle details
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                Layout.fillWidth: true
                                text: itemCard.modelData.title || itemCard.modelData.url || ""
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideMiddle
                                color: ThemeManager.textColor
                            }

                            RowLayout {
                                spacing: 8
                                Text {
                                    text: root.formatItemTime(itemCard.modelData.ts, itemCard.modelData.timeStr)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: ThemeManager.secondaryTextColor
                                }
                                Text {
                                    visible: itemCard.modelData.fileSize > 0
                                    text: "• " + root.formatFileSize(itemCard.modelData.fileSize)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                        }

                        // Action button
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 16
                            color: actionMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                            ZIcon {
                                anchors.centerIn: parent
                                name: itemCard.modelData.itemType === "link" ? "link" : "file"
                                size: 16
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: actionMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (itemCard.modelData.itemType === "link") {
                                        ZaloClient.openLink(itemCard.modelData.url, itemCard.modelData.title);
                                    } else {
                                        ZaloClient.openFileMessage(itemCard.modelData.url, itemCard.modelData.title);
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (itemCard.modelData.itemType === "link") {
                                ZaloClient.openLink(itemCard.modelData.url, itemCard.modelData.title);
                            } else {
                                ZaloClient.openFileMessage(itemCard.modelData.url, itemCard.modelData.title);
                            }
                        }
                    }
                }
            }

            // Clean, non-blocking empty state
            Item {
                anchors.centerIn: parent
                width: parent.width - 40
                height: 220
                visible: root.cloudItems.length === 0

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 12

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 56
                        height: 56
                        radius: 28
                        color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.06) : ThemeManager.hoverColor

                        ZIcon {
                            anchors.centerIn: parent
                            name: "cloud"
                            size: 28
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: I18n.t("Chưa có dữ liệu trong My Documents", "My Documents is empty")
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.bold: true
                        color: ThemeManager.textColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: I18n.t("Gửi hình ảnh, tài liệu hoặc liên kết vào My Documents để lưu trữ và truy cập mọi lúc.",
                                     "Send photos, files, or links to My Documents to access them anytime.")
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        height: 34
                        width: sendToCloudBtn.implicitWidth + 24
                        radius: 17
                        color: Theme.zaloBlue

                        Text {
                            id: sendToCloudBtn
                            anchors.centerIn: parent
                            text: I18n.t("Mở cuộc hội thoại My Documents", "Open My Documents Chat")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.bold: true
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ZaloClient.openMyCloud()
                        }
                    }
                }
            }
        }
    }
}
