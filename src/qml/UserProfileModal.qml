import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Zalos

Item {
    id: root
    anchors.fill: parent
    visible: ZaloClient.showUserProfileModal
    z: 999

    property bool isEditing: false
    property int editGender: ZaloClient.profileGender

    function formatPhone(raw: string): string {
        var digits = "";
        for (var i = 0; i < raw.length; i++) {
            var c = raw.charAt(i);
            if (c >= "0" && c <= "9") digits += c;
        }
        if (digits.length === 0) return raw;
        var national = digits;
        var prefix = "";
        if (digits.substring(0, 2) === "84") {
            prefix = "+84 ";
            national = digits.substring(2);
        } else if (digits.charAt(0) === "0") {
            national = digits.substring(1);
            prefix = "0";
        }
        var grouped = "";
        for (var j = 0; j < national.length; j++) {
            if (j > 0 && (national.length - j) % 3 === 0) grouped += " ";
            grouped += national.charAt(j);
        }
        return prefix + grouped;
    }

    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (!visible) {
            isEditing = false;
        } else {
            editGender = ZaloClient.profileGender;
            if (nameEdit) nameEdit.text = ZaloClient.profileDisplayName;
            if (dobEdit) dobEdit.text = ZaloClient.profileDob;
            if (bioEdit) bioEdit.text = ZaloClient.profileBio;
            if (ZaloClient.profileIsSelf) ZaloClient.loadMyAvatarHistory();
        }
    }

    Connections {
        target: ZaloClient
        function onCurrentProfileChanged() {
            if (!root.isEditing) {
                root.editGender = ZaloClient.profileGender;
                if (nameEdit) nameEdit.text = ZaloClient.profileDisplayName;
                if (dobEdit) dobEdit.text = ZaloClient.profileDob;
                if (bioEdit) bioEdit.text = ZaloClient.profileBio;
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.visible ? 0.5 : 0.0
        Behavior on opacity { NumberAnimation { duration: 160 } }

        MouseArea {
            anchors.fill: parent
            onClicked: ZaloClient.closeUserProfile()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 32, 380))
        height: Math.max(0, Math.min(parent.height - 40,
                avatarBorder.y + avatarBorder.height + 8 + bodyColumn.implicitHeight + 16))
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        scale: root.visible ? 1.0 : 0.92
        opacity: root.visible ? 1.0 : 0.0
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
        radius: Theme.radiusLarge
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        clip: true

        MouseArea { anchors.fill: parent }

        Item {
            id: coverArea
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 120
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: ThemeManager.isDarkMode ? "#1E293B" : Theme.zaloBlue }
                    GradientStop { position: 1.0; color: ThemeManager.isDarkMode ? "#0F172A" : Theme.zaloBlueHover }
                }
            }

            Image {
                anchors.fill: parent
                visible: (ZaloClient.profileCover || "").length > 0
                source: ZaloClient.profileCover
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                width: 30
                height: 30
                radius: 15
                color: closeMouse.containsMouse ? Qt.rgba(1, 1, 1, 1.0) : Qt.rgba(1, 1, 1, 0.85)
                border.color: Qt.rgba(0, 0, 0, 0.08)
                border.width: 1

                ZIcon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 14
                    color: "#1F2937"
                }

                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onClicked: ZaloClient.closeUserProfile()
                }
            }
        }

        Rectangle {
            visible: ZaloClient.profileIsSelf
            anchors.right: coverArea.right
            anchors.bottom: coverArea.bottom
            anchors.margins: 10
            z: 3
            width: 28
            height: 28
            radius: 14
            color: Qt.rgba(0, 0, 0, coverInfoMouse.containsMouse ? 0.55 : 0.4)
            ZIcon { anchors.centerIn: parent; name: "camera"; size: 15; color: "#FFFFFF" }
            MouseArea {
                id: coverInfoMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                ToolTip.visible: containsMouse
                ToolTip.text: I18n.t("Ảnh bìa chỉ đổi được trong ứng dụng Zalo trên điện thoại",
                                     "The cover photo can only be changed in the Zalo phone app")
                onClicked: ToolTip.show(ToolTip.text, 3000)
            }
        }

        Rectangle {
            id: avatarBorder
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.top: coverArea.bottom
            anchors.topMargin: -38
            width: 76
            height: 76
            radius: 38
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.surfaceColor
            border.width: 3
            z: 2

            ZAvatar {
                anchors.centerIn: parent
                size: 70
                avatarUrl: ZaloClient.profileAvatar
                name: ZaloClient.profileDisplayName
                showStatusDot: !ZaloClient.profileIsSelf
                isOnline: !ZaloClient.isOfflineMode
            }

            Rectangle {
                visible: ZaloClient.profileIsSelf
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 28
                height: 28
                radius: 14
                color: avatarEditMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor
                border.color: ThemeManager.surfaceColor
                border.width: 2
                ZIcon { anchors.centerIn: parent; name: "camera"; size: 15; color: ThemeManager.textColor }
                MouseArea {
                    id: avatarEditMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: avatarDialog.open()
                    ToolTip.visible: containsMouse
                    ToolTip.text: I18n.t("Đổi ảnh đại diện", "Change avatar")
                }
            }
        }

        FileDialog {
            id: avatarDialog
            title: I18n.t("Chọn ảnh đại diện", "Choose your avatar")
            nameFilters: [I18n.t("Hình ảnh (*.jpg *.jpeg *.png *.webp)", "Images (*.jpg *.jpeg *.png *.webp)")]
            onAccepted: ZaloClient.changeMyAvatar(selectedFile.toString())
        }

        ScrollView {
            id: bodyScroll
            anchors.top: avatarBorder.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 16
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                id: bodyColumn
                width: bodyScroll.availableWidth
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextEdit {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            text: ZaloClient.profileDisplayName.length > 0
                                  ? ZaloClient.profileDisplayName
                                  : I18n.t("Người dùng Zalo", "Zalo User")
                            font.family: Theme.fontFamily
                            font.bold: true
                            font.pixelSize: 18
                            color: ThemeManager.textColor
                            selectionColor: Theme.zaloBlueLight
                            wrapMode: TextEdit.Wrap
                        }

                        Rectangle {
                            Layout.preferredHeight: 22
                            Layout.preferredWidth: badgeTxt.implicitWidth + 12
                            radius: 11
                            color: ZaloClient.profileIsSelf
                                   ? Theme.zaloBlueLight
                                   : ThemeManager.hoverColor

                            Text {
                                id: badgeTxt
                                anchors.centerIn: parent
                                text: ZaloClient.profileIsSelf
                                      ? I18n.t("Bạn", "You")
                                      : (ZaloClient.profileIsFriend ? I18n.t("Bạn bè", "Friend") : I18n.t("Người lạ", "Stranger"))
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                font.bold: true
                                color: ZaloClient.profileIsSelf ? Theme.zaloBlueTonalText : ThemeManager.secondaryTextColor
                            }
                        }
                    }

                    TextEdit {
                        visible: ZaloClient.profileZaloName.length > 0 && ZaloClient.profileZaloName !== ZaloClient.profileDisplayName
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        text: I18n.t("Tên Zalo: ", "Zalo Name: ") + ZaloClient.profileZaloName
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        selectionColor: Theme.zaloBlueLight
                        wrapMode: TextEdit.Wrap
                    }

                    Rectangle {
                        id: statusCard
                        property bool editingStatus: false
                        readonly property bool hasStatus: ZaloClient.profileBio.length > 0
                        visible: !root.isEditing && (ZaloClient.profileIsSelf || statusCard.hasStatus)
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        implicitHeight: statusRow.implicitHeight + 16
                        radius: 8
                        color: ThemeManager.inputFillColor

                        function startEdit(): void {
                            statusField.text = ZaloClient.profileBio;
                            statusCard.editingStatus = true;
                            statusField.forceActiveFocus();
                        }
                        function save(): void {
                            ZaloClient.updateMyStatus(statusField.text);
                            statusCard.editingStatus = false;
                        }

                        Connections {
                            target: ZaloClient
                            function onCurrentProfileChanged() { statusCard.editingStatus = false; }
                        }

                        RowLayout {
                            id: statusRow
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            anchors.topMargin: 8
                            anchors.bottomMargin: 8
                            spacing: 8

                            ZIcon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 2
                                name: "chat"
                                size: 14
                                color: Theme.zaloBlue
                            }

                            TextEdit {
                                visible: !statusCard.editingStatus
                                Layout.fillWidth: true
                                readOnly: true
                                selectByMouse: statusCard.hasStatus
                                text: statusCard.hasStatus ? ZaloClient.profileBio
                                                           : I18n.t("Thêm trạng thái để bạn bè biết bạn đang thế nào…",
                                                                    "Add a status so friends know how you're doing…")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.italic: !statusCard.hasStatus
                                color: statusCard.hasStatus ? ThemeManager.textColor : ThemeManager.placeholderColor
                                selectionColor: Theme.zaloBlueLight
                                wrapMode: TextEdit.Wrap

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: ZaloClient.profileIsSelf && !statusCard.hasStatus
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: statusCard.startEdit()
                                }
                            }

                            ZTextField {
                                id: statusField
                                visible: statusCard.editingStatus
                                Layout.fillWidth: true
                                maximumLength: 140
                                placeholderText: I18n.t("Trạng thái của bạn", "Your status")
                                onAccepted: statusCard.save()
                                Keys.onEscapePressed: statusCard.editingStatus = false
                            }

                            Rectangle {
                                visible: ZaloClient.profileIsSelf && !statusCard.editingStatus
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 12
                                color: statusEditMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor
                                ZIcon { anchors.centerIn: parent; name: "edit"; size: 13; color: ThemeManager.secondaryTextColor }
                                MouseArea {
                                    id: statusEditMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: statusCard.startEdit()
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: I18n.t("Sửa trạng thái", "Edit status")
                                }
                            }

                            Rectangle {
                                visible: statusCard.editingStatus
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                radius: 14
                                color: statusSaveMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue
                                ZIcon { anchors.centerIn: parent; name: "check"; size: 14; color: "#FFFFFF" }
                                MouseArea {
                                    id: statusSaveMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: statusCard.save()
                                }
                            }

                            Rectangle {
                                visible: statusCard.editingStatus
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                radius: 14
                                color: statusCancelMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor
                                ZIcon { anchors.centerIn: parent; name: "close"; size: 12; color: ThemeManager.secondaryTextColor }
                                MouseArea {
                                    id: statusCancelMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: statusCard.editingStatus = false
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
                    visible: !root.isEditing
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: I18n.t("Thông tin cá nhân", "Personal Information")
                        font.family: Theme.fontFamily
                        font.bold: true
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ZIcon { name: "phone"; size: 16; color: ThemeManager.secondaryTextColor }

                        Text {
                            text: I18n.t("Điện thoại", "Phone")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.secondaryTextColor
                            Layout.preferredWidth: 80
                        }

                        TextEdit {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            text: (ZaloClient.profilePhone || "").length > 0
                                  ? root.formatPhone(ZaloClient.profilePhone)
                                  : I18n.t("Không công khai", "Private")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            selectionColor: Theme.zaloBlueLight
                            wrapMode: TextEdit.Wrap
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ZIcon { name: "user"; size: 16; color: ThemeManager.secondaryTextColor }

                        Text {
                            text: I18n.t("Giới tính", "Gender")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.secondaryTextColor
                            Layout.preferredWidth: 80
                        }

                        TextEdit {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            text: ZaloClient.profileGender === 1
                                  ? I18n.t("Nữ", "Female")
                                  : I18n.t("Nam", "Male")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            selectionColor: Theme.zaloBlueLight
                            wrapMode: TextEdit.Wrap
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ZIcon { name: "clock"; size: 16; color: ThemeManager.secondaryTextColor }

                        Text {
                            text: I18n.t("Ngày sinh", "Birthday")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.secondaryTextColor
                            Layout.preferredWidth: 80
                        }

                        TextEdit {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            text: (ZaloClient.profileDob || "").length > 0
                                  ? ZaloClient.profileDob
                                  : I18n.t("Chưa cập nhật", "Not updated")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            selectionColor: Theme.zaloBlueLight
                            wrapMode: TextEdit.Wrap
                        }
                    }

                    Item { Layout.preferredHeight: 8 }

                    ColumnLayout {
                        visible: ZaloClient.profileIsSelf
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            radius: 6
                            color: Theme.zaloBlueLight
                            border.color: Theme.zaloBlue
                            border.width: 1

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 8
                                ZIcon { name: "edit"; size: 15; color: Theme.zaloBlueTonalText }
                                Text {
                                    text: I18n.t("Chỉnh sửa thông tin", "Edit profile")
                                    font.family: Theme.fontFamily
                                    font.bold: true
                                    font.pixelSize: 13
                                    color: Theme.zaloBlueTonalText
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    nameEdit.text = ZaloClient.profileDisplayName
                                    dobEdit.text = ZaloClient.profileDob
                                    bioEdit.text = ZaloClient.profileBio
                                    root.editGender = ZaloClient.profileGender
                                    root.isEditing = true
                                }
                            }
                        }

                        Text {
                            visible: ZaloClient.myAvatarHistory.length > 0
                            text: I18n.t("Ảnh đại diện trước đây", "Previous avatars")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                            Layout.topMargin: 4
                        }

                        ListView {
                            visible: ZaloClient.myAvatarHistory.length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: 64
                            orientation: ListView.Horizontal
                            spacing: 8
                            clip: true
                            model: ZaloClient.myAvatarHistory
                            delegate: Item {
                                id: avatarCell
                                required property var modelData
                                width: 60
                                height: 60
                                ZRoundedImage {
                                    anchors.fill: parent
                                    radius: 8
                                    source: avatarCell.modelData.thumb.length > 0 ? avatarCell.modelData.thumb : avatarCell.modelData.url
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ZaloClient.reuseMyAvatar(avatarCell.modelData.photoId)
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: I18n.t("Dùng lại ảnh này", "Use this avatar again")
                                }
                                Rectangle {
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: Qt.rgba(0, 0, 0, 0.6)
                                    ZIcon { anchors.centerIn: parent; name: "close"; size: 10; color: "#FFFFFF" }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: ZaloClient.deleteMyAvatar(avatarCell.modelData.photoId)
                                    }
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        visible: !ZaloClient.profileIsSelf
                        Layout.fillWidth: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                radius: 6
                                color: Theme.zaloBlue

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    ZIcon { name: "chat"; size: 15; color: "#FFFFFF" }
                                    Text {
                                        text: I18n.t("Nhắn tin", "Chat")
                                        font.family: Theme.fontFamily
                                        font.bold: true
                                        font.pixelSize: 13
                                        color: "#FFFFFF"
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        ZaloClient.openConversationWithContact(ZaloClient.profileUserId, ZaloClient.profileDisplayName, ZaloClient.profileAvatar)
                                        ZaloClient.closeUserProfile()
                                    }
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 38
                                radius: 6
                                color: voiceCallMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor
                                border.color: ThemeManager.borderColor
                                border.width: 1

                                ZIcon { anchors.centerIn: parent; name: "phone"; size: 16; color: ThemeManager.textColor }

                                MouseArea {
                                    id: voiceCallMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        ZaloClient.closeUserProfile()
                                        ZaloClient.startVoiceCall()
                                    }
                                }
                                ToolTip.visible: voiceCallMouse.containsMouse
                                ToolTip.text: I18n.t("Gọi thoại", "Voice call")
                            }

                            Rectangle {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 38
                                radius: 6
                                color: videoCallMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor
                                border.color: ThemeManager.borderColor
                                border.width: 1

                                ZIcon { anchors.centerIn: parent; name: "video"; size: 16; color: ThemeManager.textColor }

                                MouseArea {
                                    id: videoCallMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        ZaloClient.closeUserProfile()
                                        ZaloClient.startVideoCall()
                                    }
                                }
                                ToolTip.visible: videoCallMouse.containsMouse
                                ToolTip.text: I18n.t("Gọi video", "Video call")
                            }
                        }

                        ZButton {
                            visible: ZaloClient.isLoggedIn && ZaloClient.profileUserId.length > 0
                            Layout.fillWidth: true
                            variant: "secondary"
                            iconName: "contacts"
                            text: ZaloClient.profileIsSelf ? I18n.t("Chia sẻ danh thiếp của tôi", "Share my contact card")
                                                           : I18n.t("Chia sẻ danh thiếp", "Share contact card")
                            onClicked: {
                                const uid = ZaloClient.profileUserId;
                                const name = ZaloClient.profileDisplayName;
                                ZaloClient.closeUserProfile();
                                ZaloClient.openShareCard(uid, name);
                            }
                        }

                        ZButton {
                            visible: ZaloClient.isLoggedIn && !ZaloClient.profileIsSelf && ZaloClient.profileUserId.length > 0
                            Layout.fillWidth: true
                            variant: "secondary"
                            iconName: "group_add"
                            text: I18n.t("Thêm vào nhóm", "Add to groups")
                            onClicked: groupPicker.open()
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                visible: ZaloClient.profileIsFriend
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                radius: 6
                                color: "transparent"
                                border.color: ThemeManager.borderColor
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: I18n.t("Xóa bạn", "Remove friend")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: Theme.badgeRed
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ZaloClient.removeFriend(ZaloClient.profileUserId)
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                radius: 6
                                color: "transparent"
                                border.color: ThemeManager.borderColor
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: ZaloClient.profileIsBlocked
                                          ? I18n.t("Bỏ chặn", "Unblock")
                                          : I18n.t("Chặn người này", "Block user")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: ZaloClient.profileIsBlocked ? Theme.onlineGreen : ThemeManager.secondaryTextColor
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (ZaloClient.profileIsBlocked) {
                                            ZaloClient.unblockUser(ZaloClient.profileUserId)
                                        } else {
                                            ZaloClient.blockUser(ZaloClient.profileUserId)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: root.isEditing
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: I18n.t("Chỉnh sửa thông tin tài khoản", "Edit Profile")
                        font.family: Theme.fontFamily
                        font.bold: true
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: I18n.t("Tên hiển thị", "Display name")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                        TextField {
                            id: nameEdit
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            background: Rectangle {
                                radius: 6
                                color: ThemeManager.inputFillColor
                                border.color: nameEdit.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: 1
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: I18n.t("Giới tính", "Gender")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: 6
                                color: root.editGender === 0 ? Theme.zaloBlueLight : ThemeManager.inputFillColor
                                border.color: root.editGender === 0 ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: I18n.t("Nam", "Male")
                                    font.family: Theme.fontFamily
                                    font.bold: root.editGender === 0
                                    font.pixelSize: 13
                                    color: root.editGender === 0 ? Theme.zaloBlue : ThemeManager.textColor
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.editGender = 0
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: 6
                                color: root.editGender === 1 ? Theme.zaloBlueLight : ThemeManager.inputFillColor
                                border.color: root.editGender === 1 ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: I18n.t("Nữ", "Female")
                                    font.family: Theme.fontFamily
                                    font.bold: root.editGender === 1
                                    font.pixelSize: 13
                                    color: root.editGender === 1 ? Theme.zaloBlue : ThemeManager.textColor
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.editGender = 1
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: I18n.t("Ngày sinh (dd/MM/yyyy)", "Birthday (dd/MM/yyyy)")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                        TextField {
                            id: dobEdit
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            placeholderText: "dd/MM/yyyy"
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            background: Rectangle {
                                radius: 6
                                color: ThemeManager.inputFillColor
                                border.color: dobEdit.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: 1
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: I18n.t("Tiểu sử / Giới thiệu", "Bio / Status")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                        TextField {
                            id: bioEdit
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            placeholderText: I18n.t("Nhập trạng thái hoặc câu nói yêu thích...", "Enter bio or status...")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            background: Rectangle {
                                radius: 6
                                color: ThemeManager.inputFillColor
                                border.color: bioEdit.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: 1
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 6 }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            radius: 6
                            color: ThemeManager.hoverColor

                            Text {
                                anchors.centerIn: parent
                                text: I18n.t("Hủy", "Cancel")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: ThemeManager.textColor
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.isEditing = false
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            radius: 6
                            color: Theme.zaloBlue

                            Text {
                                anchors.centerIn: parent
                                text: I18n.t("Lưu thay đổi", "Save")
                                font.family: Theme.fontFamily
                                font.bold: true
                                font.pixelSize: 13
                                color: "#FFFFFF"
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    ZaloClient.updateMyProfile(nameEdit.text, root.editGender, dobEdit.text, bioEdit.text)
                                    root.isEditing = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: groupPicker
        parent: root
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 380)
        height: Math.min(root.height - 40, 520)
        modal: true
        padding: 16
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property var groups: []
        property var picked: ({})
        property int pickedCount: 0
        onAboutToShow: {
            groups = ZaloClient.groupsForInvite(ZaloClient.profileUserId);
            picked = ({});
            pickedCount = 0;
            groupSearch.text = "";
        }
        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: 12
            border.color: ThemeManager.borderColor
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: I18n.t("Thêm %1 vào nhóm", "Add %1 to groups").arg(ZaloClient.profileDisplayName)
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.bold: true
                color: ThemeManager.textColor
            }
            ZTextField {
                id: groupSearch
                Layout.fillWidth: true
                placeholderText: I18n.t("Tìm nhóm", "Search groups")
            }
            ListView {
                id: groupList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: {
                    const q = groupSearch.text.trim().toLowerCase();
                    return q.length === 0 ? groupPicker.groups
                                          : groupPicker.groups.filter(g => g.name.toLowerCase().indexOf(q) >= 0);
                }
                delegate: Rectangle {
                    id: groupRow
                    required property var modelData
                    readonly property bool chosen: groupPicker.picked[groupRow.modelData.id] === true
                    width: groupList.width
                    height: 44
                    radius: 6
                    color: groupRowMouse.containsMouse && !groupRow.modelData.isMember ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    opacity: groupRow.modelData.isMember ? 0.6 : 1.0
                    function flip(): void {
                        if (groupRow.modelData.isMember) return;
                        const next = Object.assign({}, groupPicker.picked);
                        if (next[groupRow.modelData.id]) delete next[groupRow.modelData.id];
                        else next[groupRow.modelData.id] = true;
                        groupPicker.picked = next;
                        groupPicker.pickedCount = Object.keys(next).length;
                    }
                    MouseArea {
                        id: groupRowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: groupRow.modelData.isMember ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: groupRow.flip()
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 10
                        ZCheckBox {
                            round: true
                            enabled: !groupRow.modelData.isMember
                            checked: groupRow.modelData.isMember || groupRow.chosen
                            onToggled: groupRow.flip()
                        }
                        ZAvatar {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            size: 32
                            avatarUrl: groupRow.modelData.avatar
                            name: groupRow.modelData.name
                            showStatusDot: false
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: groupRow.modelData.name
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                        }
                        Text {
                            visible: groupRow.modelData.isMember
                            text: I18n.t("Đã tham gia", "Member")
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: groupList.count === 0
                    text: groupPicker.groups.length === 0 ? I18n.t("Chưa có nhóm nào", "No groups yet")
                                                          : I18n.t("Không tìm thấy nhóm", "No matching groups")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                ZButton {
                    variant: "secondary"
                    text: I18n.t("Hủy", "Cancel")
                    onClicked: groupPicker.close()
                }
                ZButton {
                    enabled: groupPicker.pickedCount > 0
                    text: groupPicker.pickedCount > 0 ? I18n.t("Thêm (%1)", "Add (%1)").arg(groupPicker.pickedCount)
                                                      : I18n.t("Thêm", "Add")
                    onClicked: {
                        ZaloClient.groupAdmin.inviteToGroups(ZaloClient.profileUserId, Object.keys(groupPicker.picked));
                        groupPicker.close();
                    }
                }
            }
        }
    }
}
