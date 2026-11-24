pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Zalos

Rectangle {
    id: root
    color: ThemeManager.backgroundColor

    readonly property string activeCategory: ZaloClient.contactModel.activeCategory
    readonly property bool compact: width < 620
    property string invitesTab: "received"
    readonly property bool showingSentRequests: root.activeCategory === "invites" && root.invitesTab === "sent"

    function hoverColor(): color {
        return (ThemeManager.hoverColor);
    }

    component CategoryItem: Rectangle {
        id: cat
        property string key: ""
        property string icon: ""
        property string label: ""
        property int count: 0
        readonly property bool selected: root.activeCategory === key

        Layout.fillWidth: true
        Layout.preferredHeight: 48
        radius: 8
        color: selected ? (ThemeManager.selectedColor) : (catMouse.containsMouse ? root.hoverColor() : ThemeManager.hoverClearColor)
        Behavior on color { ColorAnimation { duration: 140 } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            ZIcon {
                name: cat.icon
                size: 20
                color: cat.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
            }
            Text {
                Layout.fillWidth: true
                text: cat.label
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: cat.selected ? Font.DemiBold : Font.Normal
                color: cat.selected ? Theme.zaloBlue : ThemeManager.textColor
            }
            Text {
                text: cat.count
                visible: cat.count > 0
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }
        }

        MouseArea {
            id: catMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ZaloClient.contactModel.setCategoryFilter(cat.key)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            visible: !root.compact
            Layout.preferredWidth: Theme.sidebarWidth
            Layout.minimumWidth: 260
            Layout.maximumWidth: 360
            Layout.fillHeight: true
            color: ThemeManager.surfaceColor

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: ThemeManager.borderColor
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.rightMargin: 1
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 8

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        radius: 8
                        color: (ThemeManager.hoverColor)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 6

                            ZIcon { name: "search"; size: 14; color: ThemeManager.secondaryTextColor }

                            Timer {
                                id: contactSearchDebounce
                                interval: 150
                                property string targetText: ""
                                onTriggered: ZaloClient.contactModel.setSearchFilter(contactSearchDebounce.targetText)
                            }

                            TextField {
                                id: contactSearch
                                Layout.fillWidth: true
                                background: null
                                padding: 0
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: ThemeManager.textColor
                                placeholderText: I18n.t("Tìm kiếm", "Search")
                                placeholderTextColor: ThemeManager.secondaryTextColor
                                onTextChanged: {
                                    contactSearchDebounce.targetText = text;
                                    if (text.length === 0) {
                                        contactSearchDebounce.stop()
                                        ZaloClient.contactModel.setSearchFilter("")
                                    } else {
                                        contactSearchDebounce.restart()
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: 16
                        color: addMouse.containsMouse ? root.hoverColor() : ThemeManager.hoverClearColor
                        ZIcon { anchors.centerIn: parent; name: "user_add"; size: 18; color: ThemeManager.textColor }
                        MouseArea {
                            id: addMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ZaloClient.openAddFriend()
                        }
                        ToolTip.visible: addMouse.containsMouse
                        ToolTip.delay: 400
                        ToolTip.text: I18n.t("Thêm bạn", "Add friend")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    spacing: 2

                    CategoryItem {
                        key: "friends"
                        icon: "user"
                        label: I18n.t("Danh sách bạn bè", "Friends list")
                        count: ZaloClient.contactModel.friendCount
                    }
                    CategoryItem {
                        key: "groups"
                        icon: "contacts"
                        label: I18n.t("Danh sách nhóm và cộng đồng", "Joined groups and communities")
                        count: ZaloClient.contactModel.groupCount
                    }
                    CategoryItem {
                        key: "invites"
                        icon: "user_check"
                        label: I18n.t("Lời mời kết bạn", "Friend requests")
                        count: ZaloClient.contactModel.inviteCount
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: ThemeManager.chatBackgroundColor

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    color: ThemeManager.surfaceColor

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: ThemeManager.borderColor
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 12
                        spacing: 10

                        ZIcon {
                            name: root.activeCategory === "groups" ? "contacts" : "user"
                            size: 22
                            color: ThemeManager.textColor
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.activeCategory === "groups" ? I18n.t("Danh sách nhóm và cộng đồng", "Joined groups and communities")
                                  : root.activeCategory === "invites" ? I18n.t("Lời mời kết bạn", "Friend requests")
                                  : I18n.t("Danh sách bạn bè", "Friends list")
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }

                        Row {
                            visible: root.compact
                            spacing: 4
                            Repeater {
                                model: [
                                    { key: "friends", vi: "Bạn bè", en: "Friends" },
                                    { key: "groups", vi: "Nhóm", en: "Groups" },
                                    { key: "invites", vi: "Lời mời", en: "Requests" }
                                ]
                                Rectangle {
                                    required property var modelData
                                    readonly property bool selected: root.activeCategory === modelData.key
                                    width: chipText.implicitWidth + 20
                                    height: 28
                                    radius: 14
                                    color: selected ? Theme.zaloBlue : root.hoverColor()
                                    Text {
                                        id: chipText
                                        anchors.centerIn: parent
                                        text: I18n.t(modelData.vi, modelData.en)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: parent.selected ? "#FFFFFF" : ThemeManager.textColor
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: ZaloClient.contactModel.setCategoryFilter(modelData.key)
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    spacing: 12

                    Text {
                        text: (root.activeCategory === "groups" ? I18n.t("Nhóm", "Groups")
                               : root.activeCategory === "invites" ? (root.showingSentRequests ? I18n.t("Đã gửi", "Sent")
                                                                                               : I18n.t("Lời mời đã nhận", "Received"))
                               : I18n.t("Bạn bè", "Friends"))
                              + " (" + (root.showingSentRequests ? ZaloClient.sentFriendRequests.length : ZaloClient.contactModel.count) + ")"
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: ThemeManager.textColor
                    }

                    Flickable {
                        visible: root.activeCategory === "invites"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        clip: true
                        contentWidth: subTabsRow.implicitWidth
                        contentHeight: 30
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: contentWidth > width

                        Row {
                            id: subTabsRow
                            height: 28
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Repeater {
                                model: ListModel {
                                    ListElement { k: "received"; vi: "Đã nhận"; en: "Received" }
                                    ListElement { k: "sent"; vi: "Đã gửi"; en: "Sent" }
                                    ListElement { k: "group_invites"; vi: "Lời mời nhóm"; en: "Group invites" }
                                    ListElement { k: "recommended"; vi: "Gợi ý kết bạn"; en: "Recommended" }
                                }
                                Rectangle {
                                    id: subTabChip
                                    required property string k
                                    required property string vi
                                    required property string en
                                    readonly property bool on: root.invitesTab === subTabChip.k
                                    height: 28
                                    width: subTabText.implicitWidth + 20
                                    radius: 14
                                    color: subTabChip.on ? Theme.zaloBlueLight : ThemeManager.hoverColor
                                    Text {
                                        id: subTabText
                                        anchors.centerIn: parent
                                        text: I18n.t(subTabChip.vi, subTabChip.en)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        font.weight: subTabChip.on ? Font.DemiBold : Font.Normal
                                        color: subTabChip.on ? Theme.zaloBlueTonalText : ThemeManager.textColor
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.invitesTab = subTabChip.k;
                                            if (subTabChip.k === "sent") ZaloClient.refreshSentFriendRequests();
                                            else if (subTabChip.k === "group_invites") ZaloClient.refreshGroupInvites();
                                            else if (subTabChip.k === "recommended") ZaloClient.refreshFriendRecommendations();
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: root.activeCategory !== "invites" && !root.compact }
                    TextField {
                        visible: root.compact
                        Layout.preferredWidth: Math.min(200, root.width * 0.45)
                        Layout.preferredHeight: 32
                        placeholderText: I18n.t("Tìm kiếm", "Search")
                        placeholderTextColor: ThemeManager.placeholderColor
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        verticalAlignment: TextInput.AlignVCenter
                        leftPadding: 8
                        rightPadding: 8
                        background: Rectangle {
                            radius: 6
                            color: ThemeManager.inputFillColor
                            border.color: parent.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                            border.width: 1
                        }
                        onTextChanged: {
                            contactSearchDebounce.targetText = text;
                            if (text.length === 0) {
                                contactSearchDebounce.stop();
                                ZaloClient.contactModel.setSearchFilter("");
                            } else {
                                contactSearchDebounce.restart();
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: sentList
                        visible: root.showingSentRequests
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        clip: true
                        spacing: 4
                        model: ZaloClient.sentFriendRequests
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: sentRow
                            required property var modelData
                            width: sentList.width
                            height: 60
                            radius: 8
                            color: sentRowMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            MouseArea {
                                id: sentRowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if ((sentRow.modelData.uid || "").length > 0)
                                        ZaloClient.viewProfile(sentRow.modelData.uid);
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 10
                                ZAvatar {
                                    Layout.preferredWidth: 40
                                    Layout.preferredHeight: 40
                                    size: 40
                                    avatarUrl: sentRow.modelData.avatar
                                    name: sentRow.modelData.name
                                    showStatusDot: false
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: sentRow.modelData.name
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: sentRow.modelData.message.length > 0
                                        id: sentMsgText
                                        text: ZaloClient.linkifyText(sentRow.modelData.message)
                                        textFormat: Text.StyledText
                                        linkColor: Theme.linkColor
                                        onLinkActivated: function(link) { ZaloClient.openLink(link); }
                                        HoverHandler { cursorShape: sentMsgText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                Rectangle {
                                    Layout.preferredWidth: cancelReqText.implicitWidth + 20
                                    Layout.preferredHeight: 30
                                    radius: 6
                                    color: "transparent"
                                    border.color: ThemeManager.borderColor
                                    border.width: 1
                                    Text {
                                        id: cancelReqText
                                        anchors.centerIn: parent
                                        text: I18n.t("Thu hồi", "Cancel")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: Theme.badgeRed
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: ZaloClient.cancelSentFriendRequest(sentRow.modelData.uid)
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, 360)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: root.showingSentRequests && sentList.count === 0
                        text: I18n.t("Bạn chưa gửi lời mời kết bạn nào", "You haven't sent any friend requests")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: ThemeManager.secondaryTextColor
                    }

                    ListView {
                        id: contactsList
                        visible: !root.showingSentRequests && !(root.activeCategory === "invites" && (root.invitesTab === "group_invites" || root.invitesTab === "recommended"))
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        clip: true
                        model: ZaloClient.contactModel
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        reuseItems: true
                        cacheBuffer: 160

                        section.property: root.activeCategory === "invites" ? "" : "name"
                        section.criteria: ViewSection.FirstCharacter
                        section.delegate: Text {
                            required property string section
                            width: contactsList.width
                            height: 32
                            leftPadding: 8
                            verticalAlignment: Text.AlignVCenter
                            text: section.toUpperCase()
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }

                        delegate: Rectangle {
                            id: contactRow
                            required property string id
                            required property string name
                            required property string avatarUrl
                            required property bool isGroup
                            required property int memberCount
                            required property bool isOnline
                            required property string category
                            required property string subtitle

                            width: contactsList.width
                            radius: 8
                            color: rowMouse.containsMouse ? root.hoverColor() : ThemeManager.hoverClearColor
                            Behavior on color { ColorAnimation { duration: 140 } }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.openConversationWithContact(contactRow.id, contactRow.name, contactRow.avatarUrl)
                            }
                            readonly property bool isInvite: contactRow.category === "invites"
                            height: isInvite ? 76 : 64

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 12

                                Item {
                                    Layout.preferredWidth: 44
                                    Layout.preferredHeight: 44

                                    ZAvatar {
                                        anchors.fill: parent
                                        size: 44
                                        avatarUrl: contactRow.avatarUrl || ""
                                        name: contactRow.name
                                        isGroup: contactRow.isGroup
                                        memberCount: contactRow.memberCount
                                        memberAvatars: contactRow.isGroup ? ZaloClient.groupComposite(contactRow.id, contactRow.memberCount) : []
                                        isOnline: contactRow.isOnline
                                        showStatusDot: true
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: !contactRow.isGroup ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            if (!contactRow.isGroup) {
                                                ZaloClient.viewProfile(contactRow.id);
                                            } else {
                                                ZaloClient.openConversationWithContact(contactRow.id, contactRow.name, contactRow.avatarUrl);
                                            }
                                        }
                                        ToolTip.visible: containsMouse && !contactRow.isGroup
                                        ToolTip.text: I18n.t("Xem thông tin", "View profile")
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        text: contactRow.name
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: contactRow.isInvite && (contactRow.subtitle || "").length > 0
                                        text: contactRow.subtitle || ""
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: contactRow.isGroup && contactRow.memberCount > 0
                                        text: contactRow.memberCount + I18n.t(" thành viên", " members")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }

                                Row {
                                    visible: contactRow.isInvite
                                    spacing: 6
                                    Repeater {
                                        model: [ { accept: false, vi: "Từ chối", en: "Decline" },
                                                 { accept: true, vi: "Đồng ý", en: "Accept" } ]
                                        Rectangle {
                                            required property var modelData
                                            width: inviteLabel.implicitWidth + 24
                                            height: 30
                                            radius: 6
                                            color: modelData.accept ? (inviteMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue)
                                                                    : (inviteMouse.containsMouse ? root.hoverColor() : (ThemeManager.hoverColor))
                                            Text {
                                                id: inviteLabel
                                                anchors.centerIn: parent
                                                text: I18n.t(modelData.vi, modelData.en)
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: modelData.accept ? "#FFFFFF" : ThemeManager.textColor
                                            }
                                            MouseArea {
                                                id: inviteMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: modelData.accept ? ZaloClient.acceptFriendRequest(contactRow.id)
                                                                            : ZaloClient.rejectFriendRequest(contactRow.id)
                                            }
                                        }
                                    }
                                }

                                Row {
                                    visible: (rowMouse.containsMouse || actVoiceMouse.containsMouse || actVideoMouse.containsMouse || actChatMouse.containsMouse) && !contactRow.isInvite
                                    spacing: 4

                                    Rectangle {
                                        visible: contactRow.id !== ZaloClient.myCloudId
                                        width: 32
                                        height: 32
                                        radius: 16
                                        color: actVoiceMouse.pressed ? ThemeManager.selectedColor :
                                               (actVoiceMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                                        scale: actVoiceMouse.pressed ? 0.92 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 100 } }

                                        ZIcon {
                                            anchors.centerIn: parent
                                            name: "phone"
                                            size: 16
                                            color: ThemeManager.textColor
                                        }

                                        MouseArea {
                                            id: actVoiceMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            hoverEnabled: true
                                            ToolTip.visible: containsMouse
                                            ToolTip.delay: 350
                                            ToolTip.text: contactRow.isGroup
                                                          ? I18n.t("Cuộc gọi thoại nhóm", "Group voice call")
                                                          : I18n.t("Cuộc gọi thoại", "Voice call")
                                            onClicked: {
                                                ZaloClient.openConversationWithContact(contactRow.id, contactRow.name, contactRow.avatarUrl);
                                                if (contactRow.isGroup) ZaloClient.startGroupCall(false);
                                                else ZaloClient.startVoiceCall();
                                            }
                                        }
                                    }

                                    Rectangle {
                                        visible: contactRow.id !== ZaloClient.myCloudId
                                        width: 32
                                        height: 32
                                        radius: 16
                                        color: actVideoMouse.pressed ? ThemeManager.selectedColor :
                                               (actVideoMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                                        scale: actVideoMouse.pressed ? 0.92 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 100 } }

                                        ZIcon {
                                            anchors.centerIn: parent
                                            name: "video"
                                            size: 16
                                            color: ThemeManager.textColor
                                        }

                                        MouseArea {
                                            id: actVideoMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            hoverEnabled: true
                                            ToolTip.visible: containsMouse
                                            ToolTip.delay: 350
                                            ToolTip.text: contactRow.isGroup
                                                          ? I18n.t("Cuộc gọi video nhóm", "Group video call")
                                                          : I18n.t("Cuộc gọi video", "Video call")
                                            onClicked: {
                                                ZaloClient.openConversationWithContact(contactRow.id, contactRow.name, contactRow.avatarUrl);
                                                if (contactRow.isGroup) ZaloClient.startGroupCall(true);
                                                else ZaloClient.startVideoCall();
                                            }
                                        }
                                    }

                                    Rectangle {
                                        width: 32
                                        height: 32
                                        radius: 16
                                        color: actChatMouse.pressed ? ThemeManager.selectedColor :
                                               (actChatMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                                        scale: actChatMouse.pressed ? 0.92 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 100 } }

                                        ZIcon {
                                            anchors.centerIn: parent
                                            name: "chat"
                                            size: 16
                                            color: ThemeManager.textColor
                                        }

                                        MouseArea {
                                            id: actChatMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            hoverEnabled: true
                                            ToolTip.visible: containsMouse
                                            ToolTip.delay: 350
                                            ToolTip.text: I18n.t("Nhắn tin", "Chat")
                                            onClicked: ZaloClient.openConversationWithContact(contactRow.id, contactRow.name, contactRow.avatarUrl)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        id: groupInviteList
                        anchors.fill: parent
                        clip: true
                        visible: root.activeCategory === "invites" && root.invitesTab === "group_invites"
                        model: ZaloClient.groupInvites
                        delegate: Item {
                            id: grpInvRow
                            required property var modelData
                            readonly property string grpId: grpInvRow.modelData.groupId || ""
                            readonly property string grpName: grpInvRow.modelData.name || grpInvRow.grpId
                            readonly property string grpAvatar: grpInvRow.modelData.avatar || ""
                            readonly property string inviterName: grpInvRow.modelData.inviterName || ""
                            readonly property int memberCount: grpInvRow.modelData.memberCount || 0
                            width: groupInviteList.width
                            height: 68
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 12
                                ZAvatar {
                                    size: 40
                                    avatarUrl: grpInvRow.grpAvatar
                                    name: grpInvRow.grpName
                                    isGroup: true
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: grpInvRow.grpName
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.weight: Font.DemiBold
                                        font.pixelSize: 13
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: (grpInvRow.inviterName.length > 0 ? I18n.t("Người mời: ", "Invited by: ") + grpInvRow.inviterName + " · " : "")
                                              + grpInvRow.memberCount + " " + I18n.t("thành viên", "members")
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                ZButton {
                                    variant: "primary"
                                    text: I18n.t("Tham gia", "Join")
                                    onClicked: ZaloClient.joinGroupInvite(grpInvRow.grpId)
                                }
                                ZButton {
                                    variant: "secondary"
                                    text: I18n.t("Xóa", "Delete")
                                    onClicked: ZaloClient.deleteGroupInvite(grpInvRow.grpId, false)
                                }
                            }
                        }
                    }

                    ListView {
                        id: recommList
                        anchors.fill: parent
                        clip: true
                        visible: root.activeCategory === "invites" && root.invitesTab === "recommended"
                        model: ZaloClient.friendRecommendations
                        delegate: Item {
                            id: recRow
                            required property var modelData
                            readonly property string uid: recRow.modelData.id || ""
                            readonly property string userName: recRow.modelData.name || recRow.uid
                            readonly property string userAvatar: recRow.modelData.avatar || ""
                            readonly property string recMsg: recRow.modelData.message || ""
                            readonly property bool reqSent: !!recRow.modelData.isSent
                            width: recommList.width
                            height: 68
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 12
                                ZAvatar {
                                    size: 40
                                    avatarUrl: recRow.userAvatar
                                    name: recRow.userName
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: recRow.userName
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.weight: Font.DemiBold
                                        font.pixelSize: 13
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: recRow.recMsg.length > 0
                                        id: recMsgText
                                        text: ZaloClient.linkifyText(recRow.recMsg)
                                        textFormat: Text.StyledText
                                        linkColor: Theme.linkColor
                                        onLinkActivated: function(link) { ZaloClient.openLink(link); }
                                        HoverHandler { cursorShape: recMsgText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                ZButton {
                                    variant: "primary"
                                    enabled: !recRow.reqSent
                                    text: recRow.reqSent ? I18n.t("Đã gửi", "Sent") : I18n.t("Kết bạn", "Add friend")
                                    onClicked: ZaloClient.sendFriendRequest(recRow.uid, I18n.t("Xin chào, kết bạn với mình nhé!", "Hi, let's connect on Zalo!"))
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, 360)
                        visible: !root.showingSentRequests && contactsList.count === 0 && !(root.activeCategory === "invites" && (root.invitesTab === "group_invites" || root.invitesTab === "recommended"))
                        spacing: 10

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            visible: ZaloClient.isSyncing
                            running: visible && (Window.window ? (Window.window.visible && Window.window.visibility !== Window.Minimized) : true)
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: ZaloClient.isSyncing ? I18n.t("Đang tải danh bạ...", "Loading contacts...")
                                : ZaloClient.contactModel.searchQuery.length > 0 ? I18n.t("Không tìm thấy kết quả phù hợp", "No matching results")
                                : root.activeCategory === "groups" ? I18n.t("Bạn chưa tham gia nhóm nào", "You haven't joined any groups")
                                : root.activeCategory === "invites" ? I18n.t("Không có lời mời kết bạn nào", "No friend requests")
                                : I18n.t("Danh bạ chưa có bạn bè", "No friends yet")
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            color: ThemeManager.secondaryTextColor
                        }
                        Button {
                            id: reloadContactsBtn
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredHeight: 34
                            Layout.preferredWidth: 90
                            visible: !ZaloClient.isSyncing && ZaloClient.isLoggedIn
                            background: Rectangle {
                                radius: 6
                                color: reloadContactsBtn.pressed ? Theme.zaloBlueHover : (reloadContactsBtn.hovered ? Theme.zaloBlueHover : Theme.zaloBlue)
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                            contentItem: Text {
                                text: I18n.t("Tải lại", "Reload")
                                color: "#FFFFFF"
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: {
                                ZaloClient.refreshContacts();
                                ZaloClient.refreshFriendRequests();
                            }
                        }
                    }
                }
            }
        }
    }
}
