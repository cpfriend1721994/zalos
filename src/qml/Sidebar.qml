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
    property bool moreViewsOpen: false
    readonly property bool searching: ZaloClient.conversationModel.searchQuery.trim().length > 0

    function toggleMoreViews(): void {
        root.moreViewsOpen = !root.moreViewsOpen;
        if (!root.moreViewsOpen) ZaloClient.conversationModel.setViewMode("normal");
    }

    function requestHiddenView(): void {
        if (ZaloClient.appLock.hasPasscode) hiddenPasscodeDialog.open();
        else ZaloClient.conversationModel.setViewMode("hidden");
    }

    Dialog {
        id: hiddenPasscodeDialog
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 320
        title: I18n.t("Nhập mã khóa để xem", "Enter passcode to view")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAboutToShow: { hiddenPinField.text = ""; hiddenPinField.forceActiveFocus(); }
        onAccepted: {
            if (ZaloClient.appLock.verifyPasscode(hiddenPinField.text))
                ZaloClient.conversationModel.setViewMode("hidden");
            else
                ZaloClient.showNotice(I18n.t("Mã không đúng", "Wrong passcode"));
        }
        ColumnLayout {
            width: parent.width
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("Hội thoại đã ẩn được bảo vệ bằng mã khóa ứng dụng.",
                             "Hidden conversations are protected by the app passcode.")
                color: ThemeManager.secondaryTextColor
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }
            ZTextField {
                id: hiddenPinField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly
                placeholderText: I18n.t("Mã khóa", "Passcode")
                onAccepted: hiddenPasscodeDialog.accept()
            }
        }
    }

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
                        }

                        TextInput {
                            id: searchInput
                            Layout.fillWidth: true
                            color: ThemeManager.textColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            clip: true
                            onTextChanged: ZaloClient.conversationModel.setSearchFilter(text)
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
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 18

                Repeater {
                    model: [
                        { key: "", vi: "Tất cả", en: "All" },
                        { key: "unread", vi: "Chưa đọc", en: "Unread" }
                    ]

                    Item {
                        required property var modelData
                        readonly property bool selected: ZaloClient.conversationModel.viewMode === "normal"
                                                         && ZaloClient.conversationModel.activeCategory === modelData.key
                        height: parent.height
                        width: tabLabel.implicitWidth

                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: I18n.t(modelData.vi, modelData.en)
                            font.family: Theme.fontFamily
                            font.bold: parent.selected
                            font.pixelSize: 13
                            color: parent.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: parent.selected ? Theme.zaloBlue : "transparent"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                ZaloClient.conversationModel.setViewMode("normal");
                                ZaloClient.conversationModel.setCategoryFilter(modelData.key);
                            }
                        }
                    }
                }

                Item {
                    height: parent.height
                    width: 26
                    Rectangle {
                        anchors.centerIn: parent
                        width: 26
                        height: 26
                        radius: 13
                        color: moreViewsMouse.containsMouse || root.moreViewsOpen ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                        ZIcon {
                            anchors.centerIn: parent
                            name: "chevron_down"
                            size: 16
                            rotation: root.moreViewsOpen ? 180 : 0
                            color: ZaloClient.conversationModel.viewMode !== "normal" ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                            Behavior on rotation { NumberAnimation { duration: 140 } }
                        }
                    }
                    MouseArea {
                        id: moreViewsMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleMoreViews()
                    }
                    ToolTip.visible: moreViewsMouse.containsMouse
                    ToolTip.delay: 400
                    ToolTip.text: root.moreViewsOpen ? I18n.t("Thu gọn", "Collapse")
                                                     : I18n.t("Hội thoại khác & đã ẩn", "Other & hidden conversations")
                }
            }

            Rectangle {
                id: labelFilterButton
                visible: !root.moreViewsOpen
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                readonly property bool hasCurrent: currentText.length > 0
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
                        width: Math.min(implicitWidth, 110)
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
                                required property var modelData
                                dotColor: modelData.color
                                labelText: modelData.text
                                checked: ZaloClient.conversationModel.labelFilter === modelData.id
                                onTriggered: { ZaloClient.conversationModel.setLabelFilter(modelData.id); labelFilterMenu.close(); }
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

        Item {
            visible: root.moreViewsOpen
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 18

                ViewTab {
                    viewKey: "archived"
                    title: I18n.t("Hội thoại khác", "Other conversations")
                    onClicked: ZaloClient.conversationModel.setViewMode("archived")
                }
                ViewTab {
                    viewKey: "hidden"
                    title: I18n.t("Hội thoại đã ẩn", "Hidden conversations")
                    onClicked: root.requestHiddenView()
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
                if (!ts || ts <= 0) return fallback || "";
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
                    color: ZaloClient.currentConversationId === model.id ? ThemeManager.selectedColor
                         : mouseArea.containsMouse ? ThemeManager.hoverColor
                         : model.isPinned ? ThemeManager.pinnedColor : "transparent"
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
                            root.menuConv = { id: model.id, isPinned: model.isPinned, isMuted: model.isMuted,
                                              unread: model.unreadCount > 0, marked: ZaloClient.isMarkedUnread(model.id),
                                              labelId: model.labelId };
                            const p = mouseArea.mapToItem(root, mouse.x, mouse.y);
                            const h = Math.max(convMenu.height, convMenu.implicitHeight);
                            convMenu.x = Math.max(8, Math.min(p.x, root.width - convMenu.width - 8));
                            convMenu.y = Math.max(8, p.y + h > root.height - 8 ? p.y - h : p.y);
                            convMenu.open();
                            return;
                        }
                        ZaloClient.selectConversation(model.id, model.name, model.avatarUrl);
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
                        avatarUrl: (model.avatarUrl && model.avatarUrl.length > 0) ? model.avatarUrl : ""
                        name: model.name
                        isOnline: model.isOnline
                        isGroup: model.isGroup
                        memberCount: model.memberCount
                        memberAvatars: model.memberAvatars || []
                        showStatusDot: !model.isGroup
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 20

                            Text {
                                text: model.name && model.name.length > 0
                                      ? model.name
                                      : (model.isGroup ? I18n.t("Nhóm chưa đặt tên", "Unnamed group") : I18n.t("Người dùng Zalo", "Zalo user"))
                                font.family: Theme.fontFamily
                                font.weight: model.unreadCount > 0 ? Font.DemiBold : (model.isPinned ? Font.Medium : Font.Normal)
                                font.pixelSize: 14
                                color: ThemeManager.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                visible: text.length > 0
                                text: convList.relativeTime(model.lastTs, model.timestamp)
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: model.unreadCount > 0 ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 18
                            spacing: 6

                            Text {
                                text: {
                                    if (model.isTyping) return I18n.t("Đang soạn tin...", "Typing...");
                                    if (model.draft.length > 0 && model.id !== ZaloClient.currentConversationId) {
                                        return I18n.t("[Bản nháp] ", "[Draft] ") + model.draft.replace(/\n/g, " ");
                                    }
                                    if (model.lastMessage && model.lastMessage.length > 0) {
                                        return (model.senderPrefix ? model.senderPrefix : "") + model.lastMessage;
                                    }
                                    if (model.isGroup) {
                                        return model.memberCount > 0
                                               ? (model.memberCount + I18n.t(" thành viên", " members"))
                                               : I18n.t("Chưa có tin nhắn", "No messages yet");
                                    }
                                    if (model.isOnline) {
                                        return I18n.t("Đang hoạt động", "Active now");
                                    }
                                    return I18n.t("Chưa có tin nhắn", "No messages yet");
                                }
                                font.italic: model.isTyping
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: model.isTyping ? Theme.zaloBlue
                                     : (model.draft.length > 0 && model.id !== ZaloClient.currentConversationId) ? Theme.badgeRed
                                     : (!model.lastMessage && model.isOnline) ? Theme.onlineGreen
                                     : (model.unreadCount > 0 ? ThemeManager.textColor : ThemeManager.secondaryTextColor)
                                font.weight: model.unreadCount > 0 ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Rectangle {
                                visible: model.tag && model.tag.length > 0
                                height: 16
                                width: tagText.implicitWidth + 8
                                radius: 3
                                color: (model.tagColor && model.tagColor.length > 0) ? model.tagColor : Theme.zaloBlue

                                Text {
                                    id: tagText
                                    anchors.centerIn: parent
                                    text: model.tag ? model.tag : ""
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 9
                                    font.bold: true
                                    color: "#FFFFFF"
                                }
                            }

                            ZIcon {
                                name: "bell_off"
                                size: 12
                                visible: model.isMuted
                                color: ThemeManager.secondaryTextColor
                            }

                            ZIcon {
                                name: "pin"
                                size: 12
                                visible: model.isPinned
                                color: ThemeManager.secondaryTextColor
                            }

                            Rectangle {
                                width: Math.max(unreadBadgeText.implicitWidth + 8, 18)
                                height: 18
                                radius: 9
                                color: Theme.badgeRed
                                visible: model.unreadCount > 0
                                scale: model.unreadCount > 0 ? 1.0 : 0.0
                                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }

                                Text {
                                    id: unreadBadgeText
                                    anchors.centerIn: parent
                                    text: model.unreadCount > 99 ? "99+" : model.unreadCount
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
                                text: Qt.formatDate(new Date(hitRow.ts), "dd/MM/yy")
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
                     && !(root.searching && ZaloClient.peopleSearch.count > 0)
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
                icon: "eye_off"
                label: hidden ? I18n.t("Bỏ ẩn trò chuyện", "Unhide conversation")
                              : I18n.t("Ẩn trò chuyện", "Hide conversation")
                onTriggered: { ZaloClient.hideConversation(root.menuConv.id, !hidden); convMenu.close(); }
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
