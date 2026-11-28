import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import Zalos

Popup {
    id: root
    property string title: ""
    property string okText: I18n.t("Đồng ý", "OK")
    property string cancelText: I18n.t("Hủy", "Cancel")
    property bool danger: false
    property bool okEnabled: true
    property real maxWidth: 360
    default property alias content: body.data
    signal accepted()
    signal rejected()

    function accept(): void {
        root.close();
        root.accepted();
    }
    function reject(): void {
        root.close();
        root.rejected();
    }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: parent ? Math.max(240, Math.min(parent.width - 24, root.maxWidth)) : root.maxWidth
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            Layout.fillWidth: true
            visible: root.title.length > 0
            text: root.title
            wrapMode: Text.Wrap
            color: ThemeManager.textColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 10
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            Item { Layout.fillWidth: true }
            ZButton {
                variant: "secondary"
                text: root.cancelText
                onClicked: root.reject()
            }
            ZButton {
                variant: root.danger ? "danger" : "primary"
                text: root.okText
                enabled: root.okEnabled
                onClicked: root.accept()
            }
        }
    }
}
