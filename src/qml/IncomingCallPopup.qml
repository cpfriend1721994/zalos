pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    width: parent ? Math.min(344, parent.width - 32) : 344
    implicitHeight: callLayout.implicitHeight + 32
    height: implicitHeight
    radius: 16
    color: ThemeManager.surfaceColor
    border.color: Theme.zaloBlue
    border.width: 2
    visible: ZaloClient.isIncomingCallActive

    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        radius: 20
        color: "#20000000"
        z: -1
    }

    ColumnLayout {
        id: callLayout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ZAvatar {
                size: 48
                name: ZaloClient.incomingCallerName
                avatarUrl: ""

                SequentialAnimation on scale {
                    running: root.visible
                    loops: Animation.Infinite
                    PropertyAnimation { to: 1.08; duration: 600; easing.type: Easing.InOutQuad }
                    PropertyAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Cuộc gọi đến", "Incoming call")
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: Theme.zaloBlue
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: ZaloClient.incomingCallerName
                    font.family: Theme.fontFamily
                    font.bold: true
                    font.pixelSize: 15
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Đang đổ chuông...", "Ringing...")
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                id: declineBtn
                Layout.fillWidth: true
                implicitHeight: 38
                background: Rectangle {
                    radius: 19
                    color: declineBtn.hovered ? "#D32F2F" : "#FF3B30"
                }
                contentItem: Item {
                    implicitWidth: declineRow.implicitWidth
                    implicitHeight: declineRow.implicitHeight
                    RowLayout {
                        id: declineRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: "✕"; color: "white"; font.bold: true }
                        Text { text: I18n.t("Từ chối", "Decline"); color: "white"; font.family: Theme.fontFamily; font.bold: true; font.pixelSize: 12 }
                    }
                }
                onClicked: ZaloClient.declineCall()
            }

            Button {
                id: answerBtn
                Layout.fillWidth: true
                implicitHeight: 38
                background: Rectangle {
                    radius: 19
                    color: answerBtn.hovered ? "#2E7D32" : "#34A853"
                }
                contentItem: Item {
                    implicitWidth: answerRow.implicitWidth
                    implicitHeight: answerRow.implicitHeight
                    RowLayout {
                        id: answerRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: "✓"; color: "white"; font.bold: true }
                        Text { text: I18n.t("Trả lời", "Answer"); color: "white"; font.family: Theme.fontFamily; font.bold: true; font.pixelSize: 12 }
                    }
                }
                onClicked: ZaloClient.answerCall()
            }
        }
    }
}
