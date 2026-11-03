import QtQuick
import QtQuick.Controls.Basic
import Zalos

// Zalo-styled single-line input (skill 03 "Zero bare controls"): input fill, border that turns accent on focus,
// theme text/placeholder colours in light, dark and system modes.
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
