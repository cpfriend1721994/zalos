import QtQuick
import QtQuick.Layouts
import Zalos

// One row of a Zalo-style popup menu (message and conversation context menus).
Rectangle {
    id: row
    property string icon: ""
    property string label: ""
    property bool danger: false
    signal triggered()

    Layout.fillWidth: true
    Layout.preferredHeight: 34
    implicitWidth: 200
    radius: 6
    color: rowMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
    Behavior on color { ColorAnimation { duration: 120 } }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 10

        ZIcon {
            visible: row.icon.length > 0
            name: row.icon
            size: 16
            color: row.danger ? Theme.badgeRed : ThemeManager.iconColor
        }
        Text {
            Layout.fillWidth: true
            text: row.label
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: row.danger ? Theme.badgeRed : ThemeManager.textColor
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.triggered()
    }
}
