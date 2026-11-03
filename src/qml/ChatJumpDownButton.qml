import QtQuick
import QtQuick.Controls.Basic
import Zalos

// The one "jump to latest" button over the message list (it replaced the older scrollBottomFab): 38 px, surface
// colour, accent arrow, pop-in animation, and a red badge counting new incoming messages below (tdesktop-style
// corner button). ChatArea owns the logic; this file only the look.
Rectangle {
    id: root
    property int newCount: 0
    property bool shown: false
    signal clicked()

    width: 38
    height: 38
    radius: 19
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1
    visible: opacity > 0
    opacity: shown ? 1.0 : 0.0
    scale: shown ? (mouse.pressed ? 0.92 : (mouse.containsMouse ? 1.08 : 1.0)) : 0.6
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }

    ZIcon { anchors.centerIn: parent; name: "arrow_down"; size: 16; color: Theme.zaloBlue }

    Rectangle {
        visible: root.newCount > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: -8
        height: 18
        width: Math.max(18, countText.implicitWidth + 10)
        radius: 9
        color: Theme.badgeRed
        Text {
            id: countText
            anchors.centerIn: parent
            text: root.newCount > 99 ? "99+" : String(root.newCount)
            color: "#FFFFFF"
            font.family: Theme.fontFamily
            font.pixelSize: 11
            font.bold: true
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        ToolTip.visible: containsMouse
        ToolTip.delay: 400
        ToolTip.text: I18n.t("Tin nhắn mới nhất", "Latest messages")
        onClicked: root.clicked()
    }
}
