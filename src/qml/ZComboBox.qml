import QtQuick
import QtQuick.Controls.Basic
import Zalos

// Zalo-styled drop-down (field, arrow, popup and rows in theme colours). `model` is a list of strings, or of
// objects with `textRole`.
ComboBox {
    id: control
    implicitHeight: 34
    implicitWidth: 160
    font.family: Theme.fontFamily
    font.pixelSize: 13

    background: Rectangle {
        radius: 6
        color: ThemeManager.inputFillColor
        border.color: control.activeFocus || control.popup.visible ? Theme.zaloBlue : ThemeManager.borderColor
        border.width: 1
    }
    contentItem: Text {
        leftPadding: 10
        rightPadding: 26
        text: control.displayText
        font: control.font
        color: ThemeManager.textColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: ZIcon {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron_down"
        size: 14
        color: ThemeManager.secondaryTextColor
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(240, contentHeight)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle {
            radius: 6
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
        }
    }
    delegate: ItemDelegate {
        id: row
        required property var modelData
        required property int index
        width: control.width - 8
        height: 32
        highlighted: control.highlightedIndex === row.index
        contentItem: Text {
            text: control.textRole.length > 0 ? row.modelData[control.textRole] : row.modelData
            color: control.currentIndex === row.index ? Theme.zaloBlue : ThemeManager.textColor
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 4
            color: row.highlighted ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
        }
    }
}
