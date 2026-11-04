import QtQuick
import Zalos

Item {
    id: root
    property bool checked: false
    property bool round: false
    property string text: ""
    signal toggled(bool checked)

    implicitWidth: 24 + (text.length > 0 ? 8 + label.implicitWidth : 0)
    implicitHeight: 24

    Rectangle {
        id: box
        anchors.verticalCenter: parent.verticalCenter
        x: 4
        width: 16
        height: 16
        radius: root.round ? 8 : (root.checked ? 3 : 2.5)
        color: root.checked ? (root.enabled ? Theme.accent : Theme.activeDisabled) : ThemeManager.surfaceColor
        border.width: root.checked ? 0 : 1.5
        border.color: hover.containsMouse && root.enabled ? Theme.activeness : Theme.inactiveSubtle
        ZIcon {
            anchors.centerIn: parent
            visible: root.checked
            name: "check"
            size: 14
            color: Theme.textOnColor
        }
    }
    Text {
        id: label
        visible: root.text.length > 0
        anchors.left: box.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        font.family: Theme.fontFamily
        font.pixelSize: 13
        color: root.enabled ? ThemeManager.textColor : ThemeManager.secondaryTextColor
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        onClicked: root.toggled(!root.checked)
    }
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked
}
