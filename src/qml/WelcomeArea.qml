import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import Zalos

Rectangle {
    id: root
    color: ThemeManager.chatBackgroundColor

    property int currentSlide: 0
    readonly property int totalSlides: 5

    // Chevrons are only shown when there is room for them beside the content
    readonly property bool showChevrons: width >= 480
    readonly property int sideMargin: showChevrons ? 80 : 16

    function nextSlide() {
        currentSlide = (currentSlide + 1) % totalSlides;
    }

    function prevSlide() {
        currentSlide = (currentSlide - 1 + totalSlides) % totalSlides;
    }

    // Auto-advance timer every 6 seconds
    Timer {
        interval: 6000
        running: root.visible
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

            // Welcome Header
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: I18n.tr("welcome_title")
                    font.family: Theme.fontFamily
                    font.pixelSize: 24
                    font.bold: true
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
                    font.pixelSize: 13
                    color: ThemeManager.secondaryTextColor
                }
            }

            // Center illustration: drawn at 320x180 and scaled down to fit narrow panes
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

                    // Slide 0: Text-from-image graphic (photo with scan frame and document card)
                    Item {
                        visible: root.currentSlide === 0
                        anchors.fill: parent

                        // Photo Preview Card
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

                            // Mountains inside photo
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

                            // Sun
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

                        // Scan frame around photo
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

                        // Overlapping document card with "Copy" button
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
                                    text: I18n.t("Nội dung", "Content")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    font.bold: true
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
                                        text: I18n.t("Sao chép", "Copy")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 10
                                        font.bold: true
                                        color: "#FFFFFF"
                                    }
                                }
                            }
                        }

                        // Scan Icon Pill
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
                                name: "snip"
                                size: 22
                                color: Theme.zaloBlue
                            }
                        }
                    }

                    // Slide 1: Cloud sync (cloud, folder, floating cards)
                    Item {
                        visible: root.currentSlide === 1
                        anchors.fill: parent

                        // Floating purple media card
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

                        // Floating yellow chat bubble
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

                        // Blue cloud with down arrow
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

                        // Yellow file folder
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

                    // Slide 2: Fast file sharing (1 GB)
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
                                Text { Layout.alignment: Qt.AlignHCenter; text: "1 GB"; font.family: Theme.fontFamily; font.bold: true; font.pixelSize: 18; color: "#D97706" }
                            }
                        }
                    }

                    // Slide 3: Quick messages
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
                                    font.bold: true
                                    font.pixelSize: 14
                                    color: Theme.zaloBlue
                                }
                            }
                        }
                    }

                    // Slide 4: Task management
                    Item {
                        visible: root.currentSlide === 4
                        anchors.fill: parent

                        Rectangle {
                            anchors.centerIn: parent
                            width: 160
                            height: 120
                            radius: 16
                            color: (ThemeManager.cardBackgroundColor)
                            border.color: Theme.onlineGreen
                            border.width: 1.5

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: parent.width - 16
                                spacing: 8
                                ZIcon { Layout.alignment: Qt.AlignHCenter; name: "todo"; size: 36; color: Theme.onlineGreen }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    text: I18n.t("Giao việc", "To-do")
                                    font.family: Theme.fontFamily
                                    font.bold: true
                                    font.pixelSize: 14
                                    color: Theme.onlineGreen
                                }
                            }
                        }
                    }
                }
            }

            // Slide Title
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: {
                    switch (root.currentSlide) {
                        case 0: return I18n.t("Sao chép chữ từ ảnh nhanh chóng", "Copy text from images instantly");
                        case 1: return I18n.t("Trải nghiệm xuyên suốt", "A seamless experience");
                        case 2: return I18n.t("Gửi file nhanh chóng", "Send files fast");
                        case 3: return I18n.t("Nhắn tin nhiều hơn, soạn thảo ít hơn", "Message more, type less");
                        case 4: return I18n.t("Quản lý công việc hiệu quả", "Manage tasks efficiently");
                        default: return "";
                    }
                }
                font.family: Theme.fontFamily
                font.pixelSize: 16
                font.bold: true
                color: Theme.zaloBlue
            }

            // Slide Description
            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: {
                    switch (root.currentSlide) {
                        case 0: return I18n.t("Giờ đây bạn có thể trích xuất văn bản trực tiếp từ hình ảnh",
                                              "You can now extract text directly from images");
                        case 1: return I18n.t("Kết nối và giải quyết công việc trên mọi thiết bị với dữ liệu luôn được đồng bộ",
                                              "Stay connected and get work done on every device with your data always in sync");
                        case 2: return I18n.t("Chia sẻ tập tin, tài liệu dung lượng lên đến 1 GB mà không lo giảm chất lượng",
                                              "Share files and documents up to 1 GB without losing quality");
                        case 3: return I18n.t("Lưu và gửi nhanh các tin nhắn thường dùng với tính năng Tin nhắn nhanh",
                                              "Save and quickly send frequently used messages with Quick Messages");
                        case 4: return I18n.t("Giao việc, nhắc nhở và theo dõi tiến độ công việc ngay trong cửa sổ trò chuyện",
                                              "Assign tasks, set reminders and track progress right inside your chats");
                        default: return "";
                    }
                }
                font.family: Theme.fontFamily
                font.pixelSize: 13
                color: ThemeManager.secondaryTextColor
            }

            // Pagination Dots Indicator
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
        }
    }

    // Chevron navigation buttons (declared after the Flickable so they stay on top and receive input)
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
