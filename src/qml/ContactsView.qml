import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
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
                font.bold: cat.selected
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
                                onTextChanged: ZaloClient.contactModel.setSearchFilter(text)
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
                            font.bold: true
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
                        font.bold: true
                        color: ThemeManager.textColor
                    }

                    RowLayout {
                        visible: root.activeCategory === "invites"
                        spacing: 6
                        Repeater {
                            model: [
                                { k: "received", vi: "Đã nhận", en: "Received" },
                                { k: "sent", vi: "Đã gửi", en: "Sent" }
                            ]
                            Rectangle {
                                required property var modelData
                                readonly property bool on: root.invitesTab === modelData.k
                                Layout.preferredHeight: 28
                                Layout.preferredWidth: subTabText.implicitWidth + 20
                                radius: 14
                                color: on ? Theme.zaloBlueLight : ThemeManager.hoverColor
                                Text {
                                    id: subTabText
                                    anchors.centerIn: parent
                                    text: I18n.t(parent.modelData.vi, parent.modelData.en)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    font.bold: parent.on
                                    color: parent.on ? Theme.zaloBlueTonalText : ThemeManager.textColor
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.invitesTab = parent.modelData.k;
                                        if (parent.modelData.k === "sent") ZaloClient.refreshSentFriendRequests();
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
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
                        onTextChanged: ZaloClient.contactModel.setSearchFilter(text)
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
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: sentRow.modelData.message.length > 0
                                        text: sentRow.modelData.message
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
                        Text {
                            anchors.centerIn: parent
                            visible: sentList.count === 0
                            text: I18n.t("Bạn chưa gửi lời mời kết bạn nào", "You haven't sent any friend requests")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    ListView {
                        id: contactsList
                        visible: !root.showingSentRequests
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        clip: true
                        model: ZaloClient.contactModel
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        reuseItems: true
                        cacheBuffer: 300

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
                            font.bold: true
                            color: ThemeManager.textColor
                        }

                        delegate: Rectangle {
                            id: contactRow
                            required property var model
                            width: contactsList.width
                            radius: 8
                            color: rowMouse.containsMouse ? root.hoverColor() : ThemeManager.hoverClearColor
                            Behavior on color { ColorAnimation { duration: 140 } }

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.openConversationWithContact(model.id, model.name, model.avatarUrl)
                            }
                            readonly property bool isInvite: model.category === "invites"
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
                                        avatarUrl: model.avatarUrl || ""
                                        name: model.name
                                        isGroup: model.isGroup
                                        memberCount: model.memberCount
                                        memberAvatars: model.isGroup ? ZaloClient.groupComposite(model.id, model.memberCount) : []
                                        isOnline: model.isOnline
                                        showStatusDot: true
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: !model.isGroup ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            if (!model.isGroup) {
                                                ZaloClient.viewProfile(model.id);
                                            } else {
                                                ZaloClient.openConversationWithContact(model.id, model.name, model.avatarUrl);
                                            }
                                        }
                                        ToolTip.visible: containsMouse && !model.isGroup
                                        ToolTip.text: I18n.t("Xem thông tin", "View profile")
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        text: model.name
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: contactRow.isInvite && (model.subtitle || "").length > 0
                                        text: model.subtitle || ""
                                        elide: Text.ElideRight
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: model.isGroup && model.memberCount > 0
                                        text: model.memberCount + I18n.t(" thành viên", " members")
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
                                                font.bold: true
                                                color: modelData.accept ? "#FFFFFF" : ThemeManager.textColor
                                            }
                                            MouseArea {
                                                id: inviteMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: modelData.accept ? ZaloClient.acceptFriendRequest(contactRow.model.id)
                                                                            : ZaloClient.rejectFriendRequest(contactRow.model.id)
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: rowMouse.containsMouse && !root.compact && !contactRow.isInvite
                                    Layout.preferredWidth: chatLabel.implicitWidth + 24
                                    Layout.preferredHeight: 30
                                    radius: 6
                                    color: Theme.zaloBlueLight

                                    Text {
                                        id: chatLabel
                                        anchors.centerIn: parent
                                        text: I18n.t("Nhắn tin", "Chat")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: Theme.zaloBlueTonalText
                                    }
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 48, 360)
                        visible: contactsList.count === 0
                        spacing: 10

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            visible: ZaloClient.isSyncing
                            running: visible
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
                                font.bold: true
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
