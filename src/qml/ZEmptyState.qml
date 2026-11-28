import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

ColumnLayout {
    id: root

    property string iconName: ""
    property string title: ""
    property string subtitle: ""
    property string actionText: ""
    property bool busy: false

    signal actionTriggered()

    spacing: 8

    Rectangle {
        objectName: "emptyStateIcon"
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 64
        Layout.preferredHeight: 64
        Layout.bottomMargin: 4
        radius: 32
        visible: root.iconName.length > 0 && !root.busy
        color: Theme.zaloBlueLight

        ZIcon {
            anchors.centerIn: parent
            name: root.iconName
            size: 28
            color: Theme.zaloBlueTonalText
        }
    }

    BusyIndicator {
        Layout.alignment: Qt.AlignHCenter
        visible: root.busy
        running: visible && (root.Window.window ? (root.Window.window.visible && root.Window.window.visibility !== Window.Minimized) : true)
    }

    Text {
        Layout.fillWidth: true
        visible: root.title.length > 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: root.title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBodyLarge
        font.weight: Font.DemiBold
        color: ThemeManager.textColor
    }

    Text {
        Layout.fillWidth: true
        visible: root.subtitle.length > 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: root.subtitle
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        color: ThemeManager.secondaryTextColor
    }

    ZButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 4
        visible: root.actionText.length > 0
        variant: "tonal"
        text: root.actionText
        onClicked: root.actionTriggered()
    }
}
