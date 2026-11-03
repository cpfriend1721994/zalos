import QtQuick
import Zalos

// Zalo PC toggle (.z-toggle): 40×20 track radius 10, ON #0068FF, OFF --background-inactiveness-subtle,
// white knob; disabled tokens; pointer cursor. Emits toggled(checked) on user clicks only.
Item {
    id: root
    property bool checked: false
    signal toggled(bool checked)

    implicitWidth: 40
    implicitHeight: 24
    opacity: enabled ? 1.0 : 0.9

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: 40
        height: 20
        radius: 10
        color: root.enabled ? (root.checked ? Theme.accent : Theme.inactiveSubtle)
                            : (root.checked ? Theme.activeDisabled : Theme.inactiveDisabled)
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            width: 16
            height: 16
            radius: 8
            y: 2
            x: root.checked ? track.width - width - 2 : 2
            color: "#FFFFFF"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.08)
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
        // Controlled: never assign `checked` here (it would break the owner's binding); the owner updates
        // its source of truth in onToggled and the binding reflects it.
        onClicked: root.toggled(!root.checked)
    }
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked
}
