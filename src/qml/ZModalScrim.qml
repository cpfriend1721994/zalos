import QtQuick

Rectangle {
    id: root

    property bool shown: true
    property real shade: 0.5
    property int fadeDuration: 0
    signal dismissed()

    anchors.fill: parent
    color: "#000000"
    opacity: root.shown ? root.shade : 0
    Behavior on opacity {
        enabled: root.fadeDuration > 0
        NumberAnimation { duration: root.fadeDuration }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.dismissed()
    }
}
