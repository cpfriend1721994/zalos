import QtQuick
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    color: Qt.rgba(0, 0, 0, 0.45)

    MouseArea { anchors.fill: parent }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        implicitHeight: col.implicitHeight + 48
        height: implicitHeight
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 56
                height: 56
                radius: 28
                color: Theme.zaloBlueLight
                ZIcon { anchors.centerIn: parent; name: "refresh"; size: 28; color: Theme.zaloBlueTonalText }
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: I18n.t("Đồng bộ tin nhắn từ điện thoại?", "Sync your messages from your phone?")
                color: ThemeManager.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 17
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: I18n.t("Máy tính này chưa có lịch sử trò chuyện cũ của bạn. Đồng bộ để chuyển tin nhắn từ Zalo trên điện thoại sang đây; điện thoại sẽ hỏi bạn xác nhận và tin nhắn được lưu trên máy này.",
                             "This computer doesn't have your older chats yet. Sync to transfer them from Zalo on your phone; your phone asks you to confirm and the messages are kept on this computer.")
                color: ThemeManager.secondaryTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 13
                lineHeight: 1.2
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10
                ZButton {
                    Layout.fillWidth: true
                    variant: "secondary"
                    text: I18n.t("Để sau", "Later")
                    onClicked: ZaloClient.dismissSyncPrompt(false)
                }
                ZButton {
                    Layout.fillWidth: true
                    text: I18n.t("Đồng bộ ngay", "Sync now")
                    onClicked: ZaloClient.acceptSyncPrompt()
                }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: I18n.t("Không hỏi lại", "Don't ask again")
                color: ThemeManager.secondaryTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.underline: neverMouse.containsMouse
                MouseArea {
                    id: neverMouse
                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ZaloClient.dismissSyncPrompt(true)
                }
            }
        }
    }
}
