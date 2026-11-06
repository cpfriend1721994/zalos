import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Dialogs
import Zalos

Rectangle {
    id: root
    Layout.fillHeight: true
    Layout.preferredWidth: ZaloClient.showConversationInfo ? 280 : 0
    visible: ZaloClient.showConversationInfo
    clip: true
    color: ThemeManager.surfaceColor
    border.color: ThemeManager.borderColor
    border.width: 1

    readonly property bool hasPinState: typeof ZaloClient.currentConversationPinned === "boolean"
    readonly property bool hasMuteState: typeof ZaloClient.currentConversationMuted === "boolean"
    readonly property bool isPinned: hasPinState && ZaloClient.currentConversationPinned
    readonly property bool isMuted: hasMuteState && ZaloClient.currentConversationMuted
    readonly property bool hasConversation: ZaloClient.currentConversationId.length > 0

    property var members: []
    property string myRole: ""
    property bool showAllMembers: false
    readonly property int memberLimit: 30
    property var sharedMedia: []
    property var sharedFiles: []
    property var sharedLinks: []

    function refreshMembers(): void {
        if (!root.visible || !ZaloClient.currentConversationIsGroup) {
            root.members = [];
            root.myRole = "";
            return;
        }
        root.members = ZaloClient.groupMembers(ZaloClient.currentConversationId);
        root.myRole = ZaloClient.myGroupRole(ZaloClient.currentConversationId);
    }

    function refreshShared(): void {
        if (!root.visible) return;
        root.sharedMedia = ZaloClient.messageModel.sharedItems("media", 60);
        root.sharedFiles = ZaloClient.messageModel.sharedItems("file", 30);
        root.sharedLinks = ZaloClient.messageModel.sharedItems("link", 30);
    }

    function openViewer(msgId: string): void {
        const list = ZaloClient.messageModel.mediaItems();
        let start = 0;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].msgId === msgId) { start = i; break; }
        }
        ImageViewerState.show(list, start);
    }

    property string confirmAction: ""
    property string confirmTarget: ""
    property string confirmName: ""
    function askConfirm(action: string, target: string, name: string): void {
        root.confirmAction = action;
        root.confirmTarget = target;
        root.confirmName = name;
        confirmDialog.open();
    }

    function dueLabel(ms: real): string {
        if (!ms || ms <= 0) return "";
        var d = new Date(ms);
        var p = (n) => ("0" + n).slice(-2);
        var text = p(d.getDate()) + "/" + p(d.getMonth() + 1) + " " + p(d.getHours()) + ":" + p(d.getMinutes());
        return (ms < Date.now() ? I18n.t("Quá hạn ", "Overdue ") : I18n.t("Hạn ", "Due ")) + text;
    }

    Component.onCompleted: { root.refreshMembers(); root.refreshShared(); }
    onVisibleChanged: { root.refreshMembers(); root.refreshShared(); }
    Connections {
        target: ZaloClient
        function onCurrentConversationChanged() {
            root.showAllMembers = false;
            root.refreshMembers();
            root.refreshShared();
        }
        function onGroupMembersChanged(groupId: string) {
            if (groupId === ZaloClient.currentConversationId) root.refreshMembers();
        }
    }
    Connections {
        target: ZaloClient.messageModel
        function onRowsInserted() { sharedTimer.restart(); }
        function onModelReset() { sharedTimer.restart(); }
    }
    Timer { id: sharedTimer; interval: 400; onTriggered: root.refreshShared() }

    Dialog {
        id: confirmDialog
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 340
        onAboutToShow: leaveSilentCheck.checked = false
        title: root.confirmAction === "disperse" ? I18n.t("Giải tán nhóm?", "Disband group?")
             : root.confirmAction === "owner" ? I18n.t("Chuyển quyền trưởng nhóm?", "Transfer ownership?")
             : root.confirmAction === "block" ? I18n.t("Chặn khỏi nhóm?", "Block from group?")
             : root.confirmAction === "leave" ? I18n.t("Rời nhóm?", "Leave group?")
             : I18n.t("Xóa thành viên?", "Remove member?")
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            width: parent.width
            spacing: 12

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: ThemeManager.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 13
                text: root.confirmAction === "disperse"
                      ? I18n.t("Tất cả thành viên sẽ bị xóa khỏi nhóm và tin nhắn nhóm sẽ không còn. Không thể hoàn tác.",
                               "Everyone is removed and the group's messages are gone. This cannot be undone.")
                      : root.confirmAction === "owner"
                        ? I18n.t("%1 sẽ trở thành trưởng nhóm, bạn trở thành thành viên.", "%1 becomes the owner; you become a member.").arg(root.confirmName)
                        : root.confirmAction === "block"
                          ? I18n.t("%1 sẽ bị xóa khỏi nhóm và không thể tự tham gia lại cho đến khi được bỏ chặn.",
                                   "%1 is removed and cannot join again until unblocked.").arg(root.confirmName)
                          : root.confirmAction === "leave"
                            ? I18n.t("Bạn sẽ rời khỏi nhóm này và không còn nhận được tin nhắn của nhóm.",
                                     "You will leave this group and stop receiving its messages.")
                            : I18n.t("Xóa %1 khỏi nhóm?", "Remove %1 from the group?").arg(root.confirmName)
            }

            ZCheckBox {
                id: leaveSilentCheck
                visible: root.confirmAction === "leave"
                Layout.fillWidth: true
                checked: false
                text: I18n.t("Rời nhóm trong im lặng (không thông báo cho thành viên khác)",
                             "Leave quietly (don't notify other members)")
                onToggled: (value) => leaveSilentCheck.checked = value
            }
        }

        onAccepted: {
            const gid = ZaloClient.currentConversationId;
            if (root.confirmAction === "disperse") ZaloClient.disperseGroup(gid);
            else if (root.confirmAction === "owner") ZaloClient.transferGroupOwner(gid, root.confirmTarget);
            else if (root.confirmAction === "kick") ZaloClient.removeMemberFromGroup(gid, root.confirmTarget);
            else if (root.confirmAction === "block") ZaloClient.groupAdmin.setBlocked(gid, root.confirmTarget, true);
            else if (root.confirmAction === "leave") ZaloClient.leaveGroup(gid, leaveSilentCheck.checked);
        }
    }

    Dialog {
        id: reminderDialog
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 320
        title: I18n.t("Thêm nhắc hẹn", "Add reminder")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            const d = new Date(Date.now() + 3600000);
            const p = (n) => ("0" + n).slice(-2);
            remTitle.text = "";
            remEmoji.text = "⏰";
            remDate.text = p(d.getDate()) + "/" + p(d.getMonth() + 1) + "/" + d.getFullYear();
            remTime.text = p(d.getHours()) + ":" + p(d.getMinutes());
            remRepeat.currentIndex = 0;
        }
        ColumnLayout {
            width: parent.width
            spacing: 8
            ZTextField {
                id: remTitle
                Layout.fillWidth: true
                placeholderText: I18n.t("Nội dung nhắc hẹn", "Reminder title")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZTextField { id: remDate; Layout.fillWidth: true; placeholderText: "dd/MM/yyyy" }
                ZTextField { id: remTime; Layout.preferredWidth: 90; placeholderText: "HH:mm" }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZTextField { id: remEmoji; Layout.preferredWidth: 70; placeholderText: "⏰" }
                ZComboBox {
                    id: remRepeat
                    Layout.fillWidth: true
                    model: [I18n.t("Không lặp", "No repeat"), I18n.t("Hàng ngày", "Daily"),
                            I18n.t("Hàng tuần", "Weekly"), I18n.t("Hàng tháng", "Monthly")]
                }
            }
        }
        onAccepted: {
            const t = remTitle.text.trim();
            if (t.length === 0) return;
            const dm = remDate.text.split("/");
            const tm = remTime.text.split(":");
            let ms = Date.now() + 3600000;
            if (dm.length === 3 && tm.length >= 2) {
                const dd = new Date(parseInt(dm[2]), parseInt(dm[1]) - 1, parseInt(dm[0]), parseInt(tm[0]), parseInt(tm[1]));
                if (!isNaN(dd.getTime())) ms = dd.getTime();
            }
            ZaloClient.createReminder(t, ms, remEmoji.text.trim(), remRepeat.currentIndex);
        }
    }

    component QuickAction: ColumnLayout {
        id: qa
        property string iconName: ""
        property string caption: ""
        property bool active: false
        property bool actionEnabled: true
        signal triggered()

        spacing: 6
        Layout.preferredWidth: 76
        Layout.alignment: Qt.AlignTop

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 18
            opacity: qa.actionEnabled ? 1.0 : 0.5
            color: qa.active ? Theme.zaloBlue
                             : (qaMouse.containsMouse ? (ThemeManager.hoverColor)
                                                      : (ThemeManager.hoverColor))

            ZIcon {
                anchors.centerIn: parent
                name: qa.iconName
                size: 18
                color: qa.active ? "#FFFFFF" : ThemeManager.textColor
            }

            MouseArea {
                id: qaMouse
                anchors.fill: parent
                enabled: qa.actionEnabled
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: qa.triggered()
            }
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qa.caption
            font.family: Theme.fontFamily
            font.pixelSize: 11
            color: ThemeManager.secondaryTextColor
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 56

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Thông tin hội thoại", "Conversation info")
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.bold: true
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: closeInfoMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

                    ZIcon {
                        anchors.centerIn: parent
                        name: "close"
                        size: 16
                        color: ThemeManager.secondaryTextColor
                    }

                    MouseArea {
                        id: closeInfoMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ZaloClient.toggleConversationInfo()
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: ThemeManager.borderColor
            }
        }

        ScrollView {
            id: bodyScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: bodyScroll.availableWidth
                spacing: 16

                Item { Layout.preferredHeight: 4 }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 8

                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64

                        ZAvatar {
                            anchors.fill: parent
                            size: 64
                            avatarUrl: ZaloClient.currentConversationAvatar
                            name: ZaloClient.currentConversationName
                            isGroup: ZaloClient.currentConversationIsGroup
                            memberCount: ZaloClient.currentConversationMemberCount
                            memberAvatars: ZaloClient.currentConversationIsGroup ? ZaloClient.groupComposite(ZaloClient.currentConversationId, ZaloClient.currentConversationMemberCount) : []
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: (!ZaloClient.currentConversationIsGroup || ZaloClient.currentConversationAvatar.length > 0)
                                         ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: {
                                if (!ZaloClient.currentConversationIsGroup) {
                                    ZaloClient.viewProfile(ZaloClient.currentConversationId);
                                } else if (ZaloClient.currentConversationAvatar.length > 0) {
                                    ImageViewerState.show([{
                                        url: ZaloClient.currentConversationAvatar,
                                        thumb: ZaloClient.currentConversationAvatar,
                                        senderName: ZaloClient.currentConversationName,
                                        isVideo: false
                                    }], 0);
                                }
                            }
                            ToolTip.visible: containsMouse && (!ZaloClient.currentConversationIsGroup || ZaloClient.currentConversationAvatar.length > 0)
                            ToolTip.text: !ZaloClient.currentConversationIsGroup
                                          ? I18n.t("Xem thông tin cá nhân", "View profile")
                                          : I18n.t("Xem ảnh đại diện nhóm", "View group avatar")
                        }

                        Rectangle {
                            visible: ZaloClient.currentConversationIsGroup && (root.myRole === "owner" || root.myRole === "admin")
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: -4
                            anchors.bottomMargin: -4
                            width: 26
                            height: 26
                            radius: 13
                            color: groupPhotoMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor
                            border.color: ThemeManager.surfaceColor
                            border.width: 2
                            ZIcon { anchors.centerIn: parent; name: "camera"; size: 14; color: ThemeManager.textColor }
                            MouseArea {
                                id: groupPhotoMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: groupPhotoDialog.open()
                                ToolTip.visible: containsMouse
                                ToolTip.text: I18n.t("Đổi ảnh nhóm", "Change group photo")
                            }
                        }
                        FileDialog {
                            id: groupPhotoDialog
                            title: I18n.t("Chọn ảnh nhóm", "Choose the group photo")
                            nameFilters: [I18n.t("Hình ảnh (*.jpg *.jpeg *.png *.webp)", "Images (*.jpg *.jpeg *.png *.webp)")]
                            onAccepted: ZaloClient.changeGroupAvatar(ZaloClient.currentConversationId, selectedFile.toString())
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        spacing: 6

                        property bool isEditingName: false

                        Text {
                            visible: !parent.isEditingName
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: ZaloClient.currentConversationName
                            font.family: Theme.fontFamily
                            font.bold: true
                            font.pixelSize: 16
                            color: ThemeManager.textColor
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            visible: !parent.isEditingName && root.hasConversation
                            width: 24
                            height: 24
                            radius: 12
                            color: renameHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: "edit"; size: 14; color: ThemeManager.secondaryTextColor }
                            MouseArea {
                                id: renameHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    parent.parent.isEditingName = true
                                    nameEditField.text = ZaloClient.currentConversationName
                                    nameEditField.forceActiveFocus()
                                }
                            }
                            ToolTip.visible: renameHover.containsMouse
                            ToolTip.text: ZaloClient.currentConversationIsGroup ? I18n.t("Đổi tên nhóm", "Rename group") : I18n.t("Đổi biệt danh", "Edit alias")
                        }

                        TextField {
                            id: nameEditField
                            visible: parent.isEditingName
                            Layout.fillWidth: true
                            color: ThemeManager.textColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            background: Rectangle {
                                color: ThemeManager.inputFillColor
                                radius: 4
                                border.color: Theme.zaloBlue
                                border.width: 1
                            }
                            onAccepted: {
                                if (text.trim().length > 0 || !ZaloClient.currentConversationIsGroup) {
                                    if (ZaloClient.currentConversationIsGroup) {
                                        ZaloClient.changeGroupName(ZaloClient.currentConversationId, text.trim())
                                    } else {
                                        ZaloClient.changeFriendAlias(ZaloClient.currentConversationId, text.trim())
                                    }
                                }
                                parent.isEditingName = false
                            }
                        }

                        Rectangle {
                            visible: parent.isEditingName
                            width: 24
                            height: 24
                            radius: 12
                            color: Theme.zaloBlue
                            ZIcon { anchors.centerIn: parent; name: "check"; size: 14; color: "#FFFFFF" }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (nameEditField.text.trim().length > 0 || !ZaloClient.currentConversationIsGroup) {
                                        if (ZaloClient.currentConversationIsGroup) {
                                            ZaloClient.changeGroupName(ZaloClient.currentConversationId, nameEditField.text.trim())
                                        } else {
                                            ZaloClient.changeFriendAlias(ZaloClient.currentConversationId, nameEditField.text.trim())
                                        }
                                    }
                                    parent.parent.isEditingName = false
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8

                    QuickAction {
                        actionEnabled: root.hasConversation
                        iconName: root.isMuted ? "bell_off" : "bell"
                        active: root.isMuted
                        caption: !root.hasMuteState ? I18n.t("Bật/tắt thông báo", "Mute / unmute")
                                 : (root.isMuted ? I18n.t("Bật thông báo", "Unmute")
                                                 : I18n.t("Tắt thông báo", "Mute"))
                        onTriggered: ZaloClient.muteConversation(ZaloClient.currentConversationId)
                    }

                    QuickAction {
                        actionEnabled: root.hasConversation
                        iconName: "pin"
                        active: root.isPinned
                        caption: !root.hasPinState ? I18n.t("Ghim/bỏ ghim", "Pin / unpin")
                                 : (root.isPinned ? I18n.t("Bỏ ghim", "Unpin")
                                                  : I18n.t("Ghim hội thoại", "Pin chat"))
                        onTriggered: ZaloClient.pinConversation(ZaloClient.currentConversationId)
                    }

                    QuickAction {
                        visible: ZaloClient.currentConversationIsGroup
                        actionEnabled: root.hasConversation
                        iconName: "poll"
                        caption: I18n.t("Bình chọn", "Poll")
                        onTriggered: ZaloClient.openCreatePollModal()
                    }

                    QuickAction {
                        visible: !ZaloClient.currentConversationIsGroup && root.hasConversation
                        actionEnabled: root.hasConversation
                        iconName: "user"
                        caption: I18n.t("Hồ sơ", "Profile")
                        onTriggered: ZaloClient.viewProfile(ZaloClient.currentConversationId)
                    }

                    QuickAction {
                        visible: !ZaloClient.currentConversationIsGroup && root.hasConversation
                        actionEnabled: root.hasConversation
                        iconName: "contacts"
                        caption: I18n.t("Chia sẻ danh thiếp", "Share card")
                        onTriggered: ZaloClient.openShareCard(ZaloClient.currentConversationId, ZaloClient.currentConversationName)
                    }

                    QuickAction {
                        actionEnabled: root.hasConversation
                        iconName: "group_add"
                        caption: I18n.t("Tạo nhóm", "Create group")
                        onTriggered: ZaloClient.openCreateGroup()
                    }
                }

                ColumnLayout {
                    visible: ZaloClient.currentConversationIsGroup
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: ThemeManager.dividerColor
                    }

                    Text {
                        text: I18n.t("Cài đặt nhóm", "Group settings")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: true
                        color: ThemeManager.textColor
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 6
                        color: manageHover.containsMouse ? ThemeManager.selectedColor : ThemeManager.hoverColor
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 8
                            ZIcon { name: "settings"; size: 16; color: ThemeManager.iconColor }
                            Text {
                                Layout.fillWidth: true
                                text: root.myRole === "owner" || root.myRole === "admin"
                                      ? I18n.t("Quản lý nhóm", "Manage group")
                                      : I18n.t("Link và quyền trong nhóm", "Link and group permissions")
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: ThemeManager.textColor
                            }
                            ZIcon { name: "chevron_right"; size: 14; color: ThemeManager.secondaryTextColor }
                        }
                        MouseArea {
                            id: manageHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ZaloClient.groupAdmin.openManage(ZaloClient.currentConversationId)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 6
                        color: leaveHover.containsMouse ? ThemeManager.errorBackgroundColor : ThemeManager.hoverColor

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 8

                            ZIcon {
                                name: "leave"
                                size: 16
                                color: Theme.badgeRed
                            }

                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Rời nhóm", "Leave group")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: Theme.badgeRed
                            }
                        }

                        MouseArea {
                            id: leaveHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.askConfirm("leave", "", "")
                        }
                    }

                    Rectangle {
                        visible: root.myRole === "owner"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        radius: 6
                        color: disperseHover.containsMouse ? ThemeManager.errorBackgroundColor : ThemeManager.hoverColor
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 8
                            ZIcon { name: "trash"; size: 16; color: Theme.badgeRed }
                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Giải tán nhóm", "Disband group")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: Theme.badgeRed
                            }
                        }
                        MouseArea {
                            id: disperseHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.askConfirm("disperse", "", "")
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: ThemeManager.borderColor
                }

                ColumnLayout {
                    id: autoDeleteSection
                    visible: root.hasConversation
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 6

                    property double curTtl: 0
                    readonly property list<int> ttlOptions: [0, 86400000, 604800000, 1209600000]
                    function refreshTtl(): void {
                        autoDeleteSection.curTtl = ZaloClient.conversationAutoDelete(ZaloClient.currentConversationId);
                    }
                    function ttlLabel(ms: int): string {
                        if (ms <= 0) return I18n.t("Tắt", "Off");
                        if (ms < 604800000) return I18n.t("1 ngày", "1 day");
                        if (ms < 1209600000) return I18n.t("7 ngày", "7 days");
                        return I18n.t("14 ngày", "14 days");
                    }
                    Component.onCompleted: autoDeleteSection.refreshTtl()
                    Connections {
                        target: ZaloClient
                        function onConversationFlagsChanged() { autoDeleteSection.refreshTtl(); }
                        function onCurrentConversationChanged() { autoDeleteSection.refreshTtl(); }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ZIcon { name: "clock"; size: 16; color: Theme.zaloBlue }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Tin nhắn tự xóa", "Disappearing messages")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: autoDeleteSection.ttlOptions
                            delegate: Rectangle {
                                id: ttlChip
                                required property int modelData
                                readonly property bool active: autoDeleteSection.curTtl === modelData
                                implicitWidth: ttlChipText.implicitWidth + 20
                                implicitHeight: 28
                                radius: 14
                                color: ttlChip.active ? Theme.zaloBlueLight : ThemeManager.inputFillColor
                                border.color: ttlChip.active ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: 1
                                Text {
                                    id: ttlChipText
                                    anchors.centerIn: parent
                                    text: autoDeleteSection.ttlLabel(ttlChip.modelData)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: ttlChip.active ? Theme.zaloBlueTonalText : ThemeManager.textColor
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ZaloClient.setConversationAutoDelete(ZaloClient.currentConversationId, ttlChip.modelData)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: hideRow
                    objectName: "infoHideRow"
                    property bool hiddenNow: false
                    property bool canHide: false
                    function refresh(): void {
                        hideRow.hiddenNow = ZaloClient.isConversationHidden(ZaloClient.currentConversationId);
                        hideRow.canHide = ZaloClient.canHideConversation(ZaloClient.currentConversationId);
                    }
                    visible: root.hasConversation && (hideRow.hiddenNow || hideRow.canHide)
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.preferredHeight: 36
                    radius: 6
                    color: hideRowMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.hoverClearColor
                    Component.onCompleted: hideRow.refresh()
                    Connections {
                        target: ZaloClient
                        function onConversationFlagsChanged() { hideRow.refresh(); }
                        function onCurrentConversationChanged() { hideRow.refresh(); }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        spacing: 8
                        ZIcon { name: "eye_off"; size: 16; color: ThemeManager.secondaryTextColor }
                        Text {
                            Layout.fillWidth: true
                            text: hideRow.hiddenNow ? I18n.t("Bỏ ẩn trò chuyện", "Unhide conversation")
                                                    : I18n.t("Ẩn trò chuyện", "Hide conversation")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                        }
                    }
                    MouseArea {
                        id: hideRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ZaloClient.requestHiddenFlow(hideRow.hiddenNow ? "unhide" : "hide",
                                                                ZaloClient.currentConversationId, true)
                    }
                }

                Rectangle {
                    visible: root.hasConversation
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: ThemeManager.borderColor
                }

                ColumnLayout {
                    visible: root.hasConversation
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ZIcon { name: "reminder"; size: 16; color: Theme.zaloBlue }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Nhắc hẹn", "Reminders")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Rectangle {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            radius: 13
                            color: addReminderMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: "plus"; size: 15; color: Theme.zaloBlue }
                            MouseArea {
                                id: addReminderMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: reminderDialog.open()
                                ToolTip.visible: containsMouse
                                ToolTip.text: I18n.t("Thêm nhắc hẹn", "Add reminder")
                            }
                        }
                    }

                    Text {
                        visible: ZaloClient.reminders.length === 0
                        Layout.fillWidth: true
                        text: ZaloClient.isLoadingReminders ? I18n.t("Đang tải…", "Loading…")
                                                            : I18n.t("Chưa có nhắc hẹn nào", "No reminders yet")
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }

                    Repeater {
                        model: ZaloClient.reminders
                        delegate: Rectangle {
                            id: remRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 42
                            radius: 6
                            color: remRowMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            MouseArea { id: remRowMouse; anchors.fill: parent; hoverEnabled: true }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 4
                                spacing: 8
                                Text {
                                    text: remRow.modelData.emoji && remRow.modelData.emoji.length > 0 ? remRow.modelData.emoji : "⏰"
                                    font.pixelSize: 16
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        Layout.fillWidth: true
                                        text: remRow.modelData.title
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: text.length > 0
                                        text: root.dueLabel(remRow.modelData.startTime)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    radius: 13
                                    color: remDelMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                    ZIcon { anchors.centerIn: parent; name: "trash"; size: 14; color: Theme.badgeRed }
                                    MouseArea {
                                        id: remDelMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: ZaloClient.removeReminder(remRow.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: ThemeManager.borderColor
                }

                ColumnLayout {
                    visible: ZaloClient.currentConversationIsGroup && root.hasConversation
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ZIcon { name: "contacts"; size: 16; color: Theme.zaloBlue }
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Thành viên", "Members") + " (" + Math.max(ZaloClient.currentConversationMemberCount, root.members.length) + ")"
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Text {
                            visible: root.members.length > root.memberLimit
                            text: root.showAllMembers ? I18n.t("Thu gọn", "Collapse") : I18n.t("Xem tất cả", "Show all")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: Theme.zaloBlue
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.showAllMembers = !root.showAllMembers
                            }
                        }
                    }

                    Text {
                        visible: root.members.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: ZaloClient.isLoggedIn ? I18n.t("Đang tải danh sách thành viên…", "Loading members…")
                                                    : I18n.t("Cần kết nối để xem thành viên", "Connect to see members")
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }

                    Repeater {
                        model: root.showAllMembers ? root.members : root.members.slice(0, root.memberLimit)
                        delegate: Rectangle {
                            id: memberRow
                            required property var modelData
                            readonly property bool canManage: !memberRow.modelData.isMe
                                && (root.myRole === "owner" || (root.myRole === "admin" && memberRow.modelData.role === "member"))
                            Layout.fillWidth: true
                            implicitHeight: 44
                            radius: 6
                            color: memberHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            HoverHandler { id: memberHover }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                spacing: 8
                                ZAvatar {
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    size: 32
                                    avatarUrl: memberRow.modelData.avatar
                                    name: memberRow.modelData.name
                                    showStatusDot: false
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        Layout.fillWidth: true
                                        text: (memberRow.modelData.name.length > 0 ? memberRow.modelData.name : I18n.t("Đang tải tên…", "Loading name…"))
                                              + (memberRow.modelData.isMe ? I18n.t(" (Bạn)", " (You)") : "")
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        color: memberRow.modelData.name.length > 0 ? ThemeManager.textColor : ThemeManager.secondaryTextColor
                                    }
                                    Text {
                                        visible: memberRow.modelData.role !== "member"
                                        text: memberRow.modelData.role === "owner" ? I18n.t("Trưởng nhóm", "Owner") : I18n.t("Phó nhóm", "Deputy")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: Theme.zaloBlue
                                    }
                                }
                                Rectangle {
                                    visible: memberRow.canManage && (memberHover.hovered || memberMenu.opened)
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    radius: 13
                                    color: moreMouse.containsMouse ? ThemeManager.selectedColor : ThemeManager.hoverClearColor
                                    ZIcon { anchors.centerIn: parent; name: "more_horiz"; size: 16; color: ThemeManager.secondaryTextColor }
                                    MouseArea {
                                        id: moreMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: memberMenu.popup()
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                z: -1
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (!memberRow.modelData.isMe) ZaloClient.viewProfile(memberRow.modelData.id)
                            }

                            Menu {
                                id: memberMenu
                                MenuItem {
                                    visible: root.myRole === "owner"
                                    height: visible ? implicitHeight : 0
                                    text: memberRow.modelData.role === "admin" ? I18n.t("Gỡ quyền phó nhóm", "Remove as deputy")
                                                                               : I18n.t("Thêm làm phó nhóm", "Make deputy")
                                    onTriggered: ZaloClient.setGroupDeputy(ZaloClient.currentConversationId, memberRow.modelData.id,
                                                                           memberRow.modelData.role !== "admin")
                                }
                                MenuItem {
                                    visible: root.myRole === "owner"
                                    height: visible ? implicitHeight : 0
                                    text: I18n.t("Chuyển quyền trưởng nhóm", "Transfer ownership")
                                    onTriggered: root.askConfirm("owner", memberRow.modelData.id, memberRow.modelData.name)
                                }
                                MenuItem {
                                    text: I18n.t("Xóa khỏi nhóm", "Remove from group")
                                    onTriggered: root.askConfirm("kick", memberRow.modelData.id, memberRow.modelData.name)
                                }
                                MenuItem {
                                    text: I18n.t("Chặn khỏi nhóm", "Block from group")
                                    onTriggered: root.askConfirm("block", memberRow.modelData.id, memberRow.modelData.name)
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: root.hasConversation
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 8

                    RowLayout {
                        spacing: 8
                        ZIcon { name: "image"; size: 16; color: Theme.zaloBlue }
                        Text {
                            text: I18n.t("Ảnh/Video", "Photos/Videos") + (root.sharedMedia.length > 0 ? " (" + root.sharedMedia.length + ")" : "")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                    }
                    GridLayout {
                        visible: root.sharedMedia.length > 0
                        Layout.fillWidth: true
                        columns: 4
                        columnSpacing: 4
                        rowSpacing: 4
                        Repeater {
                            model: root.sharedMedia.slice(0, 12)
                            delegate: Item {
                                id: mediaCell
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: width
                                Image {
                                    anchors.fill: parent
                                    source: mediaCell.modelData.thumb
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    sourceSize.width: 160
                                    clip: true
                                }
                                ZIcon {
                                    visible: mediaCell.modelData.isVideo
                                    anchors.centerIn: parent
                                    name: "play"
                                    size: 20
                                    color: "#FFFFFF"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openViewer(mediaCell.modelData.msgId)
                                }
                            }
                        }
                    }
                    Text {
                        visible: root.sharedMedia.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: I18n.t("Chưa có ảnh/video trong các tin nhắn đã tải", "No photos or videos in the loaded messages")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }

                    RowLayout {
                        Layout.topMargin: 8
                        spacing: 8
                        ZIcon { name: "file"; size: 16; color: Theme.zaloBlue }
                        Text {
                            text: I18n.t("File", "Files") + (root.sharedFiles.length > 0 ? " (" + root.sharedFiles.length + ")" : "")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                    }
                    Repeater {
                        model: root.sharedFiles.slice(0, 5)
                        delegate: Text {
                            id: fileLine
                            required property var modelData
                            Layout.fillWidth: true
                            text: fileLine.modelData.title
                            elide: Text.ElideMiddle
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.textColor
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.openFileMessage(fileLine.modelData.url, fileLine.modelData.title)
                            }
                        }
                    }
                    Text {
                        visible: root.sharedFiles.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: I18n.t("Chưa có file trong các tin nhắn đã tải", "No files in the loaded messages")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }

                    RowLayout {
                        Layout.topMargin: 8
                        spacing: 8
                        ZIcon { name: "link"; size: 16; color: Theme.zaloBlue }
                        Text {
                            text: I18n.t("Link", "Links") + (root.sharedLinks.length > 0 ? " (" + root.sharedLinks.length + ")" : "")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                    }
                    Repeater {
                        model: root.sharedLinks.slice(0, 5)
                        delegate: Text {
                            id: linkLine
                            required property var modelData
                            Layout.fillWidth: true
                            text: linkLine.modelData.title
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: Theme.linkColor
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.openLink(linkLine.modelData.url)
                            }
                        }
                    }
                    Text {
                        visible: root.sharedLinks.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: I18n.t("Chưa có link trong các tin nhắn đã tải", "No links in the loaded messages")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Item { Layout.preferredHeight: 12 }
            }
        }
    }
}
