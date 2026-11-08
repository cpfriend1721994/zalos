pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import Zalos

Item {
    id: root
    anchors.fill: parent
    readonly property var labelList: ZaloClient.labels.labels
    property int editingId: 0
    property string newColor: ZaloClient.labels.palette[0]
    property string editColor: ""
    property string error: ""
    property int deleteId: 0
    property string deleteName: ""

    function close(): void { ZaloClient.labels.manageOpen = false; }

    function addLabel(): void {
        root.error = ZaloClient.labels.create(newName.text, root.newColor);
        if (root.error.length === 0) newName.text = "";
    }

    function saveEdit(text: string): void {
        root.error = ZaloClient.labels.edit(root.editingId, text, root.editColor);
        if (root.error.length === 0) root.editingId = 0;
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.5
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    component Swatches: Row {
        id: swatches
        property string selected: ""
        signal picked(string color)
        spacing: 6
        Repeater {
            model: ZaloClient.labels.palette
            Rectangle {
                required property string modelData
                width: 22
                height: 22
                radius: 11
                color: modelData
                border.color: swatches.selected === modelData ? ThemeManager.textColor : "transparent"
                border.width: 2
                ZIcon {
                    anchors.centerIn: parent
                    visible: swatches.selected === parent.modelData
                    name: "check"
                    size: 12
                    color: "#FFFFFF"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: swatches.picked(parent.modelData)
                }
            }
        }
    }

    component IconAction: Rectangle {
        id: action
        property string icon: ""
        property string tip: ""
        property bool danger: false
        signal triggered()
        width: 26
        height: 26
        radius: 13
        opacity: enabled ? 1 : 0.35
        color: actionMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
        ZIcon { anchors.centerIn: parent; name: action.icon; size: 15; color: action.danger ? Theme.badgeRed : ThemeManager.iconColor }
        MouseArea {
            id: actionMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: action.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: action.triggered()
        }
        ToolTip.visible: actionMouse.containsMouse && action.tip.length > 0
        ToolTip.text: action.tip
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 460)
        height: Math.min(root.height - 40, 600)
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZIcon { name: "edit"; size: 20; color: Theme.zaloBlue }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Quản lý thẻ phân loại", "Manage labels")
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                }
                IconAction { icon: "close"; onTriggered: root.close() }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("Gắn thẻ cho hội thoại bằng chuột phải trong danh sách tin nhắn. Thẻ được đồng bộ với Zalo trên các thiết bị khác.",
                             "Label a conversation with a right click in the chat list. Labels sync with Zalo on your other devices.")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }

            Text {
                visible: !ZaloClient.labels.loaded
                text: ZaloClient.isLoggedIn ? I18n.t("Đang tải thẻ phân loại…", "Loading labels…")
                                            : I18n.t("Cần kết nối tới Zalo", "Connect to Zalo first")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: root.labelList
                delegate: Rectangle {
                    id: labelRow
                    required property var modelData
                    required property int index
                    readonly property int labelRowId: labelRow.modelData.id || 0
                    readonly property string labelRowColor: labelRow.modelData.color || ""
                    readonly property string labelRowText: labelRow.modelData.text || ""
                    readonly property int labelRowCount: labelRow.modelData.count || 0
                    readonly property bool editing: root.editingId === labelRow.labelRowId
                    width: list.width
                    height: editing ? editCol.implicitHeight + 16 : 44
                    radius: 8
                    color: rowHover.hovered || editing ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    HoverHandler { id: rowHover }

                    RowLayout {
                        visible: !labelRow.editing
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 10
                        Rectangle { Layout.preferredWidth: 14; Layout.preferredHeight: 14; radius: 7; color: labelRow.labelRowColor }
                        Text {
                            Layout.fillWidth: true
                            text: labelRow.labelRowText
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                        }
                        Text {
                            text: I18n.t("%1 hội thoại", "%1 chats").arg(labelRow.labelRowCount)
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                        IconAction {
                            icon: "arrow_up"
                            tip: I18n.t("Lên", "Up")
                            enabled: labelRow.index > 0 && !ZaloClient.labels.busy
                            onTriggered: ZaloClient.labels.move(labelRow.labelRowId, -1)
                        }
                        IconAction {
                            icon: "arrow_down"
                            tip: I18n.t("Xuống", "Down")
                            enabled: labelRow.index < root.labelList.length - 1 && !ZaloClient.labels.busy
                            onTriggered: ZaloClient.labels.move(labelRow.labelRowId, 1)
                        }
                        IconAction {
                            icon: "edit"
                            tip: I18n.t("Sửa", "Edit")
                            onTriggered: {
                                root.error = "";
                                root.editColor = labelRow.labelRowColor;
                                root.editingId = labelRow.labelRowId;
                            }
                        }
                        IconAction {
                            icon: "trash"
                            danger: true
                            tip: I18n.t("Xóa", "Delete")
                            onTriggered: {
                                root.deleteId = labelRow.labelRowId;
                                root.deleteName = labelRow.labelRowText;
                                deleteDialog.open();
                            }
                        }
                    }

                    ColumnLayout {
                        id: editCol
                        visible: labelRow.editing
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8
                        ZTextField {
                            id: editName
                            Layout.fillWidth: true
                            text: labelRow.modelData.text
                            maximumLength: ZaloClient.labels.maxNameLength
                            onAccepted: root.saveEdit(text)
                            onVisibleChanged: if (visible) forceActiveFocus()
                        }
                        Swatches {
                            selected: root.editColor
                            onPicked: (c) => root.editColor = c
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Hủy", "Cancel")
                                onClicked: { root.editingId = 0; root.error = ""; }
                            }
                            ZButton {
                                text: I18n.t("Lưu", "Save")
                                enabled: !ZaloClient.labels.busy
                                onClicked: root.saveEdit(editName.text)
                            }
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: ThemeManager.dividerColor }

            Text {
                text: I18n.t("Thêm thẻ phân loại", "Add a label")
                      + " (" + root.labelList.length + "/" + ZaloClient.labels.maxLabels + ")"
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.bold: true
                color: ThemeManager.textColor
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZTextField {
                    id: newName
                    Layout.fillWidth: true
                    placeholderText: I18n.t("Tên thẻ", "Label name")
                    maximumLength: ZaloClient.labels.maxNameLength
                    enabled: ZaloClient.labels.loaded && root.labelList.length < ZaloClient.labels.maxLabels
                    onAccepted: root.addLabel()
                }
                ZButton {
                    text: I18n.t("Thêm", "Add")
                    enabled: newName.enabled && newName.text.trim().length > 0 && !ZaloClient.labels.busy
                    onClicked: root.addLabel()
                }
            }
            Swatches {
                selected: root.newColor
                onPicked: (c) => root.newColor = c
            }
            Text {
                visible: root.error.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.error
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: Theme.badgeRed
            }
        }
    }

    Dialog {
        id: deleteDialog
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 340
        title: I18n.t("Xóa thẻ phân loại?", "Delete label?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: I18n.t("Thẻ “%1” sẽ bị gỡ khỏi mọi hội thoại trên tất cả thiết bị.",
                         "“%1” is removed from every conversation on all devices.").arg(root.deleteName)
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: ThemeManager.textColor
        }
        onAccepted: ZaloClient.labels.remove(root.deleteId)
    }
}
