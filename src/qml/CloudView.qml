import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    color: ThemeManager.chatBackgroundColor

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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

                ZIcon {
                    name: "cloud"
                    size: 22
                    color: Theme.zaloBlue
                }

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "Cloud của tôi"
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.bold: true
                        color: ThemeManager.textColor
                    }
                    Text {
                        text: "Lưu trữ tin nhắn, hình ảnh và tài liệu đồng bộ tức thì giữa các thiết bị"
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                spacing: 12

                Repeater {
                    model: ["Tất cả", "Tài liệu", "Ảnh & Video", "Liên kết", "Ghi chú"]

                    Rectangle {
                        height: 28
                        width: tagText.implicitWidth + 16
                        radius: 14
                        color: index === 0 ? Theme.zaloBlueLight : (ThemeManager.hoverColor)

                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: modelData
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.bold: index === 0
                            color: index === 0 ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        ListView {
            id: cloudList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            topMargin: 16
            bottomMargin: 16

            model: []

            Item {
                visible: cloudList.count === 0
                anchors.centerIn: parent
                width: parent.width - 40
                height: 200

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 12

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "☁️"
                        font.pixelSize: 40
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Cloud của bạn đang trống"
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.bold: true
                        color: ThemeManager.textColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Gửi tin nhắn, hình ảnh hoặc tài liệu vào Cloud để lưu trữ dữ liệu cá nhân"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                }
            }

            delegate: Rectangle {
                width: Math.min(cloudList.width - 40, 780)
                anchors.horizontalCenter: parent.horizontalCenter
                height: 64
                radius: 10
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12

                    ZIcon {
                        name: modelData.type === "file" ? "file" : (modelData.type === "link" ? "link" : "chat")
                        size: 22
                        color: Theme.zaloBlue
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: modelData.title
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }

                        Text {
                            text: modelData.size + " • " + modelData.date
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    Rectangle {
                        width: 32
                        height: 32
                        radius: width / 2
                        color: copyBtnMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

                        ZIcon {
                            anchors.centerIn: parent
                            name: "copy"
                            size: 16
                            color: ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            id: copyBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "Sao chép liên kết / nội dung"
                            onClicked: {
                                TrayController.showMessage("Đã sao chép", "Đã sao chép nội dung vào Clipboard.");
                            }
                        }
                    }
                }
            }
        }
    }
}
