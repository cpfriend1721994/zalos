pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    anchors.fill: parent
    readonly property var preview: ZaloClient.groupAdmin.preview
    readonly property string previewGroupId: root.preview.groupId || ""
    readonly property string previewLink: root.preview.link || ""
    readonly property bool previewLoading: root.preview.loading === true
    readonly property string previewError: root.preview.error || ""
    readonly property string previewAvatar: root.preview.avatar || ""
    readonly property string previewName: root.preview.name || ""
    readonly property int previewTotalMember: root.preview.totalMember || 0
    readonly property string previewDesc: root.preview.desc || ""
    readonly property var previewMembers: root.preview.members || []
    readonly property bool previewNeedsApproval: root.preview.needsApproval === true
    readonly property bool previewIsMember: root.preview.isMember === true
    readonly property bool previewJoining: root.preview.joining === true
    readonly property bool hasGroup: root.previewGroupId.length > 0

    function close(): void { ZaloClient.groupAdmin.closeJoin(); }

    Component.onCompleted: {
        linkField.text = root.previewLink;
        if (linkField.text.length === 0) linkField.forceActiveFocus();
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.5
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 420)
        height: Math.min(root.height - 40, content.implicitHeight + 40)
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZIcon { name: "link"; size: 20; color: Theme.zaloBlue }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Tham gia nhóm bằng link", "Join a group by link")
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: closeMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "close"; size: 16; color: ThemeManager.iconColor }
                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZTextField {
                    id: linkField
                    Layout.fillWidth: true
                    placeholderText: "https://zalo.me/g/…"
                    onAccepted: ZaloClient.groupAdmin.previewLink(text)
                }
                ZButton {
                    variant: "secondary"
                    text: I18n.t("Xem", "Open")
                    enabled: linkField.text.trim().length > 0 && !root.previewLoading
                    onClicked: ZaloClient.groupAdmin.previewLink(linkField.text)
                }
            }

            Text {
                visible: root.previewLoading
                text: I18n.t("Đang tải thông tin nhóm…", "Loading the group…")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }
            Text {
                visible: root.previewError.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.previewError
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: Theme.badgeRed
            }

            ColumnLayout {
                visible: root.hasGroup
                Layout.fillWidth: true
                spacing: 8
                ZAvatar {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 72
                    size: 72
                    avatarUrl: root.previewAvatar
                    name: root.previewName
                    showStatusDot: false
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: root.previewName
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: I18n.t("%1 thành viên", "%1 members").arg(root.previewTotalMember)
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                }
                Text {
                    visible: root.previewDesc.length > 0
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    text: root.previewDesc
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    color: ThemeManager.textColor
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: -6
                    Repeater {
                        model: root.previewMembers
                        ZAvatar {
                            id: previewMemberAvatar
                            required property var modelData
                            readonly property string mAvatar: previewMemberAvatar.modelData.avatar || ""
                            readonly property string mName: previewMemberAvatar.modelData.name || ""
                            width: 28
                            height: 28
                            size: 28
                            avatarUrl: previewMemberAvatar.mAvatar
                            name: previewMemberAvatar.mName
                            showStatusDot: false
                        }
                    }
                }
                Text {
                    visible: root.previewNeedsApproval && !root.previewIsMember
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: I18n.t("Nhóm cần trưởng hoặc phó nhóm duyệt trước khi bạn vào nhóm.",
                                 "The owner or a deputy must approve you before you join.")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                }
                ZButton {
                    Layout.fillWidth: true
                    enabled: !root.previewJoining
                    text: root.previewIsMember ? I18n.t("Mở nhóm", "Open group")
                          : root.previewJoining ? I18n.t("Đang tham gia…", "Joining…")
                          : root.previewNeedsApproval ? I18n.t("Gửi yêu cầu tham gia", "Ask to join")
                          : I18n.t("Tham gia nhóm", "Join group")
                    onClicked: ZaloClient.groupAdmin.join()
                }
            }
        }
    }
}
