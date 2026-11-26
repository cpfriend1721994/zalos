import QtQuick
import QtQuick.Layouts
import Zalos

RowLayout {
    id: root

    property int avatarSize: 36
    property string avatarUrl: ""
    property string title: ""
    property string subtitle: ""
    property string dateText: ""
    property bool isGroup: false

    anchors.leftMargin: 16
    anchors.rightMargin: 12
    spacing: 10

    ZAvatar {
        size: root.avatarSize
        avatarUrl: root.avatarUrl
        name: root.title
        isGroup: root.isGroup
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2

        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: root.title
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
            }

            Text {
                text: root.dateText
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: ThemeManager.secondaryTextColor
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.subtitle
            textFormat: Text.PlainText
            maximumLineCount: 1
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: 12
            color: ThemeManager.secondaryTextColor
        }
    }
}
