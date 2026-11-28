pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Window
import Zalos

Rectangle {
    id: root
    objectName: "welcomeArea"
    readonly property bool windowShowing: UiWindow.showing
    color: ThemeManager.chatBackgroundColor

    property int currentSlide: 0
    readonly property int totalSlides: 6

    readonly property bool showChevrons: width >= 480
    readonly property int sideMargin: showChevrons ? 80 : 16

    function keyText(command: string): string {
        const keys = ZaloClient.shortcuts.primaryText(command);
        return keys.length > 0 ? keys : "—";
    }

    function keyParts(command: string): var {
        const keys = ZaloClient.shortcuts.primaryText(command);
        return keys.length > 0 ? keys.split("+") : [];
    }

    function nextSlide() {
        currentSlide = (currentSlide + 1) % totalSlides;
    }

    function prevSlide() {
        currentSlide = (currentSlide - 1 + totalSlides) % totalSlides;
    }

    Timer {
        interval: 6000
        running: root.visible && root.windowShowing
        repeat: true
        onTriggered: root.nextSlide()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, mainCol.implicitHeight + 40)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        interactive: contentHeight > height

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        ColumnLayout {
            id: mainCol
            width: Math.max(0, Math.min(flick.width - 2 * root.sideMargin, 680))
            x: Math.floor((flick.width - width) / 2)
            y: Math.max(20, Math.floor((flick.contentHeight - implicitHeight) / 2))
            spacing: 28

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: I18n.tr("welcome_title")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHero
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 520
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: I18n.tr("welcome_desc")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: ThemeManager.secondaryTextColor
                }
            }

            Item {
                id: illustrationFrame
                readonly property real scaleFactor: Math.min(1, mainCol.width / 320)
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 320 * scaleFactor
                Layout.preferredHeight: 180 * scaleFactor

                Item {
                    id: illustration
                    width: 320
                    height: 180
                    scale: illustrationFrame.scaleFactor
                    transformOrigin: Item.TopLeft

                    Item {
                        visible: root.currentSlide === 0
                        anchors.fill: parent

                        Rectangle {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: -28
                            width: 170
                            height: 120
                            radius: 12
                            clip: true
                            color: (ThemeManager.infoBackgroundColor)
                            border.color: Theme.zaloBlue
                            border.width: 2

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 8
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                width: 70
                                height: 50
                                radius: 8
                                color: (ThemeManager.accentTonalColor)
                                rotation: 45
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 16
                                width: 90
                                height: 60
                                radius: 8
                                color: (ThemeManager.accentTonalColor)
                                rotation: 45
                            }

                            Rectangle {
                                width: 20
                                height: 20
                                radius: 10
                                color: "#FBBF24"
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                anchors.left: parent.left
                                anchors.leftMargin: 20
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: -28
                            width: 194
                            height: 144
                            color: "transparent"
                            border.color: Theme.zaloBlue
                            border.width: 2
                            radius: 16
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.verticalCenterOffset: -10
                            width: 110
                            height: 130
                            radius: 10
                            color: "#FFFFFF"
                            border.color: ThemeManager.borderColor
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: I18n.t("Cuộc gọi HD", "HD Calls")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Font.DemiBold
                                    color: "#1E293B"
                                }

                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: "#93C5FD" }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: "#93C5FD" }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: "#93C5FD" }
                                Rectangle { Layout.preferredWidth: 45; Layout.preferredHeight: 3; radius: 1.5; color: "#93C5FD" }

                                Item { Layout.fillHeight: true }

                                Rectangle {
                                    Layout.alignment: Qt.AlignRight
                                    Layout.preferredWidth: copyLabel.implicitWidth + 16
                                    Layout.preferredHeight: 24
                                    radius: 6
                                    color: Theme.zaloBlue

                                    Text {
                                        id: copyLabel
                                        anchors.centerIn: parent
                                        text: I18n.t("Gọi ngay", "Call now")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontTiny
                                        font.weight: Font.DemiBold
                                        color: "#FFFFFF"
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 44
                            height: 44
                            radius: 22
                            color: (ThemeManager.infoBackgroundColor)
                            border.color: Theme.zaloBlue
                            border.width: 2

                            ZIcon {
                                anchors.centerIn: parent
                                name: "video"
                                size: 22
                                color: Theme.zaloBlue
                            }
                        }
                    }

                    Item {
                        visible: root.currentSlide === 1
                        anchors.fill: parent

                        Rectangle {
                            anchors.top: parent.top
                            anchors.topMargin: 10
                            anchors.right: parent.right
                            anchors.rightMargin: 40
                            width: 60
                            height: 40
                            radius: 8
                            color: "#A855F7"

                            ZIcon {
                                anchors.centerIn: parent
                                name: "image"
                                size: 20
                                color: "#FFFFFF"
                            }
                        }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.topMargin: 20
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            width: 70
                            height: 34
                            radius: 8
                            color: "#FCD34D"
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: -30
                            width: 140
                            height: 90
                            radius: 45
                            color: Theme.zaloBlue

                            ZIcon {
                                anchors.centerIn: parent
                                name: "chevron_down"
                                size: 28
                                color: "#FFFFFF"
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 30
                            anchors.verticalCenterOffset: 20
                            width: 110
                            height: 75
                            radius: 8
                            color: "#FBBF24"

                            Rectangle {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                width: 40
                                height: 12
                                radius: 4
                                color: "#F59E0B"
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: 90
                                height: 50
                                radius: 6
                                color: "#FFFFFF"
                            }
                        }
                    }

                    Item {
                        visible: root.currentSlide === 2
                        anchors.fill: parent

                        Rectangle {
                            anchors.centerIn: parent
                            width: 150
                            height: 120
                            radius: 16
                            color: (ThemeManager.warningBackgroundColor)
                            border.color: "#F59E0B"
                            border.width: 1.5

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 8
                                ZIcon { Layout.alignment: Qt.AlignHCenter; name: "clip"; size: 36; color: "#F59E0B" }
                                Text { Layout.alignment: Qt.AlignHCenter; text: "1 GB"; font.family: Theme.fontFamily; font.weight: Font.DemiBold; font.pixelSize: Theme.fontTitleLarge; color: "#D97706" }
                            }
                        }
                    }

                    Item {
                        visible: root.currentSlide === 3
                        anchors.fill: parent

                        Rectangle {
                            anchors.centerIn: parent
                            width: 160
                            height: 120
                            radius: 16
                            color: (ThemeManager.infoBackgroundColor)
                            border.color: Theme.zaloBlue
                            border.width: 1.5

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: parent.width - 16
                                spacing: 8
                                ZIcon { Layout.alignment: Qt.AlignHCenter; name: "chat"; size: 36; color: Theme.zaloBlue }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    text: I18n.t("⚡ Gửi tức thì", "⚡ Instant send")
                                    font.family: Theme.fontFamily
                                    font.weight: Font.DemiBold
                                    font.pixelSize: Theme.fontBodyLarge
                                    color: Theme.zaloBlue
                                }
                            }
                        }
                    }

                    Item {
                        visible: root.currentSlide === 4
                        anchors.fill: parent

                        Rectangle {
                            anchors.centerIn: parent
                            width: 180
                            height: 120
                            radius: 16
                            color: (ThemeManager.infoBackgroundColor)
                            border.color: Theme.zaloBlue
                            border.width: 1.5

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 18
                                ZIcon { name: "eye_off"; size: 34; color: Theme.zaloBlue }
                                ZIcon { name: "lock"; size: 34; color: Theme.zaloBlue }
                                ZIcon { name: "shield"; size: 34; color: Theme.zaloBlue }
                            }
                        }
                    }

                    Item {
                        visible: root.currentSlide === 5
                        anchors.fill: parent

                        Rectangle {
                            anchors.centerIn: parent
                            width: 200
                            height: 120
                            radius: 16
                            color: (ThemeManager.infoBackgroundColor)
                            border.color: Theme.zaloBlue
                            border.width: 1.5

                            Row {
                                anchors.centerIn: parent
                                spacing: 6

                                Repeater {
                                    model: root.keyParts("chat.search")
                                    delegate: Rectangle {
                                        id: keyCap
                                        required property string modelData
                                        width: Math.max(44, keyCapText.implicitWidth + 22)
                                        height: 44
                                        radius: 8
                                        color: ThemeManager.surfaceColor
                                        border.color: Theme.zaloBlue
                                        border.width: 1.5

                                        Text {
                                            id: keyCapText
                                            anchors.fill: parent
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            text: keyCap.modelData
                                            font.family: Theme.fontFamily
                                            font.weight: Font.DemiBold
                                            font.pixelSize: Theme.fontTitle
                                            color: Theme.zaloBlue
                                        }
                                    }
                                }
                            }
                        }
                    }

                }
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: {
                    switch (root.currentSlide) {
                        case 0: return I18n.t("Gọi thoại và video trực tiếp", "Direct voice and video calls");
                        case 1: return I18n.t("Đồng bộ cùng điện thoại", "In sync with your phone");
                        case 2: return I18n.t("Gửi file nhanh chóng", "Send files fast");
                        case 3: return I18n.t("Nhắn tin nhiều hơn, soạn thảo ít hơn", "Message more, type less");
                        case 4: return I18n.t("Riêng tư theo cách của bạn", "Privacy your way");
                        case 5: return I18n.t("Thao tác nhanh bằng bàn phím", "Faster with the keyboard");
                        default: return "";
                    }
                }
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: Theme.zaloBlue
            }

            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: {
                    switch (root.currentSlide) {
                        case 0: return I18n.t("Gọi thoại, gọi video 1-1 và chia sẻ màn hình ngay trên máy tính; cuộc gọi nhóm đang trong giai đoạn thử nghiệm",
                                              "Voice calls, 1-on-1 video calls and screen sharing right from your desktop; group calls are in beta");
                        case 1: return I18n.t("Tin nhắn mới, trạng thái đã đọc, ghim và thẻ phân loại luôn khớp với điện thoại. Tin nhắn cũ có thể chuyển sang máy tính trong Cài đặt › Đồng bộ tin nhắn",
                                              "New messages, read status, pins and labels stay in step with your phone. Older history can be moved over in Settings › Sync messages");
                        case 2: return I18n.t("Chia sẻ tập tin, tài liệu dung lượng lên đến 1 GB — kéo thả vào khung chat để đính kèm nhanh",
                                              "Share files and documents up to 1 GB — drag them into the chat to attach quickly");
                        case 3: return I18n.t("Lưu các tin nhắn thường dùng, rồi gõ / trong ô nhập để gửi lại chỉ trong một chạm",
                                              "Save the messages you send often, then type / in the composer to send one in a tap");
                        case 4: return I18n.t("Bấm %1 để chống nhìn trộm, đặt mã khóa cho Zalos và ẩn trò chuyện bằng mã PIN",
                                              "Press %1 for anti-peeking, lock Zalos with a passcode and hide chats behind a PIN")
                                       .arg(root.keyText("privacy.antiPeeking"));
                        case 5: return I18n.t("%1 để tìm kiếm, %2 để sang hội thoại tiếp theo, %3 để xem mọi phím tắt",
                                              "%1 to search, %2 for the next chat, %3 for every shortcut")
                                       .arg(root.keyText("chat.search")).arg(root.keyText("chat.next")).arg(root.keyText("app.shortcuts"));
                        default: return "";
                    }
                }
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: ThemeManager.secondaryTextColor
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8

                Repeater {
                    model: root.totalSlides
                    Rectangle {
                        required property int index
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 3.5
                        color: index === root.currentSlide ? Theme.zaloBlue : (ThemeManager.dividerColor)

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentSlide = parent.index
                        }
                    }
                }
            }

            GridLayout {
                objectName: "welcomeDiscover"
                Layout.alignment: Qt.AlignHCenter
                columns: mainCol.width >= 540 ? 3 : 1
                columnSpacing: 8
                rowSpacing: 8

                ZButton {
                    objectName: "welcomeShortcuts"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 200
                    variant: "tonal"
                    iconName: "info"
                    text: I18n.t("Xem phím tắt", "Keyboard shortcuts")
                    onClicked: UiWindow.shortcutsRequested()
                }
                ZButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 200
                    variant: "tonal"
                    iconName: "refresh"
                    text: I18n.t("Đồng bộ từ điện thoại", "Sync from your phone")
                    onClicked: ZaloClient.openSettings("sync")
                }
                ZButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 200
                    variant: "tonal"
                    iconName: "palette"
                    text: I18n.t("Tùy chỉnh giao diện", "Customize appearance")
                    onClicked: ZaloClient.openSettings("appearance")
                }
            }
        }
    }

    Item {
        visible: root.showChevrons
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        width: 44
        height: 44

        Rectangle {
            anchors.fill: parent
            radius: 22
            color: prevMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

            ZIcon {
                anchors.centerIn: parent
                name: "chevron_left"
                size: 24
                color: ThemeManager.secondaryTextColor
            }
        }

        MouseArea {
            id: prevMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.prevSlide()
            ToolTip.visible: containsMouse
            ToolTip.text: I18n.t("Trước", "Previous")
        }
    }

    Item {
        visible: root.showChevrons
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        width: 44
        height: 44

        Rectangle {
            anchors.fill: parent
            radius: 22
            color: nextMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

            ZIcon {
                anchors.centerIn: parent
                name: "chevron_right"
                size: 24
                color: ThemeManager.secondaryTextColor
            }
        }

        MouseArea {
            id: nextMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.nextSlide()
            ToolTip.visible: containsMouse
            ToolTip.text: I18n.t("Tiếp", "Next")
        }
    }
}
