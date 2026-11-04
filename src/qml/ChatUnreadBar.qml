import QtQuick
import Zalos

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
