import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Zalos

Rectangle {
    id: root
    objectName: "chatArea"
    color: ThemeManager.chatBackgroundColor

    property bool showInChatSearch: false
    property bool isFormattingActive: false
    property bool isEditorExpanded: false
    readonly property int composerLineHeight: Math.ceil(composerMetrics.height)
    readonly property real composerMinTextHeight: composerLineHeight * 3 + 16 // ~3 lines
    readonly property real composerMaxTextHeight: composerLineHeight * 8 + 16
    FontMetrics { id: composerMetrics; font.family: Theme.fontFamily; font.pixelSize: 15 }
    // In-chat search: matching rows of messageModel (accent-insensitive) and the focused hit.
    property var searchHits: []
    property int searchCursor: -1
    // Message the context menu acts on.
    property var menuMessage: ({})

    function runSearch(): void {
        root.searchHits = ZaloClient.messageModel.searchMessages(inChatSearchInput.text);
        root.searchCursor = root.searchHits.length > 0 ? root.searchHits.length - 1 : -1;
        if (root.searchCursor >= 0) messageList.positionViewAtIndex(root.searchHits[root.searchCursor], ListView.Center);
    }

    function stepSearch(delta: int): void {
        if (root.searchHits.length === 0) return;
        root.searchCursor = (root.searchCursor + delta + root.searchHits.length) % root.searchHits.length;
        messageList.positionViewAtIndex(root.searchHits[root.searchCursor], ListView.Center);
    }

    function openSearch(): void {
        root.showInChatSearch = true;
        inChatSearchInput.forceActiveFocus();
        inChatSearchInput.selectAll();
    }

    function closeSearch(): void {
        inChatSearchInput.text = "";
        root.searchHits = [];
        root.searchCursor = -1;
        root.showInChatSearch = false;
    }

    // Esc: close the innermost thing first (search, then reply target).
    function handleEscape(): bool {
        if (root.showInChatSearch) { root.closeSearch(); return true; }
        if (ZaloClient.replyTarget.id !== undefined) { ZaloClient.clearReplyTarget(); return true; }
        return false;
    }

    // Reaction details (who reacted with what): opened from a message's reaction pill.
    property string reactionMsgId: ""
    property var reactionInfo: ({ total: 0, known: false, groups: [], people: [] })
    property string reactionTab: ""
    function openReactionDetails(msgId: string, anchor: Item): void {
        root.reactionMsgId = msgId;
        root.reactionTab = "";
        root.reactionInfo = ZaloClient.reactionDetails(msgId);
        reactionPopup.open();
    }

    function openMessageMenu(item: Item, msg: var, mx: real, my: real): void {
        root.menuMessage = msg;
        var p = item.mapToItem(root, mx, my);
        messageMenu.x = Math.min(p.x, root.width - messageMenu.width - 8);
        messageMenu.y = Math.min(p.y, root.height - messageMenu.implicitHeight - 8);
        messageMenu.open();
    }

    // ---- Drafts: unsent composer text per conversation (ZaloClient.setDraft/draftFor) ----
    property string draftConvId: ""
    property bool loadingDraft: false
    // New incoming messages below the viewport (badge on the jump-down button).
    property int newBelow: 0

    Timer {
        id: draftSaveTimer
        interval: 400
        onTriggered: if (root.draftConvId.length > 0) ZaloClient.setDraft(root.draftConvId, msgInput.text)
    }

    function swapDraft(): void {
        draftSaveTimer.stop();
        if (root.draftConvId.length > 0) ZaloClient.setDraft(root.draftConvId, msgInput.text);
        root.draftConvId = ZaloClient.currentConversationId;
        root.loadingDraft = true;
        msgInput.text = root.draftConvId.length > 0 ? ZaloClient.draftFor(root.draftConvId) : "";
        msgInput.cursorPosition = msgInput.text.length;
        ZaloClient.composer.clearFormatting(0, 0); // drafts keep text only
        root.loadingDraft = false;
    }

    // Scrolls to a loaded message (pinned message, quote) and briefly highlights it.
    property string flashMsgId: ""
    function jumpToMessage(msgId: string, cliMsgId: string): bool {
        let row = ZaloClient.messageModel.rowOfMessage(msgId);
        if (row < 0 && cliMsgId.length > 0) row = ZaloClient.messageModel.rowOfMessage(cliMsgId);
        if (row < 0) {
            ZaloClient.showNotice(I18n.t("Tin nhắn này chưa có trong phần lịch sử đã tải. Cuộn lên để tải thêm.",
                                         "This message is not in the loaded history yet. Scroll up to load more."));
            return false;
        }
        messageList.stickToBottom = false;
        messageList.positionViewAtIndex(row, ListView.Center);
        root.flashMsgId = ZaloClient.messageModel.data(ZaloClient.messageModel.index(row, 0), 257) || msgId;
        flashTimer.restart();
        return true;
    }
    Timer { id: flashTimer; interval: 1600; onTriggered: root.flashMsgId = "" }

    function jumpToBottom(): void {
        messageList.stickToBottom = true;
        messageList.positionViewAtEnd();
        root.newBelow = 0;
    }

    Component.onDestruction: {
        if (draftSaveTimer.running && root.draftConvId.length > 0) ZaloClient.setDraft(root.draftConvId, msgInput.text);
    }

    function sendComposer(): void {
        Qt.inputMethod.commit(); // never drop a pending IME word
        const text = msgInput.text;
        if (text.trim() === "") return;
        const mentions = ZaloClient.currentConversationIsGroup ? root.collectMentions(text) : [];
        ZaloClient.sendComposedMessage(text, mentions, ZaloClient.composer.styles());
        msgInput.text = "";
        ZaloClient.composer.clearFormatting(0, 0); // the next message starts plain
        root.mentionPicks = [];
        root.closeSuggestions();
    }

    // ---- Composer suggestions: "@" members (groups) and "/keyword" quick messages ----
    property var mentionPicks: []      // [{uid, label}] chosen from the @ list
    property string suggestMode: ""    // "", "mention", "quick"
    property int suggestStart: -1
    property var suggestItems: []
    property int suggestIndex: 0

    function foldText(t: string): string {
        return t.toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/đ/g, "d");
    }

    function closeSuggestions(): void {
        root.suggestMode = "";
        root.suggestItems = [];
        root.suggestStart = -1;
        root.suggestIndex = 0;
    }

    function updateSuggestions(): void {
        const before = msgInput.text.substring(0, msgInput.cursorPosition);
        const mention = ZaloClient.currentConversationIsGroup ? before.match(/(^|\s)@([^\s@]{0,30})$/) : null;
        if (mention) {
            const q = root.foldText(mention[2]);
            const items = [];
            if (q.length === 0 || root.foldText("all").startsWith(q) || root.foldText("tất cả").startsWith(q)) {
                items.push({ uid: "-1", name: "All", avatar: "", hint: I18n.t("Báo cho cả nhóm", "Notify everyone") });
            }
            const members = ZaloClient.groupMembers(ZaloClient.currentConversationId);
            for (let i = 0; i < members.length && items.length < 8; ++i) {
                const m = members[i];
                if (m.isMe || m.name.length === 0) continue;
                if (q.length === 0 || root.foldText(m.name).indexOf(q) >= 0) {
                    items.push({ uid: m.id, name: m.name, avatar: m.avatar, hint: "" });
                }
            }
            root.suggestMode = "mention";
            root.suggestStart = before.length - mention[2].length - 1;
            root.suggestItems = items;
            root.suggestIndex = 0;
            return;
        }
        const quick = before.match(/^\/(\S{0,30})$/);
        if (quick && ZaloClient.quickMessages.length > 0) {
            const q = root.foldText(quick[1]);
            const list = ZaloClient.quickMessages;
            const items = [];
            for (let i = 0; i < list.length && items.length < 8; ++i) {
                if (q.length === 0 || root.foldText(list[i].keyword).startsWith(q)) items.push(list[i]);
            }
            root.suggestMode = "quick";
            root.suggestStart = 0;
            root.suggestItems = items;
            root.suggestIndex = 0;
            return;
        }
        root.closeSuggestions();
    }

    function acceptSuggestion(index: int): void {
        if (index < 0 || index >= root.suggestItems.length) return;
        const item = root.suggestItems[index];
        const t = msgInput.text;
        const cursor = msgInput.cursorPosition;
        if (root.suggestMode === "mention") {
            const label = "@" + item.name;
            msgInput.text = t.substring(0, root.suggestStart) + label + " " + t.substring(cursor);
            msgInput.cursorPosition = root.suggestStart + label.length + 1;
            const picks = root.mentionPicks.slice();
            picks.push({ uid: item.uid, label: label });
            root.mentionPicks = picks;
        } else if (root.suggestMode === "quick") {
            msgInput.text = item.text;
            msgInput.cursorPosition = msgInput.text.length;
        }
        root.closeSuggestions();
    }

    // Positions of the picked "@Name" labels still present in the text (each occurrence used once).
    function collectMentions(text: string): var {
        const out = [];
        const used = {};
        for (let i = 0; i < root.mentionPicks.length; ++i) {
            const pick = root.mentionPicks[i];
            let from = 0;
            while (true) {
                const pos = text.indexOf(pick.label, from);
                if (pos < 0) break;
                if (!used[pos]) {
                    used[pos] = true;
                    out.push({ uid: pick.uid, pos: pos, len: pick.label.length });
                    break;
                }
                from = pos + 1;
            }
        }
        return out;
    }

    // Opens the photo viewer on this message, with every photo of the conversation for prev/next.
    function openViewer(msgId: string): void {
        const list = ZaloClient.messageModel.mediaItems();
        let start = 0;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].msgId === msgId) { start = i; break; }
        }
        ImageViewerState.show(list, start);
    }

    // Date pill text (Zalo .chat-date): "HH:mm Hôm nay", "HH:mm Hôm qua", else "HH:mm dd/MM/yyyy".
    function dateLabel(ts: real): string {
        if (!ts) return "";
        const d = new Date(ts);
        const today = new Date();
        today.setHours(0, 0, 0, 0);
        const day = new Date(d);
        day.setHours(0, 0, 0, 0);
        const diff = Math.round((today - day) / 86400000);
        const hm = Qt.formatTime(d, "HH:mm");
        if (diff === 0) return hm + " " + I18n.t("Hôm nay", "Today");
        if (diff === 1) return hm + " " + I18n.t("Hôm qua", "Yesterday");
        return hm + " " + Qt.formatDate(d, "dd/MM/yyyy");
    }

    function fileSizeText(bytes: real): string {
        if (!bytes || bytes <= 0) return I18n.t("Tệp đính kèm", "Attachment");
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB";
        if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MB";
        return (bytes / 1073741824).toFixed(2) + " GB";
    }

    // File badge colour by extension (types follow Zalo's File_<Type> icon map; colours are our own).
    function fileColor(ext: string): color {
        if (["doc", "docx"].indexOf(ext) >= 0) return "#2B579A";
        if (["xls", "xlsx", "csv"].indexOf(ext) >= 0) return "#217346";
        if (["ppt", "pptx"].indexOf(ext) >= 0) return "#D24726";
        if (ext === "pdf") return "#E2574C";
        if (["gif", "jpg", "jpeg", "png", "svg", "webp"].indexOf(ext) >= 0) return "#2E9E6B";
        if (["zip", "rar", "7z", "tar", "gz"].indexOf(ext) >= 0) return "#8E6E53";
        if (["m4a", "mp4", "mov", "avi", "mkv"].indexOf(ext) >= 0) return "#7E57C2";
        if (["mp3", "mpg", "wav", "flac"].indexOf(ext) >= 0) return "#EC407A";
        if (["txt", "log", "md"].indexOf(ext) >= 0) return "#607D8B";
        return "#8A94A6";
    }

    function statusLabel(status: string): string {
        if (status === "sending") return I18n.t("Đang gửi", "Sending");
        if (status === "seen") return I18n.t("Đã xem", "Seen");
        if (status === "failed") return I18n.t("Gửi lỗi", "Failed");
        return I18n.t("Đã gửi", "Sent");
    }

    Shortcut {
        sequences: ZaloClient.shortcuts.keys("chat.find")
        enabled: root.visible
        onActivated: root.openSearch()
    }
    Shortcut {
        sequence: "Esc"
        enabled: root.visible && (root.showInChatSearch || ZaloClient.replyTarget.id !== undefined)
        onActivated: root.handleEscape()
    }
    Connections {
        target: ZaloClient
        function onCurrentConversationChanged() {
            root.closeSearch();
            root.mentionPicks = [];
            root.closeSuggestions();
            root.swapDraft();
            root.newBelow = 0;
            // With an unread bar the view starts there (MessageModel::unreadMarkerChanged jumps to it).
            const markerRow = ZaloClient.messageModel.unreadMarkerRow();
            messageList.stickToBottom = markerRow < 0;
            if (markerRow >= 0) Qt.callLater(() => messageList.positionViewAtIndex(markerRow, ListView.Beginning));
        }
        function onGroupMembersChanged(groupId: string) {
            if (root.suggestMode === "mention" && groupId === ZaloClient.currentConversationId) root.updateSuggestions();
        }
    }

    // Zalo text styles on the composer selection (ZaloClient.composer, real document formatting).
    function applyStyle(st: string): void {
        if (!ZaloClient.composer.apply(st, msgInput.selectionStart, msgInput.selectionEnd))
            ZaloClient.showNotice(I18n.t("Chọn đoạn chữ cần định dạng", "Select the text to format"));
        msgInput.forceActiveFocus();
    }

    function toggleList(st: string): void {
        ZaloClient.composer.toggleList(st, msgInput.selectionStart, msgInput.selectionEnd);
        msgInput.forceActiveFocus();
    }

    function clearFormatting(): void {
        ZaloClient.composer.clearFormatting(msgInput.selectionStart, msgInput.selectionEnd);
        msgInput.forceActiveFocus();
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Conversation Header Bar (Matching screenshot 4)
            Rectangle {
                Layout.fillWidth: true
                height: 56
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 16
                    spacing: 10

                    // Back Button (Adaptive Compact / Quarter-Screen mode)
                    Rectangle {
                        visible: mainWindow.isCompactMode
                        width: 32
                        height: 32
                        radius: 16
                        color: btnBackMouse.pressed ? ThemeManager.selectedColor :
                               (btnBackMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                        scale: btnBackMouse.pressed ? 0.92 : 1.0
                        Behavior on scale { NumberAnimation { duration: 100 } }

                        ZIcon {
                            anchors.centerIn: parent
                            name: "arrow_left"
                            size: 16
                            color: ThemeManager.textColor
                        }

                        MouseArea {
                            id: btnBackMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ZaloClient.openWelcomeScreen()
                            ToolTip.visible: containsMouse
                            ToolTip.text: I18n.t("Quay lại danh sách hội thoại", "Back to conversations")
                        }
                    }

                    // Contact / Group Avatar (Round circle with online green status dot)
                    ZAvatar {
                        id: headerAvatar
                        size: 40
                        avatarUrl: ZaloClient.currentConversationAvatar
                        name: ZaloClient.currentConversationName
                        isOnline: ZaloClient.currentConversationIsOnline
                        isGroup: ZaloClient.currentConversationIsGroup
                        memberCount: ZaloClient.currentConversationMemberCount
                        memberAvatars: ZaloClient.currentConversationIsGroup ? ZaloClient.groupComposite(ZaloClient.currentConversationId, ZaloClient.currentConversationMemberCount) : []
                        showStatusDot: !ZaloClient.currentConversationIsGroup

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 350
                            ToolTip.text: I18n.t("Xem thông tin hội thoại", "View conversation info")
                            onClicked: {
                                if (!ZaloClient.currentConversationIsGroup) {
                                    ZaloClient.viewProfile(ZaloClient.currentConversationId);
                                } else {
                                    ZaloClient.toggleConversationInfo();
                                }
                            }
                        }
                    }

                    // Contact Name & Status (Zalo PC authentic presence layout)
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: ZaloClient.currentConversationName.length > 0
                                  ? ZaloClient.currentConversationName
                                  : I18n.t("Người dùng Zalo", "Zalo user")
                            font.family: Theme.fontFamily
                            font.bold: true
                            font.pixelSize: 15
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            spacing: 5
                            visible: ZaloClient.currentConversationIsGroup || ZaloClient.currentConversationIsOnline || (ZaloClient.currentConversationStatus && ZaloClient.currentConversationStatus.length > 0)

                            // Green dot: ONLY visible when actually online in 1-1 chat
                            Rectangle {
                                visible: !ZaloClient.currentConversationIsGroup && ZaloClient.currentConversationIsOnline
                                width: 7
                                height: 7
                                radius: 3.5
                                color: Theme.onlineGreen
                            }

                            Text {
                                text: {
                                    if (ZaloClient.currentConversationIsGroup) {
                                        return ZaloClient.currentConversationMemberCount > 0
                                               ? (ZaloClient.currentConversationMemberCount + I18n.t(" thành viên", " members"))
                                               : I18n.t("Nhóm", "Group");
                                    }
                                    if (ZaloClient.currentConversationIsOnline) {
                                        return I18n.t("Đang hoạt động", "Active now");
                                    }
                                    return (ZaloClient.currentConversationStatus && ZaloClient.currentConversationStatus.length > 0)
                                           ? ZaloClient.currentConversationStatus
                                           : I18n.t("Truy cập gần đây", "Recently active");
                                }
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: (!ZaloClient.currentConversationIsGroup && ZaloClient.currentConversationIsOnline)
                                       ? Theme.onlineGreen
                                       : ThemeManager.secondaryTextColor
                                font.underline: ZaloClient.currentConversationIsGroup && memberCountMouse.containsMouse
                                // Groups: "N thành viên" opens the info panel with the member list (Zalo behaviour).
                                MouseArea {
                                    id: memberCountMouse
                                    anchors.fill: parent
                                    enabled: ZaloClient.currentConversationIsGroup
                                    hoverEnabled: true
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: ZaloClient.showConversationInfo = true
                                }
                            }
                        }
                    }

                    // Action Buttons on Right (Round Circular Hover Highlight)
                    RowLayout {
                        spacing: 4

                        // Add Member Button 👥⁺
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 16
                            color: addMemMouse.pressed ? ThemeManager.selectedColor :
                                   (addMemMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: addMemMouse.pressed ? 0.92 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "user_add"
                                size: 18
                                color: ThemeManager.textColor
                            }

                            MouseArea {
                                id: addMemMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Thêm thành viên vào trò chuyện", "Add member to chat")
                                onClicked: ZaloClient.openCreateGroup()
                            }
                        }

                        // Search Button 🔍
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 16
                            color: (root.showInChatSearch || searchMouse.containsMouse) ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            scale: searchMouse.pressed ? 0.92 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "search"
                                size: 18
                                color: root.showInChatSearch ? Theme.zaloBlue : ThemeManager.textColor
                            }

                            MouseArea {
                                id: searchMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Tìm kiếm trong tin nhắn", "Search messages")
                                onClicked: root.showInChatSearch ? root.closeSearch() : root.openSearch()
                            }
                        }

                        // Voice Call Button 📞
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 16
                            color: voiceMouse.pressed ? ThemeManager.selectedColor :
                                   (voiceMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: voiceMouse.pressed ? 0.92 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "phone"
                                size: 18
                                color: ThemeManager.textColor
                            }

                            MouseArea {
                                id: voiceMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Cuộc gọi thoại", "Voice call")
                                onClicked: ZaloClient.startVoiceCall()
                            }
                        }

                        // Video Call Button 📹
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 16
                            color: videoMouse.pressed ? ThemeManager.selectedColor :
                                   (videoMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: videoMouse.pressed ? 0.92 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "video"
                                size: 18
                                color: ThemeManager.textColor
                            }

                            MouseArea {
                                id: videoMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Cuộc gọi video", "Video call")
                                onClicked: ZaloClient.startVideoCall()
                            }
                        }

                        // Toggle Info Panel 📑
                        Rectangle {
                            width: 32
                            height: 32
                            radius: 16
                            color: (ZaloClient.showConversationInfo || infoMouse.containsMouse) ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            scale: infoMouse.pressed ? 0.92 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "sidebar_panel"
                                size: 18
                                color: ZaloClient.showConversationInfo ? Theme.zaloBlue : ThemeManager.textColor
                            }

                            MouseArea {
                                id: infoMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Thông tin hội thoại", "Conversation info")
                                onClicked: ZaloClient.toggleConversationInfo()
                            }
                        }
                    }
                }
            }

            // In-Chat Search Bar Overlay
            Rectangle {
                visible: root.showInChatSearch
                Layout.fillWidth: true
                height: 42
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 10

                    ZIcon { name: "search"; size: 16; color: Theme.zaloBlue }

                    TextInput {
                        id: inChatSearchInput
                        Layout.fillWidth: true
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        clip: true

                        onTextChanged: searchDebounce.restart()
                        Keys.onReturnPressed: function(event) {
                            root.stepSearch((event.modifiers & Qt.ShiftModifier) ? 1 : -1);
                        }
                        Keys.onEscapePressed: root.closeSearch()
                        Timer { id: searchDebounce; interval: 200; onTriggered: root.runSearch() }

                        Text {
                            text: I18n.t("Tìm tin nhắn trong cuộc trò chuyện (không cần dấu)...",
                                         "Search messages in this conversation...")
                            color: ThemeManager.secondaryTextColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            visible: !inChatSearchInput.text && !inChatSearchInput.activeFocus
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // Counts only messages already loaded in this conversation.
                    Text {
                        visible: inChatSearchInput.text.trim().length > 0
                        text: root.searchHits.length === 0
                              ? I18n.t("Không có kết quả trong tin đã tải", "No results in loaded messages")
                              : (root.searchHits.length - root.searchCursor) + "/" + root.searchHits.length
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: root.searchHits.length === 0 ? ThemeManager.secondaryTextColor : Theme.zaloBlue
                    }

                    Repeater {
                        model: [ { icon: "arrow_up", step: -1, vi: "Kết quả trước", en: "Previous result" },
                                 { icon: "arrow_down", step: 1, vi: "Kết quả sau", en: "Next result" } ]
                        Rectangle {
                            required property var modelData
                            visible: root.searchHits.length > 1
                            width: 24
                            height: 24
                            radius: 12
                            color: stepMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: modelData.icon; size: 14; color: ThemeManager.secondaryTextColor }
                            MouseArea {
                                id: stepMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 400
                                ToolTip.text: I18n.t(modelData.vi, modelData.en)
                                onClicked: root.stepSearch(modelData.step)
                            }
                        }
                    }

                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        color: closeSearchMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                        ZIcon {
                            anchors.centerIn: parent
                            name: "close"
                            size: 14
                            color: ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            id: closeSearchMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.closeSearch()
                        }
                    }
                }
            }

        // Anti-peeking indicator: always visible while the mode is on, with a one-click way out.
        Rectangle {
            visible: ZaloClient.antiPeeking
            Layout.fillWidth: true
            implicitHeight: 32
            color: Theme.zaloBlueLight
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 8
                ZIcon { name: "eye_off"; size: 16; color: Theme.zaloBlueTonalText }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Đang bật chống nhìn lén · rê chuột vào tin nhắn để xem (Ctrl+Shift+H để tắt)",
                                 "Anti-peeking is on · hover a message to read it (Ctrl+Shift+H to turn off)")
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: Theme.zaloBlueTonalText
                }
                Text {
                    text: I18n.t("Tắt", "Turn off")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    font.bold: true
                    font.underline: offMouse.containsMouse
                    color: Theme.zaloBlueTonalText
                    MouseArea {
                        id: offMouse
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ZaloClient.setAntiPeeking(false)
                    }
                }
            }
        }

        // Messages Stream Area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Empty State Placeholder (when no messages exist yet)
            Item {
                anchors.centerIn: parent
                visible: messageList.count === 0
                width: Math.min(parent.width - 40, 360)
                height: emptyCol.implicitHeight

                ColumnLayout {
                    id: emptyCol
                    anchors.centerIn: parent
                    spacing: 12

                    ZAvatar {
                        Layout.alignment: Qt.AlignHCenter
                        size: 72
                        avatarUrl: ZaloClient.currentConversationAvatar
                        name: ZaloClient.currentConversationName
                        isOnline: ZaloClient.currentConversationIsOnline
                        isGroup: ZaloClient.currentConversationIsGroup
                        memberCount: ZaloClient.currentConversationMemberCount
                        memberAvatars: ZaloClient.currentConversationIsGroup ? ZaloClient.groupComposite(ZaloClient.currentConversationId, ZaloClient.currentConversationMemberCount) : []
                        showStatusDot: false
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: ZaloClient.currentConversationName
                        font.family: Theme.fontFamily
                        font.bold: true
                        font.pixelSize: 17
                        color: ThemeManager.textColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Chưa có tin nhắn nào trong cuộc trò chuyện này."
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 140
                        height: 36
                        radius: 18
                        color: helloBtnMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 6
                            Text { text: "👋"; font.pixelSize: 14 }
                            Text {
                                text: "Gửi lời chào"
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                font.bold: true
                                color: "#FFFFFF"
                            }
                        }

                        MouseArea {
                            id: helloBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                ZaloClient.sendMessage("👋 Xin chào!");
                            }
                        }
                    }
                }
            }

            // Drag files/photos onto the conversation to send them (Zalo .drag-over-message-view).
            DropArea {
                id: dropArea
                anchors.fill: parent
                z: 50
                keys: ["text/uri-list"]
                onDropped: function(drop) {
                    for (let i = 0; i < drop.urls.length; ++i) ZaloClient.sendFile(drop.urls[i].toString());
                    drop.acceptProposedAction();
                }
                Rectangle {
                    visible: dropArea.containsDrag
                    anchors.fill: parent
                    anchors.margins: 8
                    radius: 8
                    color: Theme.dropOverlay
                    opacity: 0.95
                    border.width: 2
                    border.color: Theme.zaloBlue
                    Text {
                        anchors.centerIn: parent
                        text: I18n.t("Thả tệp hoặc ảnh vào đây để gửi nhanh", "Drop files or photos here to send")
                        font.family: Theme.fontFamily
                        font.pixelSize: 18
                        font.weight: Font.Medium
                        color: Theme.zaloBlue
                    }
                }
            }

            // Settings > Giao diện > "Dùng ảnh đại diện làm hình nền": the open conversation's avatar, faint and
            // cropped, behind the messages (theme surface stays the base colour).
            Image {
                parent: messageList.parent
                z: -1
                anchors.fill: messageList
                visible: ZaloClient.useAvatarAsChatBackground && ZaloClient.currentConversationAvatar.length > 0
                         && status === Image.Ready
                // The original picture (image://avatar returns a circle, unsuited for a backdrop).
                source: ZaloClient.useAvatarAsChatBackground ? ZaloClient.currentConversationAvatar : ""
                sourceSize.width: 640
                fillMode: Image.PreserveAspectCrop
                opacity: ThemeManager.isDarkMode ? 0.14 : 0.1
                asynchronous: true
            }

            // Jump to the newest message, with the number of new incoming messages below.
            ChatJumpDownButton {
                parent: messageList.parent
                z: 10
                anchors.right: messageList.right
                anchors.bottom: messageList.bottom
                anchors.rightMargin: 16
                anchors.bottomMargin: 14
                newCount: root.newBelow
                shown: messageList.count > 0 && !messageList.atYEnd
                onClicked: root.jumpToBottom()
            }

            // Pinned messages (ZaloClient.pins): the top pin; "+N" opens the list (move to top, unpin).
            Rectangle {
                id: pinStrip
                parent: messageList.parent
                z: 11
                visible: ZaloClient.pins.pins.length > 0
                readonly property var topPin: ZaloClient.pins.pins.length > 0 ? ZaloClient.pins.pins[0] : ({})
                anchors.top: messageList.top
                anchors.left: messageList.left
                anchors.right: messageList.right
                anchors.margins: 8
                height: 52
                radius: 8
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 10
                    ZIcon { name: "pin"; size: 18; color: Theme.zaloBlue }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Tin nhắn đã ghim", "Pinned message")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: (pinStrip.topPin.senderName ? pinStrip.topPin.senderName + ": " : "")
                                  + (pinStrip.topPin.title || I18n.t("[Tin nhắn]", "[Message]"))
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                    ZButton {
                        visible: ZaloClient.pins.pins.length > 1
                        variant: "secondary"
                        text: "+" + (ZaloClient.pins.pins.length - 1)
                        onClicked: pinListPopup.open()
                    }
                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 14
                        color: pinMoreMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                        ZIcon { anchors.centerIn: parent; name: "more_horiz"; size: 16; color: ThemeManager.iconColor }
                        MouseArea {
                            id: pinMoreMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pinListPopup.open()
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    z: -1
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.jumpToMessage(pinStrip.topPin.msgId || "", pinStrip.topPin.cliMsgId || "")
                }
            }

            ListView {
                id: messageList
                anchors.fill: parent
                clip: true
                model: ZaloClient.messageModel
                spacing: 14
                topMargin: pinStrip.visible ? 16 + 60 : 16 // keep the first message clear of the pinned strip
                bottomMargin: 16

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 150; easing.type: Easing.OutQuad }
                }

                // Follow new messages only while the user is at the bottom; older pages are
                // prepended without moving what is on screen.
                property bool stickToBottom: true
                property int countBeforeReset: 0
                onMovementEnded: stickToBottom = atYEnd
                onAtYEndChanged: if (atYEnd) root.newBelow = 0
                onCountChanged: {
                    if (stickToBottom) Qt.callLater(() => messageList.positionViewAtEnd());
                }
                // Rows grow after layout (pictures loading, wrapped text): stay pinned to the newest message.
                onContentHeightChanged: {
                    if (stickToBottom && !moving) Qt.callLater(() => messageList.positionViewAtEnd());
                }
                onAtYBeginningChanged: {
                    if (atYBeginning && count > 0 && !stickToBottom && !ZaloClient.isLoadingHistory
                            && !ZaloClient.historyExhausted) {
                        ZaloClient.loadOlderMessages();
                    }
                }
                Connections {
                    target: ZaloClient.messageModel
                    function onIncomingAppended(count: int) {
                        if (!messageList.stickToBottom) root.newBelow += count;
                    }
                    // Placed late (history arrived after the thread opened): start at the unread bar.
                    function onUnreadMarkerChanged(row: int) {
                        if (row < 0) return;
                        messageList.stickToBottom = false;
                        Qt.callLater(() => messageList.positionViewAtIndex(row, ListView.Beginning));
                    }
                    function onModelAboutToBeReset() { messageList.countBeforeReset = messageList.count; }
                    function onModelReset() {
                        const added = messageList.count - messageList.countBeforeReset;
                        if (!messageList.stickToBottom && added > 0 && messageList.countBeforeReset > 0) {
                            Qt.callLater(() => messageList.positionViewAtIndex(added, ListView.Beginning));
                        }
                    }
                }

                header: Item {
                    width: messageList.width
                    height: ZaloClient.isLoadingHistory ? 36 : 0
                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 24
                        height: 24
                        running: ZaloClient.isLoadingHistory
                        visible: running
                    }
                }

                delegate: Item {
                    id: msgRow
                    width: messageList.width
                    // Tiles folded into the media run of an earlier row take no space (MessageModel::InMediaRunRole).
                    visible: !model.inMediaRun
                    height: model.inMediaRun ? 0 : rowCol.implicitHeight + (model.lastInGroup ? 10 : 2)

                    readonly property bool mine: model.isMine
                    readonly property bool isPhoto: model.messageType === "image" || model.messageType === "video"
                    readonly property bool isSticker: model.messageType === "sticker"
                    // Consecutive photos / stickers of this sender, shown side by side in this row.
                    readonly property var mediaRun: model.mediaRun
                    readonly property bool runHead: mediaRun.length > 1
                    readonly property bool isFile: model.messageType === "file"
                    readonly property bool isSystem: model.messageType === "system"
                    readonly property bool isLink: model.messageType === "link"
                    readonly property bool isVoice: model.messageType === "voice"
                    readonly property bool isCall: model.messageType === "call"
                    readonly property bool isPoll: model.messageType === "poll"
                    readonly property bool isContactCard: model.messageType === "card"
                    readonly property bool hasCard: isFile || isLink || isVoice || isCall || isPoll || isContactCard
                    // Photos/videos without caption and stickers have no bubble (.img-msg-v2).
                    readonly property bool bubbleless: (isPhoto && model.text.length === 0 && model.quoteText.length === 0) || isSticker
                    readonly property real maxBubble: Math.min(messageList.width * 0.7, 560)
                    readonly property bool isSearchHit: root.searchCursor >= 0 && root.searchHits[root.searchCursor] === index
                    readonly property var menuInfo: ({ id: model.msgId, text: model.text, isMine: model.isMine,
                                                       messageType: model.messageType,
                                                       url: model.href.length > 0 ? model.href : model.mediaUrl })

                    HoverHandler { id: rowHover }

                    // Briefly highlighted after jumping here from a pinned message.
                    Rectangle {
                        anchors.fill: parent
                        z: -1
                        radius: 6
                        color: Theme.zaloBlueLight
                        opacity: root.flashMsgId.length > 0 && model.msgId === root.flashMsgId ? 0.7 : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }

                    ColumnLayout {
                        id: rowCol
                        width: parent.width
                        spacing: 4

                        // Date/time pill (.chat-date): first message of a day.
                        Item {
                            id: datePill
                            visible: model.dateSeparator
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            implicitHeight: visible ? 30 : 0
                            Rectangle {
                                anchors.centerIn: parent
                                width: dateText.implicitWidth + 20
                                height: 22
                                radius: 10
                                color: Theme.timestampPill
                                Text {
                                    id: dateText
                                    anchors.centerIn: parent
                                    text: root.dateLabel(model.ts)
                                    color: "#FFFFFF"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                }
                            }
                        }

                        // "Tin nhắn chưa đọc" bar above the first unread message of the opened thread.
                        ChatUnreadBar {
                            visible: model.unreadMarker
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                        }

                        // Group event notice (rename, members, admins): centered line, no bubble.
                        Text {
                            visible: msgRow.isSystem
                            Layout.fillWidth: true
                            Layout.leftMargin: 40
                            Layout.rightMargin: 40
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: model.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }

                        RowLayout {
                            id: bubbleRow
                            visible: !msgRow.isSystem
                            Layout.fillWidth: true
                            spacing: 8

                            Item { visible: msgRow.mine; Layout.fillWidth: true }

                            // Avatar column: the avatar only on the first message of a run (Zalo grouping).
                            Item {
                                visible: !msgRow.mine
                                Layout.leftMargin: 16
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: 32
                                implicitHeight: 32
                                ZAvatar {
                                    visible: model.firstInGroup
                                    anchors.fill: parent
                                    size: 32
                                    avatarUrl: (model.senderAvatar && model.senderAvatar.length > 0)
                                               ? model.senderAvatar
                                               : (ZaloClient.currentConversationIsGroup ? "" : ZaloClient.currentConversationAvatar)
                                    name: (model.senderName && model.senderName.length > 0) ? model.senderName : ZaloClient.currentConversationName
                                    showStatusDot: false
                                }
                                MouseArea {
                                    visible: model.firstInGroup && model.senderId.length > 0
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ZaloClient.viewProfile(model.senderId)
                                }
                            }

                            ColumnLayout {
                                id: contentCol
                                Layout.maximumWidth: msgRow.maxBubble
                                Layout.rightMargin: msgRow.mine ? 16 : 0
                                // Anti-peeking: dimmed until the pointer is on this message.
                                opacity: (ZaloClient.antiPeeking && !rowHover.hovered) ? 0.08 : 1.0
                                Behavior on opacity { NumberAnimation { duration: 150 } }
                                spacing: 0

                                Text {
                                    visible: !msgRow.mine && ZaloClient.currentConversationIsGroup && model.firstInGroup
                                             && model.senderName.length > 0
                                    Layout.bottomMargin: 4
                                    text: model.senderName
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: ThemeManager.secondaryTextColor
                                }

                                // ---- Photo / video thumbnail (no bubble unless captioned) ----
                                Item {
                                    id: mediaBox
                                    visible: msgRow.isPhoto && model.mediaUrl.length > 0 && !msgRow.runHead
                                    // Fit into max(min(798, chat-237)) × 368, at least 150 × 110 (Zalo .img-msg-v2).
                                    readonly property real srcW: mediaImg.implicitImageWidth > 0 ? mediaImg.implicitImageWidth : 240
                                    readonly property real srcH: mediaImg.implicitImageHeight > 0 ? mediaImg.implicitImageHeight : 180
                                    readonly property real fit: Math.min(Math.min(360, messageList.width - 237) / srcW, 368 / srcH, 1.0)
                                    Layout.preferredWidth: visible ? Math.max(150, srcW * fit) : 0
                                    Layout.preferredHeight: visible ? Math.max(110, srcH * fit) : 0
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft

                                    ZRoundedImage {
                                        id: mediaImg
                                        anchors.fill: parent
                                        source: mediaBox.visible ? model.mediaUrl : ""
                                        animatedSource: mediaBox.visible && model.extra.animatedUrl ? model.extra.animatedUrl : ""
                                        // Decoded at the shown size (max 360 px wide) × screen density, not a fixed 720.
                                        sourceSize.width: Math.ceil(360 * (Screen.devicePixelRatio || 1))
                                        topOnly: !msgRow.bubbleless
                                    }
                                    Rectangle { // hover overlay (--media-message-hover)
                                        anchors.fill: parent
                                        radius: 8
                                        color: Theme.mediaHover
                                        visible: mediaMouse.containsMouse
                                    }
                                    Rectangle { // video: play button
                                        visible: model.messageType === "video"
                                        anchors.centerIn: parent
                                        width: 44
                                        height: 44
                                        radius: 22
                                        color: Qt.rgba(0, 0, 0, 0.55)
                                        ZIcon { anchors.centerIn: parent; name: "play"; size: 26; color: "#FFFFFF" }
                                    }
                                    MouseArea {
                                        id: mediaMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        ToolTip.visible: containsMouse && model.messageType === "video"
                                        ToolTip.delay: 600
                                        ToolTip.text: I18n.t("Mở video trong trình duyệt", "Open video in browser")
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                root.openMessageMenu(mediaMouse, msgRow.menuInfo, mouse.x, mouse.y);
                                            } else if (model.messageType === "video") {
                                                if (model.href.length > 0) Qt.openUrlExternally(model.href);
                                            } else {
                                                root.openViewer(model.msgId);
                                            }
                                        }
                                    }
                                }

                                // ---- Sticker (no bubble) ----
                                // Click opens it in the photo viewer. Until its URL is known (sticker_detail lookup,
                                // queued while offline and retried) a placeholder shows instead of an empty gap.
                                Item {
                                    id: stickerBox
                                    visible: msgRow.isSticker && !msgRow.runHead
                                    Layout.preferredWidth: visible ? 130 : 0
                                    Layout.preferredHeight: visible ? 130 : 0
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    // Sticker URLs are animated WebP (sticker_detail stickerWebpUrl): AnimatedImage plays them
                                    // (needs Qt's qwebp image plugin, qt6-qtimageformats) and shows still PNGs as is.
                                    ZSticker {
                                        id: stickerImage
                                        anchors.fill: parent
                                        info: stickerBox.visible && model.extra.sticker ? model.extra.sticker : ({})
                                        fallbackUrl: stickerBox.visible ? model.mediaUrl : ""
                                        playing: stickerBox.visible
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        visible: stickerImage.status !== Image.Ready
                                        radius: 12
                                        color: Theme.mediaPlaceholder
                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 6
                                            ZIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "sticker"; size: 32; color: ThemeManager.secondaryTextColor }
                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: stickerImage.status === Image.Loading ? I18n.t("Đang tải…", "Loading…")
                                                      : stickerImage.status === Image.Error ? I18n.t("Không tải được", "Couldn't load")
                                                      : I18n.t("Nhãn dán", "Sticker")
                                                color: ThemeManager.secondaryTextColor
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: stickerImage.status === Image.Ready ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                root.openMessageMenu(stickerBox, msgRow.menuInfo, mouse.x, mouse.y);
                                            } else if (stickerImage.status === Image.Ready) {
                                                ImageViewerState.show([{ msgId: model.msgId, url: model.mediaUrl, thumb: model.mediaUrl,
                                                                         senderName: model.senderName, isMine: model.isMine, ts: model.ts }], 0);
                                            }
                                        }
                                    }
                                }

                                // ---- Media run: consecutive photos / stickers side by side (wraps when narrow) ----
                                Flow {
                                    id: runFlow
                                    visible: msgRow.runHead
                                    spacing: 4
                                    layoutDirection: msgRow.mine ? Qt.RightToLeft : Qt.LeftToRight
                                    readonly property real maxW: Math.max(150, Math.min(msgRow.maxBubble, messageList.width - 140))
                                    readonly property real naturalW: {
                                        let w = 0;
                                        for (let i = 0; i < msgRow.mediaRun.length; ++i)
                                            w += (msgRow.mediaRun[i].messageType === "sticker" ? 130 : 150) + (i > 0 ? spacing : 0);
                                        return w;
                                    }
                                    Layout.preferredWidth: visible ? Math.min(maxW, naturalW) : 0
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    width: Layout.preferredWidth
                                    Repeater {
                                        model: msgRow.runHead ? msgRow.mediaRun : []
                                        delegate: Item {
                                            id: tile
                                            required property var modelData
                                            readonly property bool sticker: tile.modelData.messageType === "sticker"
                                            width: tile.sticker ? 130 : 150
                                            height: tile.sticker ? 130 : 150
                                            ZRoundedImage {
                                                visible: !tile.sticker
                                                anchors.fill: parent
                                                source: tile.sticker ? "" : tile.modelData.mediaUrl
                                                animatedSource: !tile.sticker && tile.modelData.animatedUrl ? tile.modelData.animatedUrl : ""
                                                sourceSize.width: 320
                                            }
                                            ZSticker {
                                                id: tileSticker
                                                visible: tile.sticker
                                                anchors.fill: parent
                                                info: tile.sticker && tile.modelData.sticker ? tile.modelData.sticker : ({})
                                                fallbackUrl: tile.sticker ? tile.modelData.mediaUrl : ""
                                                playing: visible
                                            }
                                            Rectangle {
                                                anchors.fill: parent
                                                visible: tile.sticker && tileSticker.status !== Image.Ready
                                                radius: 12
                                                color: Theme.mediaPlaceholder
                                                ZIcon { anchors.centerIn: parent; name: "sticker"; size: 28; color: ThemeManager.secondaryTextColor }
                                            }
                                            Rectangle {
                                                anchors.fill: parent
                                                radius: 8
                                                color: Theme.mediaHover
                                                visible: tileMouse.containsMouse && !tile.sticker
                                            }
                                            MouseArea {
                                                id: tileMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                onClicked: function(mouse) {
                                                    const t = tile.modelData;
                                                    if (mouse.button === Qt.RightButton) {
                                                        root.openMessageMenu(tile, { id: t.msgId, text: "", isMine: t.isMine, messageType: t.messageType,
                                                                                     url: t.href.length > 0 ? t.href : t.mediaUrl }, mouse.x, mouse.y);
                                                    } else if (tile.sticker) {
                                                        if (tileSticker.status === Image.Ready)
                                                            ImageViewerState.show([{ msgId: t.msgId, url: t.mediaUrl, thumb: t.mediaUrl,
                                                                                     senderName: t.senderName, isMine: t.isMine, ts: t.ts }], 0);
                                                    } else {
                                                        root.openViewer(t.msgId);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                // ---- Bubble: text, caption, quote, file card ----
                                Rectangle {
                                    id: bubbleRect
                                    visible: !msgRow.bubbleless
                                    readonly property real innerMax: msgRow.maxBubble - 24
                                    implicitWidth: msgRow.isPhoto ? mediaBox.Layout.preferredWidth
                                                                  : Math.min(bubbleInner.implicitWidth + 24, msgRow.maxBubble)
                                    implicitHeight: bubbleInner.implicitHeight + (msgRow.isPhoto ? 17 : 16)
                                    Layout.preferredWidth: implicitWidth
                                    Layout.preferredHeight: implicitHeight
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    // Captioned photo: square top corners under the picture (radius 0 0 8 8).
                                    radius: 8
                                    color: msgRow.mine ? ThemeManager.chatBubbleMine : ThemeManager.chatBubbleTheir
                                    border.color: msgRow.isSearchHit ? Theme.zaloBlue : (msgRow.mine ? "transparent" : ThemeManager.borderColor)
                                    border.width: msgRow.isSearchHit ? 2 : 1
                                    Rectangle {
                                        visible: msgRow.isPhoto
                                        width: parent.width
                                        height: 8
                                        color: parent.color
                                    }

                                    MouseArea { // right click anywhere on the bubble: message menu
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        onClicked: function(mouse) {
                                            if (!model.isRecalled) root.openMessageMenu(bubbleRect, msgRow.menuInfo, mouse.x, mouse.y);
                                        }
                                    }

                                    ColumnLayout {
                                        id: bubbleInner
                                        x: 12
                                        y: msgRow.isPhoto ? 7 : 8
                                        width: bubbleRect.width - 24
                                        spacing: 4

                                        // Quoted message (quote reply)
                                        Rectangle {
                                            visible: model.quoteMsgId.length > 0 || model.quoteText.length > 0
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 160
                                            implicitHeight: quoteCol.implicitHeight + 10
                                            implicitWidth: quoteCol.implicitWidth + 18
                                            radius: 4
                                            color: ThemeManager.hoverColor
                                            Rectangle { width: 3; height: parent.height; radius: 1.5; color: Theme.zaloBlue }
                                            ColumnLayout {
                                                id: quoteCol
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                anchors.rightMargin: 8
                                                anchors.topMargin: 5
                                                anchors.bottomMargin: 5
                                                spacing: 1
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: model.quoteSender
                                                    elide: Text.ElideRight
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    font.bold: true
                                                    color: ThemeManager.textColor
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: model.quoteText
                                                    elide: Text.ElideRight
                                                    maximumLineCount: 2
                                                    wrapMode: Text.Wrap
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: ThemeManager.secondaryTextColor
                                                }
                                            }
                                        }

                                        // File card (.file-message): type badge, name, size, open/download.
                                        Item {
                                            id: fileCardBox
                                            visible: msgRow.isFile
                                            Layout.preferredWidth: visible ? Math.min(352, messageList.width - 184) : 0
                                            implicitWidth: Layout.preferredWidth
                                            implicitHeight: visible ? fileCard.implicitHeight : 0
                                        RowLayout {
                                            id: fileCard
                                            anchors.fill: parent
                                            spacing: 12
                                            readonly property string ext: {
                                                const n = model.text || "";
                                                const dot = n.lastIndexOf(".");
                                                return dot > 0 ? n.substring(dot + 1).toLowerCase() : "";
                                            }
                                            Rectangle {
                                                Layout.preferredWidth: 38
                                                Layout.preferredHeight: 48
                                                radius: 6
                                                color: root.fileColor(fileCard.ext)
                                                Text {
                                                    anchors.centerIn: parent
                                                    width: parent.width - 4
                                                    horizontalAlignment: Text.AlignHCenter
                                                    text: (fileCard.ext || "file").toUpperCase().substring(0, 4)
                                                    color: "#FFFFFF"
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 10
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: model.text
                                                    elide: Text.ElideMiddle
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 14
                                                    font.weight: Font.DemiBold
                                                    color: ThemeManager.textColor
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: root.fileSizeText(model.fileSize)
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: ThemeManager.secondaryTextColor
                                                }
                                            }
                                            Rectangle {
                                                Layout.preferredWidth: 32
                                                Layout.preferredHeight: 32
                                                radius: 4
                                                color: fileOpenMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                                ZIcon { anchors.centerIn: parent; name: "download"; size: 20; color: ThemeManager.secondaryTextColor }
                                            }
                                        }
                                            MouseArea {
                                                id: fileOpenMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                ToolTip.visible: containsMouse
                                                ToolTip.delay: 500
                                                ToolTip.text: I18n.t("Tải về và mở (lưu trong Tải xuống/Zalos)", "Download and open (saved in Downloads/Zalos)")
                                                onClicked: ZaloClient.openFileMessage(model.href.length > 0 ? model.href : model.mediaUrl, model.text)
                                            }
                                        }

                                        // Contact card (Zalo .name-card): avatar, name, phone; Nhắn tin / Xem hồ sơ.
                                        ColumnLayout {
                                            id: contactCard
                                            visible: msgRow.isContactCard && !model.isRecalled
                                            readonly property string uid: model.extra.uid || ""
                                            readonly property string phone: model.extra.phone || ""
                                            Layout.preferredWidth: visible ? Math.min(280, bubbleRect.innerMax) : 0
                                            spacing: 8
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 10
                                                ZAvatar {
                                                    size: 48
                                                    avatarUrl: model.mediaUrl
                                                    name: model.text
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 2
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: model.text
                                                        elide: Text.ElideRight
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 15
                                                        font.weight: Font.DemiBold
                                                        color: ThemeManager.textColor
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: contactCard.phone.length > 0 ? contactCard.phone
                                                              : (model.extra.official ? I18n.t("Official Account", "Official Account")
                                                                                      : I18n.t("Danh thiếp Zalo", "Zalo contact card"))
                                                        elide: Text.ElideRight
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 12
                                                        color: ThemeManager.secondaryTextColor
                                                    }
                                                }
                                            }
                                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: ThemeManager.dividerColor }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8
                                                ZButton {
                                                    Layout.fillWidth: true
                                                    variant: "tonal"
                                                    enabled: contactCard.uid.length > 0 && contactCard.uid !== ZaloClient.myUid
                                                    text: I18n.t("Nhắn tin", "Message")
                                                    onClicked: ZaloClient.openConversationWithContact(contactCard.uid, model.text, model.mediaUrl)
                                                }
                                                ZButton {
                                                    Layout.fillWidth: true
                                                    variant: "secondary"
                                                    enabled: contactCard.uid.length > 0
                                                    text: I18n.t("Xem hồ sơ", "View profile")
                                                    onClicked: ZaloClient.viewProfile(contactCard.uid)
                                                }
                                            }
                                        }

                                        // Link card (.link-message): thumb, title, description, domain.
                                        ColumnLayout {
                                            visible: msgRow.isLink && !model.isRecalled
                                            Layout.preferredWidth: visible ? Math.min(320, bubbleRect.innerMax) : 0
                                            spacing: 4
                                            Image {
                                                visible: model.mediaUrl.length > 0 && status !== Image.Error
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: visible ? 150 : 0
                                                source: msgRow.isLink ? model.mediaUrl : ""
                                                asynchronous: true
                                                fillMode: Image.PreserveAspectCrop
                                                sourceSize.width: Math.ceil(320 * (Screen.devicePixelRatio || 1))
                                                clip: true
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: model.text
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: ThemeManager.textColor
                                            }
                                            Text {
                                                visible: text.length > 0
                                                Layout.fillWidth: true
                                                text: msgRow.isLink ? (model.extra.desc || "") : ""
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: ThemeManager.secondaryTextColor
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: msgRow.isLink ? (model.extra.domain || model.href) : ""
                                                elide: Text.ElideRight
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: Theme.linkColor
                                            }
                                            TapHandler {
                                                onTapped: if (model.href.length > 0) ZaloClient.openLink(model.href)
                                            }
                                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        }

                                        // Voice message: play/pause in the app, duration.
                                        RowLayout {
                                            id: voiceRow
                                            visible: msgRow.isVoice && !model.isRecalled
                                            spacing: 10
                                            readonly property bool playing: ZaloClient.playingVoiceId === model.msgId
                                            Rectangle {
                                                Layout.preferredWidth: 32
                                                Layout.preferredHeight: 32
                                                radius: 16
                                                color: Theme.zaloBlue
                                                ZIcon { anchors.centerIn: parent; name: voiceRow.playing ? "pause" : "play"; size: 18; color: "#FFFFFF" }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    ToolTip.visible: containsMouse
                                                    ToolTip.delay: 500
                                                    ToolTip.text: I18n.t("Phát / dừng tin nhắn thoại", "Play / stop voice message")
                                                    onClicked: ZaloClient.toggleVoice(model.msgId, model.href)
                                                }
                                            }
                                            Text {
                                                text: I18n.t("Tin nhắn thoại", "Voice message")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 14
                                                color: ThemeManager.textColor
                                            }
                                            Text {
                                                readonly property int secs: msgRow.isVoice ? Math.round((model.extra.durationMs || 0) / 1000) : 0
                                                visible: secs > 0
                                                text: Math.floor(secs / 60) + ":" + ("0" + (secs % 60)).slice(-2)
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: ThemeManager.secondaryTextColor
                                            }
                                        }

                                        // Call history (chat.recommended calltime / misscall) with call back.
                                        RowLayout {
                                            visible: msgRow.isCall
                                            spacing: 10
                                            Rectangle {
                                                Layout.preferredWidth: 32
                                                Layout.preferredHeight: 32
                                                radius: 16
                                                color: msgRow.isCall && model.extra.missed ? Theme.badgeRed : ThemeManager.hoverColor
                                                ZIcon {
                                                    anchors.centerIn: parent
                                                    name: msgRow.isCall && model.extra.video ? "video" : "phone"
                                                    size: 16
                                                    color: msgRow.isCall && model.extra.missed ? "#FFFFFF" : ThemeManager.textColor
                                                }
                                            }
                                            ColumnLayout {
                                                spacing: 0
                                                Text {
                                                    text: model.text
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 14
                                                    font.weight: Font.DemiBold
                                                    color: msgRow.isCall && model.extra.missed ? Theme.badgeRed : ThemeManager.textColor
                                                }
                                                Text {
                                                    visible: text.length > 0
                                                    text: msgRow.isCall ? (model.extra.duration || "") : ""
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: ThemeManager.secondaryTextColor
                                                }
                                            }
                                            Rectangle {
                                                visible: !ZaloClient.currentConversationIsGroup
                                                Layout.leftMargin: 6
                                                Layout.preferredHeight: 28
                                                Layout.preferredWidth: callBackText.implicitWidth + 20
                                                radius: 14
                                                color: callBackMouse.containsMouse ? Theme.zaloBlueLight : ThemeManager.hoverColor
                                                Text {
                                                    id: callBackText
                                                    anchors.centerIn: parent
                                                    text: I18n.t("Gọi lại", "Call back")
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: Theme.zaloBlueTonalText
                                                }
                                                MouseArea {
                                                    id: callBackMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        if (model.extra.video) ZaloClient.startVideoCall();
                                                        else ZaloClient.startVoiceCall();
                                                    }
                                                }
                                            }
                                        }

                                        // Poll card: question, options with vote bars (detail from /api/poll/detail).
                                        ColumnLayout {
                                            id: pollCard
                                            visible: msgRow.isPoll && !model.isRecalled
                                            Layout.preferredWidth: visible ? Math.min(320, bubbleRect.innerMax) : 0
                                            spacing: 6
                                            readonly property var poll: msgRow.isPoll ? model.pollData : ({})
                                            readonly property int total: pollCard.poll.num_vote || 0
                                            Component.onCompleted: {
                                                if (msgRow.isPoll && (model.pollData.poll_id || 0) > 0) ZaloClient.refreshPoll(model.pollData.poll_id, false);
                                            }
                                            Text {
                                                text: I18n.t("BÌNH CHỌN", "POLL")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 11
                                                font.bold: true
                                                color: ThemeManager.secondaryTextColor
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: model.text
                                                wrapMode: Text.Wrap
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 15
                                                font.weight: Font.DemiBold
                                                color: ThemeManager.textColor
                                            }
                                            Repeater {
                                                model: pollCard.poll.options || []
                                                delegate: Rectangle {
                                                    id: optRow
                                                    required property var modelData
                                                    Layout.fillWidth: true
                                                    implicitHeight: 34
                                                    radius: 6
                                                    color: ThemeManager.hoverColor
                                                    border.color: optRow.modelData.voted ? Theme.zaloBlue : "transparent"
                                                    border.width: 1
                                                    Rectangle { // vote share bar
                                                        width: pollCard.total > 0 ? parent.width * (optRow.modelData.votes / pollCard.total) : 0
                                                        height: parent.height
                                                        radius: 6
                                                        color: Theme.zaloBlueLight
                                                    }
                                                    RowLayout {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: 10
                                                        anchors.rightMargin: 10
                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: optRow.modelData.content
                                                            elide: Text.ElideRight
                                                            font.family: Theme.fontFamily
                                                            font.pixelSize: 13
                                                            color: ThemeManager.textColor
                                                        }
                                                        Text {
                                                            text: optRow.modelData.votes
                                                            font.family: Theme.fontFamily
                                                            font.pixelSize: 12
                                                            color: ThemeManager.secondaryTextColor
                                                        }
                                                    }
                                                    MouseArea {
                                                        anchors.fill: parent
                                                        enabled: !pollCard.poll.closed && !optRow.modelData.voted
                                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                        onClicked: ZaloClient.votePoll(pollCard.poll.poll_id, optRow.modelData.option_id)
                                                    }
                                                }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                wrapMode: Text.Wrap
                                                text: !pollCard.poll.loaded
                                                      ? ((pollCard.poll.poll_id || 0) > 0 ? I18n.t("Đang tải kết quả bình chọn…", "Loading poll results…")
                                                                                         : I18n.t("Không đọc được mã bình chọn", "Poll id unavailable"))
                                                      : (pollCard.total + I18n.t(" lượt bình chọn", " votes")
                                                         + (pollCard.poll.closed ? I18n.t(" · Đã khóa", " · Closed") : ""))
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: ThemeManager.secondaryTextColor
                                            }
                                        }

                                        // Text / caption: selectable, links open in the browser.
                                        TextEdit {
                                            id: msgText
                                            visible: !msgRow.hasCard && model.text.length > 0 && !model.isRecalled
                                            Layout.preferredWidth: Math.min(implicitWidth, bubbleRect.innerMax)
                                            Layout.fillWidth: msgRow.isPhoto
                                            readOnly: true
                                            selectByMouse: true
                                            persistentSelection: false
                                            textFormat: TextEdit.RichText
                                            wrapMode: TextEdit.Wrap
                                            text: "<style>a{color:" + Theme.linkColor + ";text-decoration:none}</style>" + model.richText
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                            color: ThemeManager.textColor
                                            selectionColor: Theme.zaloBlueLight
                                            selectedTextColor: ThemeManager.textColor
                                            onLinkActivated: function(link) {
                                                if (link.startsWith("zalos-mention:")) {
                                                    const uid = link.substring(14);
                                                    if (uid !== "-1") ZaloClient.viewProfile(uid);
                                                } else {
                                                    ZaloClient.openLink(link);
                                                }
                                            }
                                            HoverHandler { cursorShape: msgText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor }
                                        }
                                        Text { // recalled
                                            visible: model.isRecalled
                                            text: model.text
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                            font.italic: true
                                            color: ThemeManager.secondaryTextColor
                                        }
                                    }
                                }

                                // Reactions (mobile / Zalo PC): a pill over the bottom-right corner of the content with
                                // the top emojis + total, and a quick 👍 on hover. Click the pill: who reacted with what.
                                // Takes 12 px below the content only when there are reactions; the hover 👍 floats over
                                // the corner without changing the row height (no list jump on hover).
                                Item {
                                    id: reactionRow
                                    readonly property bool canReact: !model.isRecalled && !msgRow.isSystem
                                                                     && !model.msgId.startsWith("local-") && ZaloClient.isLoggedIn
                                    // Right edge of whatever content is shown (bubble, photo, sticker, media run).
                                    readonly property real contentEdge: Math.max(
                                        bubbleRect.visible ? bubbleRect.x + bubbleRect.width : 0,
                                        mediaBox.visible ? mediaBox.x + mediaBox.width : 0,
                                        stickerBox.visible ? stickerBox.x + stickerBox.width : 0,
                                        runFlow.visible ? runFlow.x + runFlow.width : 0)
                                    // Left edge of the same content (own messages: the action bar sits left of it).
                                    readonly property real contentLeft: Math.min(
                                        bubbleRect.visible ? bubbleRect.x : 1e6,
                                        mediaBox.visible ? mediaBox.x : 1e6,
                                        stickerBox.visible ? stickerBox.x : 1e6,
                                        runFlow.visible ? runFlow.x : 1e6)
                                    Layout.alignment: Qt.AlignLeft
                                    Layout.leftMargin: Math.max(0, contentEdge - implicitWidth - 6)
                                    implicitWidth: reactionPills.implicitWidth
                                    implicitHeight: model.reaction.length > 0 ? 12 : 0
                                    z: 2
                                    Row {
                                    id: reactionPills
                                    anchors.right: parent.right
                                    y: -10
                                    spacing: 4
                                    Rectangle {
                                        visible: model.reaction.length > 0
                                        implicitHeight: 22
                                        implicitWidth: reactionText.implicitWidth + 14
                                        radius: 11
                                        color: Theme.reactionBackground
                                        border.color: ThemeManager.borderColor
                                        border.width: 1
                                        Text {
                                            id: reactionText
                                            anchors.centerIn: parent
                                            text: model.reaction
                                            font.pixelSize: 12
                                            color: ThemeManager.textColor
                                        }
                                        MouseArea {
                                            id: reactionPillMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.openReactionDetails(model.msgId, parent)
                                        }
                                        ToolTip.visible: reactionPillMouse.containsMouse
                                        ToolTip.delay: 400
                                        ToolTip.text: I18n.t("Xem ai đã thả cảm xúc", "See who reacted")
                                    }
                                    Rectangle {
                                        visible: reactionRow.canReact && rowHover.hovered
                                        implicitWidth: 22
                                        implicitHeight: 22
                                        radius: 11
                                        color: quickLikeMouse.containsMouse ? ThemeManager.hoverColor : Theme.reactionBackground
                                        border.color: ThemeManager.borderColor
                                        border.width: 1
                                        Text {
                                            anchors.centerIn: parent
                                            text: "👍"
                                            font.pixelSize: 11
                                        }
                                        MouseArea {
                                            id: quickLikeMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: ZaloClient.reactToMessage(model.msgId, "like")
                                        }
                                        ToolTip.visible: quickLikeMouse.containsMouse
                                        ToolTip.delay: 400
                                        ToolTip.text: I18n.t("Thích", "Like")
                                    }
                                    }
                                }

                                // Time (last message of a run) and delivery state (newest own message only).
                                RowLayout {
                                    visible: model.lastInGroup || model.showStatus
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    Layout.topMargin: 4
                                    spacing: 6
                                    Text {
                                        visible: model.lastInGroup
                                        text: model.time
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                    Text {
                                        visible: model.showStatus
                                        text: root.statusLabel(model.status)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: model.status === "failed" ? Theme.badgeRed
                                               : model.status === "seen" ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                                    }
                                }

                            }

                            Item { visible: !msgRow.mine; Layout.fillWidth: true }
                        }
                    }

                    // Hover action bar (.chat-message__actions): reply, forward, more — beside the content.
                    Rectangle {
                        visible: rowHover.hovered && !model.isRecalled && !msgRow.isSystem && !model.msgId.startsWith("local-")
                        y: bubbleRow.y + 2
                        // Right next to the shown content (bubble / photo / sticker), not the column edge.
                        x: msgRow.mine ? contentCol.x + Math.min(reactionRow.contentLeft, contentCol.width) - width - 8
                                       : contentCol.x + Math.max(reactionRow.contentEdge, 0) + 8
                        width: actionRow.implicitWidth + 8
                        height: 28
                        radius: 6
                        color: Theme.reactionBackground
                        border.color: ThemeManager.borderColor
                        border.width: 1
                        Row {
                            id: actionRow
                            anchors.centerIn: parent
                            spacing: 2
                            Repeater {
                                model: [ { icon: "reply", vi: "Trả lời", en: "Reply", act: "reply" },
                                         { icon: "forward", vi: "Chuyển tiếp", en: "Forward", act: "forward" },
                                         { icon: "more_horiz", vi: "Thêm", en: "More", act: "more" } ]
                                Rectangle {
                                    required property var modelData
                                    width: 26
                                    height: 24
                                    radius: 4
                                    color: actMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                    ZIcon { anchors.centerIn: parent; name: modelData.icon; size: 16; color: ThemeManager.secondaryTextColor }
                                    MouseArea {
                                        id: actMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        ToolTip.visible: containsMouse
                                        ToolTip.delay: 400
                                        ToolTip.text: I18n.t(modelData.vi, modelData.en)
                                        onClicked: function(mouse) {
                                            if (modelData.act === "reply") {
                                                ZaloClient.setReplyTarget(model.msgId);
                                                msgInput.forceActiveFocus();
                                            } else if (modelData.act === "forward") {
                                                ZaloClient.openForwardModal(model.msgId, model.text);
                                            } else {
                                                root.openMessageMenu(actMouse, msgRow.menuInfo, mouse.x, mouse.y);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Floating Animated Typing Indicator Pill
            Rectangle {
                id: typingIndicatorPill
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 16
                anchors.bottomMargin: 14
                height: 28
                radius: 14
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1
                z: 10

                readonly property bool isPartnerTyping: ZaloClient.currentConversationStatus === I18n.t("Đang soạn tin...", "Typing...")
                visible: opacity > 0
                opacity: isPartnerTyping ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 6

                    // 3 Bouncing dots
                    Row {
                        spacing: 3
                        Layout.alignment: Qt.AlignVCenter

                        Repeater {
                            model: 3
                            Rectangle {
                                id: dot
                                width: 5
                                height: 5
                                radius: 2.5
                                color: Theme.zaloBlue

                                SequentialAnimation on y {
                                    running: typingIndicatorPill.isPartnerTyping
                                    loops: Animation.Infinite
                                    PauseAnimation { duration: index * 140 }
                                    NumberAnimation { from: 0; to: -4; duration: 240; easing.type: Easing.OutQuad }
                                    NumberAnimation { from: -4; to: 0; duration: 240; easing.type: Easing.InQuad }
                                    PauseAnimation { duration: (2 - index) * 140 }
                                }
                            }
                        }
                    }

                    Text {
                        text: I18n.t("Đang soạn tin...", "Typing...")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.italic: true
                        color: Theme.zaloBlue
                    }
                }
            }

        }

        // Scheduled messages of this conversation (local scheduler, see ZaloClient::scheduleMessage)
        Rectangle {
            Layout.fillWidth: true
            visible: ZaloClient.scheduledMessages.length > 0
            implicitHeight: schedCol.implicitHeight + 12
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            ColumnLayout {
                id: schedCol
                anchors.fill: parent
                anchors.margins: 6
                anchors.leftMargin: 14
                spacing: 2
                Repeater {
                    model: ZaloClient.scheduledMessages
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        ZIcon { name: "reminder"; size: 14; color: modelData.state === "failed" ? Theme.badgeRed : Theme.zaloBlue }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.textColor
                            text: (modelData.state === "failed" ? I18n.t("Gửi lỗi · ", "Failed · ")
                                   : modelData.state === "sending" ? I18n.t("Đang gửi · ", "Sending · ") : "")
                                  + Qt.formatDateTime(new Date(modelData.sendAt), "HH:mm dd/MM") + " — " + modelData.text
                        }
                        Text {
                            visible: modelData.state !== "sending"
                            text: I18n.t("Huỷ", "Cancel")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: Theme.zaloBlue
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.cancelScheduledMessage(modelData.id)
                            }
                        }
                    }
                }
            }
        }

        // Reply banner (quote reply target)
        Rectangle {
            Layout.fillWidth: true
            visible: ZaloClient.replyTarget.id !== undefined
            implicitHeight: 48
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 10
                Rectangle { Layout.preferredWidth: 3; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8; color: Theme.zaloBlue }
                ZIcon { name: "reply"; size: 16; color: Theme.zaloBlue }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.bold: true
                        color: ThemeManager.textColor
                        text: I18n.t("Trả lời ", "Replying to ") + (ZaloClient.replyTarget.senderName || "")
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                        text: ZaloClient.replyTarget.text || ""
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: 12
                    color: cancelReplyMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "close"; size: 14; color: ThemeManager.secondaryTextColor }
                    MouseArea {
                        id: cancelReplyMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ZaloClient.clearReplyTarget()
                    }
                }
            }
        }

        // Message Input & Actions Toolbar (Authentic Zalo PC Responsive Composer)
        Rectangle {
            id: composerBox
            objectName: "composerBox"
            Layout.fillWidth: true
            // Toolbar 32 + divider 1 + format bar (34 when open) + text area + bottom row 32. The text area shows
            // about 3 lines by default and grows with the text up to ~8 (expanded mode: a fixed tall editor).
            readonly property real textAreaHeight: root.isEditorExpanded ? 230
                : Math.max(root.composerMinTextHeight, Math.min(root.composerMaxTextHeight, msgInput.implicitHeight + 16))
            height: 32 + 1 + (root.isFormattingActive ? 34 : 0) + textAreaHeight + 32
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            // Voice recording bar over the composer (ZaloClient.voiceRecorder): timer, cancel, send.
            Rectangle {
                anchors.fill: parent
                z: 50
                visible: ZaloClient.voiceRecorder.recording
                color: ThemeManager.surfaceColor
                MouseArea { anchors.fill: parent } // swallow clicks meant for the composer below
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        color: Theme.badgeRed
                        SequentialAnimation on opacity {
                            running: ZaloClient.voiceRecorder.recording
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.25; duration: 600 }
                            NumberAnimation { to: 1.0; duration: 600 }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        readonly property int secs: Math.floor(ZaloClient.voiceRecorder.durationMs / 1000)
                        text: I18n.t("Đang ghi âm ", "Recording ") + Math.floor(secs / 60) + ":" + ("0" + (secs % 60)).slice(-2)
                              + I18n.t(" · tối đa 5 phút", " · up to 5 minutes")
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                    }
                    ZButton {
                        variant: "secondary"
                        text: I18n.t("Hủy", "Cancel")
                        onClicked: ZaloClient.voiceRecorder.cancel()
                    }
                    ZButton {
                        text: I18n.t("Gửi", "Send")
                        onClicked: ZaloClient.voiceRecorder.stop()
                    }
                }
            }

            Behavior on height {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // 1. Primary Actions Toolbar Row (Height: 32px)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6

                        // Sticker / Emoji Picker
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbStickerMouse.pressed ? ThemeManager.selectedColor :
                                   (tbStickerMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbStickerMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "sticker"
                                size: 18
                                color: emojiPicker.visible ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbStickerMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Biểu tượng cảm xúc (Emoji/Stickers)", "Stickers & Emojis")
                                onClicked: emojiPicker.visible = !emojiPicker.visible
                            }
                        }

                        // Send Photo
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbImageMouse.pressed ? ThemeManager.selectedColor :
                                   (tbImageMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbImageMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "image"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbImageMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Gửi hình ảnh", "Send image")
                                onClicked: imageFileDialog.open()
                            }
                        }

                        // Attach File
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbClipMouse.pressed ? ThemeManager.selectedColor :
                                   (tbClipMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbClipMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "clip"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbClipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Đính kèm tệp tin", "Attach file")
                                onClicked: documentFileDialog.open()
                            }
                        }

                        // Screenshot Capture (Ctrl+Alt+S)
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbSnipMouse.pressed ? ThemeManager.selectedColor :
                                   (tbSnipMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbSnipMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "snip"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbSnipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Chụp ảnh màn hình (Ctrl+Alt+S)", "Capture screenshot (Ctrl+Alt+S)")
                                onClicked: ZaloClient.requestScreenshot() // the capture is sent to this chat
                            }
                        }

                        // Mention (@): groups only
                        Rectangle {
                            visible: ZaloClient.currentConversationIsGroup
                            width: 28
                            height: 28
                            radius: 4
                            color: tbMentionMouse.pressed ? ThemeManager.selectedColor :
                                   (tbMentionMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbMentionMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "mention"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbMentionMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Nhắc tên thành viên (@)", "Mention a member (@)")
                                onClicked: {
                                    msgInput.forceActiveFocus();
                                    msgInput.insert(msgInput.cursorPosition, "@");
                                }
                            }
                        }

                        // Send a contact card (pick a friend) into this conversation
                        Rectangle {
                            visible: ZaloClient.currentConversationId.length > 0
                            width: visible ? 28 : 0
                            height: 28
                            radius: 4
                            color: tbCardMouse.pressed ? ThemeManager.selectedColor :
                                   (tbCardMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            ZIcon {
                                anchors.centerIn: parent
                                name: "contacts"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }
                            MouseArea {
                                id: tbCardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Gửi danh thiếp", "Send a contact card")
                                onClicked: ZaloClient.openSendCardPicker()
                            }
                        }

                        // Group Poll (Visible only in group chats)
                        Rectangle {
                            visible: ZaloClient.currentConversationIsGroup
                            width: visible ? 28 : 0
                            height: 28
                            radius: 4
                            color: tbPollMouse.pressed ? ThemeManager.selectedColor :
                                   (tbPollMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbPollMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "poll"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbPollMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Tạo cuộc bình chọn", "Create poll")
                                onClicked: ZaloClient.openCreatePollModal()
                            }
                        }

                        // Reminder / Nhắc hẹn
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbReminderMouse.pressed ? ThemeManager.selectedColor :
                                   (tbReminderMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbReminderMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "reminder"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbReminderMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Hẹn giờ gửi tin nhắn", "Schedule message")
                                onClicked: {
                                    scheduleContentInput.text = msgInput.text;
                                    reminderPopup.open();
                                }
                            }
                        }

                        // Quick Message / Tin nhắn nhanh
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbQuickMsgMouse.pressed ? ThemeManager.selectedColor :
                                   (tbQuickMsgMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbQuickMsgMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "quick_message"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbQuickMsgMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Tin nhắn nhanh", "Quick message")
                                onClicked: quickMsgPopup.open()
                            }
                        }

                        // Voice message / Ghi âm (Ctrl+Shift+V): records with the default microphone.
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbVoiceMouse.pressed ? ThemeManager.selectedColor :
                                   (tbVoiceMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbVoiceMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }
                            opacity: ZaloClient.voiceRecorder.available ? 1.0 : 0.45

                            ZIcon {
                                anchors.centerIn: parent
                                name: "mic"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbVoiceMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: ZaloClient.voiceRecorder.available
                                              ? I18n.t("Ghi âm tin nhắn thoại (Ctrl+Shift+V)", "Record a voice message (Ctrl+Shift+V)")
                                              : (ZaloClient.voiceRecorder.unavailableReason === "no_microphone"
                                                 ? I18n.t("Không tìm thấy micro", "No microphone found")
                                                 : I18n.t("Thiếu bộ mã hóa AAC nên chưa ghi âm được", "No AAC encoder: recording unavailable"))
                                onClicked: ZaloClient.voiceRecorder.start()
                            }
                        }

                        // To-do / Task assignment
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbTodoMouse.pressed ? ThemeManager.selectedColor :
                                   (tbTodoMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbTodoMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "todo"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbTodoMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Giao việc (To-Do)", "Assign task (To-Do)")
                                onClicked: ZaloClient.setCurrentNavigationTab("todo")
                            }
                        }

                        // Text Formatting Toggle (In đậm, nghiêng, màu, cỡ chữ)
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: root.isFormattingActive ? ThemeManager.accentTonalColor :
                                   (tbFormatMouse.pressed ? ThemeManager.selectedColor :
                                   (tbFormatMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor))
                            scale: tbFormatMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "format_text"
                                size: 18
                                color: root.isFormattingActive ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbFormatMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Định dạng tin nhắn", "Format message")
                                onClicked: root.isFormattingActive = !root.isFormattingActive
                            }
                        }

                        Item { Layout.fillWidth: true }

                        // Expand / Collapse Editor Height Button
                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: tbExpandMouse.pressed ? ThemeManager.selectedColor :
                                   (tbExpandMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: tbExpandMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: root.isEditorExpanded ? "collapse" : "expand"
                                size: 18
                                color: ThemeManager.secondaryTextColor
                            }

                            MouseArea {
                                id: tbExpandMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: root.isEditorExpanded ? I18n.t("Thu gọn ô soạn thảo", "Collapse editor") : I18n.t("Mở rộng ô soạn thảo", "Expand editor")
                                onClicked: root.isEditorExpanded = !root.isEditorExpanded
                            }
                        }
                    }
                }

                // 1.5 Secondary Rich Text Format (RTF) Toolbar Row
                Rectangle {
                    id: rtfToolbar
                    objectName: "formatToolbar"
                    Layout.fillWidth: true
                    // Inside a ColumnLayout: `height` is ignored (implicitHeight 0 collapsed the toolbar).
                    Layout.preferredHeight: root.isFormattingActive ? 34 : 0
                    visible: root.isFormattingActive
                    clip: true
                    color: ThemeManager.cardBackgroundColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Behavior on Layout.preferredHeight {
                        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 4

                        // Zalo text styles (ZaloClient.composer): applied to the selection, or to the word at the caret.
                        Repeater {
                            model: [
                                { st: "b", icon: "format_bold", vi: "In đậm (Ctrl+B)", en: "Bold (Ctrl+B)" },
                                { st: "i", icon: "format_italic", vi: "In nghiêng (Ctrl+I)", en: "Italic (Ctrl+I)" },
                                { st: "u", icon: "format_underlined", vi: "Gạch chân (Ctrl+U)", en: "Underline (Ctrl+U)" },
                                { st: "s", icon: "format_strikethrough", vi: "Gạch ngang (Ctrl+Shift+X)", en: "Strikethrough (Ctrl+Shift+X)" },
                                { st: "size", icon: "format_size", vi: "Cỡ chữ", en: "Text size" },
                                { st: "color", icon: "palette", vi: "Màu chữ", en: "Text colour" },
                                { st: "lst_1", icon: "format_list_bulleted", vi: "Danh sách gạch đầu dòng", en: "Bulleted list" },
                                { st: "lst_2", icon: "format_list_numbered", vi: "Danh sách đánh số", en: "Numbered list" },
                                { st: "undo", icon: "undo", vi: "Hoàn tác (Ctrl+Z)", en: "Undo (Ctrl+Z)" },
                                { st: "redo", icon: "redo", vi: "Làm lại (Ctrl+Y)", en: "Redo (Ctrl+Y)" },
                                { st: "clear", icon: "format_clear", vi: "Xóa định dạng", en: "Clear formatting" }
                            ]
                            delegate: Rectangle {
                                id: fmtButton
                                required property var modelData
                                readonly property var state: ZaloClient.composer.active
                                readonly property bool on: fmtButton.modelData.st === "lst_1" || fmtButton.modelData.st === "lst_2"
                                                           ? fmtButton.state.list === fmtButton.modelData.st
                                                           : fmtButton.modelData.st === "size" ? (fmtButton.state.size || "").length > 0
                                                           : fmtButton.modelData.st === "color" ? (fmtButton.state.color || "").length > 0
                                                           : fmtButton.state[fmtButton.modelData.st] === true
                                Layout.preferredWidth: 26
                                Layout.preferredHeight: 26
                                radius: 4
                                color: fmtButton.on ? ThemeManager.selectedColor
                                       : fmtMouse.pressed ? ThemeManager.selectedColor
                                       : fmtMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                ZIcon {
                                    anchors.centerIn: parent
                                    name: fmtButton.modelData.icon
                                    size: 16
                                    color: fmtButton.modelData.st === "color" && (fmtButton.state.color || "").length > 2
                                           ? "#" + fmtButton.state.color.substring(2)
                                           : (fmtButton.on ? ThemeManager.textColor : ThemeManager.secondaryTextColor)
                                }
                                MouseArea {
                                    id: fmtMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    ToolTip.visible: containsMouse
                                    ToolTip.delay: 300
                                    ToolTip.text: I18n.t(fmtButton.modelData.vi, fmtButton.modelData.en)
                                    onClicked: {
                                        const st = fmtButton.modelData.st;
                                        if (st === "size") { fontSizePopup.parent = fmtButton; fontSizePopup.x = 0; fontSizePopup.y = -fontSizePopup.implicitHeight - 6; fontSizePopup.open(); }
                                        else if (st === "color") { textColorPopup.parent = fmtButton; textColorPopup.x = 0; textColorPopup.y = -textColorPopup.implicitHeight - 6; textColorPopup.open(); }
                                        else if (st === "lst_1" || st === "lst_2") root.toggleList(st);
                                        else if (st === "undo") { msgInput.undo(); msgInput.forceActiveFocus(); }
                                        else if (st === "redo") { msgInput.redo(); msgInput.forceActiveFocus(); }
                                        else if (st === "clear") root.clearFormatting();
                                        else root.applyStyle(st);
                                    }
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // Divider line inside composer
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: ThemeManager.borderColor
                    opacity: 0.5
                }

                // 2. Middle Text Input Area (Multi-line TextArea)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Flickable {
                        id: flickArea
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 6
                        anchors.bottomMargin: 4
                        contentWidth: width
                        contentHeight: msgInput.implicitHeight
                        clip: true

                        TextArea.flickable: TextArea {
                            id: msgInput
                            wrapMode: TextEdit.Wrap
                            color: ThemeManager.textColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            // Toolbar clicks and the size/colour pickers must not drop the selection they format.
                            persistentSelection: true
                            Component.onCompleted: ZaloClient.composer.attach(msgInput.textDocument)
                            // Past the maximum height the editor scrolls: keep the caret in view.
                            onCursorRectangleChanged: {
                                const r = msgInput.cursorRectangle;
                                if (r.y < flickArea.contentY) flickArea.contentY = r.y;
                                else if (r.y + r.height > flickArea.contentY + flickArea.height)
                                    flickArea.contentY = r.y + r.height - flickArea.height;
                            }
                            onSelectionStartChanged: ZaloClient.composer.refresh(msgInput.selectionStart, msgInput.selectionEnd)
                            onSelectionEndChanged: ZaloClient.composer.refresh(msgInput.selectionStart, msgInput.selectionEnd)
                            padding: 0
                            background: null
                            placeholderText: I18n.t("Nhập @, tin nhắn tới ", "Type @, message to ") + ZaloClient.currentConversationName
                            placeholderTextColor: ThemeManager.secondaryTextColor

                            Keys.onPressed: function(event) {
                                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_B) {
                                    event.accepted = true;
                                    root.applyStyle("b");
                                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_I) {
                                    event.accepted = true;
                                    root.applyStyle("i");
                                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_U) {
                                    event.accepted = true;
                                    root.applyStyle("u");
                                } else if ((event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier) && event.key === Qt.Key_X) {
                                    event.accepted = true;
                                    root.applyStyle("s");
                                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
                                    event.accepted = true;
                                    msgInput.undo();
                                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Y) {
                                    event.accepted = true;
                                    msgInput.redo();
                                }
                            }

                            onTextChanged: {
                                root.updateSuggestions();
                                if (!root.loadingDraft) draftSaveTimer.restart();
                            }
                            onCursorPositionChanged: {
                                ZaloClient.composer.refresh(msgInput.selectionStart, msgInput.selectionEnd);
                                if (root.suggestMode !== "") root.updateSuggestions();
                            }
                            Keys.onUpPressed: function(event) {
                                if (root.suggestItems.length === 0) { event.accepted = false; return; }
                                root.suggestIndex = (root.suggestIndex + root.suggestItems.length - 1) % root.suggestItems.length;
                            }
                            Keys.onDownPressed: function(event) {
                                if (root.suggestItems.length === 0) { event.accepted = false; return; }
                                root.suggestIndex = (root.suggestIndex + 1) % root.suggestItems.length;
                            }
                            Keys.onTabPressed: function(event) {
                                if (root.suggestItems.length === 0) { event.accepted = false; return; }
                                root.acceptSuggestion(root.suggestIndex);
                            }

                            Keys.onReturnPressed: function(event) {
                                // IBus/Fcitx (Telex, VNI): Return first commits the pre-edit word.
                                if (msgInput.inputMethodComposing) {
                                    event.accepted = false;
                                    return;
                                }
                                if (root.suggestItems.length > 0 && !(event.modifiers & Qt.ShiftModifier)) {
                                    event.accepted = true;
                                    root.acceptSuggestion(root.suggestIndex);
                                    return;
                                }
                                if (event.modifiers & Qt.ShiftModifier) {
                                    msgInput.insert(msgInput.cursorPosition, "\n");
                                } else {
                                    event.accepted = true;
                                    root.sendComposer();
                                }
                            }
                            Keys.onEscapePressed: function(event) {
                                if (root.suggestItems.length > 0) {
                                    event.accepted = true;
                                    root.closeSuggestions();
                                    return;
                                }
                                event.accepted = root.handleEscape();
                            }
                        }
                    }

                    // Suggestion list above the composer: @members or /quick messages.
                    Popup {
                        id: suggestPopup
                        visible: root.suggestItems.length > 0 && msgInput.activeFocus
                        x: 12
                        y: -height - 8
                        width: Math.min(360, parent.width - 24)
                        height: Math.min(suggestList.contentHeight, 8 * 44) + 8
                        padding: 4
                        focus: false
                        closePolicy: Popup.NoAutoClose
                        background: Rectangle {
                            color: ThemeManager.surfaceColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                            radius: 8
                        }
                        ListView {
                            id: suggestList
                            anchors.fill: parent
                            clip: true
                            model: root.suggestItems
                            currentIndex: root.suggestIndex
                            delegate: Rectangle {
                                id: sugRow
                                required property var modelData
                                required property int index
                                width: suggestList.width
                                height: 44
                                radius: 6
                                color: sugRow.index === root.suggestIndex || sugMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8
                                    ZAvatar {
                                        visible: root.suggestMode === "mention"
                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 28
                                        size: 28
                                        avatarUrl: sugRow.modelData.avatar || ""
                                        name: sugRow.modelData.name || ""
                                        showStatusDot: false
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        Text {
                                            Layout.fillWidth: true
                                            text: root.suggestMode === "mention" ? "@" + sugRow.modelData.name : "/" + sugRow.modelData.keyword
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 13
                                            font.bold: root.suggestMode === "quick"
                                            color: root.suggestMode === "quick" ? Theme.zaloBlue : ThemeManager.textColor
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            visible: text.length > 0
                                            Layout.fillWidth: true
                                            text: root.suggestMode === "mention" ? (sugRow.modelData.hint || "") : sugRow.modelData.text
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 11
                                            color: ThemeManager.secondaryTextColor
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                MouseArea {
                                    id: sugMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.acceptSuggestion(sugRow.index)
                                }
                            }
                        }
                    }
                }

                // 3. Bottom Control Row (Hint & Send/Like Buttons)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32 // inside a ColumnLayout: `height` was ignored and the row covered the text
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8

                        Text {
                            text: I18n.t("Nhấn Enter để gửi, Shift + Enter để xuống dòng", "Press Enter to send, Shift + Enter for new line")
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                            visible: msgInput.activeFocus || msgInput.text.length > 0
                        }

                        Item { Layout.fillWidth: true }

                        // Quick Like 👍 Button (visible when input is empty)
                        Rectangle {
                            visible: msgInput.text.trim() === ""
                            width: 30
                            height: 30
                            radius: 15
                            color: likeBtnMouse.pressed ? ThemeManager.selectedColor :
                                   (likeBtnMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                            scale: likeBtnMouse.pressed ? 0.88 : (likeBtnMouse.containsMouse ? 1.1 : 1.0)
                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "thumbs_up"
                                size: 18
                                color: Theme.zaloBlue
                            }

                            MouseArea {
                                id: likeBtnMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Gửi nhanh biểu tượng thích", "Send quick thumbs up")
                                onClicked: ZaloClient.sendMessage("👍")
                            }
                        }

                        // Send [ Gửi ] Button (visible when input has text)
                        Rectangle {
                            visible: msgInput.text.trim() !== ""
                            width: 60
                            height: 28
                            radius: 4
                            color: sendBtnMouse.pressed ? Theme.zaloBlueHover :
                                   (sendBtnMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue)
                            scale: sendBtnMouse.pressed ? 0.95 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            Text {
                                anchors.centerIn: parent
                                text: I18n.t("Gửi", "Send")
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                font.bold: true
                                color: "#FFFFFF"
                            }

                            MouseArea {
                                id: sendBtnMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                onClicked: root.sendComposer()
                            }
                        }
                    }
                }
            }
        }
    }

    // Collapsible Conversation Info Panel (280px width)
    ConversationInfoPanel {
        id: conversationInfoPanel
        Layout.fillHeight: true
    }
    }

    // Floating Interactive Emoji Picker Popover
    EmojiPickerPopup {
        id: emojiPicker
        visible: false
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.bottomMargin: composerBox.height + 6
        z: 99
        onEmojiSelected: function(emoji) {
            msgInput.insert(msgInput.cursorPosition, emoji);
            msgInput.forceActiveFocus();
            emojiPicker.visible = false;
        }
        onStickerSelected: function(sticker) {
            ZaloClient.stickers.send(sticker); // to the open conversation; kept in "Gần đây"
            emojiPicker.visible = false;
            msgInput.forceActiveFocus();
        }
    }

    // All pinned messages of the conversation (Zalo "Danh sách ghim").
    Popup {
        id: pinListPopup
        focus: true
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 420)
        height: Math.min(root.height - 60, 460)
        modal: true
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: ThemeManager.surfaceColor; radius: 12; border.color: ThemeManager.borderColor }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                ZIcon { name: "pin"; size: 18; color: Theme.zaloBlue }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Danh sách ghim", "Pinned messages") + " (" + ZaloClient.pins.pins.length + ")"
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Rectangle {
                    width: 26
                    height: 26
                    radius: 13
                    color: pinListClose.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "close"; size: 14; color: ThemeManager.iconColor }
                    MouseArea { id: pinListClose; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pinListPopup.close() }
                }
            }
            ListView {
                id: pinList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: ZaloClient.pins.pins
                delegate: Rectangle {
                    id: pinRow
                    required property var modelData
                    required property int index
                    width: pinList.width
                    height: 58
                    radius: 8
                    color: pinRowHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    HoverHandler { id: pinRowHover }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.jumpToMessage(pinRow.modelData.msgId || "", pinRow.modelData.cliMsgId || "")) pinListPopup.close();
                        }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: pinRow.modelData.title || I18n.t("[Tin nhắn]", "[Message]")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                color: ThemeManager.textColor
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: I18n.t("Tin nhắn của ", "Message from ") + (pinRow.modelData.senderName || "")
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: ThemeManager.secondaryTextColor
                            }
                        }
                        ZButton {
                            visible: pinRow.index > 0
                            variant: "secondary"
                            text: I18n.t("Đưa lên đầu", "Move to top")
                            onClicked: ZaloClient.pins.moveToTop(pinRow.modelData.id)
                        }
                        ZButton {
                            variant: "secondary"
                            text: I18n.t("Bỏ ghim", "Unpin")
                            onClicked: ZaloClient.pins.unpin(pinRow.modelData.id)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: reactionPopup
        focus: true // Esc closes it
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 360)
        height: Math.min(root.height - 60, 420)
        modal: true
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: ThemeManager.surfaceColor; radius: 12; border.color: ThemeManager.borderColor }
        Connections {
            target: ZaloClient.messageModel
            enabled: reactionPopup.opened
            function onDataChanged() { root.reactionInfo = ZaloClient.reactionDetails(root.reactionMsgId); }
        }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Cảm xúc", "Reactions")
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Rectangle {
                    width: 26
                    height: 26
                    radius: 13
                    color: reactionCloseMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "close"; size: 14; color: ThemeManager.iconColor }
                    MouseArea { id: reactionCloseMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: reactionPopup.close() }
                }
            }
            // Tabs: Tất cả N, then each emoji with its count.
            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: root.reactionInfo.known
                Repeater {
                    model: [{ emoji: "", count: root.reactionInfo.total }].concat(root.reactionInfo.groups)
                    Rectangle {
                        required property var modelData
                        readonly property bool active: root.reactionTab === modelData.emoji
                        height: 28
                        width: tabText.implicitWidth + 20
                        radius: 14
                        color: active ? ThemeManager.selectedColor : (tabMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.inputFillColor)
                        Text {
                            id: tabText
                            anchors.centerIn: parent
                            text: (parent.modelData.emoji.length > 0 ? parent.modelData.emoji : I18n.t("Tất cả", "All")) + " " + parent.modelData.count
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.bold: parent.active
                            color: ThemeManager.textColor
                        }
                        MouseArea { id: tabMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.reactionTab = parent.modelData.emoji }
                    }
                }
            }
            Text {
                visible: !root.reactionInfo.known
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("Chưa có danh sách người thả cảm xúc cho tin nhắn này (chỉ có với cảm xúc nhận được khi Zalos đang chạy).",
                             "Who reacted is not known for this message (only reactions received while Zalos is running).")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }
            ListView {
                id: reactionList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                // A tab lists everyone who used that emoji (people react many times, with several icons).
                model: root.reactionInfo.people.filter(p => root.reactionTab.length === 0 || (p.perEmoji[root.reactionTab] || 0) > 0)
                delegate: Rectangle {
                    id: personRow
                    required property var modelData
                    width: reactionList.width
                    height: 46
                    radius: 8
                    color: personHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    HoverHandler { id: personHover }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 10
                        ZAvatar { size: 34; avatarUrl: personRow.modelData.avatar; name: personRow.modelData.name }
                        Text {
                            Layout.fillWidth: true
                            text: personRow.modelData.name + (personRow.modelData.isMe ? I18n.t(" (Bạn)", " (You)") : "")
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            MouseArea {
                                anchors.fill: parent
                                enabled: !personRow.modelData.isMe
                                cursorShape: personRow.modelData.isMe ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: { reactionPopup.close(); ZaloClient.viewProfile(personRow.modelData.uid); }
                            }
                        }
                        ZButton {
                            visible: personRow.modelData.isMe
                            variant: "secondary"
                            text: I18n.t("Gỡ", "Remove")
                            onClicked: ZaloClient.removeMyReaction(root.reactionMsgId)
                        }
                        // "👍❤️ 9" in "Tất cả", "❤️ 4" in an emoji tab (like the Zalo apps).
                        Text {
                            text: root.reactionTab.length > 0
                                  ? root.reactionTab + " " + (personRow.modelData.perEmoji[root.reactionTab] || 0)
                                  : personRow.modelData.emojis + (personRow.modelData.count > 1 ? " " + personRow.modelData.count : "")
                            font.pixelSize: 15
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }
            }
        }
    }

    // Text size / colour pickers of the format toolbar: non-modal, opened above their button, never taking focus
    // (the composer keeps its selection, which the chosen style applies to).
    component FormatPopup: Popup {
        padding: 6
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            radius: 8
        }
    }

    FormatPopup {
        id: fontSizePopup
        width: 170
        contentItem: ColumnLayout {
            spacing: 2
            Repeater {
                model: [
                    { label: I18n.t("Chữ nhỏ", "Small text"), st: "f_13", px: 12 },
                    { label: I18n.t("Chữ thường", "Normal text"), st: "f_", px: 14 },
                    { label: I18n.t("Chữ lớn", "Large text"), st: "f_18", px: 18 }
                ]
                ZMenuRow {
                    required property var modelData
                    readonly property bool current: (ZaloClient.composer.active.size || "f_") === modelData.st
                    label: modelData.label + (current ? "  ✓" : "")
                    onTriggered: { root.applyStyle(modelData.st); fontSizePopup.close(); }
                }
            }
        }
    }

    FormatPopup {
        id: textColorPopup
        width: 196
        contentItem: RowLayout {
            spacing: 8
            Repeater {
                // Zalo's four text colours (zca-js TextStyle Red/Orange/Yellow/Green); "c_" = default colour.
                model: [ { st: "c_", color: ThemeManager.textColor, tip: I18n.t("Mặc định", "Default") },
                         { st: "c_db342e", color: "#db342e", tip: I18n.t("Đỏ", "Red") },
                         { st: "c_f27806", color: "#f27806", tip: I18n.t("Cam", "Orange") },
                         { st: "c_f7b503", color: "#f7b503", tip: I18n.t("Vàng", "Yellow") },
                         { st: "c_15a85f", color: "#15a85f", tip: I18n.t("Xanh lá", "Green") } ]
                Rectangle {
                    id: swatch
                    required property var modelData
                    readonly property bool current: (ZaloClient.composer.active.color || "c_") === swatch.modelData.st
                    width: 28
                    height: 28
                    radius: 14
                    color: swatch.modelData.color
                    border.color: swatch.current ? Theme.zaloBlue : ThemeManager.borderColor
                    border.width: swatch.current ? 2 : 1
                    MouseArea {
                        id: swatchMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.applyStyle(swatch.modelData.st); textColorPopup.close(); }
                    }
                    ToolTip.visible: swatchMouse.containsMouse
                    ToolTip.text: swatch.modelData.tip
                }
            }
        }
    }

    // Quick Message Selection Popup (Zalo PC Quick Responses)
    Popup {
        id: quickMsgPopup
        width: 320
        padding: 8
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        anchors.centerIn: parent
        background: Rectangle {
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            radius: 8
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            Text {
                text: I18n.t("Tin nhắn nhanh", "Quick messages")
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.bold: true
                color: ThemeManager.textColor
                Layout.leftMargin: 4
                Layout.topMargin: 2
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: ThemeManager.borderColor
            }

            Text { // honest empty state: the list comes from the account (/api/quickmessage/list)
                visible: ZaloClient.quickMessages.length === 0
                Layout.fillWidth: true
                Layout.margins: 4
                wrapMode: Text.Wrap
                text: I18n.t("Chưa có tin nhắn nhanh. Tạo tin nhắn nhanh trong Zalo trên điện thoại, rồi gõ \"/\" + từ khóa trong ô soạn tin.",
                             "No quick messages yet. Create them in Zalo on your phone, then type \"/\" + keyword in the composer.")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }

            Repeater {
                model: ZaloClient.quickMessages
                delegate: Rectangle {
                    id: qmRow
                    required property var modelData
                    Layout.fillWidth: true
                    height: 40
                    radius: 4
                    color: qmMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: "/" + qmRow.modelData.keyword
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.bold: true
                            color: Theme.zaloBlue
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qmRow.modelData.text + (qmRow.modelData.hasMedia ? I18n.t("  (kèm ảnh: chưa hỗ trợ gửi)", "  (with photo: not sent yet)") : "")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: qmMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            msgInput.text = qmRow.modelData.text;
                            msgInput.cursorPosition = msgInput.text.length;
                            quickMsgPopup.close();
                            msgInput.forceActiveFocus();
                        }
                    }
                }
            }
        }
    }

    // Schedule-send popup. Delivery is local: Zalos sends the message at the chosen time
    // while it is running and signed in (Zalo has no server-side scheduled send for Web).
    Popup {
        id: reminderPopup
        width: 320
        padding: 12
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        anchors.centerIn: parent
        background: Rectangle {
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
            radius: 8
        }

        function nextAt(hour: int): real {
            var d = new Date();
            d.setHours(hour, 0, 0, 0);
            if (d.getTime() <= Date.now() + 60000) d.setDate(d.getDate() + 1);
            return d.getTime();
        }

        function schedule(atMs: real): void {
            if (ZaloClient.scheduleMessage(scheduleContentInput.text, atMs)) {
                if (scheduleContentInput.text.trim() === msgInput.text.trim()) msgInput.text = "";
                scheduleContentInput.text = "";
                reminderPopup.close();
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                text: I18n.t("Hẹn giờ gửi tin nhắn", "Schedule message")
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.bold: true
                color: ThemeManager.textColor
            }

            TextField {
                id: scheduleContentInput
                Layout.fillWidth: true
                placeholderText: I18n.t("Nội dung tin nhắn...", "Message...")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.textColor
                placeholderTextColor: ThemeManager.secondaryTextColor
                background: Rectangle {
                    color: ThemeManager.inputFillColor
                    border.color: scheduleContentInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                    border.width: 1
                    radius: 4
                }
            }

            Text {
                text: I18n.t("Gửi lúc:", "Send at:")
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: ThemeManager.secondaryTextColor
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 6
                rowSpacing: 6

                Repeater {
                    model: [
                        { vi: "Sau 15 phút", en: "In 15 minutes", offset: 15 * 60000, hour: -1 },
                        { vi: "Sau 1 giờ", en: "In 1 hour", offset: 3600000, hour: -1 },
                        { vi: "18:00", en: "6:00 PM", offset: 0, hour: 18 },
                        { vi: "08:00 sáng", en: "8:00 AM", offset: 0, hour: 8 }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 28
                        radius: 4
                        enabled: scheduleContentInput.text.trim().length > 0
                        opacity: enabled ? 1 : 0.5
                        color: schedMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.inputFillColor
                        border.color: ThemeManager.borderColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: I18n.t(modelData.vi, modelData.en)
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.textColor
                        }

                        MouseArea {
                            id: schedMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: reminderPopup.schedule(modelData.hour >= 0 ? reminderPopup.nextAt(modelData.hour)
                                                                                  : Date.now() + modelData.offset)
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: I18n.t("Tin nhắn được gửi từ máy này, nên Zalos cần đang mở và đăng nhập vào thời điểm đó.",
                             "The message is sent from this computer, so Zalos must be running and signed in at that time.")
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: ThemeManager.secondaryTextColor
            }
        }
    }

    // Message context menu (right click on a bubble)
    Popup {
        id: messageMenu
        width: 230
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: ThemeManager.surfaceColor
            radius: 8
            border.color: ThemeManager.borderColor
        }

        readonly property string msgId: root.menuMessage.id || ""
        readonly property bool isServerMessage: msgId.length > 0 && !msgId.startsWith("local-")

        contentItem: ColumnLayout {
            spacing: 2

            // Reactions (same set and codes as Zalo, ZaloClient.reactionList()): 6 visible, the arrows (or the
            // wheel) scroll horizontally to the others.
            RowLayout {
                visible: messageMenu.isServerMessage
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                spacing: 0

                Rectangle {
                    id: reactPrev
                    readonly property bool usable: !reactList.atXBeginning
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 32
                    radius: 10
                    color: reactPrevMouse.containsMouse && usable ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    opacity: usable ? 1.0 : 0.3
                    ZIcon { anchors.centerIn: parent; name: "chevron_left"; size: 16; color: ThemeManager.secondaryTextColor }
                    MouseArea {
                        id: reactPrevMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: reactPrev.usable
                        cursorShape: Qt.PointingHandCursor
                        onClicked: reactList.scrollBy(-3)
                    }
                }

                ListView {
                    id: reactList
                    Layout.preferredWidth: 6 * 34
                    Layout.preferredHeight: 32
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    model: ZaloClient.reactionList()
                    function scrollBy(items: int): void {
                        const target = Math.max(0, Math.min(reactList.contentWidth - reactList.width,
                                                            reactList.contentX + items * 34));
                        reactScroll.to = target;
                        reactScroll.restart();
                    }
                    NumberAnimation { id: reactScroll; target: reactList; property: "contentX"; duration: 180; easing.type: Easing.OutCubic }
                    delegate: Rectangle {
                        id: reactItem
                        required property var modelData
                        width: 32
                        height: 32
                        radius: 16
                        color: reactMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                        Text { anchors.centerIn: parent; text: reactItem.modelData.emoji; font.pixelSize: 17 }
                        MouseArea {
                            id: reactMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { ZaloClient.reactToMessage(messageMenu.msgId, reactItem.modelData.key); messageMenu.close(); }
                        }
                    }
                }

                Rectangle {
                    id: reactNext
                    readonly property bool usable: !reactList.atXEnd
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 32
                    radius: 10
                    color: reactNextMouse.containsMouse && usable ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    opacity: usable ? 1.0 : 0.3
                    ZIcon { anchors.centerIn: parent; name: "chevron_right"; size: 16; color: ThemeManager.secondaryTextColor }
                    MouseArea {
                        id: reactNextMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: reactNext.usable
                        cursorShape: Qt.PointingHandCursor
                        onClicked: reactList.scrollBy(3)
                    }
                }
            }

            // Photos: view / copy / save (Zalo media actions).
            ZMenuRow {
                visible: root.menuMessage.messageType === "image" && (root.menuMessage.url || "").length > 0
                icon: "zoom_in"
                label: I18n.t("Xem ảnh", "View picture")
                onTriggered: { root.openViewer(messageMenu.msgId); messageMenu.close(); }
            }
            ZMenuRow {
                visible: root.menuMessage.messageType === "image" && (root.menuMessage.url || "").length > 0
                icon: "copy"
                label: I18n.t("Sao chép ảnh", "Copy picture")
                onTriggered: { ZaloClient.copyImage(root.menuMessage.url); messageMenu.close(); }
            }
            ZMenuRow {
                visible: root.menuMessage.messageType === "image" && (root.menuMessage.url || "").length > 0
                icon: "download"
                label: I18n.t("Lưu ảnh về máy", "Save picture")
                onTriggered: { saveImageDialog.open(); messageMenu.close(); }
            }
            ZMenuRow {
                visible: messageMenu.isServerMessage
                icon: "reply"
                label: I18n.t("Trả lời", "Reply")
                onTriggered: { ZaloClient.setReplyTarget(messageMenu.msgId); messageMenu.close(); msgInput.forceActiveFocus(); }
            }
            ZMenuRow {
                visible: (root.menuMessage.text || "").length > 0
                icon: "copy"
                label: I18n.t("Sao chép", "Copy")
                onTriggered: { ZaloClient.copyText(root.menuMessage.text); messageMenu.close(); }
            }
            ZMenuRow {
                visible: messageMenu.isServerMessage
                icon: "forward"
                label: I18n.t("Chuyển tiếp", "Forward")
                onTriggered: { ZaloClient.openForwardModal(messageMenu.msgId, root.menuMessage.text || ""); messageMenu.close(); }
            }
            ZMenuRow {
                id: pinMenuRow
                readonly property bool pinned: ZaloClient.pins.pins.length >= 0 && ZaloClient.pins.isPinned(messageMenu.msgId)
                visible: messageMenu.isServerMessage
                icon: "pin"
                label: pinMenuRow.pinned ? I18n.t("Bỏ ghim tin nhắn", "Unpin message") : I18n.t("Ghim tin nhắn", "Pin message")
                onTriggered: {
                    if (pinMenuRow.pinned) ZaloClient.pins.unpinMessage(messageMenu.msgId);
                    else ZaloClient.pins.pinMessage(messageMenu.msgId);
                    messageMenu.close();
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: 3
                Layout.bottomMargin: 3
                color: ThemeManager.borderColor
                visible: messageMenu.isServerMessage
            }
            ZMenuRow {
                visible: messageMenu.isServerMessage && root.menuMessage.isMine === true
                         && ZaloClient.canRecall(messageMenu.msgId)
                icon: "undo"
                danger: true
                label: I18n.t("Thu hồi", "Recall")
                onTriggered: { ZaloClient.recallMessage(messageMenu.msgId); messageMenu.close(); }
            }
            ZMenuRow {
                visible: messageMenu.isServerMessage
                icon: "trash"
                danger: true
                label: I18n.t("Xoá chỉ ở phía tôi", "Delete for me")
                onTriggered: { ZaloClient.deleteMessageForMe(messageMenu.msgId); messageMenu.close(); }
            }
        }
    }

    FileDialog {
        id: saveImageDialog
        title: I18n.t("Lưu ảnh", "Save picture")
        fileMode: FileDialog.SaveFile
        currentFile: "file:///" + ZaloClient.suggestedMediaFileName(root.menuMessage.url || "")
        onAccepted: ZaloClient.saveMediaTo(root.menuMessage.url, selectedFile.toString())
    }

    // Photo File Selection Dialog
    FileDialog {
        id: imageFileDialog
        title: I18n.t("Chọn hình ảnh để gửi", "Select photo to send")
        nameFilters: ["Hình ảnh (*.png *.jpg *.jpeg *.gif *.webp *.bmp)", "Tất cả các tệp (*.*)"]
        onAccepted: {
            ZaloClient.sendFile(selectedFile.toString());
        }
    }

    // Attachment Document File Selection Dialog
    FileDialog {
        id: documentFileDialog
        title: I18n.t("Chọn tệp tin đính kèm", "Select document to attach")
        nameFilters: ["Tất cả các tệp (*.*)"]
        onAccepted: {
            ZaloClient.sendFile(selectedFile.toString());
        }
    }
}
