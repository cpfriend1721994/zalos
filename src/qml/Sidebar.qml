pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQuick.Window
import Zalos

Rectangle {
    id: root
    readonly property bool compactWindow: (Window.window ? Window.window.width : 1000) < 720
    readonly property bool shortWindow: (Window.window ? Window.window.height : 800) < 600
    readonly property int rowHeight: root.shortWindow ? 60 : 72
    readonly property bool windowShowing: UiWindow.showing
    objectName: "sidebar"
    width: Theme.sidebarWidth
    color: ThemeManager.surfaceColor

    property string menuConvId: ""
    property bool menuConvPinned: false
    property bool menuConvMuted: false
    property bool menuConvUnread: false
    property bool menuConvMarked: false
    property int menuConvLabelId: 0
    property bool menuConvRevealed: false

    function openConvMenu(id: string, pinned: bool, muted: bool, unread: bool, marked: bool, labelId: int,
                          revealed: bool, p: point): void {
        root.menuConvId = id;
        root.menuConvPinned = pinned;
        root.menuConvMuted = muted;
        root.menuConvUnread = unread;
        root.menuConvMarked = marked;
        root.menuConvLabelId = labelId;
        root.menuConvRevealed = revealed;
        const h = Math.max(convMenu.height, convMenu.implicitHeight);
        convMenu.x = Math.max(8, Math.min(p.x, root.width - convMenu.width - 8));
        convMenu.y = Math.max(8, p.y + h > root.height - 8 ? p.y - h : p.y);
        convMenu.open();
    }
    readonly property bool searching: ZaloClient.conversationModel.searchQuery.trim().length > 0
    property string searchTab: "chats"
    property bool searchTabPicked: false
    readonly property int searchChatCount: convList.count + ZaloClient.hiddenSearch.count
    function searchTabCount(tab: string): int {
        if (tab === "people") return ZaloClient.peopleSearch.count;
        if (tab === "messages") return ZaloClient.messageSearch.count;
        return root.searchChatCount;
    }
    function pickSearchTab(): void {
        if (!root.searching) {
            root.searchTab = "chats";
            root.searchTabPicked = false;
            return;
        }
        if (root.searchTabPicked || root.searchTabCount(root.searchTab) > 0) return;
        const order = ["chats", "people", "messages"];
        for (let i = 0; i < order.length; ++i) {
            if (root.searchTabCount(order[i]) > 0) {
                root.searchTab = order[i];
                return;
            }
        }
    }
    onSearchingChanged: root.pickSearchTab()
    onSearchChatCountChanged: Qt.callLater(root.pickSearchTab)
    Connections {
        target: ZaloClient.peopleSearch
        function onCountChanged(): void { Qt.callLater(root.pickSearchTab); }
    }
    Connections {
        target: ZaloClient.messageSearch
        function onCountChanged(): void { Qt.callLater(root.pickSearchTab); }
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
                font.pixelSize: Theme.fontBody
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

    component ViewCategoryTab: Item {
        id: ctab
        property string categoryKey: ""
        property string title: ""
        readonly property bool selected: ZaloClient.conversationModel.viewMode === "normal"
                                         && ZaloClient.conversationModel.activeCategory === ctab.categoryKey
        height: parent ? parent.height : 36
        width: ctabLabel.implicitWidth

        Text {
            id: ctabLabel
            anchors.centerIn: parent
            text: ctab.title
            font.family: Theme.fontFamily
            font.weight: ctab.selected ? Font.DemiBold : Font.Normal
            font.pixelSize: Theme.fontBody
            color: ctab.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 2
            color: ctab.selected ? Theme.zaloBlue : "transparent"
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                ZaloClient.conversationModel.setViewMode("normal");
                ZaloClient.conversationModel.setCategoryFilter(ctab.categoryKey);
            }
        }
    }

    component SearchTab: Item {
        id: stab
        property string tabKey: ""
        property string title: ""
        readonly property bool selected: root.searchTab === stab.tabKey
        readonly property int hits: root.searchTabCount(stab.tabKey)
        objectName: "searchTab_" + stab.tabKey
        height: parent ? parent.height : 36
        width: stabLabel.implicitWidth
        Text {
            id: stabLabel
            anchors.centerIn: parent
            text: stab.hits > 0 ? stab.title + " (" + stab.hits + ")" : stab.title
            font.family: Theme.fontFamily
            font.weight: stab.selected ? Font.DemiBold : Font.Normal
            font.pixelSize: Theme.fontBody
            color: stab.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 2
            color: stab.selected ? Theme.zaloBlue : "transparent"
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.searchTab = stab.tabKey;
                root.searchTabPicked = true;
            }
        }
    }
    component TabDivider: Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: 12
        color: ThemeManager.borderColor
        opacity: 0.85
    }

    component ViewTab: Item {
        id: vtab
        property string viewKey: ""
        property string title: ""
        readonly property bool selected: ZaloClient.conversationModel.viewMode === vtab.viewKey
        signal clicked()
        height: parent ? parent.height : 36
        width: vtabLabel.implicitWidth
        Text {
            id: vtabLabel
            anchors.centerIn: parent
            text: vtab.title
            font.family: Theme.fontFamily
            font.weight: vtab.selected ? Font.DemiBold : Font.Normal
            font.pixelSize: Theme.fontBody
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

                            ZIcon {
                                objectName: "hiddenUnreadDot"
                                visible: ZaloClient.hiddenUnread
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: -2
                                anchors.rightMargin: -3
                                name: "dot"
                                size: 7
                                color: Theme.badgeRed
                            }
                        }

                        TextInput {
                            id: searchInput
                            Layout.fillWidth: true
                            color: ThemeManager.textColor
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            clip: true
                            function effectiveQuery(): string {
                                if (!searchInput.preeditText || searchInput.preeditText.length === 0) return searchInput.text;
                                const pos = Math.max(0, Math.min(searchInput.cursorPosition, searchInput.text.length));
                                return searchInput.text.substring(0, pos) + searchInput.preeditText + searchInput.text.substring(pos);
                            }
                            function syncQuery(): void {
                                const q = searchInput.effectiveQuery();
                                if (q.length === 0) {
                                    searchDebounce.stop();
                                    ZaloClient.conversationModel.setSearchFilter("");
                                    ZaloClient.messageSearch.setQuery("");
                                    ZaloClient.peopleSearch.setQuery("");
                                    ZaloClient.hiddenSearch.setQuery("");
                                } else {
                                    searchDebounce.restart();
                                }
                            }
                            onTextChanged: syncQuery()
                            onPreeditTextChanged: syncQuery()
                            Keys.onReturnPressed: function(event) { event.accepted = true; Qt.inputMethod.commit(); syncQuery(); }
                            Keys.onEnterPressed: function(event) { event.accepted = true; Qt.inputMethod.commit(); syncQuery(); }
                            Timer {
                                id: searchDebounce
                                interval: 150
                                onTriggered: {
                                    const q = searchInput.effectiveQuery();
                                    ZaloClient.conversationModel.setSearchFilter(q);
                                    ZaloClient.messageSearch.setQuery(q);
                                    ZaloClient.peopleSearch.setQuery(q);
                                    ZaloClient.hiddenSearch.setQuery(q);
                                }
                            }
                            onActiveFocusChanged: if (activeFocus && ZaloClient.hiddenUnread) ZaloClient.dismissHiddenUnread()
                            Keys.onEscapePressed: { Qt.inputMethod.reset(); text = ""; focus = false; }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: I18n.t("Tìm kiếm", "Search")
                                color: ThemeManager.secondaryTextColor
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                visible: !searchInput.text && !searchInput.preeditText && !searchInput.activeFocus
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

        Flickable {
            objectName: "searchTabBar"
            visible: root.searching
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            clip: true
            contentWidth: searchTabRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width

            Row {
                id: searchTabRow
                height: parent.height
                spacing: 8

                SearchTab {
                    tabKey: "chats"
                    title: I18n.t("Trò chuyện", "Chats")
                }

                TabDivider {}

                SearchTab {
                    tabKey: "people"
                    title: I18n.t("Liên hệ & nhóm", "Contacts & groups")
                }

                TabDivider {}

                SearchTab {
                    tabKey: "messages"
                    title: I18n.t("Tin nhắn", "Messages")
                }
            }
        }

        Item {
            visible: !root.searching
            Layout.fillWidth: true
            Layout.preferredHeight: 36

            Flickable {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.right: labelFilterButton.left
                anchors.rightMargin: 6
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                clip: true
                contentWidth: tabRow.implicitWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentWidth > width

                Row {
                    id: tabRow
                    height: parent.height
                    spacing: 8

                ViewCategoryTab {
                    categoryKey: ""
                    title: I18n.t("Tất cả", "All")
                }

                TabDivider {}

                ViewCategoryTab {
                    categoryKey: "unread"
                    title: I18n.t("Chưa đọc", "Unread")
                }

                TabDivider {}

                ViewTab {
                    viewKey: "archived"
                    title: I18n.t("Khác", "Other")
                    onClicked: ZaloClient.conversationModel.setViewMode("archived")
                }

                TabDivider {}

                    ViewTab {
                        viewKey: "direct"
                        title: I18n.t("Nhắn tôi", "Direct")
                        onClicked: ZaloClient.conversationModel.setViewMode("direct")
                    }
                }
            }

            Rectangle {
                id: labelFilterButton
                anchors.right: typeFilterButton.left
                anchors.rightMargin: 4
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
                width: labelFilterRow.implicitWidth + 14
                radius: 13
                color: labelFilterMouse.containsMouse || labelFilterMenu.opened ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                Row {
                    id: labelFilterRow
                    anchors.centerIn: parent
                    spacing: 4

                    ZIcon {
                        visible: labelFilterButton.hasCurrent
                        anchors.verticalCenter: parent.verticalCenter
                        name: "dot"
                        size: 7
                        color: labelFilterButton.hasCurrent ? labelFilterButton.currentColor : "transparent"
                    }

                    ZIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "tag"
                        size: 15
                        color: labelFilterButton.hasCurrent
                               ? (labelFilterButton.currentColor || Theme.zaloBlue)
                               : (labelFilterMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                    }

                    ZIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron_down"
                        size: 10
                        color: ThemeManager.secondaryTextColor
                    }
                }

                MouseArea {
                    id: labelFilterMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: labelFilterMouse.containsMouse && !labelFilterMenu.opened
                    ToolTip.delay: 350
                    ToolTip.text: labelFilterButton.hasCurrent
                                  ? (I18n.t("Thẻ: ", "Label: ") + labelFilterButton.currentText)
                                  : I18n.t("Phân loại thẻ", "Filter by label")
                    onClicked: labelFilterMenu.open()
                }
                Popup {
                    id: labelFilterMenu
                    margins: 8
                    y: parent.height + 4
                    x: parent.width - width
                    width: 230
                    height: Math.min(labelFilterCol.implicitHeight + 12, Math.max(120, root.height - 32))
                    padding: 6
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { color: ThemeManager.surfaceColor; radius: 8; border.color: ThemeManager.borderColor }
                    contentItem: Flickable {
                        clip: true
                        contentWidth: width
                        contentHeight: labelFilterCol.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds

                        ColumnLayout {
                            id: labelFilterCol
                            width: parent.width
                            spacing: 2
                        LabelMenuRow {
                            labelText: I18n.t("Tất cả hội thoại", "All conversations")
                            checked: ZaloClient.conversationModel.labelFilter === 0
                            onTriggered: { ZaloClient.conversationModel.setLabelFilter(0); labelFilterMenu.close(); }
                        }
                        Repeater {
                            model: labelFilterMenu.opened ? ZaloClient.labels.labels : []
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
                id: typeFilterButton
                objectName: "typeFilterButton"
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: typeFilterRow.implicitWidth + 14
                height: 26
                radius: 13
                readonly property string currentType: ZaloClient.conversationModel.typeFilter
                readonly property bool hasCurrent: currentType !== "all"
                color: typeFilterMenu.opened ? ThemeManager.selectedColor
                     : (hasCurrent ? Theme.zaloBlueLight
                     : (typeFilterMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor))

                Row {
                    id: typeFilterRow
                    anchors.centerIn: parent
                    spacing: 4

                    ZIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "filter"
                        size: 17
                        color: typeFilterButton.hasCurrent || typeFilterMouse.containsMouse
                               ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                    }

                    ZIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevron_down"
                        size: 10
                        color: ThemeManager.secondaryTextColor
                    }
                }

                MouseArea {
                    id: typeFilterMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: typeFilterMouse.containsMouse && !typeFilterMenu.opened
                    ToolTip.delay: 350
                    ToolTip.text: typeFilterButton.currentType === "group"
                                  ? I18n.t("Phân loại hội thoại: Chỉ hội thoại nhóm", "Conversation filter: Groups only")
                                  : (typeFilterButton.currentType === "direct"
                                     ? I18n.t("Phân loại hội thoại: Chỉ hội thoại cá nhân", "Conversation filter: Direct chats only")
                                     : (typeFilterButton.currentType === "friend"
                                        ? I18n.t("Phân loại hội thoại: Chỉ hội thoại bạn bè", "Conversation filter: Friends only")
                                        : (typeFilterButton.currentType === "stranger"
                                           ? I18n.t("Phân loại hội thoại: Chỉ hội thoại người lạ", "Conversation filter: Strangers only")
                                           : I18n.t("Phân loại hội thoại", "Filter conversations"))))
                    onClicked: typeFilterMenu.open()
                }

                Popup {
                    id: typeFilterMenu
                    objectName: "typeFilterMenu"
                    y: parent.height + 4
                    x: parent.width - width
                    width: 210
                    padding: 6
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    background: Rectangle { color: ThemeManager.surfaceColor; radius: 8; border.color: ThemeManager.borderColor }
                    contentItem: ColumnLayout {
                        spacing: 2
                        LabelMenuRow {
                            labelText: I18n.t("Tất cả hội thoại", "All conversations")
                            checked: typeFilterButton.currentType === "all"
                            onTriggered: { ZaloClient.conversationModel.setTypeFilter("all"); typeFilterMenu.close(); }
                        }
                        LabelMenuRow {
                            objectName: "typeFilterGroup"
                            labelText: I18n.t("Chỉ hội thoại nhóm", "Groups only")
                            checked: typeFilterButton.currentType === "group"
                            onTriggered: { ZaloClient.conversationModel.setTypeFilter("group"); typeFilterMenu.close(); }
                        }
                        LabelMenuRow {
                            labelText: I18n.t("Chỉ hội thoại cá nhân", "Direct chats only")
                            checked: typeFilterButton.currentType === "direct"
                            onTriggered: { ZaloClient.conversationModel.setTypeFilter("direct"); typeFilterMenu.close(); }
                        }
                        LabelMenuRow {
                            objectName: "typeFilterFriend"
                            labelText: I18n.t("Chỉ hội thoại bạn bè", "Friends only")
                            checked: typeFilterButton.currentType === "friend"
                            onTriggered: { ZaloClient.conversationModel.setTypeFilter("friend"); typeFilterMenu.close(); }
                        }
                        LabelMenuRow {
                            objectName: "typeFilterStranger"
                            labelText: I18n.t("Chỉ hội thoại người lạ", "Strangers only")
                            checked: typeFilterButton.currentType === "stranger"
                            onTriggered: { ZaloClient.conversationModel.setTypeFilter("stranger"); typeFilterMenu.close(); }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 3; Layout.bottomMargin: 3; color: ThemeManager.borderColor }
                        ZMenuRow {
                            objectName: "markAllReadRow"
                            enabled: ZaloClient.conversationModel.totalUnreadCount > 0
                            icon: "check"
                            label: I18n.t("Đánh dấu tất cả đã đọc", "Mark all as read")
                            onTriggered: { typeFilterMenu.close(); markAllReadDialog.open(); }
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
                font.pixelSize: Theme.fontCaption
                color: ZaloClient.isConnected && !ZaloClient.isOfflineMode ? Theme.zaloBlue : "#B26A00"
            }
        }

        Text {
            visible: root.searching && root.searchTab === "chats" && ZaloClient.hiddenSearch.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            text: I18n.t("Trò chuyện ẩn", "Hidden conversations")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }
        ListView {
            id: hiddenHitList
            objectName: "hiddenHitList"
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            visible: root.searching && root.searchTab === "chats" && ZaloClient.hiddenSearch.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: convList.count > 0 ? Math.min(hiddenHitList.contentHeight, 64 * 4) : Number.POSITIVE_INFINITY
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
                SearchHitRow {
                    anchors.fill: parent
                    avatarSize: 40
                    avatarUrl: hiddenRow.avatar
                    title: hiddenRow.convName
                    subtitle: hiddenRow.text
                    isGroup: hiddenRow.isGroup
                }
            }
        }

        Text {
            visible: root.searching && root.searchTab === "chats" && convList.count > 0 && ZaloClient.hiddenSearch.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            text: I18n.t("Trò chuyện (%1)", "Chats (%1)").arg(convList.count)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
            color: ThemeManager.textColor
        }

        ListView {
            id: convList
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            visible: convList.count > 0 && (!root.searching || root.searchTab === "chats")
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: ZaloClient.conversationModel
            spacing: 0
            reuseItems: true
            cacheBuffer: 300

            property bool pinnedToTop: true
            onMovementEnded: convList.pinnedToTop = convList.atYBeginning
            Connections {
                target: ZaloClient.conversationModel
                function onRowsMoved(parent: var, start: int, end: int, destination: var, row: int) {
                    if (row === 0 && convList.pinnedToTop) Qt.callLater(() => convList.positionViewAtBeginning());
                }
                function onRowsInserted(parent: var, first: int, last: int) {
                    if (first === 0 && convList.pinnedToTop) Qt.callLater(() => convList.positionViewAtBeginning());
                }
            }

            property double now: Date.now()
            Timer {
                interval: 60000
                running: convList.visible && root.windowShowing
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
                required property bool hasUnreadMention
                required property bool isPinned
                required property bool isMuted
                required property bool isHidden
                required property bool isOfficial
                required property bool isTyping
                required property string draft
                required property string lastMessage
                required property string senderPrefix
                required property string tag
                required property string tagColor
                required property double lastTs
                required property string timestamp
                required property int labelId
                property bool reusing: false

                ListView.onPooled: itemDelegate.reusing = true
                ListView.onReused: itemDelegate.reusing = false

                width: convList.width
                height: root.rowHeight
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
                    Behavior on color {
                        enabled: !itemDelegate.reusing
                        ColorAnimation { duration: 130 }
                    }
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
                        size: root.rowHeight === 60 ? 40 : 48
                        avatarUrl: (itemDelegate.avatarUrl && itemDelegate.avatarUrl.length > 0) ? itemDelegate.avatarUrl : ""
                        name: itemDelegate.name
                        isOnline: itemDelegate.isOnline
                        isGroup: itemDelegate.isGroup
                        isVerified: itemDelegate.isOfficial
                        memberCount: itemDelegate.memberCount
                        memberAvatars: itemDelegate.memberAvatars || []
                        showStatusDot: !itemDelegate.isGroup && !itemDelegate.isOfficial
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
                                textFormat: Text.PlainText
                                font.family: Theme.fontFamily
                                font.weight: (itemDelegate.unreadCount > 0) ? Font.DemiBold : Font.Normal
                                font.pixelSize: Theme.fontBodyLarge
                                color: ThemeManager.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                visible: text.length > 0
                                text: convList.relativeTime(itemDelegate.lastTs, itemDelegate.timestamp)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
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
                                    if (ZaloClient.antiPeeking && !mouseArea.containsMouse) return I18n.t("Nội dung đang được ẩn", "Content hidden");
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
                                textFormat: Text.PlainText
                                font.italic: itemDelegate.isTyping
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                color: itemDelegate.isTyping ? Theme.zaloBlue
                                     : (itemDelegate.draft.length > 0 && itemDelegate.id !== ZaloClient.currentConversationId) ? Theme.badgeRed
                                     : (!itemDelegate.lastMessage && itemDelegate.isOnline) ? Theme.onlineGreen
                                     : (itemDelegate.unreadCount > 0 ? ThemeManager.textColor : ThemeManager.secondaryTextColor)
                                font.weight: Font.Normal
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
                                    font.pixelSize: Theme.fontMicro
                                    font.weight: Font.DemiBold
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
                                objectName: "mentionBadge"
                                width: 20
                                height: 20
                                radius: 10
                                color: Theme.zaloBlue
                                visible: itemDelegate.hasUnreadMention && itemDelegate.unreadCount > 0
                                Text {
                                    anchors.centerIn: parent
                                    text: "@"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                    color: "#FFFFFF"
                                }
                            }

                            Rectangle {
                                width: Math.max(unreadBadgeText.implicitWidth + 10, 20)
                                height: 20
                                radius: 10
                                color: Theme.badgeRed
                                visible: itemDelegate.unreadCount > 0
                                scale: itemDelegate.unreadCount > 0 ? 1.0 : 0.0
                                Behavior on scale {
                                    enabled: !itemDelegate.reusing
                                    NumberAnimation { duration: 180; easing.type: Easing.OutBack }
                                }

                                Text {
                                    id: unreadBadgeText
                                    anchors.centerIn: parent
                                    text: itemDelegate.unreadCount > 99 ? "99+" : itemDelegate.unreadCount
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Font.DemiBold
                                    color: "#FFFFFF"
                                }
                            }
                        }
                    }
                }
            }
        }

        ListView {
            id: peopleHitList
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            objectName: "peopleHitList"
            visible: root.searching && root.searchTab === "people" && ZaloClient.peopleSearch.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                SearchHitRow {
                    anchors.fill: parent
                    avatarUrl: personRow.avatar
                    title: personRow.convName
                    subtitle: personRow.text
                    isGroup: personRow.isGroup
                }
            }
        }
        ListView {
            id: messageHitList
            objectName: "messageHitList"
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            visible: root.searching && root.searchTab === "messages" && ZaloClient.messageSearch.count > 0
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
                SearchHitRow {
                    anchors.fill: parent
                    avatarUrl: hitRow.avatar
                    title: hitRow.convName
                    subtitle: (hitRow.senderName.length > 0 ? hitRow.senderName + ": " : "") + hitRow.text
                    dateText: hitRow.ts >= 946684800000 ? Qt.formatDate(new Date(hitRow.ts), "dd/MM/yy") : ""
                    isGroup: hitRow.isGroup
                }
            }
        }

        Item {
            visible: root.searching ? root.searchTabCount(root.searchTab) === 0 : convList.count === 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            ZEmptyState {
                objectName: "conversationsEmptyState"
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 260)
                busy: ZaloClient.isSyncing
                iconName: ZaloClient.conversationModel.searchQuery.length > 0 ? "search" : "chat"
                title: ZaloClient.conversationModel.searchQuery.length > 0 ? I18n.t("Không tìm thấy kết quả phù hợp", "No matching results")
                       : ZaloClient.conversationModel.viewMode === "archived" ? I18n.t("Không có hội thoại trong mục khác", "No conversations in other")
                       : ZaloClient.conversationModel.viewMode === "direct" ? I18n.t("Không có tin nhắn trực tiếp", "No direct messages")
                       : ZaloClient.conversationModel.activeCategory === "unread" ? I18n.t("Không có tin nhắn chưa đọc", "No unread messages")
                       : I18n.tr("no_conversations")
                subtitle: ZaloClient.isSyncing ? I18n.t("Đang tải danh sách trò chuyện...", "Loading your conversations...")
                          : ZaloClient.conversationModel.searchQuery.length > 0 ? ""
                          : I18n.t("Bấm biểu tượng thêm bạn ở trên để bắt đầu trò chuyện", "Use the add-friend button above to start chatting")
            }
        }
    }

    Popup {
        id: convMenu
        margins: 8
        width: 240
        height: Math.min(convMenuCol.implicitHeight + 12, Math.max(140, root.height - 24))
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: 8
            border.color: ThemeManager.borderColor
        }

        contentItem: Flickable {
            clip: true
            contentWidth: width
            contentHeight: convMenuCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: convMenuCol
                width: parent.width
                spacing: 2
            ZMenuRow {
                icon: "pin"
                label: root.menuConvPinned ? I18n.t("Bỏ ghim hội thoại", "Unpin conversation")
                                              : I18n.t("Ghim hội thoại", "Pin conversation")
                onTriggered: { ZaloClient.pinConversation(root.menuConvId); convMenu.close(); }
            }
            ZMenuRow {
                objectName: "markReadRow"
                visible: root.menuConvUnread === true && root.menuConvMarked !== true
                icon: "check"
                label: I18n.t("Đánh dấu đã đọc", "Mark as read")
                onTriggered: { ZaloClient.markConversationRead(root.menuConvId); convMenu.close(); }
            }
            ZMenuRow {
                readonly property bool marked: root.menuConvMarked === true
                visible: marked || !root.menuConvUnread
                icon: "mark_unread"
                label: marked ? I18n.t("Bỏ đánh dấu chưa đọc", "Remove unread mark")
                              : I18n.t("Đánh dấu chưa đọc", "Mark as unread")
                onTriggered: { ZaloClient.toggleUnreadMark(root.menuConvId); convMenu.close(); }
            }
            ZMenuRow {
                visible: root.menuConvMuted === true
                icon: "bell"
                label: I18n.t("Bật thông báo", "Unmute")
                onTriggered: { ZaloClient.muteConversationFor(root.menuConvId, 0); convMenu.close(); }
            }
            Repeater {
                model: convMenu.opened ? [ { vi: "Tắt thông báo 1 giờ", en: "Mute for 1 hour", s: 3600 },
                         { vi: "Tắt thông báo 4 giờ", en: "Mute for 4 hours", s: 14400 },
                         { vi: "Tắt đến 8 giờ sáng", en: "Mute until 8 AM", s: -2 },
                         { vi: "Tắt cho đến khi mở lại", en: "Mute until I turn it back on", s: -1 } ] : []
                ZMenuRow {
                    required property var modelData
                    visible: root.menuConvMuted !== true
                    icon: "bell_off"
                    label: I18n.t(modelData.vi, modelData.en)
                    onTriggered: { ZaloClient.muteConversationFor(root.menuConvId, modelData.s); convMenu.close(); }
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
                font.pixelSize: Theme.fontCaption
                color: ThemeManager.secondaryTextColor
            }
            Repeater {
                model: (convMenu.opened && ZaloClient.labels.loaded) ? ZaloClient.labels.labels : []
                LabelMenuRow {
                    required property var modelData
                    dotColor: modelData.color
                    labelText: modelData.text
                    checked: root.menuConvLabelId === modelData.id
                    onTriggered: { ZaloClient.labels.assign(root.menuConvId, modelData.id); convMenu.close(); }
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
                readonly property bool archived: ZaloClient.isConversationArchived(root.menuConvId)
                icon: "archive"
                label: archived ? I18n.t("Đưa về hội thoại chính", "Move back to main chats")
                                : I18n.t("Chuyển vào Hội thoại khác", "Move to Other conversations")
                onTriggered: { ZaloClient.archiveConversation(root.menuConvId, !archived); convMenu.close(); }
            }
            ZMenuRow {
                readonly property bool hidden: ZaloClient.isConversationHidden(root.menuConvId)
                visible: hidden || ZaloClient.canHideConversation(root.menuConvId)
                icon: "eye_off"
                label: hidden ? I18n.t("Bỏ ẩn trò chuyện", "Unhide conversation")
                              : I18n.t("Ẩn trò chuyện", "Hide conversation")
                onTriggered: {
                    const id = root.menuConvId;
                    const bypass = !!root.menuConvRevealed || id === ZaloClient.currentConversationId;
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
                label: I18n.t("Xóa hội thoại", "Delete conversation")
                onTriggered: { convMenu.close(); deleteConfirm.open(); }
            }
            }
        }
    }

    ZConfirmDialog {
        id: deleteConfirm
        maxWidth: 340
        danger: true
        title: I18n.t("Xóa hội thoại", "Delete conversation")
        okText: I18n.t("Xóa", "Delete")
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: I18n.t("Toàn bộ tin nhắn của hội thoại này sẽ bị xóa ở phía bạn trên mọi thiết bị. Người kia vẫn giữ tin nhắn.",
                         "All messages of this conversation will be deleted for you on every device. The other side keeps them.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: ThemeManager.textColor
        }
        onAccepted: ZaloClient.deleteConversationWithUndo(root.menuConvId)
    }

    ZConfirmDialog {
        id: markAllReadDialog
        objectName: "markAllReadDialog"
        title: I18n.t("Đánh dấu tất cả đã đọc?", "Mark all as read?")
        okText: I18n.t("Đánh dấu", "Mark")
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: I18n.t("Mọi tin nhắn trong danh sách đang hiển thị sẽ được đánh dấu là đã đọc.",
                         "All messages in the conversations shown will be marked as read.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: ThemeManager.textColor
        }
        onAccepted: ZaloClient.markAllConversationsRead()
    }
}
