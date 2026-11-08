pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    objectName: "sidebar"
    width: Theme.sidebarWidth
    color: ThemeManager.surfaceColor

    property var menuConv: ({})

    function openConvMenu(id: string, pinned: bool, muted: bool, unread: bool, marked: bool, labelId: int,
                          revealed: bool, p: point): void {
        root.menuConv = { id: id, isPinned: pinned, isMuted: muted, unread: unread, marked: marked,
                          labelId: labelId, revealed: revealed };
        const h = Math.max(convMenu.height, convMenu.implicitHeight);
        convMenu.x = Math.max(8, Math.min(p.x, root.width - convMenu.width - 8));
        convMenu.y = Math.max(8, p.y + h > root.height - 8 ? p.y - h : p.y);
        convMenu.open();
    }
    readonly property bool searching: ZaloClient.conversationModel.searchQuery.trim().length > 0

    component LabelMenuRow: Rectangle {
        id: lrow
        property string labelText: ""
        property color dotColor: "transparent"
        property bool checked: false
        signal triggered()
        Layout.fillWidth: true
        Layout.preferredHeight: 32
        implicitWidth: 200
        radius: 6
        color: lrowMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                radius: 6
                color: lrow.dotColor
                border.width: lrow.dotColor.a === 0 ? 1 : 0
                border.color: ThemeManager.borderColor
            }
            Text {
                Layout.fillWidth: true
                text: lrow.labelText
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 13
                color: ThemeManager.textColor
            }
            ZIcon { visible: lrow.checked; name: "check"; size: 14; color: Theme.zaloBlue }
        }
        MouseArea {
            id: lrowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: lrow.triggered()
        }
    }

    component ViewTab: Item {
        id: vtab
        property string viewKey: ""
        property string title: ""
        readonly property bool selected: ZaloClient.conversationModel.viewMode === vtab.viewKey
        signal clicked()
        height: parent ? parent.height : 32
        width: vtabLabel.implicitWidth
        Text {
            id: vtabLabel
            anchors.centerIn: parent
            text: vtab.title
            font.family: Theme.fontFamily
            font.bold: vtab.selected
            font.pixelSize: 13
            color: vtab.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 2
            color: vtab.selected ? Theme.zaloBlue : "transparent"
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: vtab.clicked()
        }
    }

    Shortcut {
        sequences: ZaloClient.shortcuts.keys("chat.search")
        enabled: root.visible
        onActivated: { searchInput.forceActiveFocus(); searchInput.selectAll(); }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: ThemeManager.borderColor
        z: 5
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 4

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: 8
                    color: searchInput.activeFocus ? ThemeManager.surfaceColor : ThemeManager.inputFillColor
                    border.color: searchInput.activeFocus ? Theme.zaloBlue : "transparent"
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        ZIcon {
                            name: "search"
                            size: 14
                            color: ThemeManager.secondaryTextColor

                            Rectangle {
                                objectName: "hiddenUnreadDot"
                                visible: ZaloClient.hiddenUnread
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: -2
                                anchors.rightMargin: -3
                                width: 7
                                height: 7
                                radius: 3.5
                                color: Theme.badgeRed
                            }
                        }

                        TextInput {
                            id: searchInput
                            Layout.fillWidth: true
                            color: ThemeManager.textColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            clip: true
                            onTextChanged: ZaloClient.conversationModel.setSearchFilter(text)
                            onActiveFocusChanged: if (activeFocus && ZaloClient.hiddenUnread) ZaloClient.dismissHiddenUnread()
                            Keys.onEscapePressed: { text = ""; focus = false; }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: I18n.t("Tìm kiếm", "Search")
                                color: ThemeManager.secondaryTextColor
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                visible: !searchInput.text && !searchInput.activeFocus
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: width / 2
                    color: addFriendMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor
                    Behavior on color { ColorAnimation { duration: 140 } }
                    scale: addFriendMouse.pressed ? 0.92 : (addFriendMouse.containsMouse ? 1.08 : 1.0)
                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

                    ZIcon {
                        anchors.centerIn: parent
                        name: "user_add"
                        size: 18
                        color: ThemeManager.textColor
                    }

                    MouseArea {
                        id: addFriendMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        ToolTip.visible: containsMouse
                        ToolTip.text: I18n.t("Thêm bạn", "Add friend")
                        onClicked: ZaloClient.openAddFriend()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: width / 2
                    color: addGroupMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor
                    Behavior on color { ColorAnimation { duration: 140 } }
                    scale: addGroupMouse.pressed ? 0.92 : (addGroupMouse.containsMouse ? 1.08 : 1.0)
                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

                    ZIcon {
                        anchors.centerIn: parent
                        name: "group_add"
                        size: 18
                        color: ThemeManager.textColor
                    }

                    MouseArea {
                        id: addGroupMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        ToolTip.visible: containsMouse && !groupMenu.opened
                        ToolTip.text: I18n.t("Nhóm", "Groups")
                        onClicked: groupMenu.open()
                    }

                    Popup {
                        id: groupMenu
                        y: parent.height + 4
                        x: parent.width - width
                        width: 240
                        padding: 6
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                        background: Rectangle {
                            color: ThemeManager.surfaceColor
                            radius: 8
                            border.color: ThemeManager.borderColor
                        }
                        contentItem: ColumnLayout {
                            spacing: 2
                            ZMenuRow {
                                icon: "group_add"
                                label: I18n.t("Tạo nhóm", "Create group")
                                onTriggered: { groupMenu.close(); ZaloClient.openCreateGroup(); }
                            }
                            ZMenuRow {
                                icon: "link"
                                label: I18n.t("Tham gia nhóm bằng link", "Join a group by link")
                                onTriggered: { groupMenu.close(); ZaloClient.groupAdmin.openJoin(""); }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36

            Row {
                id: tabRow
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 14

                Repeater {
                    model: [
                        { key: "", vi: "Tất cả", en: "All" },
                        { key: "unread", vi: "Chưa đọc", en: "Unread" }
                    ]

                    Item {
                        id: tabItem
                        required property var modelData
                        readonly property string tabKey: tabItem.modelData.key || ""
                        readonly property string tabVi: tabItem.modelData.vi || ""
                        readonly property string tabEn: tabItem.modelData.en || ""
                        readonly property bool selected: ZaloClient.conversationModel.viewMode === "normal"
                                                         && ZaloClient.conversationModel.activeCategory === tabItem.tabKey
                        height: parent.height
                        width: tabLabel.implicitWidth

                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: I18n.t(tabItem.tabVi, tabItem.tabEn)
                            font.family: Theme.fontFamily
                            font.bold: tabItem.selected
                            font.pixelSize: 13
                            color: tabItem.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: tabItem.selected ? Theme.zaloBlue : "transparent"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                ZaloClient.conversationModel.setViewMode("normal");
                                ZaloClient.conversationModel.setCategoryFilter(tabItem.tabKey);
                            }
                        }
                    }
                }

                ViewTab {
                    viewKey: "archived"
                    title: I18n.t("Khác", "Other")
                    onClicked: ZaloClient.conversationModel.setViewMode("archived")
                }
            }

            Rectangle {
                id: labelFilterButton
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                readonly property bool hasCurrent: currentText.length > 0
                readonly property real maxTextWidth: Math.max(24, parent.width - (tabRow.x + tabRow.width) - 12 - 12 - 16
                                                                  - 14 - 10 - (hasCurrent ? 13 : 0))
                readonly property string currentText: labelFilterButton.labelField(ZaloClient.conversationModel.labelFilter, "text")
                readonly property string currentColor: labelFilterButton.labelField(ZaloClient.conversationModel.labelFilter, "color")
                function labelField(id: int, key: string): string {
                    if (id === 0) return "";
                    const list = ZaloClient.labels.labels;
                    for (let i = 0; i < list.length; ++i) if (list[i].id === id) return String(list[i][key] || "");
                    return "";
                }
                height: 26
                width: labelFilterRow.implicitWidth + 16
                radius: 13
                color: labelFilterMouse.containsMouse || labelFilterMenu.opened ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                Row {
                    id: labelFilterRow
                    anchors.centerIn: parent
                    spacing: 5
                    Rectangle {
                        visible: labelFilterButton.hasCurrent
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: labelFilterButton.hasCurrent ? labelFilterButton.currentColor : "transparent"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: labelFilterButton.hasCurrent ? labelFilterButton.currentText : I18n.t("Phân loại", "Labels")
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: labelFilterButton.hasCurrent ? ThemeManager.textColor : ThemeManager.secondaryTextColor
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, 110, labelFilterButton.maxTextWidth)
                    }
                    ZIcon { anchors.verticalCenter: parent.verticalCenter; name: "chevron_down"; size: 14; color: ThemeManager.secondaryTextColor }
                }
                MouseArea {
                    id: labelFilterMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: labelFilterMenu.open()
                }
                Popup {
                    id: labelFilterMenu
                    y: parent.height + 4
                    x: parent.width - width
                    width: 230
                    padding: 6
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { color: ThemeManager.surfaceColor; radius: 8; border.color: ThemeManager.borderColor }
                    contentItem: ColumnLayout {
                        spacing: 2
                        LabelMenuRow {
                            labelText: I18n.t("Tất cả hội thoại", "All conversations")
                            checked: ZaloClient.conversationModel.labelFilter === 0
                            onTriggered: { ZaloClient.conversationModel.setLabelFilter(0); labelFilterMenu.close(); }
                        }
                        Repeater {
                            model: ZaloClient.labels.labels
                            LabelMenuRow {
                                id: labelRow
                                required property var modelData
                                readonly property string dotCol: labelRow.modelData.color || ""
                                readonly property string labelTitle: labelRow.modelData.text || ""
                                readonly property int labelId: labelRow.modelData.id || 0
                                dotColor: labelRow.dotCol
                                labelText: labelRow.labelTitle
                                checked: ZaloClient.conversationModel.labelFilter === labelRow.labelId
                                onTriggered: { ZaloClient.conversationModel.setLabelFilter(labelRow.labelId); labelFilterMenu.close(); }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 3; Layout.bottomMargin: 3; color: ThemeManager.borderColor }
                        ZMenuRow {
                            icon: "edit"
                            label: I18n.t("Quản lý thẻ phân loại", "Manage labels")
                            onTriggered: { labelFilterMenu.close(); ZaloClient.labels.manageOpen = true; }
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 28 : 0
            visible: ZaloClient.isOfflineMode || (ZaloClient.isLoggedIn && (!ZaloClient.isConnected || ZaloClient.isSyncing))
            color: ZaloClient.isConnected ? (ThemeManager.infoBackgroundColor)
                                          : (ThemeManager.warningBackgroundColor)

            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: ZaloClient.isOfflineMode
                      ? I18n.t("Ngoại tuyến · đang xem tin đã lưu trên máy", "Offline · showing messages saved on this computer")
                      : ZaloClient.isConnected ? I18n.t("Đang đồng bộ tin nhắn...", "Syncing messages...")
                                               : I18n.t("Đang kết nối máy chủ...", "Connecting to server...")
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: ZaloClient.isConnected && !ZaloClient.isOfflineMode ? Theme.zaloBlue : "#B26A00"
            }
        }

        Text {
            visible: root.searching && ZaloClient.hiddenSearch.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            text: I18n.t("Trò chuyện ẩn", "Hidden conversations")
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }
        ListView {
            id: hiddenHitList
            objectName: "hiddenHitList"
            visible: root.searching && ZaloClient.hiddenSearch.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: convList.count > 0 || ZaloClient.peopleSearch.count > 0 || ZaloClient.messageSearch.count > 0
                                  ? Math.min(hiddenHitList.contentHeight, 64 * 4) : Number.POSITIVE_INFINITY
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: ZaloClient.hiddenSearch
            delegate: Rectangle {
                id: hiddenRow
                required property string convId
                required property string convName
                required property string avatar
                required property bool isGroup
                required property string text
                width: ListView.view.width
                height: 64
                color: hiddenHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                HoverHandler { id: hiddenHover }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: ZaloClient.openHiddenConversation(hiddenRow.convId, "")
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: (point) => root.openConvMenu(hiddenRow.convId, false, false, false, false, 0, true,
                                                           hiddenRow.mapToItem(root, point.position.x, point.position.y))
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 10
                    ZAvatar {
                        size: 40
                        avatarUrl: hiddenRow.avatar
                        name: hiddenRow.convName
                        isGroup: hiddenRow.isGroup
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: hiddenRow.convName
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }
                        Text {
                            Layout.fillWidth: true
                            text: hiddenRow.text
                            textFormat: Text.PlainText
                            maximumLineCount: 1
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }
            }
        }

        Text {
            visible: root.searching && convList.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            text: I18n.t("Trò chuyện (%1)", "Chats (%1)").arg(convList.count)
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }

        ListView {
            id: convList
            visible: convList.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: root.searching && (ZaloClient.peopleSearch.count > 0 || ZaloClient.messageSearch.count > 0)
                                  ? Math.min(convList.contentHeight, 72 * 4) : Number.POSITIVE_INFINITY
            clip: true
            model: ZaloClient.conversationModel
            spacing: 0
            reuseItems: true
            cacheBuffer: 300

            property double now: Date.now()
            Timer {
                interval: 60000
                running: convList.visible
                repeat: true
                onTriggered: convList.now = Date.now()
            }

            function relativeTime(ts: real, fallback: string): string {
                if (!ts || ts < 946684800000) return fallback || "";
                var lang = I18n.currentLanguage;
                var diffMin = Math.floor((convList.now - ts) / 60000);
                if (diffMin < 1) return I18n.t("Vừa xong", "Now");
                if (diffMin < 60) return diffMin + I18n.t(" phút", " min");
                var diffHour = Math.floor(diffMin / 60);
                if (diffHour < 24) return diffHour + I18n.t(" giờ", " h");
                var diffDay = Math.floor(diffHour / 24);
                if (diffDay < 7) return diffDay + I18n.t(" ngày", " d");
                var d = new Date(ts);
                return ("0" + d.getDate()).slice(-2) + "/" + ("0" + (d.getMonth() + 1)).slice(-2);
            }

            delegate: Rectangle {
                id: itemDelegate
                required property string id
                required property string name
                required property string avatarUrl
                required property bool isOnline
                required property bool isGroup
                required property int memberCount
                required property var memberAvatars
                required property int unreadCount
                required property bool isPinned
                required property bool isMuted
                required property bool isHidden
                required property bool isTyping
                required property string draft
                required property string lastMessage
                required property string senderPrefix
                required property string tag
                required property string tagColor
                required property double lastTs
                required property string timestamp
                required property int labelId

                width: convList.width
                height: 72
                color: "transparent"

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    radius: 8
                    color: ZaloClient.currentConversationId === itemDelegate.id ? ThemeManager.selectedColor
                         : mouseArea.containsMouse ? ThemeManager.hoverColor
                         : itemDelegate.isPinned ? ThemeManager.pinnedColor : "transparent"
                    Behavior on color { ColorAnimation { duration: 130 } }
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.openConvMenu(itemDelegate.id, itemDelegate.isPinned, itemDelegate.isMuted, itemDelegate.unreadCount > 0,
                                              ZaloClient.isMarkedUnread(itemDelegate.id), itemDelegate.labelId, false,
                                              mouseArea.mapToItem(root, mouse.x, mouse.y));
                            return;
                        }
                        if (ZaloClient.needsHiddenPin(itemDelegate.id)) ZaloClient.requestHiddenFlow("open", itemDelegate.id, false);
                        else ZaloClient.selectConversation(itemDelegate.id, itemDelegate.name, itemDelegate.avatarUrl);
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    ZAvatar {
                        Layout.alignment: Qt.AlignVCenter
                        size: 48
                        avatarUrl: (itemDelegate.avatarUrl && itemDelegate.avatarUrl.length > 0) ? itemDelegate.avatarUrl : ""
                        name: itemDelegate.name
                        isOnline: itemDelegate.isOnline
                        isGroup: itemDelegate.isGroup
                        memberCount: itemDelegate.memberCount
                        memberAvatars: itemDelegate.memberAvatars || []
                        showStatusDot: !itemDelegate.isGroup
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 20

                            Text {
                                text: itemDelegate.name && itemDelegate.name.length > 0
                                      ? itemDelegate.name
                                      : (itemDelegate.isGroup ? I18n.t("Nhóm chưa đặt tên", "Unnamed group") : I18n.t("Người dùng Zalo", "Zalo user"))
                                font.family: Theme.fontFamily
                                font.weight: itemDelegate.unreadCount > 0 ? Font.DemiBold : (itemDelegate.isPinned ? Font.Medium : Font.Normal)
                                font.pixelSize: 14
                                color: ThemeManager.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                visible: text.length > 0
                                text: convList.relativeTime(itemDelegate.lastTs, itemDelegate.timestamp)
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: itemDelegate.unreadCount > 0 ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 18
                            spacing: 6

                            Text {
                                text: {
                                    if (itemDelegate.isHidden) return I18n.t("Trò chuyện ẩn", "Hidden conversation");
                                    if (itemDelegate.isTyping) return I18n.t("Đang soạn tin...", "Typing...");
                                    if (itemDelegate.draft.length > 0 && itemDelegate.id !== ZaloClient.currentConversationId) {
                                        return I18n.t("[Bản nháp] ", "[Draft] ") + itemDelegate.draft.replace(/\n/g, " ");
                                    }
                                    if (itemDelegate.lastMessage && itemDelegate.lastMessage.length > 0) {
                                        return (itemDelegate.senderPrefix ? itemDelegate.senderPrefix : "") + itemDelegate.lastMessage;
                                    }
                                    if (itemDelegate.isGroup) {
                                        return itemDelegate.memberCount > 0
                                               ? (itemDelegate.memberCount + I18n.t(" thành viên", " members"))
                                               : I18n.t("Chưa có tin nhắn", "No messages yet");
                                    }
                                    if (itemDelegate.isOnline) {
                                        return I18n.t("Đang hoạt động", "Active now");
                                    }
                                    return I18n.t("Chưa có tin nhắn", "No messages yet");
                                }
                                font.italic: itemDelegate.isTyping
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: itemDelegate.isTyping ? Theme.zaloBlue
                                     : (itemDelegate.draft.length > 0 && itemDelegate.id !== ZaloClient.currentConversationId) ? Theme.badgeRed
                                     : (!itemDelegate.lastMessage && itemDelegate.isOnline) ? Theme.onlineGreen
                                     : (itemDelegate.unreadCount > 0 ? ThemeManager.textColor : ThemeManager.secondaryTextColor)
                                font.weight: itemDelegate.unreadCount > 0 ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Rectangle {
                                visible: itemDelegate.tag && itemDelegate.tag.length > 0
                                height: 16
                                width: tagText.implicitWidth + 8
                                radius: 3
                                color: (itemDelegate.tagColor && itemDelegate.tagColor.length > 0) ? itemDelegate.tagColor : Theme.zaloBlue

                                Text {
                                    id: tagText
                                    anchors.centerIn: parent
                                    text: itemDelegate.tag ? itemDelegate.tag : ""
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 9
                                    font.bold: true
                                    color: "#FFFFFF"
                                }
                            }

                            ZIcon {
                                name: "bell_off"
                                size: 12
                                visible: itemDelegate.isMuted
                                color: ThemeManager.secondaryTextColor
                            }

                            ZIcon {
                                name: "pin"
                                size: 12
                                visible: itemDelegate.isPinned
                                color: ThemeManager.secondaryTextColor
                            }

                            Rectangle {
                                width: Math.max(unreadBadgeText.implicitWidth + 8, 18)
                                height: 18
                                radius: 9
                                color: Theme.badgeRed
                                visible: itemDelegate.unreadCount > 0
                                scale: itemDelegate.unreadCount > 0 ? 1.0 : 0.0
                                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }

                                Text {
                                    id: unreadBadgeText
                                    anchors.centerIn: parent
                                    text: itemDelegate.unreadCount > 99 ? "99+" : itemDelegate.unreadCount
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 10
                                    font.bold: true
                                    color: "#FFFFFF"
                                }
                            }
                        }
                    }
                }
            }
        }

        Connections {
            target: searchInput
            function onTextChanged() {
                ZaloClient.messageSearch.setQuery(searchInput.text);
                ZaloClient.peopleSearch.setQuery(searchInput.text);
                ZaloClient.hiddenSearch.setQuery(searchInput.text);
            }
        }
        Text {
            visible: root.searching && ZaloClient.peopleSearch.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            text: I18n.t("Liên hệ & nhóm (%1)", "Contacts & groups (%1)").arg(ZaloClient.peopleSearch.count)
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }
        ListView {
            id: peopleHitList
            visible: root.searching && ZaloClient.peopleSearch.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: ZaloClient.messageSearch.count > 0 ? Math.min(peopleHitList.contentHeight, 56 * 4)
                                                                     : Number.POSITIVE_INFINITY
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: ZaloClient.peopleSearch
            delegate: Rectangle {
                id: personRow
                required property string convId
                required property string convName
                required property string avatar
                required property bool isGroup
                required property string text
                width: ListView.view.width
                height: 56
                color: personHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                HoverHandler { id: personHover }
                TapHandler {
                    onTapped: {
                        ZaloClient.openSearchResult(personRow.convId, personRow.convName, personRow.avatar, personRow.isGroup);
                        searchInput.text = "";
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 10
                    ZAvatar {
                        size: 36
                        avatarUrl: personRow.avatar
                        name: personRow.convName
                        isGroup: personRow.isGroup
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: personRow.convName
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }
                        Text {
                            Layout.fillWidth: true
                            text: personRow.text
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }
            }
        }
        Text {
            visible: ZaloClient.messageSearch.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            text: I18n.t("Tin nhắn (%1)", "Messages (%1)").arg(ZaloClient.messageSearch.count)
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }
        ListView {
            id: messageHitList
            visible: ZaloClient.messageSearch.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: ZaloClient.messageSearch
            delegate: Rectangle {
                id: hitRow
                required property string msgId
                required property string convId
                required property string convName
                required property string avatar
                required property bool isGroup
                required property string senderName
                required property string text
                required property real ts
                width: ListView.view.width
                height: 60
                color: hitHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                HoverHandler { id: hitHover }
                TapHandler { onTapped: ZaloClient.openMessageFromSearch(hitRow.convId, hitRow.msgId) }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 10
                    ZAvatar {
                        size: 36
                        avatarUrl: hitRow.avatar
                        name: hitRow.convName
                        isGroup: hitRow.isGroup
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: hitRow.convName
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: ThemeManager.textColor
                            }
                            Text {
                                text: hitRow.ts >= 946684800000 ? Qt.formatDate(new Date(hitRow.ts), "dd/MM/yy") : ""
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: ThemeManager.secondaryTextColor
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (hitRow.senderName.length > 0 ? hitRow.senderName + ": " : "") + hitRow.text
                            textFormat: Text.PlainText
                            maximumLineCount: 1
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }
            }
        }

        Item {
            visible: convList.count === 0 && ZaloClient.messageSearch.count === 0
                     && !(root.searching && (ZaloClient.peopleSearch.count > 0 || ZaloClient.hiddenSearch.count > 0))
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                width: Math.min(parent.width - 32, 260)

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "💬"
                    font.pixelSize: 32
                    visible: !ZaloClient.isSyncing
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: ZaloClient.conversationModel.searchQuery.length > 0 ? I18n.t("Không tìm thấy kết quả phù hợp", "No matching results")
                          : ZaloClient.conversationModel.activeCategory === "unread" ? I18n.t("Không có tin nhắn chưa đọc", "No unread messages")
                          : I18n.tr("no_conversations")
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.bold: true
                    color: ThemeManager.textColor
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: ZaloClient.isSyncing ? I18n.t("Đang tải danh sách trò chuyện...", "Loading your conversations...")
                                               : I18n.t("Bấm biểu tượng thêm bạn ở trên để bắt đầu trò chuyện", "Use the add-friend button above to start chatting")
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    Popup {
        id: convMenu
        margins: 8
        width: 240
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: 8
            border.color: ThemeManager.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 2
            ZMenuRow {
                icon: "pin"
                label: root.menuConv.isPinned ? I18n.t("Bỏ ghim hội thoại", "Unpin conversation")
                                              : I18n.t("Ghim hội thoại", "Pin conversation")
                onTriggered: { ZaloClient.pinConversation(root.menuConv.id); convMenu.close(); }
            }
            ZMenuRow {
                readonly property bool marked: root.menuConv.marked === true
                visible: marked || !root.menuConv.unread
                icon: "mark_unread"
                label: marked ? I18n.t("Bỏ đánh dấu chưa đọc", "Remove unread mark")
                              : I18n.t("Đánh dấu chưa đọc", "Mark as unread")
                onTriggered: { ZaloClient.toggleUnreadMark(root.menuConv.id); convMenu.close(); }
            }
            ZMenuRow {
                visible: root.menuConv.isMuted === true
                icon: "bell"
                label: I18n.t("Bật thông báo", "Unmute")
                onTriggered: { ZaloClient.muteConversationFor(root.menuConv.id, 0); convMenu.close(); }
            }
            Repeater {
                model: [ { vi: "Tắt thông báo 1 giờ", en: "Mute for 1 hour", s: 3600 },
                         { vi: "Tắt thông báo 4 giờ", en: "Mute for 4 hours", s: 14400 },
                         { vi: "Tắt đến 8 giờ sáng", en: "Mute until 8 AM", s: -2 },
                         { vi: "Tắt cho đến khi mở lại", en: "Mute until I turn it back on", s: -1 } ]
                ZMenuRow {
                    required property var modelData
                    visible: root.menuConv.isMuted !== true
                    icon: "bell_off"
                    label: I18n.t(modelData.vi, modelData.en)
                    onTriggered: { ZaloClient.muteConversationFor(root.menuConv.id, modelData.s); convMenu.close(); }
                }
            }
            Rectangle {
                visible: ZaloClient.labels.loaded
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 3
                Layout.bottomMargin: 3
                color: ThemeManager.borderColor
            }
            Text {
                visible: ZaloClient.labels.loaded
                Layout.leftMargin: 10
                text: I18n.t("Phân loại", "Label")
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: ThemeManager.secondaryTextColor
            }
            Repeater {
                model: ZaloClient.labels.loaded ? ZaloClient.labels.labels : []
                LabelMenuRow {
                    required property var modelData
                    dotColor: modelData.color
                    labelText: modelData.text
                    checked: root.menuConv.labelId === modelData.id
                    onTriggered: { ZaloClient.labels.assign(root.menuConv.id, modelData.id); convMenu.close(); }
                }
            }
            ZMenuRow {
                visible: ZaloClient.labels.loaded
                icon: "edit"
                label: I18n.t("Quản lý thẻ phân loại", "Manage labels")
                onTriggered: { convMenu.close(); ZaloClient.labels.manageOpen = true; }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 3
                Layout.bottomMargin: 3
                color: ThemeManager.borderColor
            }
            ZMenuRow {
                readonly property bool archived: ZaloClient.isConversationArchived(root.menuConv.id)
                icon: "archive"
                label: archived ? I18n.t("Đưa về hội thoại chính", "Move back to main chats")
                                : I18n.t("Chuyển vào Hội thoại khác", "Move to Other conversations")
                onTriggered: { ZaloClient.archiveConversation(root.menuConv.id, !archived); convMenu.close(); }
            }
            ZMenuRow {
                readonly property bool hidden: ZaloClient.isConversationHidden(root.menuConv.id)
                visible: hidden || ZaloClient.canHideConversation(root.menuConv.id)
                icon: "eye_off"
                label: hidden ? I18n.t("Bỏ ẩn trò chuyện", "Unhide conversation")
                              : I18n.t("Ẩn trò chuyện", "Hide conversation")
                onTriggered: {
                    const id = root.menuConv.id;
                    const bypass = !!root.menuConv.revealed || id === ZaloClient.currentConversationId;
                    convMenu.close();
                    ZaloClient.requestHiddenFlow(hidden ? "unhide" : "hide", id, bypass);
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 3
                Layout.bottomMargin: 3
                color: ThemeManager.borderColor
            }
            ZMenuRow {
                icon: "trash"
                danger: true
                label: I18n.t("Xoá hội thoại", "Delete conversation")
                onTriggered: { convMenu.close(); deleteConfirm.open(); }
            }
        }
    }

    Dialog {
        id: deleteConfirm
        anchors.centerIn: T.Overlay.overlay
        modal: true
        width: 340
        title: I18n.t("Xoá hội thoại", "Delete conversation")
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: I18n.t("Toàn bộ tin nhắn của hội thoại này sẽ bị xoá ở phía bạn trên mọi thiết bị. Người kia vẫn giữ tin nhắn.",
                         "All messages of this conversation will be deleted for you on every device. The other side keeps them.")
            font.family: Theme.fontFamily
        }
        onAccepted: ZaloClient.deleteConversation(root.menuConv.id)
    }
}
