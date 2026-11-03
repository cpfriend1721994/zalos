import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    visible: ZaloClient.showCreatePollModal
    anchors.fill: parent
    z: 220

    property var pollOptions: ["", ""]
    property bool multiChoice: false

    function closeModal(): void {
        ZaloClient.closeCreatePollModal()
        questionInput.text = ""
        pollOptions = ["", ""]
        multiChoice = false
    }

    // Created on demand by a Loader in Main.qml, already visible: run the same init.
    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (visible) {
            questionInput.text = ""
            pollOptions = ["", ""]
            multiChoice = false
            questionInput.forceActiveFocus()
        }
    }

    // Backdrop Dim
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.visible ? 0.5 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        MouseArea {
            anchors.fill: parent
            onClicked: root.closeModal()
        }
    }

    // Modal Card
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 460)
        height: Math.min(root.height - 40, 520)
        scale: root.visible ? 1.0 : 0.94
        opacity: root.visible ? 1.0 : 0.0
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ZIcon {
                    name: "poll"
                    size: 20
                    color: Theme.zaloBlue
                }

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Tạo cuộc bình chọn", "Create poll")
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: 28
                    height: 28
                    radius: 14
                    color: closeHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                    ZIcon {
                        anchors.centerIn: parent
                        name: "close"
                        size: 16
                        color: ThemeManager.secondaryTextColor
                    }

                    MouseArea {
                        id: closeHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeModal()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: ThemeManager.dividerColor
            }

            // Question Input
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: I18n.t("Câu hỏi bình chọn", "Poll question")
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.bold: true
                    color: ThemeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: ThemeManager.inputFillColor
                    border.color: questionInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                    border.width: questionInput.activeFocus ? 2 : 1

                    TextField {
                        id: questionInput
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: I18n.t("Nhập câu hỏi bình chọn...", "Enter poll question...")
                        placeholderTextColor: ThemeManager.placeholderColor
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        background: null
                        selectByMouse: true
                    }
                }
            }

            // Options List
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Text {
                    text: I18n.t("Các phương án lựa chọn", "Options")
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.bold: true
                    color: ThemeManager.textColor
                }

                ScrollView {
                    id: optionsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: optionsScroll.availableWidth
                        spacing: 8

                        Repeater {
                            id: optionsRepeater
                            model: root.pollOptions.length

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 38
                                    radius: 6
                                    color: ThemeManager.inputFillColor
                                    border.color: optInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                                    border.width: optInput.activeFocus ? 2 : 1

                                    TextField {
                                        id: optInput
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        verticalAlignment: TextInput.AlignVCenter
                                        placeholderText: I18n.t("Phương án ", "Option ") + (index + 1)
                                        placeholderTextColor: ThemeManager.placeholderColor
                                        color: ThemeManager.textColor
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        background: null
                                        selectByMouse: true
                                        text: root.pollOptions[index] || ""
                                        onTextChanged: {
                                            if (root.pollOptions[index] !== text) {
                                                var opts = root.pollOptions.slice()
                                                opts[index] = text
                                                root.pollOptions = opts
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: root.pollOptions.length > 2
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    radius: 14
                                    color: delHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                                    ZIcon {
                                        anchors.centerIn: parent
                                        name: "close"
                                        size: 14
                                        color: Theme.badgeRed
                                    }

                                    MouseArea {
                                        id: delHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var opts = root.pollOptions.slice()
                                            opts.splice(index, 1)
                                            root.pollOptions = opts
                                        }
                                    }
                                }
                            }
                        }

                        // Add Option Button
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            radius: 6
                            color: addOptMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            border.color: Theme.zaloBlue
                            border.width: 1

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6

                                ZIcon {
                                    name: "plus"
                                    size: 14
                                    color: Theme.zaloBlue
                                }

                                Text {
                                    text: I18n.t("+ Thêm phương án", "+ Add option")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    color: Theme.zaloBlue
                                }
                            }

                            MouseArea {
                                id: addOptMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var opts = root.pollOptions.slice()
                                    opts.push("")
                                    root.pollOptions = opts
                                }
                            }
                        }
                    }
                }
            }

            // Options settings
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ZCheckBox {
                    id: multiCheck
                    checked: root.multiChoice
                    onToggled: function(value) { root.multiChoice = value; }
                }

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Cho phép chọn nhiều phương án", "Allow multiple choices")
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    color: ThemeManager.textColor

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.multiChoice = !root.multiChoice
                    }
                }
            }

            // Footer Buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 12

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: 36
                    radius: 6
                    color: cancelMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: I18n.t("Hủy", "Cancel")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                    }

                    MouseArea {
                        id: cancelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeModal()
                    }
                }

                Rectangle {
                    readonly property bool canCreate: {
                        if (questionInput.text.trim().length === 0) return false
                        var count = 0
                        for (var i = 0; i < root.pollOptions.length; ++i) {
                            if (root.pollOptions[i] && root.pollOptions[i].trim().length > 0) count++
                        }
                        return count >= 2
                    }

                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 36
                    radius: 6
                    color: canCreate ? (createMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue) : ThemeManager.borderColor
                    opacity: canCreate ? 1.0 : 0.6

                    Text {
                        anchors.centerIn: parent
                        text: I18n.t("Tạo bình chọn", "Create poll")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: true
                        color: "#FFFFFF"
                    }

                    MouseArea {
                        id: createMouse
                        anchors.fill: parent
                        enabled: parent.canCreate
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            var cleanOpts = []
                            for (var i = 0; i < root.pollOptions.length; ++i) {
                                var s = root.pollOptions[i] ? root.pollOptions[i].trim() : ""
                                if (s.length > 0) cleanOpts.push(s)
                            }
                            ZaloClient.createPoll(ZaloClient.currentConversationId, questionInput.text.trim(), cleanOpts, root.multiChoice)
                        }
                    }
                }
            }
        }
    }
}

