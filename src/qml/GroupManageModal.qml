import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

// Group administration (ZaloClient.groupAdmin): invite link, member permissions (GroupSetting), join requests
// and the blocked list of one group. Members see the link and the permissions read-only; owner and deputies edit.
Item {
    id: root
    anchors.fill: parent
    readonly property bool canEdit: ZaloClient.groupAdmin.canAdminister
    readonly property bool settingsKnown: Object.keys(ZaloClient.groupAdmin.settings).length > 0

    // Permission rows. `inverted`: the switch shows "members may …" while the server flag says "locked".
    readonly property var memberRights: [
        { key: "blockName", inverted: true, vi: "Thay đổi tên và ảnh đại diện nhóm", en: "Change the group name and photo" },
        { key: "setTopicOnly", inverted: true, vi: "Ghim tin nhắn, ghi chú, bình chọn lên đầu hội thoại", en: "Pin messages, notes and polls" },
        { key: "lockCreatePost", inverted: true, vi: "Tạo mới ghi chú, nhắc hẹn", en: "Create notes and reminders" },
        { key: "lockCreatePoll", inverted: true, vi: "Tạo mới bình chọn", en: "Create polls" },
        { key: "lockSendMsg", inverted: true, vi: "Gửi tin nhắn", en: "Send messages" },
        { key: "lockViewMember", inverted: true, vi: "Xem danh sách thành viên", en: "See the member list" }
    ]
    readonly property var groupModes: [
        { key: "joinAppr", inverted: false, vi: "Phê duyệt thành viên mới", en: "Approve new members" },
        { key: "signAdminMsg", inverted: false, vi: "Đánh dấu tin nhắn từ trưởng và phó nhóm", en: "Highlight messages from the owner and deputies" },
        { key: "enableMsgHistory", inverted: false, vi: "Cho phép thành viên mới đọc tin nhắn gần nhất", en: "New members can read recent messages" }
    ]

    function close(): void { ZaloClient.groupAdmin.closeManage(); }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.5
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    component SectionTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        font.family: Theme.fontFamily
        font.pixelSize: 14
        font.bold: true
        color: ThemeManager.textColor
    }

    component Hint: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: 12
        color: ThemeManager.secondaryTextColor
    }

    component SettingRow: RowLayout {
        id: settingRow
        required property var modelData
        Layout.fillWidth: true
        spacing: 12
        readonly property bool serverValue: ZaloClient.groupAdmin.settings[settingRow.modelData.key] === true
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: I18n.t(settingRow.modelData.vi, settingRow.modelData.en)
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: ThemeManager.textColor
        }
        ZSwitch {
            enabled: root.canEdit && root.settingsKnown
            checked: settingRow.modelData.inverted ? !settingRow.serverValue : settingRow.serverValue
            onToggled: (on) => ZaloClient.groupAdmin.setSetting(settingRow.modelData.key, settingRow.modelData.inverted ? !on : on)
        }
    }

    component PersonRow: RowLayout {
        id: personRow
        required property var modelData
        default property alias actions: actionRow.data
        Layout.fillWidth: true
        spacing: 10
        ZAvatar {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            size: 32
            avatarUrl: personRow.modelData.avatar
            name: personRow.modelData.name
            showStatusDot: false
        }
        Text {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: personRow.modelData.name.length > 0 ? personRow.modelData.name : personRow.modelData.id
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: ThemeManager.textColor
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: ZaloClient.viewProfile(personRow.modelData.id)
            }
        }
        Row { id: actionRow; spacing: 6 }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 520)
        height: Math.min(root.height - 40, 680)
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
                ZIcon { name: "settings"; size: 20; color: Theme.zaloBlue }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Quản lý nhóm", "Manage group")
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: refreshMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "refresh"; size: 16; color: ThemeManager.iconColor }
                    MouseArea {
                        id: refreshMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ZaloClient.groupAdmin.reload()
                    }
                    ToolTip.visible: refreshMouse.containsMouse
                    ToolTip.text: I18n.t("Tải lại", "Reload")
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

            ScrollView {
                id: scroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: scroll.availableWidth
                    spacing: 10

                    // ---- Invite link
                    SectionTitle { text: I18n.t("Link tham gia nhóm", "Group invite link") }
                    Hint {
                        visible: !ZaloClient.groupAdmin.link.loaded
                        text: ZaloClient.isLoggedIn ? I18n.t("Đang tải…", "Loading…")
                                                    : I18n.t("Cần kết nối tới Zalo", "Connect to Zalo first")
                    }
                    Rectangle {
                        visible: ZaloClient.groupAdmin.link.enabled === true
                        Layout.fillWidth: true
                        implicitHeight: linkText.implicitHeight + 16
                        radius: 6
                        color: ThemeManager.inputFillColor
                        border.color: ThemeManager.borderColor
                        Text {
                            id: linkText
                            anchors.fill: parent
                            anchors.margins: 8
                            text: ZaloClient.groupAdmin.link.link || ""
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: Theme.linkColor
                        }
                    }
                    Hint {
                        visible: ZaloClient.groupAdmin.link.loaded === true && ZaloClient.groupAdmin.link.enabled !== true
                        text: root.canEdit ? I18n.t("Link tham gia đang tắt. Bật để mọi người có link đều xin vào được nhóm.",
                                                    "The invite link is off. Turn it on so anyone with the link can ask to join.")
                                           : I18n.t("Nhóm chưa bật link tham gia.", "This group has no invite link.")
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: ZaloClient.groupAdmin.link.loaded === true
                        ZButton {
                            visible: ZaloClient.groupAdmin.link.enabled === true
                            variant: "tonal"
                            iconName: "copy"
                            text: I18n.t("Sao chép link", "Copy link")
                            onClicked: ZaloClient.groupAdmin.copyLink()
                        }
                        ZButton {
                            visible: root.canEdit
                            variant: ZaloClient.groupAdmin.link.enabled === true ? "secondary" : "primary"
                            iconName: ZaloClient.groupAdmin.link.enabled === true ? "refresh" : "link"
                            text: ZaloClient.groupAdmin.link.enabled === true ? I18n.t("Tạo link mới", "New link")
                                                                   : I18n.t("Bật link tham gia", "Turn on invite link")
                            onClicked: ZaloClient.groupAdmin.createLink()
                        }
                        ZButton {
                            visible: root.canEdit && ZaloClient.groupAdmin.link.enabled === true
                            variant: "secondary"
                            text: I18n.t("Tắt link", "Turn off link")
                            onClicked: ZaloClient.groupAdmin.disableLink()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: ThemeManager.dividerColor }

                    // ---- Permissions
                    SectionTitle { text: I18n.t("Cho phép thành viên", "Members can") }
                    Hint {
                        visible: !root.settingsKnown
                        text: I18n.t("Đang tải cài đặt nhóm…", "Loading group settings…")
                    }
                    Hint {
                        visible: root.settingsKnown && !root.canEdit
                        text: I18n.t("Chỉ trưởng hoặc phó nhóm thay đổi được các cài đặt này.",
                                     "Only the owner or deputies can change these settings.")
                    }
                    Repeater { model: root.memberRights; delegate: SettingRow {} }

                    SectionTitle { text: I18n.t("Chế độ nhóm", "Group mode") }
                    Repeater { model: root.groupModes; delegate: SettingRow {} }

                    // ---- Join requests
                    Rectangle { visible: root.canEdit; Layout.fillWidth: true; Layout.preferredHeight: 1; color: ThemeManager.dividerColor }
                    RowLayout {
                        visible: root.canEdit
                        Layout.fillWidth: true
                        SectionTitle {
                            text: I18n.t("Yêu cầu tham gia", "Join requests")
                                  + (ZaloClient.groupAdmin.pending.length > 0 ? " (" + ZaloClient.groupAdmin.pending.length + ")" : "")
                        }
                        ZButton {
                            visible: ZaloClient.groupAdmin.pending.length > 1
                            variant: "tonal"
                            text: I18n.t("Duyệt tất cả", "Approve all")
                            onClicked: ZaloClient.groupAdmin.reviewAll(true)
                        }
                    }
                    Hint {
                        visible: root.canEdit && ZaloClient.groupAdmin.pending.length === 0
                        text: !ZaloClient.groupAdmin.pendingLoaded ? I18n.t("Đang tải…", "Loading…")
                              : ZaloClient.groupAdmin.settings.joinAppr === true ? I18n.t("Không có yêu cầu nào đang chờ.", "No pending requests.")
                              : I18n.t("Bật “Phê duyệt thành viên mới” để duyệt từng người trước khi vào nhóm.",
                                       "Turn on “Approve new members” to review people before they join.")
                    }
                    Repeater {
                        model: root.canEdit ? ZaloClient.groupAdmin.pending : []
                        delegate: PersonRow {
                            id: pendingRow
                            ZButton {
                                text: I18n.t("Duyệt", "Approve")
                                onClicked: ZaloClient.groupAdmin.reviewRequest(pendingRow.modelData.id, true)
                            }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Từ chối", "Decline")
                                onClicked: ZaloClient.groupAdmin.reviewRequest(pendingRow.modelData.id, false)
                            }
                        }
                    }

                    // ---- Blocked members
                    Rectangle { visible: root.canEdit; Layout.fillWidth: true; Layout.preferredHeight: 1; color: ThemeManager.dividerColor }
                    SectionTitle {
                        visible: root.canEdit
                        text: I18n.t("Chặn khỏi nhóm", "Blocked from the group")
                              + (ZaloClient.groupAdmin.blocked.length > 0 ? " (" + ZaloClient.groupAdmin.blocked.length + ")" : "")
                    }
                    Hint {
                        visible: root.canEdit && ZaloClient.groupAdmin.blocked.length === 0
                        text: !ZaloClient.groupAdmin.blockedLoaded ? I18n.t("Đang tải…", "Loading…")
                              : I18n.t("Chưa chặn ai. Người bị chặn không thể tự tham gia lại nhóm.",
                                       "Nobody is blocked. Blocked people cannot join the group again.")
                    }
                    Repeater {
                        model: root.canEdit ? ZaloClient.groupAdmin.blocked : []
                        delegate: PersonRow {
                            id: blockedRow
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Bỏ chặn", "Unblock")
                                onClicked: ZaloClient.groupAdmin.setBlocked(ZaloClient.groupAdmin.groupId, blockedRow.modelData.id, false)
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 4 }
                }
            }
        }
    }
}
