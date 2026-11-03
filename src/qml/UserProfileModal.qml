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

    // Created on demand by a Loader in Main.qml, already visible: run the same init.
    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (!visible) {
            isEditing = false;
        } else {
            editGender = ZaloClient.profileGender;
            if (nameEdit) nameEdit.text = ZaloClient.profileDisplayName;
            if (dobEdit) dobEdit.text = ZaloClient.profileDob;
            if (bioEdit) bioEdit.text = ZaloClient.profileBio;
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

    // Backdrop
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
        height: Math.max(0, Math.min(parent.height - 40, root.isEditing ? 560 : 520))
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

        // Swallow clicks
        MouseArea { anchors.fill: parent }

        // Top Cover Image / Gradient
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

            // Close button pinned to the top-right
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                width: 30
                height: 30
                radius: 15
                color: "#000000"
                opacity: closeMouse.containsMouse ? 0.75 : 0.45

                ZIcon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 14
                    color: "#FFFFFF"
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

        // The cover photo has no desktop API (Zalo PC cannot change it either): say where it can be changed.
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

        // Avatar overlapping the cover area
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

            // Own profile: change the avatar (/api/profile/upavatar).
            Rectangle {
                visible: ZaloClient.profileIsSelf
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 28
                height: 28
                radius: 14
                color: avatarEditMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.inputFillColor
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

        // Scrollable content body
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
                width: bodyScroll.availableWidth
                spacing: 12

                // Header info (Name, alias, bio)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: ZaloClient.profileDisplayName.length > 0
                                  ? ZaloClient.profileDisplayName
                                  : I18n.t("Người dùng Zalo", "Zalo User")
                            font.family: Theme.fontFamily
                            font.bold: true
                            font.pixelSize: 18
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                        }

                        // Badge
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

                    Text {
                        visible: ZaloClient.profileZaloName.length > 0 && ZaloClient.profileZaloName !== ZaloClient.profileDisplayName
                        Layout.fillWidth: true
                        text: I18n.t("Tên Zalo: ", "Zalo Name: ") + ZaloClient.profileZaloName
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: (ZaloClient.profileBio || "").length > 0 && !root.isEditing
                        Layout.fillWidth: true
                        text: "\"" + ZaloClient.profileBio + "\""
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.italic: true
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: ThemeManager.borderColor
                }

                // ==========================================
                // VIEW MODE
                // ==========================================
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

                    // Phone
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

                        Text {
                            Layout.fillWidth: true
                            text: (ZaloClient.profilePhone || "").length > 0
                                  ? ZaloClient.profilePhone
                                  : I18n.t("Không công khai", "Private")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                        }
                    }

                    // Gender
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

                        Text {
                            Layout.fillWidth: true
                            text: ZaloClient.profileGender === 1
                                  ? I18n.t("Nữ", "Female")
                                  : I18n.t("Nam", "Male")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                        }
                    }

                    // DOB
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

                        Text {
                            Layout.fillWidth: true
                            text: (ZaloClient.profileDob || "").length > 0
                                  ? ZaloClient.profileDob
                                  : I18n.t("Chưa cập nhật", "Not updated")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                        }
                    }

                    Item { Layout.preferredHeight: 8 }

                    // ACTION BUTTONS FOR SELF
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
                    }

                    // ACTION BUTTONS FOR OTHER USER
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
                                // Secondary button look: visible on the card in light and dark (hoverColor alone was not).
                                color: voiceCallMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.inputFillColor
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
                                // Secondary button look: visible on the card in light and dark (hoverColor alone was not).
                                color: videoCallMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.inputFillColor
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

                        // Invite this person to several groups at once (/api/group/invite/multi).
                        ZButton {
                            visible: ZaloClient.isLoggedIn && !ZaloClient.profileIsSelf && ZaloClient.profileUserId.length > 0
                            Layout.fillWidth: true
                            variant: "secondary"
                            iconName: "group_add"
                            text: I18n.t("Thêm vào nhóm", "Add to groups")
                            onClicked: groupPicker.open()
                        }

                        // Relationship management (Remove friend / Block)
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

                // ==========================================
                // EDIT MODE (Chỉ cho tài khoản cá nhân)
                // ==========================================
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

                    // Tên hiển thị
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

                    // Giới tính
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

                    // Ngày sinh
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

                    // Tiểu sử / Bio
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

                    // Save / Cancel buttons
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

    // Group picker for "Add to groups": groups the person is already in are shown but not selectable.
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
