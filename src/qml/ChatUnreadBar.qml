import QtQuick
import Zalos

// "Tin nhắn chưa đọc" separator drawn above the first unread message when a thread is opened
// (MessageModel::UnreadMarkerRole). Part of the ChatArea split.
Item {
    implicitHeight: visible ? 28 : 0

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 1
        color: Theme.zaloBlue
        opacity: 0.35
    }
    Rectangle {
        anchors.centerIn: parent
        width: label.implicitWidth + 24
        height: 22
        radius: 11
        color: Theme.zaloBlueLight
        Text {
            id: label
            anchors.centerIn: parent
            text: I18n.t("Tin nhắn chưa đọc", "Unread messages")
            color: Theme.zaloBlueTonalText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }
}
