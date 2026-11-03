import QtQuick
import QtQuick.Controls.Basic
import Zalos

// Zalo-styled button (never use a bare Button: its Basic style paints QPalette colours that ignore the Zalo theme).
// variant: "primary" (accent, white text), "secondary" (input fill + border, text colour), "tonal" (accent tonal
// background + accentTonalTextColor — the only readable pair in dark mode), "danger" (red, white text).
Button {
    id: control
    property string variant: "primary"
    property string iconName: ""
    readonly property bool filled: variant === "primary" || variant === "danger"

    implicitHeight: 34
    implicitWidth: Math.max(72, label.implicitWidth + (iconName.length > 0 ? 22 : 0) + 28)
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: 13
    opacity: enabled ? 1.0 : 0.5

    background: Rectangle {
        radius: 6
        color: {
            switch (control.variant) {
            case "danger": return control.hovered || control.pressed ? Qt.darker(Theme.badgeRed, 1.1) : Theme.badgeRed;
            case "secondary": return control.pressed ? ThemeManager.selectedColor
                                   : control.hovered ? ThemeManager.hoverColor : ThemeManager.inputFillColor;
            case "tonal": return control.hovered || control.pressed ? Qt.darker(Theme.zaloBlueLight, 1.06) : Theme.zaloBlueLight;
            }
            return control.hovered || control.pressed ? Theme.zaloBlueHover : Theme.zaloBlue;
        }
        border.color: control.variant === "secondary" ? ThemeManager.borderColor : "transparent"
        border.width: control.variant === "secondary" ? 1 : 0
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            ZIcon {
                visible: control.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: 16
                color: label.color
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.filled ? "#FFFFFF"
                     : control.variant === "tonal" ? Theme.zaloBlueTonalText : ThemeManager.textColor
            }
        }
    }
}
