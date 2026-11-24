import QtQuick
import QtQuick.Controls.Basic
import Zalos

Rectangle {
    id: root
    property int count: 0
    property string emoji: ""
    property bool shown: false
    signal clicked()

    width: content.implicitWidth + 20
    height: 38
    radius: 19
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    visible: opacity > 0
    opacity: shown ? 1.0 : 0.0
    scale: shown ? (mouse.pressed ? 0.92 : (mouse.containsMouse ? 1.06 : 1.0)) : 0.6
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 8
        ZIcon { anchors.verticalCenter: parent.verticalCenter; name: "arrow_up"; size: 16; color: Theme.zaloBlue }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 18
            color: ThemeManager.dividerColor
        }
        Item {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            Text {
                anchors.centerIn: parent
                text: root.emoji.length > 0 ? root.emoji : "❤️"
                textFormat: Text.PlainText
                font.family: Theme.fontFamily
                font.pixelSize: 36
                scale: parent.width / 36
                smooth: true
                antialiasing: true
            }
        }
    }

    Rectangle {
        visible: root.count > 1
        anchors.right: parent.right
        anchors.rightMargin: -4
        anchors.bottom: parent.top
        anchors.bottomMargin: -10
        height: 18
        width: Math.max(18, countText.implicitWidth + 10)
        radius: 9
        color: Theme.badgeRed
        Text {
            id: countText
            anchors.centerIn: parent
            text: root.count > 99 ? "99+" : String(root.count)
            color: "#FFFFFF"
            font.family: Theme.fontFamily
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        ToolTip.visible: containsMouse
        ToolTip.delay: 400
        ToolTip.text: I18n.t("Tới tin nhắn có cảm xúc chưa xem", "Go to unseen reactions")
        onClicked: root.clicked()
    }
}
