pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Zalos

ColumnLayout {
    id: root
    required property bool isMine
    required property string senderName
    required property var pollData
    required property var extra
    required property bool showCard
    required property string question
    required property real availableWidth

    readonly property var poll: root.pollData ? root.pollData : ({})
    readonly property real pollId: Number(root.poll.poll_id || 0)
    readonly property bool loaded: root.poll.loaded === true
    readonly property var options: root.poll.options || []
    readonly property bool hideResults: root.poll.hide_results === true
    readonly property bool canViewVoters: root.poll.can_view_voters === true
    readonly property bool anonymous: root.poll.is_anonymous === true
    readonly property string settingsVi: root.loaded && root.poll.settings_card_vi ? String(root.poll.settings_card_vi) : ""
    readonly property string settingsEn: root.loaded && root.poll.settings_card_en ? String(root.poll.settings_card_en) : ""
    readonly property int voterCount: root.loaded ? Number(root.poll.voter_count) : 0
    readonly property string lockReason: root.loaded && root.poll.lock_reason ? String(root.poll.lock_reason) : ""
    readonly property string action: root.loaded && root.poll.action ? String(root.poll.action) : "vote"
    readonly property int optionCount: root.options.length
    readonly property string pollTextVi: root.extra && root.extra.pollTextVi ? String(root.extra.pollTextVi) : ""
    readonly property string pollTextEn: root.extra && root.extra.pollTextEn ? String(root.extra.pollTextEn) : ""
    readonly property bool anonActor: root.extra ? root.extra.pollAnonActor === true : false
    readonly property string actor: root.isMine ? I18n.t("Bạn", "You")
                                                : (root.anonActor ? I18n.t("Có thành viên", "A member")
                                                                                     : root.senderName)
    readonly property string actionText: I18n.t(root.pollTextVi, root.pollTextEn)
    readonly property string shortQuestion: root.question.length > 50 ? root.question.substring(0, 50) + "…" : root.question
    readonly property real cardWidth: Math.min(360, root.availableWidth - 48)

    spacing: 8

    function openDialog(voters: bool): void {
        if (root.pollId > 0) ZaloClient.openPollDialog(root.pollId, ZaloClient.currentConversationId, voters);
    }
    function refresh(): void {
        if (root.showCard && root.pollId > 0 && !root.loaded) ZaloClient.refreshPoll(root.pollId, false);
    }
    function actionLabel(): string {
        if (root.action === "change") return I18n.t("Đổi lựa chọn", "Change vote");
        if (root.action === "view") return I18n.t("Xem lựa chọn", "View options");
        return I18n.t("Bình chọn", "Vote");
    }

    Component.onCompleted: root.refresh()
    onPollIdChanged: root.refresh()
    onShowCardChanged: root.refresh()

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: false
        Layout.maximumWidth: root.availableWidth - 48
        spacing: 6

        ZIcon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            name: "poll"
            size: 14
            color: ThemeManager.secondaryTextColor
        }

        Text {
            visible: root.actor.length > 0
            Layout.alignment: Qt.AlignTop
            Layout.maximumWidth: 160
            text: root.actor
            elide: Text.ElideRight
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }

        Text {
            Layout.maximumWidth: root.availableWidth - 280
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: root.actionText + (root.shortQuestion.length > 0 ? " “" + root.shortQuestion + "”" : "")
            font.family: Theme.fontFamily
            font.pixelSize: 12
            color: ThemeManager.secondaryTextColor
        }

        Text {
            Layout.alignment: Qt.AlignTop
            visible: root.pollId > 0
            text: I18n.t("Xem", "View")
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.underline: viewInfoMouse.containsMouse
            color: Theme.zaloBlue

            MouseArea {
                id: viewInfoMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.openDialog(false)
            }
        }
    }

    Rectangle {
        id: card
        visible: root.showCard
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: root.cardWidth
        implicitWidth: root.cardWidth
        implicitHeight: cardCol.implicitHeight + 28
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: cardMouse.containsMouse ? Theme.zaloBlue : ThemeManager.borderColor
        border.width: 1

        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openDialog(false)
        }

        ColumnLayout {
            id: cardCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: root.question
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: ThemeManager.textColor
            }

            Text {
                readonly property string value: I18n.t(root.settingsVi, root.settingsEn)
                visible: value.length > 0
                Layout.fillWidth: true
                text: value
                wrapMode: Text.Wrap
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }

            Text {
                visible: root.loaded && root.voterCount > 0
                text: I18n.t(root.voterCount + " người bình chọn", root.voterCount + " voters")
                      + (root.canViewVoters ? "  ›" : "")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.underline: votersMouse.containsMouse && root.canViewVoters
                color: root.canViewVoters ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                MouseArea {
                    id: votersMouse
                    anchors.fill: parent
                    enabled: root.canViewVoters
                    hoverEnabled: true
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.openDialog(true)
                }
            }

            Repeater {
                model: root.loaded ? 0 : 3
                delegate: Rectangle {
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: 8
                    color: ThemeManager.inputFillColor
                }
            }

            Repeater {
                model: root.loaded ? root.options.slice(0, 3) : []
                delegate: Rectangle {
                    id: optRow
                    required property var modelData
                    readonly property bool voted: optRow.modelData.voted === true
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: 8
                    color: ThemeManager.inputFillColor
                    border.color: optRow.voted ? Theme.zaloBlue : ThemeManager.inputFillColor
                    border.width: 1
                    clip: true

                    Rectangle {
                        visible: !root.hideResults
                        width: parent.width * Number(optRow.modelData.weight || 0)
                        height: parent.height
                        radius: 8
                        color: Theme.zaloBlueLight
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: optRow.modelData.content
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                        }
                        ZIcon {
                            visible: optRow.voted
                            name: "check"
                            size: 14
                            color: Theme.zaloBlue
                        }
                        Row {
                            visible: !root.hideResults && !root.anonymous
                            spacing: -6
                            Repeater {
                                model: optRow.modelData.preview || []
                                delegate: ZAvatar {
                                    required property var modelData
                                    size: 20
                                    avatarUrl: modelData.avatar || ""
                                    name: modelData.name || ""
                                }
                            }
                        }
                        Text {
                            visible: !root.hideResults
                            text: optRow.modelData.votes
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }
            }

            Text {
                visible: root.loaded && root.optionCount > 3
                text: I18n.t("+" + (root.optionCount - 3) + " phương án khác", "+" + (root.optionCount - 3) + " more options")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }

            Text {
                visible: root.lockReason === "closed" || root.lockReason === "expired"
                text: root.lockReason === "closed" ? I18n.t("Bình chọn đã khóa", "Poll closed")
                                                         : I18n.t("Bình chọn đã hết hạn", "Poll ended")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: ThemeManager.secondaryTextColor
            }

            ZButton {
                visible: root.loaded
                Layout.fillWidth: true
                variant: "tonal"
                text: root.actionLabel()
                onClicked: root.openDialog(false)
            }
        }
    }
}
