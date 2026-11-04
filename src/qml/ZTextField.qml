import QtQuick
import QtQuick.Controls.Basic
import Zalos

TextField {
    id: control
    implicitHeight: 36
    leftPadding: 10
    rightPadding: 10
    color: ThemeManager.textColor
    placeholderTextColor: ThemeManager.placeholderColor
    selectionColor: Theme.zaloBlue
    selectedTextColor: "#FFFFFF"
    font.family: Theme.fontFamily
    font.pixelSize: 13
    background: Rectangle {
        radius: 6
        color: ThemeManager.inputFillColor
        border.color: control.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
        border.width: 1
    }
}
