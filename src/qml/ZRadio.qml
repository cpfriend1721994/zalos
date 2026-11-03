import QtQuick
import Zalos

// Zalo PC radio (.z-radio): 24px; unchecked ring in --background-inactiveness-subtle, checked ring and dot in
// --background-activeness; hover tints the ring. Emits clicked() — the owner decides the selection.
Item {
    id: root
    property bool checked: false
    signal clicked()

    implicitWidth: 24
    implicitHeight: 24

    Rectangle {
        anchors.centerIn: parent
        width: 18
        height: 18
        radius: 9
        color: "transparent"
        border.width: 1.5
        border.color: root.checked || (mouse.containsMouse && root.enabled) ? Theme.activeness : Theme.inactiveSubtle
        Rectangle {
            anchors.centerIn: parent
            visible: root.checked
            width: 10
            height: 10
            radius: 5
            color: Theme.activeness
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
    Accessible.role: Accessible.RadioButton
    Accessible.checked: checked
}
