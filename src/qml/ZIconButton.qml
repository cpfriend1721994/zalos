import QtQuick
import QtQuick.Controls.Basic
import Zalos

Rectangle {
    id: root

    property string iconName: ""
    property int iconSize: 18
    property color iconColor: ThemeManager.textColor
    property string tooltip: ""
    property int tooltipDelay: 350
    property bool highlighted: false
    property bool pressTint: true
    property real pressedScale: 0.92
    readonly property bool pressed: mouse.pressed
    readonly property bool hovered: mouse.containsMouse

    signal clicked()
    signal pressStarted()

    width: 32
    height: 32
    radius: 16
    color: {
        if (root.pressTint && mouse.pressed) return ThemeManager.selectedColor;
        return root.highlighted || mouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor;
    }
    scale: mouse.pressed ? root.pressedScale : 1.0
    Behavior on scale { NumberAnimation { duration: 100 } }

    ZIcon {
        anchors.centerIn: parent
        name: root.iconName
        size: root.iconSize
        color: root.iconColor
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        ToolTip.visible: containsMouse
        ToolTip.delay: root.tooltipDelay
        ToolTip.text: root.tooltip
        onPressed: root.pressStarted()
        onClicked: root.clicked()
    }
}
