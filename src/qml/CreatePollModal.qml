pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    visible: ZaloClient.showCreatePollModal
    anchors.fill: parent
    z: 220

    property list<string> pollOptions: ["", ""]
    readonly property int optionCount: root.pollOptions.length
    property bool multiChoice: true
    property bool allowAddOption: true
    property bool hideResults: false
    property bool hideVoters: false
    property bool pinToTop: false
    property int deadlineIndex: 0
    property bool submitting: false
    readonly property list<real> deadlineMs: [0, 3600000, 86400000, 259200000, 604800000]
    readonly property list<string> deadlineVi: ["Không thời hạn", "1 giờ", "1 ngày", "3 ngày", "7 ngày"]
    readonly property list<string> deadlineEn: ["No deadline", "1 hour", "1 day", "3 days", "7 days"]
    readonly property string problem: ZaloClient.pollDraftError(questionInput.text, root.pollOptions)
    readonly property bool touched: questionInput.length > 0

    component SettingRow: RowLayout {
        id: settingRow
        property string label: ""
        property string hint: ""
        property bool checked: false
        property bool available: true
        signal toggled(bool value)
        Layout.fillWidth: true
        spacing: 10
        opacity: settingRow.available ? 1.0 : 0.5
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: settingRow.label
                wrapMode: Text.Wrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: ThemeManager.textColor
            }
            Text {
                visible: settingRow.hint.length > 0
                Layout.fillWidth: true
                text: settingRow.hint
                wrapMode: Text.Wrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontCaption
                color: ThemeManager.secondaryTextColor
            }
        }
        ZSwitch {
            enabled: settingRow.available
            checked: settingRow.checked
            onToggled: settingRow.toggled(!settingRow.checked)
        }
    }

    function reset(): void {
        questionInput.text = "";
        root.pollOptions = ["", ""];
        root.multiChoice = true;
        root.allowAddOption = true;
        root.hideResults = false;
        root.hideVoters = false;
        root.pinToTop = false;
        root.deadlineIndex = 0;
        root.submitting = false;
    }
    function closeModal(): void {
        root.submitting = false;
        ZaloClient.closeCreatePollModal();
        root.reset();
    }
    function create(): void {
        if (root.submitting) return;
        if (root.problem.length > 0) {
            ZaloClient.notice(root.problem);
            return;
        }
        root.submitting = true;
        const ms = root.deadlineMs[root.deadlineIndex];
        ZaloClient.createPoll(ZaloClient.currentConversationId, questionInput.text.trim(), root.pollOptions, {
            allowMultiChoices: root.multiChoice,
            allowAddNewOption: root.allowAddOption && !root.hideResults,
            hideVotePreview: root.hideResults,
            isAnonymous: root.hideVoters,
            pinAct: root.pinToTop,
            expiredTime: ms > 0 ? Date.now() + ms : 0
        });
    }

    Connections {
        target: ZaloClient
        function onNotice(message: string): void {
            root.submitting = false;
        }
    }

    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (visible) {
            root.reset();
            questionInput.forceActiveFocus();
        } else {
            root.submitting = false;
        }
    }

    ZModalScrim {
        shown: root.visible
        fadeDuration: 150
        onDismissed: root.closeModal()
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        height: Math.min(root.height - 40, 660)
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
            spacing: 12

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
                    text: I18n.t("Tạo bình chọn", "Create poll")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
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
                Layout.preferredHeight: 1
                color: ThemeManager.dividerColor
            }

            ScrollView {
                id: bodyScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: bodyScroll.availableWidth
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Chủ đề bình chọn", "Poll question")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }
                        Text {
                            text: questionInput.text.length + "/200"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    ZTextField {
                        id: questionInput
                        Layout.fillWidth: true
                        implicitHeight: 40
                        maximumLength: 200
                        placeholderText: I18n.t("Đặt câu hỏi bình chọn", "Ask a question")
                        font.pixelSize: Theme.fontBodyLarge
                    }

                    Text {
                        text: I18n.t("Các lựa chọn", "Options")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        color: ThemeManager.textColor
                    }

                    Repeater {
                        model: root.optionCount

                        RowLayout {
                            id: pollOptionRow
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8

                            ZTextField {
                                Layout.fillWidth: true
                                maximumLength: 120
                                placeholderText: I18n.t("Lựa chọn ", "Option ") + (pollOptionRow.index + 1)
                                text: root.pollOptions[pollOptionRow.index] || ""
                                onTextChanged: {
                                    if (root.pollOptions[pollOptionRow.index] !== text) {
                                        const opts = root.pollOptions.slice();
                                        opts[pollOptionRow.index] = text;
                                        root.pollOptions = opts;
                                    }
                                }
                            }

                            Rectangle {
                                visible: root.optionCount > 2
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                radius: 14
                                color: delHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                                ZIcon {
                                    anchors.centerIn: parent
                                    name: "close"
                                    size: 14
                                    color: ThemeManager.secondaryTextColor
                                }

                                MouseArea {
                                    id: delHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const opts = root.pollOptions.slice();
                                        opts.splice(pollOptionRow.index, 1);
                                        root.pollOptions = opts;
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: root.optionCount < 30
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
                                text: I18n.t("Thêm lựa chọn", "Add option")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                color: Theme.zaloBlue
                            }
                        }

                        MouseArea {
                            id: addOptMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const opts = root.pollOptions.slice();
                                opts.push("");
                                root.pollOptions = opts;
                            }
                        }
                    }

                    Text {
                        Layout.topMargin: 6
                        text: I18n.t("Thiết lập nâng cao", "Advanced settings")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        color: ThemeManager.textColor
                    }

                    SettingRow {
                        label: I18n.t("Ghim lên đầu trò chuyện", "Pin to the top of the chat")
                        checked: root.pinToTop
                        onToggled: root.pinToTop = !root.pinToTop
                    }
                    SettingRow {
                        label: I18n.t("Chọn nhiều phương án", "Allow multiple choices")
                        checked: root.multiChoice
                        onToggled: root.multiChoice = !root.multiChoice
                    }
                    SettingRow {
                        label: I18n.t("Có thể thêm phương án", "Members can add options")
                        hint: root.hideResults ? I18n.t("Không dùng được khi ẩn kết quả", "Unavailable while results are hidden") : ""
                        available: !root.hideResults
                        checked: root.allowAddOption && !root.hideResults
                        onToggled: root.allowAddOption = !root.allowAddOption
                    }
                    SettingRow {
                        label: I18n.t("Ẩn kết quả khi chưa bình chọn", "Hide results until voted")
                        hint: I18n.t("Người tham gia chỉ thấy kết quả khi đã bình chọn và không thể thay đổi phương án",
                                     "Participants see results only after voting and can't change their vote")
                        checked: root.hideResults
                        onToggled: root.hideResults = !root.hideResults
                    }
                    SettingRow {
                        label: I18n.t("Ẩn người bình chọn", "Hide voters")
                        hint: I18n.t("Người tham gia chỉ thấy bình chọn của bản thân", "Participants see only their own vote")
                        checked: root.hideVoters
                        onToggled: root.hideVoters = !root.hideVoters
                    }

                    Text {
                        Layout.topMargin: 4
                        text: I18n.t("Thời hạn bình chọn", "Voting deadline")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        color: ThemeManager.textColor
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: root.deadlineMs.length
                            delegate: Rectangle {
                                id: chip
                                required property int index
                                readonly property bool active: root.deadlineIndex === chip.index
                                width: chipLbl.implicitWidth + 20
                                height: 28
                                radius: 14
                                color: chip.active ? Theme.zaloBlueLight
                                                   : (chipMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor)
                                border.color: chip.active ? Theme.zaloBlue : ThemeManager.inputFillColor
                                border.width: 1
                                Text {
                                    id: chipLbl
                                    anchors.centerIn: parent
                                    text: I18n.t(root.deadlineVi[chip.index], root.deadlineEn[chip.index])
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    color: chip.active ? Theme.zaloBlueTonalText : ThemeManager.textColor
                                }
                                MouseArea {
                                    id: chipMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.deadlineIndex = chip.index
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: root.touched && root.problem.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.problem
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.badgeRed
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                ZButton {
                    variant: "secondary"
                    text: I18n.t("Hủy", "Cancel")
                    onClicked: root.closeModal()
                }

                ZButton {
                    variant: "primary"
                    text: I18n.t("Tạo bình chọn", "Create poll")
                    enabled: root.problem.length === 0 && !root.submitting
                    onClicked: root.create()
                }
            }
        }
    }
}
