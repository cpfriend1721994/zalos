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
    onIsFormattingActiveChanged: {
        if (messageList.stickToBottom || messageList.distanceToEnd < 48) {
            Qt.callLater(() => messageList.positionViewAtEnd());
        }
    }
    onIsEditorExpandedChanged: {
        if (messageList.stickToBottom || messageList.distanceToEnd < 48) {
            Qt.callLater(() => messageList.positionViewAtEnd());
        }
    }
    readonly property bool infoOverlay: ZaloClient.showConversationInfo && width < 620
    readonly property int composerLineHeight: Math.ceil(composerMetrics.height)
    readonly property real composerMinTextHeight: composerLineHeight * 3 + 16
    readonly property real composerMaxTextHeight: composerLineHeight * 8 + 16
    FontMetrics { id: composerMetrics; font.family: Theme.fontFamily; font.pixelSize: 15 }
    property list<string> searchHits: []
    property int searchCursor: -1
    readonly property string searchFocusId: root.searchCursor >= 0 && root.searchCursor < root.searchHits.length
                                            ? root.searchHits[root.searchCursor] : ""

    function showSearchHit(): void {
        const id = root.searchFocusId;
        if (id.length === 0 || !ZaloClient.revealMessage(id)) return;
        messageList.stickToBottom = false;
        messageList.positionViewAtIndex(ZaloClient.messageModel.rowOfMessage(id), ListView.Center);
    }
    property var menuMessage: ({})

    function runSearch(): void {
        root.searchHits = ZaloClient.searchConversationMessages(inChatSearchInput.text);
        root.searchCursor = root.searchHits.length > 0 ? root.searchHits.length - 1 : -1;
        root.showSearchHit();
    }

    function stepSearch(delta: int): void {
        if (root.searchHits.length === 0) return;
        root.searchCursor = (root.searchCursor + delta + root.searchHits.length) % root.searchHits.length;
        root.showSearchHit();
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

    function handleEscape(): bool {
        if (root.showInChatSearch) { root.closeSearch(); return true; }
        if (ZaloClient.replyTarget.id !== undefined) { ZaloClient.clearReplyTarget(); return true; }
        return false;
    }

    property string reactionMsgId: ""
    property var reactionInfo: ({ total: 0, known: false, groups: [], people: [] })
    property string reactionTab: ""
    function openReactionDetails(msgId: string, anchor: Item): void {
        root.reactionMsgId = msgId;
        root.reactionTab = "";
        root.reactionInfo = ZaloClient.reactionDetails(msgId);
        root.popupOf(reactionPopupLoader).open();
    }

    function openHeaderTarget(): void {
        if (!ZaloClient.currentConversationIsGroup) ZaloClient.viewProfile(ZaloClient.currentConversationId);
        else ZaloClient.toggleConversationInfo();
    }

    function placePopup(menu: Popup, px: real, py: real): void {
        const w = Math.max(menu.width, menu.implicitWidth);
        const h = Math.max(menu.height, menu.implicitHeight);
        const x = px + w > root.width - 8 ? px - w : px;
        const y = py + h > root.height - 8 ? py - h : py;
        menu.x = Math.max(8, Math.min(x, root.width - w - 8));
        menu.y = Math.max(8, Math.min(y, root.height - h - 8));
    }

    function openMessageMenu(item: Item, msg: var, mx: real, my: real): void {
        root.menuMessage = msg;
        const p = root.mapFromItem(item, mx, my);
        const menu = root.popupOf(messageMenuLoader);
        root.placePopup(menu, p.x, p.y);
        menu.open();
    }

    function popupOf(loader: Loader): Popup {
        loader.active = true;
        return (loader.item as PopupHost).popup;
    }

    property string draftConvId: ""
    property bool loadingDraft: false
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
        ZaloClient.composer.clearFormatting(0, 0);
        root.loadingDraft = false;
    }

    property string flashMsgId: ""
    function jumpToMessage(msgId: string, cliMsgId: string): bool {
        ZaloClient.revealMessage(msgId);
        let row = ZaloClient.messageModel.rowOfMessage(msgId);
        if (row < 0 && cliMsgId.length > 0) row = ZaloClient.messageModel.rowOfMessage(cliMsgId);
        if (row < 0) {
            ZaloClient.showNotice(I18n.t("Tin nhắn này không có trong lịch sử đã lưu trên máy.",
                                         "This message is not in the history stored on this device."));
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

    ListModel {
        id: draftAttachmentsModel
    }

    function addDraftAttachment(item: var): void {
        if (!item || !item.url) return;
        const targetUrl = "" + item.url;
        for (let i = 0; i < draftAttachmentsModel.count; ++i) {
            if (draftAttachmentsModel.get(i).url === targetUrl) return;
        }
        draftAttachmentsModel.append({
            "url": targetUrl,
            "name": "" + (item.name || ""),
            "isImage": !!item.isImage,
            "isVideo": !!item.isVideo,
            "fileSize": Number(item.size || 0)
        });
    }

    function addDraftFile(fileUrlOrPath: string): void {
        if (!fileUrlOrPath || fileUrlOrPath.length === 0) return;
        const info = ZaloClient.fileInfoForUrl(fileUrlOrPath);
        if (info && info.url) root.addDraftAttachment(info);
    }

    function removeDraftAttachment(idx: int): void {
        if (idx >= 0 && idx < draftAttachmentsModel.count) {
            draftAttachmentsModel.remove(idx);
        }
    }

    function sendComposer(): void {
        Qt.inputMethod.commit();
        const text = msgInput.text;
        const trimmed = text.trim();
        const attCount = draftAttachmentsModel.count;
        if (trimmed === "" && attCount === 0) return;

        if (attCount > 0) {
            const hasText = trimmed.length > 0;
            if (attCount === 1 && hasText && draftAttachmentsModel.get(0).isImage) {
                ZaloClient.sendFile(draftAttachmentsModel.get(0).url, trimmed);
            } else {
                if (hasText) {
                    const mentions = ZaloClient.currentConversationIsGroup ? root.collectMentions(text) : [];
                    ZaloClient.sendComposedMessage(text, mentions, ZaloClient.composer.styles());
                }
                for (let i = 0; i < attCount; ++i) {
                    ZaloClient.sendFile(draftAttachmentsModel.get(i).url, "");
                }
            }
            draftAttachmentsModel.clear();
        } else {
            const mentions = ZaloClient.currentConversationIsGroup ? root.collectMentions(text) : [];
            ZaloClient.sendComposedMessage(text, mentions, ZaloClient.composer.styles());
        }
        msgInput.text = "";
        ZaloClient.composer.clearFormatting(0, 0);
        root.mentionPicks = [];
        root.closeSuggestions();
    }

    property var mentionPicks: []
    property string suggestMode: ""
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

    function openViewer(msgId: string): void {
        const list = ZaloClient.messageModel.mediaItems();
        let start = 0;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].msgId === msgId) { start = i; break; }
        }
        ImageViewerState.show(list, start);
    }

    function dateLabel(ts: real): string {
        if (!ts || ts < 946684800000) return "";
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

    function fullTimestamp(ts: real, fallback: string): string {
        if (ts && ts >= 946684800000) {
            const d = new Date(ts);
            return Qt.formatTime(d, "HH:mm:ss") + " " + Qt.formatDate(d, "dd/MM/yyyy");
        }
        return fallback || "";
    }

    function fileSizeText(bytes: real): string {
        if (!bytes || bytes <= 0) return I18n.t("Tệp đính kèm", "Attachment");
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB";
        if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MB";
        return (bytes / 1073741824).toFixed(2) + " GB";
    }

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
        function onRevealRequested(msgId: string) { Qt.callLater(() => root.jumpToMessage(msgId, "")); }
        function onCurrentConversationChanged() {
            root.closeSearch();
            root.mentionPicks = [];
            root.closeSuggestions();
            draftAttachmentsModel.clear();
            root.swapDraft();
            root.newBelow = 0;
            pinBar.expanded = false;
            const markerRow = ZaloClient.messageModel.unreadMarkerRow();
            messageList.stickToBottom = markerRow < 0;
            if (markerRow >= 0) Qt.callLater(() => messageList.positionViewAtIndex(markerRow, ListView.Beginning));
        }
        function onGroupMembersChanged(groupId: string) {
            if (root.suggestMode === "mention" && groupId === ZaloClient.currentConversationId) root.updateSuggestions();
        }
    }

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
            visible: !root.infoOverlay
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

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
                            onClicked: root.openHeaderTarget()
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: Number.POSITIVE_INFINITY
                        spacing: 2

                        Text {
                            text: ZaloClient.currentConversationName.length > 0
                                  ? ZaloClient.currentConversationName
                                  : I18n.t("Người dùng Zalo", "Zalo user")
                            font.family: Theme.fontFamily
                            font.bold: true
                            font.pixelSize: 15
                            color: ThemeManager.textColor
                            font.underline: headerNameMouse.containsMouse
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.maximumWidth: implicitWidth

                            MouseArea {
                                id: headerNameMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openHeaderTarget()
                            }
                        }

                        RowLayout {
                            spacing: 5
                            visible: !ZaloClient.currentConversationIsCloud && (ZaloClient.currentConversationIsGroup || ZaloClient.currentConversationIsOnline
                                     || ZaloClient.currentConversationStatus.length > 0 || ZaloClient.currentConversationHasPresence)

                            Rectangle {
                                visible: !ZaloClient.currentConversationIsGroup && ZaloClient.currentConversationIsOnline
                                width: 7
                                height: 7
                                radius: 3.5
                                color: Theme.onlineGreen
                            }

                            Text {
                                text: {
                                    if (ZaloClient.currentConversationIsCloud) return "";
                                    if (ZaloClient.currentConversationIsGroup) {
                                        return ZaloClient.currentConversationMemberCount > 0
                                               ? (ZaloClient.currentConversationMemberCount + I18n.t(" thành viên", " members"))
                                               : I18n.t("Nhóm", "Group");
                                    }
                                    if (ZaloClient.currentConversationIsOnline) {
                                        return I18n.t("Đang hoạt động", "Active now");
                                    }
                                    if (ZaloClient.currentConversationStatus.length > 0) return ZaloClient.currentConversationStatus;
                                    return ZaloClient.currentConversationHasPresence ? I18n.t("Truy cập gần đây", "Recently active") : "";
                                }
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: (!ZaloClient.currentConversationIsGroup && ZaloClient.currentConversationIsOnline)
                                       ? Theme.onlineGreen
                                       : ThemeManager.secondaryTextColor
                                font.underline: ZaloClient.currentConversationIsGroup && memberCountMouse.containsMouse
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

                    RowLayout {
                        spacing: 4

                        Rectangle {
                            visible: ZaloClient.currentConversationCanCreateGroup
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
                                ToolTip.text: ZaloClient.currentConversationIsGroup
                                              ? I18n.t("Thêm thành viên vào nhóm", "Add member to group")
                                              : I18n.t("Tạo nhóm với người này", "Create group with this person")
                                onClicked: ZaloClient.currentConversationIsGroup ? ZaloClient.openAddMemberModal() : ZaloClient.openCreateGroup()
                            }
                        }

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

                        Rectangle {
                            visible: ZaloClient.currentConversationCanCall
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
                                ToolTip.text: ZaloClient.currentConversationIsGroup
                                              ? I18n.t("Cuộc gọi thoại nhóm", "Group voice call")
                                              : I18n.t("Cuộc gọi thoại", "Voice call")
                                onClicked: {
                                    if (ZaloClient.currentConversationIsGroup) ZaloClient.startGroupCall(false);
                                    else ZaloClient.startVoiceCall();
                                }
                            }
                        }

                        Rectangle {
                            visible: ZaloClient.currentConversationCanCall
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
                                ToolTip.text: ZaloClient.currentConversationIsGroup
                                              ? I18n.t("Cuộc gọi video nhóm", "Group video call")
                                              : I18n.t("Cuộc gọi video", "Video call")
                                onClicked: {
                                    if (ZaloClient.currentConversationIsGroup) ZaloClient.startGroupCall(true);
                                    else ZaloClient.startVideoCall();
                                }
                            }
                        }

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

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

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

            Image {
                id: bgImage
                parent: messageList.parent
                z: -1
                anchors.fill: messageList
                visible: false
                source: ZaloClient.currentConversationBackground.length > 0
                        ? "image://cover/" + encodeURIComponent(ZaloClient.currentConversationBackground) : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                mipmap: true
            }

            Loader {
                id: bgBlurLoader
                parent: messageList.parent
                z: -1
                anchors.fill: messageList
                clip: true
                active: GraphicsInfo.api !== GraphicsInfo.Software && ZaloClient.currentConversationBackground.length > 0
                visible: bgImage.status === Image.Ready && ZaloClient.currentConversationBackground.length > 0
                opacity: ZaloClient.chatBackgroundOpacity
                source: "ZChatBackgroundBlur.qml"
                onLoaded: {
                    if (bgBlurLoader.item) {
                        bgBlurLoader.item.source = bgImage;
                    }
                }
            }

            Image {
                id: bgImageFallback
                parent: messageList.parent
                z: -1
                anchors.fill: messageList
                clip: true
                visible: ZaloClient.currentConversationBackground.length > 0 && status === Image.Ready && !bgBlurLoader.active
                source: ZaloClient.currentConversationBackground.length > 0
                        ? "image://cover/" + encodeURIComponent(ZaloClient.currentConversationBackground) : ""
                fillMode: Image.PreserveAspectCrop
                opacity: ZaloClient.chatBackgroundOpacity
                asynchronous: true
                smooth: true
                mipmap: true
            }

            ChatJumpDownButton {
                parent: messageList.parent
                z: 10
                anchors.right: messageList.right
                anchors.bottom: messageList.bottom
                anchors.rightMargin: 16
                anchors.bottomMargin: 14
                newCount: root.newBelow
                shown: messageList.count > 0 && messageList.distanceToEnd > Math.max(160, messageList.height * 0.35)
                onClicked: root.jumpToBottom()
            }

            Rectangle {
                id: pinBar
                objectName: "pinBar"
                parent: messageList.parent
                z: 11
                visible: ZaloClient.pins.pins.length > 0
                property bool expanded: false
                readonly property var topPin: ZaloClient.pins.pins.length > 0 ? ZaloClient.pins.pins[0] : ({})
                anchors.top: messageList.top
                anchors.left: messageList.left
                anchors.right: messageList.right
                height: 56
                color: ThemeManager.surfaceColor
                onVisibleChanged: if (!visible) expanded = false
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: ThemeManager.borderColor
                }
                PinTopicRow {
                    anchors.left: parent.left
                    anchors.right: pinBarActions.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 1
                    topic: pinBar.topPin
                    hoverable: false
                    showMore: false
                    onActivated: root.openPinDetail(pinBar.topPin.id || "")
                }
                RowLayout {
                    id: pinBarActions
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Rectangle {
                        visible: ZaloClient.pins.pins.length > 1
                        Layout.preferredHeight: 26
                        Layout.preferredWidth: pinMoreLabel.implicitWidth + 34
                        radius: 4
                        border.color: ThemeManager.borderColor
                        color: pinMoreMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                        Text {
                            id: pinMoreLabel
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: I18n.t("+%1 ghim", "+%1 pins").arg(ZaloClient.pins.pins.length - 1)
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            color: ThemeManager.textColor
                        }
                        ZIcon {
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron_down"
                            size: 16
                            color: ThemeManager.iconColor
                        }
                        MouseArea {
                            id: pinMoreMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pinBar.expanded = true
                        }
                    }
                    PinMoreButton {
                        id: pinBarMore
                        onClicked: root.openPinMenu(pinBar.topPin, 0, pinBarMore)
                    }
                }
            }

            Rectangle {
                id: pinOverlay
                parent: messageList.parent
                z: 12
                anchors.fill: messageList
                visible: pinBar.visible && pinBar.expanded
                color: Qt.rgba(0, 0, 0, ThemeManager.isDarkMode ? 0.5 : 0.3)
                focus: visible
                onVisibleChanged: if (visible) forceActiveFocus()
                Keys.onEscapePressed: pinBar.expanded = false
                MouseArea {
                    anchors.fill: parent
                    onClicked: pinBar.expanded = false
                    onWheel: function(wheel) { wheel.accepted = true }
                }
                Rectangle {
                    id: pinPanel
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: Math.min(44 + pinPanelList.contentHeight + 8, parent.height * 0.7)
                    color: ThemeManager.surfaceColor
                    MouseArea { anchors.fill: parent }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.bottomMargin: 8
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            Layout.leftMargin: 16
                            Layout.rightMargin: 10
                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Danh sách ghim (%1)", "Pinboard (%1)").arg(ZaloClient.pins.pins.length)
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: ThemeManager.textColor
                            }
                            Rectangle {
                                Layout.preferredHeight: 28
                                Layout.preferredWidth: pinCollapseLabel.implicitWidth + 36
                                radius: 4
                                color: pinCollapseMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                Text {
                                    id: pinCollapseLabel
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: I18n.t("Thu gọn", "Collapse")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: ThemeManager.textColor
                                }
                                ZIcon {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "chevron_down"
                                    rotation: 180
                                    size: 16
                                    color: ThemeManager.iconColor
                                }
                                MouseArea {
                                    id: pinCollapseMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: pinBar.expanded = false
                                }
                            }
                        }
                        ListView {
                            id: pinPanelList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: ZaloClient.pins.pins
                            delegate: PinTopicRow {
                                id: pinPanelRow
                                required property var modelData
                                required property int index
                                width: pinPanelList.width
                                height: 56
                                topic: pinPanelRow.modelData
                                onActivated: root.openPinDetail(pinPanelRow.modelData.id || "")
                                onMoreClicked: function(anchorItem) {
                                    root.openPinMenu(pinPanelRow.modelData, pinPanelRow.index, anchorItem);
                                }
                            }
                        }
                    }
                }
            }

            ListView {
                id: messageList
                anchors.fill: parent
                clip: true
                model: ZaloClient.messageModel
                spacing: 14
                topMargin: pinBar.visible ? 16 + 56 : 16
                bottomMargin: typingIndicatorPill.isPartnerTyping ? 54 : 16
                Behavior on bottomMargin { NumberAnimation { duration: 150 } }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 150; easing.type: Easing.OutQuad }
                }

                property bool stickToBottom: true
                property int countBeforeReset: 0
                readonly property real distanceToEnd: Math.max(0, originY + contentHeight + bottomMargin - height - contentY)
                onMovementEnded: stickToBottom = distanceToEnd < 48
                onAtYEndChanged: if (atYEnd) root.newBelow = 0
                onCountChanged: {
                    if (stickToBottom) Qt.callLater(() => messageList.positionViewAtEnd());
                }
                onContentHeightChanged: {
                    if (stickToBottom && !moving) Qt.callLater(() => messageList.positionViewAtEnd());
                }
                onHeightChanged: {
                    if (stickToBottom || distanceToEnd < 48) {
                        messageList.positionViewAtEnd();
                    }
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
                    objectName: "messageRow"
                    width: messageList.width
                    visible: !model.inMediaRun
                    height: model.inMediaRun ? 0 : rowCol.implicitHeight + (model.lastInGroup ? 10 : 2)

                    required property var model
                    required property string messageType
                    required property bool isMine
                    required property string text
                    required property string quoteText
                    required property string msgId
                    required property string href
                    required property string mediaUrl
                    required property var mediaRun

                    readonly property bool mine: isMine
                    readonly property bool isPhoto: messageType === "image" || messageType === "video"
                    readonly property bool isSticker: messageType === "sticker"
                    readonly property bool runHead: mediaRun.length > 1
                    readonly property bool isFile: messageType === "file"
                    readonly property bool isSystem: messageType === "system"
                    readonly property bool isEcard: messageType === "ecard"
                    readonly property bool isLink: messageType === "link"
                    readonly property bool isVoice: messageType === "voice"
                    readonly property bool isCall: messageType === "call"
                    readonly property bool isPoll: messageType === "poll"
                    readonly property bool isContactCard: messageType === "card"
                    readonly property bool isLocation: messageType === "location"
                    readonly property bool isBankCard: messageType === "bankcard"
                    readonly property bool hasCard: isFile || isLink || isVoice || isCall || isPoll || isContactCard || isLocation
                                                    || isBankCard
                    readonly property bool bubbleless: (isPhoto && text.length === 0 && quoteText.length === 0) || isSticker
                    readonly property real maxBubble: Math.min(messageList.width * 0.7, 560)
                    readonly property bool isSearchHit: root.searchFocusId.length > 0 && msgId === root.searchFocusId
                    readonly property var menuInfo: ({ id: msgRow.msgId, text: msgRow.text, isMine: msgRow.isMine,
                                                       messageType: msgRow.messageType,
                                                       url: msgRow.href.length > 0 ? msgRow.href : msgRow.mediaUrl })

                    HoverHandler { id: rowHover }

                    Rectangle {
                        anchors.fill: parent
                        z: -1
                        radius: 6
                        color: Theme.zaloBlueLight
                        opacity: root.flashMsgId.length > 0 && msgRow.msgId === root.flashMsgId ? 0.7 : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }
                    }

                    ColumnLayout {
                        id: rowCol
                        width: parent.width
                        spacing: 4

                        Loader {
                            active: model.dateSeparator
                            visible: active
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            sourceComponent: Item {
                                id: datePill
                                implicitHeight: 30
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
                        }

                        Loader {
                            active: model.unreadMarker
                            visible: active
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            sourceComponent: ChatUnreadBar {
                            }
                        }

                        Text {
                            visible: msgRow.isSystem
                            Layout.fillWidth: true
                            Layout.leftMargin: 40
                            Layout.rightMargin: 40
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: msgRow.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.underline: systemLinkMouse.enabled && systemLinkMouse.containsMouse
                            color: systemLinkMouse.enabled ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                            MouseArea {
                                id: systemLinkMouse
                                anchors.fill: parent
                                enabled: msgRow.isSystem && !!model.extra.reminderEventId
                                hoverEnabled: true
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: ZaloClient.showConversationInfo = true
                            }
                        }

                        Loader {
                            active: msgRow.isEcard
                            visible: active
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 4
                            Layout.bottomMargin: 4
                            sourceComponent: Rectangle {
                                id: ecardCard
                                readonly property bool suggest: model.extra.suggestStickers === true
                                readonly property var stickers: (model.extra.stickers && model.extra.stickers.length > 0)
                                                                ? model.extra.stickers
                                                                : (ecardCard.suggest ? ZaloClient.stickers.birthdayStickers : [])
                                readonly property string desc: ecardCard.suggest && ecardCard.stickers.length === 0 ? "" : (model.extra.desc || "")
                                implicitWidth: Math.min(340, messageList.width - 48)
                                implicitHeight: ecardCol.implicitHeight + 24
                                radius: 12
                                color: ThemeManager.surfaceColor
                                border.color: ThemeManager.borderColor
                                border.width: 1
                                Component.onCompleted: {
                                    if (ecardCard.suggest) ZaloClient.stickers.loadBirthdayStickers();
                                    else if (ecardCard.stickers.length > 0) ZaloClient.prefetchStickers(ecardCard.stickers);
                                }

                                ColumnLayout {
                                    id: ecardCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 12
                                    spacing: 8

                                    ZAvatar {
                                        Layout.alignment: Qt.AlignHCenter
                                        size: 56
                                        avatarUrl: model.senderAvatar
                                        name: model.senderName
                                        showStatusDot: false
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.Wrap
                                        text: msgRow.text
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: ThemeManager.textColor
                                    }

                                    Text {
                                        visible: ecardCard.desc.length > 0
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.Wrap
                                        text: ecardCard.desc
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }

                                    ListView {
                                        id: ecardStickers
                                        visible: ecardCard.stickers.length > 0
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 64
                                        orientation: ListView.Horizontal
                                        spacing: 6
                                        clip: true
                                        boundsBehavior: Flickable.StopAtBounds
                                        model: ecardCard.stickers
                                        delegate: Rectangle {
                                            id: ecardSticker
                                            required property var modelData
                                            width: 64
                                            height: 64
                                            radius: 8
                                            color: ecardStickerMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                            Image {
                                                anchors.fill: parent
                                                anchors.margins: 4
                                                fillMode: Image.PreserveAspectFit
                                                asynchronous: true
                                                source: ZaloClient.stickerRevision >= 0 ? ZaloClient.stickerThumb(ecardSticker.modelData.id) : ""
                                            }
                                            MouseArea {
                                                id: ecardStickerMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: ZaloClient.sendStickerItem(Number(ecardSticker.modelData.id), Number(ecardSticker.modelData.cateId),
                                                                                      Number(ecardSticker.modelData.type))
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            id: bubbleRow
                            visible: !msgRow.isSystem && !msgRow.isEcard
                            Layout.fillWidth: true
                            spacing: 8

                            Item { visible: msgRow.mine; Layout.fillWidth: true }

                            Item {
                                visible: !msgRow.mine
                                Layout.leftMargin: 16
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: 32
                                implicitHeight: 32
                                Loader {
                                    anchors.fill: parent
                                    active: model.firstInGroup
                                    sourceComponent: Item {
                                        ZAvatar {
                                            anchors.fill: parent
                                            size: 32
                                            avatarUrl: (model.senderAvatar && model.senderAvatar.length > 0)
                                                       ? model.senderAvatar
                                                       : (ZaloClient.currentConversationIsGroup ? "" : ZaloClient.currentConversationAvatar)
                                            name: (model.senderName && model.senderName.length > 0) ? model.senderName
                                                  : (ZaloClient.currentConversationIsGroup ? "" : ZaloClient.currentConversationName)
                                            showStatusDot: false
                                        }
                                        MouseArea {
                                            visible: model.senderId.length > 0
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: ZaloClient.viewProfile(model.senderId)
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                id: contentCol
                                Layout.maximumWidth: msgRow.maxBubble
                                Layout.rightMargin: msgRow.mine ? 16 : 0
                                opacity: (ZaloClient.antiPeeking && !rowHover.hovered) ? 0.08 : 1.0
                                Behavior on opacity { NumberAnimation { duration: 150 } }
                                spacing: 0

                                Text {
                                    id: senderNameText
                                    visible: !msgRow.mine && ZaloClient.currentConversationIsGroup && model.firstInGroup
                                             && (model.senderName.length > 0 || (model.senderId.length > 0 && model.senderId !== "system"))
                                    Layout.bottomMargin: 4
                                    text: model.senderName.length > 0 ? model.senderName : I18n.t("Người dùng Zalo", "Zalo user")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: senderNameMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                                    MouseArea {
                                        id: senderNameMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: { if (model.senderId.length > 0) ZaloClient.viewProfile(model.senderId); }
                                    }
                                }

                                Loader {
                                    id: mediaLoader
                                    active: msgRow.isPhoto && msgRow.mediaUrl.length > 0 && !msgRow.runHead
                                    visible: active
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    sourceComponent: Item {
                                        id: mediaBox
                                        readonly property real srcW: mediaImg.implicitImageWidth > 0 ? mediaImg.implicitImageWidth : 240
                                        readonly property real srcH: mediaImg.implicitImageHeight > 0 ? mediaImg.implicitImageHeight : 180
                                        readonly property real fit: Math.min(Math.min(360, messageList.width - 237) / srcW, 368 / srcH, 1.0)
                                        implicitWidth: Math.max(150, srcW * fit)
                                        implicitHeight: Math.max(110, srcH * fit)

                                        ZRoundedImage {
                                            id: mediaImg
                                            anchors.fill: parent
                                            source: msgRow.mediaUrl
                                            animatedSource: model.extra.animatedUrl ? model.extra.animatedUrl : ""
                                            sourceSize.width: Math.ceil(360 * (Screen.devicePixelRatio || 1))
                                            topOnly: !msgRow.bubbleless
                                        }
                                        ColumnLayout {
                                            objectName: "mediaExpiredHint"
                                            visible: mediaImg.status === Image.Error
                                            anchors.centerIn: parent
                                            spacing: 6
                                            ZIcon {
                                                Layout.alignment: Qt.AlignHCenter
                                                name: msgRow.messageType === "video" ? "video" : "image"
                                                size: 24
                                                color: ThemeManager.secondaryTextColor
                                            }
                                            Text {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: msgRow.messageType === "video" ? I18n.t("Video đã hết hạn", "Video expired")
                                                                                     : I18n.t("Ảnh đã hết hạn", "Photo expired")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: ThemeManager.secondaryTextColor
                                            }
                                        }
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 8
                                            color: Theme.mediaHover
                                            visible: mediaMouse.containsMouse
                                        }
                                        Rectangle {
                                            visible: msgRow.messageType === "video"
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
                                            ToolTip.visible: containsMouse && msgRow.messageType === "video"
                                            ToolTip.delay: 600
                                            ToolTip.text: I18n.t("Xem video", "Watch video")
                                            onClicked: function(mouse) {
                                                if (mouse.button === Qt.RightButton) {
                                                    root.openMessageMenu(mediaMouse, msgRow.menuInfo, mouse.x, mouse.y);
                                                } else {
                                                    root.openViewer(msgRow.msgId);
                                                }
                                            }
                                        }
                                    }
                                }

                                Loader {
                                    id: stickerLoader
                                    active: msgRow.isSticker && !msgRow.runHead
                                    visible: active
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    sourceComponent: Item {
                                        id: stickerBox
                                        implicitWidth: 130
                                        implicitHeight: 130
                                        ZSticker {
                                            id: stickerImage
                                            anchors.fill: parent
                                            info: model.extra.sticker ? model.extra.sticker : ({})
                                            fallbackUrl: msgRow.mediaUrl
                                            playing: msgRow.visible
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
                                                    ImageViewerState.show([{ msgId: msgRow.msgId, url: msgRow.mediaUrl, thumb: msgRow.mediaUrl,
                                                                             senderName: model.senderName, isMine: msgRow.isMine, ts: model.ts }], 0);
                                                }
                                            }
                                        }
                                    }
                                }

                                Loader {
                                    id: runLoader
                                    active: msgRow.runHead
                                    visible: active
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    sourceComponent: Item {
                                        implicitWidth: Math.min(runFlow.maxW, runFlow.naturalW)
                                        implicitHeight: runFlow.implicitHeight
                                    Flow {
                                        id: runFlow
                                        width: parent.width
                                        spacing: 4
                                        layoutDirection: msgRow.mine ? Qt.RightToLeft : Qt.LeftToRight
                                        readonly property real maxW: Math.max(150, Math.min(msgRow.maxBubble, messageList.width - 140))
                                        readonly property real naturalW: {
                                            let w = 0;
                                            for (let i = 0; i < msgRow.mediaRun.length; ++i)
                                                w += (msgRow.mediaRun[i].messageType === "sticker" ? 130 : 150) + (i > 0 ? spacing : 0);
                                            return w;
                                        }
                                        Repeater {
                                            model: msgRow.mediaRun
                                            delegate: Item {
                                                id: tile
                                                required property var modelData
                                                readonly property bool sticker: tile.modelData.messageType === "sticker"
                                                width: tile.sticker ? 130 : 150
                                                height: tile.sticker ? 130 : 150
                                                ZRoundedImage {
                                                    id: tileImage
                                                    visible: !tile.sticker
                                                    anchors.fill: parent
                                                    source: tile.sticker ? "" : tile.modelData.mediaUrl
                                                    animatedSource: !tile.sticker && tile.modelData.animatedUrl ? tile.modelData.animatedUrl : ""
                                                    sourceSize.width: 320
                                                }
                                                Text {
                                                    visible: !tile.sticker && tileImage.status === Image.Error
                                                    anchors.centerIn: parent
                                                    text: I18n.t("Ảnh đã hết hạn", "Photo expired")
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: ThemeManager.secondaryTextColor
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
}
                                }

                                Rectangle {
                                    id: bubbleRect
                                    visible: !msgRow.bubbleless
                                    readonly property real innerMax: msgRow.maxBubble - 24
                                    implicitWidth: msgRow.isPhoto ? mediaLoader.implicitWidth
                                                                  : Math.min(bubbleInner.implicitWidth + 24, msgRow.maxBubble)
                                    implicitHeight: bubbleInner.implicitHeight + (msgRow.isPhoto ? 17 : 16)
                                    Layout.preferredWidth: implicitWidth
                                    Layout.preferredHeight: implicitHeight
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
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

                                    MouseArea {
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

                                        Loader {
                                            active: model.quoteMsgId.length > 0 || msgRow.quoteText.length > 0
                                            visible: active
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 160
                                            sourceComponent: Rectangle {
                                                implicitHeight: quoteCol.implicitHeight + 10
                                                implicitWidth: Math.min(quoteCol.implicitWidth + 18, bubbleRect.innerMax)
                                                radius: 4
                                                color: quoteMouse.containsMouse ? ThemeManager.selectedColor : ThemeManager.hoverColor
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
                                                        text: msgRow.quoteText
                                                        elide: Text.ElideRight
                                                        maximumLineCount: 2
                                                        wrapMode: Text.Wrap
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 12
                                                        color: ThemeManager.secondaryTextColor
                                                    }
                                                }
                                                MouseArea {
                                                    id: quoteMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: { if (model.quoteMsgId.length > 0) root.jumpToMessage(model.quoteMsgId, ""); }
                                                }
                                            }
                                        }

                                        Loader {
                                            active: msgRow.isFile
                                            visible: active
                                            sourceComponent: Item {
                                                id: fileCardBox
                                                implicitWidth: Math.min(352, messageList.width - 184)
                                                implicitHeight: fileCard.implicitHeight
                                            RowLayout {
                                                id: fileCard
                                                anchors.fill: parent
                                                spacing: 12
                                                readonly property string ext: {
                                                    const n = msgRow.text || "";
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
                                                        text: msgRow.text
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
                                                    onClicked: ZaloClient.openFileMessage(msgRow.href.length > 0 ? msgRow.href : msgRow.mediaUrl, msgRow.text)
                                                }
                                            }
                                        }

                                        Loader {
                                            active: msgRow.isContactCard && !model.isRecalled
                                            visible: active
                                            Layout.preferredWidth: Math.min(280, bubbleRect.innerMax)
                                            sourceComponent: ColumnLayout {
                                                id: contactCard
                                                readonly property string uid: model.extra.uid || ""
                                                readonly property string phone: model.extra.phone || ""
                                                spacing: 8
                                                Item {
                                                    Layout.fillWidth: true
                                                    implicitHeight: contactHeaderRow.implicitHeight
                                                    RowLayout {
                                                        id: contactHeaderRow
                                                        anchors.fill: parent
                                                        spacing: 10
                                                        ZAvatar {
                                                            size: 48
                                                            avatarUrl: msgRow.mediaUrl
                                                            name: msgRow.text
                                                        }
                                                        ColumnLayout {
                                                            Layout.fillWidth: true
                                                            spacing: 2
                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: msgRow.text
                                                                elide: Text.ElideRight
                                                                font.family: Theme.fontFamily
                                                                font.pixelSize: 15
                                                                font.weight: Font.DemiBold
                                                                color: contactHeaderMouse.containsMouse && contactCard.uid.length > 0
                                                                       ? Theme.zaloBlue : ThemeManager.textColor
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
                                                    MouseArea {
                                                        id: contactHeaderMouse
                                                        anchors.fill: parent
                                                        enabled: contactCard.uid.length > 0
                                                        hoverEnabled: true
                                                        cursorShape: contactCard.uid.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                        onClicked: if (contactCard.uid.length > 0) ZaloClient.viewProfile(contactCard.uid)
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
                                                        onClicked: ZaloClient.openConversationWithContact(contactCard.uid, msgRow.text, msgRow.mediaUrl)
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
                                        }

                                        Loader {
                                            active: msgRow.isLink && !model.isRecalled
                                            visible: active
                                            Layout.preferredWidth: Math.min(320, bubbleRect.innerMax)
                                            sourceComponent: ColumnLayout {
                                                spacing: 4
                                                Image {
                                                    visible: msgRow.mediaUrl.length > 0 && status !== Image.Error
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: visible ? 150 : 0
                                                    source: msgRow.isLink ? msgRow.mediaUrl : ""
                                                    asynchronous: true
                                                    fillMode: Image.PreserveAspectCrop
                                                    sourceSize.width: Math.ceil(320 * (Screen.devicePixelRatio || 1))
                                                    clip: true
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: msgRow.text
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
                                                    maximumLineCount: msgRow.href.length > 0 ? 2 : 8
                                                    elide: Text.ElideRight
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: ThemeManager.secondaryTextColor
                                                }
                                                Text {
                                                    visible: msgRow.href.length > 0
                                                    Layout.fillWidth: true
                                                    text: msgRow.isLink ? (model.extra.domain || msgRow.href) : ""
                                                    elide: Text.ElideRight
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: Theme.linkColor
                                                }
                                                TapHandler {
                                                    enabled: msgRow.href.length > 0
                                                    onTapped: ZaloClient.openLink(msgRow.href, msgRow.text, msgRow.mediaUrl)
                                                }
                                                HoverHandler { cursorShape: msgRow.href.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                            }
                                        }

                                        Loader {
                                            active: msgRow.isBankCard && !model.isRecalled
                                            visible: active
                                            Layout.preferredWidth: Math.min(280, bubbleRect.innerMax)
                                            sourceComponent: RowLayout {
                                                spacing: 10
                                                Rectangle {
                                                    Layout.preferredWidth: 40
                                                    Layout.preferredHeight: 40
                                                    radius: 20
                                                    color: Theme.zaloBlueLight
                                                    ZIcon { anchors.centerIn: parent; name: "contacts"; size: 20; color: Theme.zaloBlueTonalText }
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 2
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: I18n.t("Thông tin chuyển khoản", "Bank transfer details")
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 14
                                                        font.weight: Font.DemiBold
                                                        color: ThemeManager.textColor
                                                    }
                                                    Text {
                                                        visible: msgRow.text !== I18n.t("Thông tin chuyển khoản", "Bank transfer details")
                                                        Layout.fillWidth: true
                                                        text: msgRow.text
                                                        wrapMode: Text.Wrap
                                                        maximumLineCount: 2
                                                        elide: Text.ElideRight
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 13
                                                        color: ThemeManager.textColor
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: msgRow.isBankCard ? (model.extra.desc || "") : ""
                                                        wrapMode: Text.Wrap
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 12
                                                        color: ThemeManager.secondaryTextColor
                                                    }
                                                }
                                            }
                                        }

                                        Loader {
                                            active: msgRow.isLocation && !model.isRecalled
                                            visible: active
                                            Layout.preferredWidth: Math.min(280, bubbleRect.innerMax)
                                            sourceComponent: RowLayout {
                                                spacing: 10
                                                Rectangle {
                                                    Layout.preferredWidth: 40
                                                    Layout.preferredHeight: 40
                                                    radius: 20
                                                    color: Theme.zaloBlueLight
                                                    ZIcon { anchors.centerIn: parent; name: "location"; size: 20; color: Theme.zaloBlueTonalText }
                                                }
                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 2
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: msgRow.text
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
                                                        text: msgRow.isLocation ? (model.extra.desc || model.extra.coords || "") : ""
                                                        wrapMode: Text.Wrap
                                                        maximumLineCount: 2
                                                        elide: Text.ElideRight
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 12
                                                        color: ThemeManager.secondaryTextColor
                                                    }
                                                    Text {
                                                        visible: msgRow.href.length > 0
                                                        text: I18n.t("Mở bản đồ", "Open map")
                                                        font.family: Theme.fontFamily
                                                        font.pixelSize: 12
                                                        color: Theme.linkColor
                                                    }
                                                }
                                                TapHandler {
                                                    onTapped: if (msgRow.href.length > 0) Qt.openUrlExternally(msgRow.href)
                                                }
                                                HoverHandler { cursorShape: msgRow.href.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                            }
                                        }

                                        Loader {
                                            active: msgRow.isVoice && !model.isRecalled
                                            visible: active
                                            sourceComponent: RowLayout {
                                                id: voiceRow
                                                spacing: 10
                                                readonly property bool playing: ZaloClient.playingVoiceId === msgRow.msgId
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
                                                        onClicked: ZaloClient.toggleVoice(msgRow.msgId, msgRow.href)
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
                                        }

                                        Loader {
                                            active: msgRow.isCall
                                            visible: active
                                            sourceComponent: RowLayout {
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
                                                        text: msgRow.text
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
                                        }

                                        Loader {
                                            active: msgRow.isPoll && !model.isRecalled
                                            visible: active
                                            Layout.preferredWidth: Math.min(320, bubbleRect.innerMax)
                                            sourceComponent: ColumnLayout {
                                                id: pollCard
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
                                                    text: msgRow.text
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
                                                        Rectangle {
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
                                        }

                                        TextEdit {
                                            id: msgText
                                            visible: !msgRow.hasCard && msgRow.text.length > 0 && !model.isRecalled
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
                                        Text {
                                            visible: model.isRecalled
                                            text: msgRow.text
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 14
                                            font.italic: true
                                            color: ThemeManager.secondaryTextColor
                                        }
                                    }
                                }

                                Item {
                                    id: reactionRow
                                    readonly property bool canReact: !model.isRecalled && !msgRow.isSystem && !msgRow.isEcard
                                                                     && !msgRow.msgId.startsWith("local-") && ZaloClient.isLoggedIn
                                    readonly property real contentEdge: Math.max(
                                        bubbleRect.visible ? bubbleRect.x + bubbleRect.width : 0,
                                        mediaLoader.visible ? mediaLoader.x + mediaLoader.width : 0,
                                        stickerLoader.visible ? stickerLoader.x + stickerLoader.width : 0,
                                        runLoader.visible ? runLoader.x + runLoader.width : 0)
                                    readonly property real contentLeft: Math.min(
                                        bubbleRect.visible ? bubbleRect.x : 1e6,
                                        mediaLoader.visible ? mediaLoader.x : 1e6,
                                        stickerLoader.visible ? stickerLoader.x : 1e6,
                                        runLoader.visible ? runLoader.x : 1e6)
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
                                    Loader {
                                        active: model.reaction.length > 0
                                        sourceComponent: Rectangle {
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
                                                onClicked: root.openReactionDetails(msgRow.msgId, parent)
                                            }
                                            ToolTip.visible: reactionPillMouse.containsMouse
                                            ToolTip.delay: 400
                                            ToolTip.text: I18n.t("Xem ai đã thả cảm xúc", "See who reacted")
                                        }
                                    }
                                    Loader {
                                        active: reactionRow.canReact && rowHover.hovered
                                        sourceComponent: Rectangle {
                                            implicitWidth: 22
                                            implicitHeight: 22
                                            radius: 11
                                            color: quickLikeMouse.containsMouse ? ThemeManager.inputFillHoverColor : Theme.reactionBackground
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
                                                onClicked: ZaloClient.reactToMessage(msgRow.msgId, "like")
                                            }
                                            ToolTip.visible: quickLikeMouse.containsMouse
                                            ToolTip.delay: 400
                                            ToolTip.text: I18n.t("Thích", "Like")
                                        }
                                    }
                                    }
                                }

                                RowLayout {
                                    visible: model.lastInGroup || model.showStatus
                                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                                    Layout.topMargin: 4
                                    spacing: 6
                                    Text {
                                        id: messageTimeLabel
                                        visible: model.lastInGroup
                                        text: model.time
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor

                                        MouseArea {
                                            id: timeHoverArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.ArrowCursor
                                        }

                                        ToolTip.visible: timeHoverArea.containsMouse && messageTimeLabel.text.length > 0
                                        ToolTip.delay: 300
                                        ToolTip.text: root.fullTimestamp(model.ts, model.time)
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

                    Loader {
                        active: rowHover.hovered && !model.isRecalled && !msgRow.isSystem && !msgRow.isEcard && !msgRow.msgId.startsWith("local-")
                        y: bubbleRow.y + (senderNameText.visible ? (senderNameText.height + 4) : 0) + 2
                        x: msgRow.mine ? contentCol.x + Math.min(reactionRow.contentLeft, contentCol.width) - width - 8
                                       : contentCol.x + Math.max(reactionRow.contentEdge, 0) + 8
                        sourceComponent: Rectangle {
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
                                                    ZaloClient.setReplyTarget(msgRow.msgId);
                                                    msgInput.forceActiveFocus();
                                                } else if (modelData.act === "forward") {
                                                    ZaloClient.openForwardModal(msgRow.msgId, msgRow.text);
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
            }

            Rectangle {
                id: typingIndicatorPill
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 16
                anchors.bottomMargin: 14
                implicitWidth: typingRow.implicitWidth + 24
                width: implicitWidth
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
                    id: typingRow
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 6

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
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
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
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (ZaloClient.replyTarget.id)
                                root.jumpToMessage(ZaloClient.replyTarget.id, "");
                        }
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

        Rectangle {
            id: draftAttachmentsBar
            Layout.fillWidth: true
            visible: draftAttachmentsModel.count > 0 && ZaloClient.composerLockReason.length === 0
            implicitHeight: 76
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Text {
                    text: I18n.t("Đính kèm:", "Attachments:") + " (" + draftAttachmentsModel.count + ")"
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    font.bold: true
                    color: ThemeManager.secondaryTextColor
                }

                ListView {
                    id: draftList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    spacing: 8
                    clip: true
                    model: draftAttachmentsModel
                    delegate: Rectangle {
                        id: draftChip
                        required property string url
                        required property string name
                        required property bool isImage
                        required property bool isVideo
                        required property int index
                        width: draftChip.isImage ? 56 : Math.min(180, draftNameText.implicitWidth + 48)
                        height: 56
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 6
                        color: ThemeManager.hoverColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                        clip: true

                        Image {
                            visible: draftChip.isImage
                            anchors.fill: parent
                            source: draftChip.isImage ? draftChip.url : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 112
                            sourceSize.height: 112
                        }

                        RowLayout {
                            visible: !draftChip.isImage
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 24
                            spacing: 6
                            ZIcon {
                                name: draftChip.isVideo ? "camera" : "file"
                                size: 18
                                color: Theme.zaloBlue
                            }
                            Text {
                                id: draftNameText
                                Layout.fillWidth: true
                                text: draftChip.name
                                font.family: Theme.fontFamily
                                font.pixelSize: 11
                                color: ThemeManager.textColor
                                elide: Text.ElideMiddle
                            }
                        }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 2
                            width: 18
                            height: 18
                            radius: 9
                            color: removeDraftMouse.containsMouse ? "#D32F2F" : Qt.rgba(0, 0, 0, 0.6)
                            ZIcon { anchors.centerIn: parent; name: "close"; size: 10; color: "#FFFFFF" }
                            MouseArea {
                                id: removeDraftMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.removeDraftAttachment(draftChip.index)
                            }
                        }
                    }
                }

                ZButton {
                    variant: "secondary"
                    text: I18n.t("Xóa hết", "Clear all")
                    onClicked: draftAttachmentsModel.clear()
                }
            }
        }

        Rectangle {
            id: blockedBanner
            objectName: "blockedBanner"
            visible: ZaloClient.currentConversationBlocked && ZaloClient.composerLockReason.length === 0
            Layout.fillWidth: true
            Layout.preferredHeight: blockedRow.implicitHeight + 16
            color: ThemeManager.hoverColor

            RowLayout {
                id: blockedRow
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12
                ZIcon { name: "lock"; size: 16; color: ThemeManager.secondaryTextColor }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Bạn đã chặn người này. Người này sẽ không thể trả lời tin nhắn của bạn",
                                 "You blocked this person. They can't reply to your messages")
                    wrapMode: Text.Wrap
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    color: ThemeManager.textColor
                }
                ZButton {
                    variant: "secondary"
                    text: I18n.t("Bỏ chặn", "Unblock")
                    onClicked: ZaloClient.unblockUser(ZaloClient.currentConversationId)
                }
            }
        }

        Rectangle {
            id: composerLockBar
            objectName: "composerLockBar"
            visible: ZaloClient.composerLockReason.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: ThemeManager.surfaceColor

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: ThemeManager.dividerColor
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12
                ZIcon { name: "lock"; size: 16; color: ThemeManager.secondaryTextColor }
                Text {
                    Layout.fillWidth: true
                    text: ZaloClient.composerLockReason
                    wrapMode: Text.Wrap
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    color: ThemeManager.secondaryTextColor
                }
                ZButton {
                    visible: ZaloClient.composerLockDeletable
                    variant: "secondary"
                    text: I18n.t("Xoá trò chuyện", "Delete conversation")
                    onClicked: ZaloClient.deleteConversation(ZaloClient.currentConversationId)
                }
            }
        }

        Rectangle {
            id: composerBox
            objectName: "composerBox"
            visible: ZaloClient.composerLockReason.length === 0
            Layout.fillWidth: true
            readonly property real textAreaHeight: root.isEditorExpanded ? 230
                : Math.max(root.composerMinTextHeight, Math.min(root.composerMaxTextHeight, msgInput.implicitHeight + 16))
            readonly property real calculatedHeight: 32 + 1 + (root.isFormattingActive ? 34 : 0) + textAreaHeight + 32
            height: calculatedHeight
            implicitHeight: calculatedHeight
            Layout.preferredHeight: calculatedHeight

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }
            color: ThemeManager.surfaceColor

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                z: 60
                color: ThemeManager.dividerColor
            }

            Rectangle {
                anchors.fill: parent
                z: 50
                visible: ZaloClient.voiceRecorder.recording
                color: ThemeManager.surfaceColor
                MouseArea { anchors.fill: parent }
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

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbStickerMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "sticker"
                                size: 18
                                color: root.emojiPickerOpen ? Theme.zaloBlue
                                     : (tbStickerMouse.pressed ? Theme.zaloBlueHover
                                     : (tbStickerMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor))
                            }

                            MouseArea {
                                id: tbStickerMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Biểu tượng cảm xúc (Emoji/Stickers)", "Stickers & Emojis")
                                onClicked: { emojiPickerLoader.active = true; root.emojiPickerOpen = !root.emojiPickerOpen; }
                            }
                        }

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbImageMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "image"
                                size: 18
                                color: tbImageMouse.pressed ? Theme.zaloBlueHover
                                     : (tbImageMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
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

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbClipMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "clip"
                                size: 18
                                color: tbClipMouse.pressed ? Theme.zaloBlueHover
                                     : (tbClipMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
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

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbSnipMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "snip"
                                size: 18
                                color: tbSnipMouse.pressed ? Theme.zaloBlueHover
                                     : (tbSnipMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            }

                            MouseArea {
                                id: tbSnipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Chụp ảnh màn hình (Ctrl+Alt+S)", "Capture screenshot (Ctrl+Alt+S)")
                                onClicked: ZaloClient.requestScreenshot()
                            }
                        }

                        Rectangle {
                            visible: ZaloClient.currentConversationIsGroup
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbMentionMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "mention"
                                size: 18
                                color: tbMentionMouse.pressed ? Theme.zaloBlueHover
                                     : (tbMentionMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
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

                        Rectangle {
                            visible: ZaloClient.currentConversationId.length > 0
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbCardMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "contacts"
                                size: 18
                                color: tbCardMouse.pressed ? Theme.zaloBlueHover
                                     : (tbCardMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
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

                        Rectangle {
                            visible: ZaloClient.currentConversationIsGroup
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbPollMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "poll"
                                size: 18
                                color: tbPollMouse.pressed ? Theme.zaloBlueHover
                                     : (tbPollMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
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

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbReminderMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "reminder"
                                size: 18
                                color: tbReminderMouse.pressed ? Theme.zaloBlueHover
                                     : (tbReminderMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            }

                            MouseArea {
                                id: tbReminderMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Hẹn giờ gửi tin nhắn", "Schedule message")
                                onClicked: root.popupOf(reminderPopupLoader).open()
                            }
                        }

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbQuickMsgMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "quick_message"
                                size: 18
                                color: tbQuickMsgMouse.pressed ? Theme.zaloBlueHover
                                     : (tbQuickMsgMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            }

                            MouseArea {
                                id: tbQuickMsgMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Tin nhắn nhanh", "Quick message")
                                onClicked: root.popupOf(quickMsgPopupLoader).open()
                            }
                        }

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbVoiceMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }
                            opacity: ZaloClient.voiceRecorder.available ? 1.0 : 0.45

                            ZIcon {
                                anchors.centerIn: parent
                                name: "mic"
                                size: 18
                                color: tbVoiceMouse.pressed ? Theme.zaloBlueHover
                                     : (tbVoiceMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
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

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbFormatMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "format_text"
                                size: 18
                                color: root.isFormattingActive ? Theme.zaloBlue
                                     : (tbFormatMouse.pressed ? Theme.zaloBlueHover
                                     : (tbFormatMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor))
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

                        Rectangle {
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            scale: tbExpandMouse.pressed ? 0.94 : 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: root.isEditorExpanded ? "collapse" : "expand"
                                size: 18
                                color: tbExpandMouse.pressed ? Theme.zaloBlueHover
                                     : (tbExpandMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
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

                Rectangle {
                    id: rtfToolbar
                    objectName: "formatToolbar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.isFormattingActive ? 34 : 0
                    visible: root.isFormattingActive || Layout.preferredHeight > 0
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
                            persistentSelection: true
                            Component.onCompleted: ZaloClient.composer.attach(msgInput.textDocument)
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
                            placeholderTextColor: ThemeManager.placeholderColor

                            Keys.onPressed: function(event) {
                                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_V) {
                                    const clipItems = ZaloClient.getClipboardAttachments();
                                    if (clipItems.length > 0) {
                                        for (var ci = 0; ci < clipItems.length; ci++) {
                                            root.addDraftAttachment(clipItems[ci]);
                                        }
                                        event.accepted = true;
                                        return;
                                    }
                                } else if (event.key === Qt.Key_Escape && draftAttachmentsModel.count > 0) {
                                    draftAttachmentsModel.clear();
                                    event.accepted = true;
                                    return;
                                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_B) {
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

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: ThemeManager.dividerColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
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

                        Rectangle {
                            visible: msgInput.text.trim() === ""
                            width: 30
                            height: 30
                            radius: 15
                            color: "transparent"
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

                        Rectangle {
                            visible: msgInput.text.trim() !== "" || draftAttachmentsModel.count > 0
                            width: 30
                            height: 30
                            radius: 15
                            color: "transparent"
                            scale: sendBtnMouse.pressed ? 0.88 : (sendBtnMouse.containsMouse ? 1.1 : 1.0)
                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "send"
                                size: 20
                                color: sendBtnMouse.pressed ? Theme.zaloBlueHover : Theme.zaloBlue
                            }

                            MouseArea {
                                id: sendBtnMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Gửi tin nhắn", "Send message")
                                onClicked: root.sendComposer()
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: conversationInfoLoader
        objectName: "conversationInfoLoader"
        active: ZaloClient.showConversationInfo
        visible: active
        Layout.fillHeight: true
        Layout.fillWidth: root.infoOverlay
        Layout.preferredWidth: active ? 280 : 0
        sourceComponent: ConversationInfoPanel {
            objectName: "conversationInfoPanel"
            anchors.fill: parent
        }
    }
    }

    property bool emojiPickerOpen: false
    Loader {
        id: emojiPickerLoader
        active: false
        visible: root.emojiPickerOpen
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.bottomMargin: composerBox.height + 6
        z: 99
        sourceComponent: EmojiPickerPopup {
            id: emojiPicker
            width: Math.min(380, root.width - 32)
            height: Math.min(root.height - 32, 420)
            onEmojiSelected: function(emoji) {
                msgInput.insert(msgInput.cursorPosition, emoji);
                msgInput.forceActiveFocus();
                root.emojiPickerOpen = false;
            }
            onStickerSelected: function(sticker) {
                ZaloClient.stickers.send(sticker);
                root.emojiPickerOpen = false;
                msgInput.forceActiveFocus();
            }
        }
    }

    Loader {
        id: pinDetailPopupLoader
        anchors.fill: parent
        active: false
        sourceComponent: PinDetailHost {
            id: pinDetailHost
            popup: pinDetailPopup
            Popup {
                id: pinDetailPopup
                objectName: "pinDetailPopup"
                onAboutToShow: {
                    pinDetailText.deselect();
                    pinDetailFlick.contentY = 0;
                }
                readonly property var detail: pinDetailHost.detail
                readonly property string fullText: pinDetailPopup.detail.text || ""
                readonly property string image: pinDetailPopup.detail.image || ""
                readonly property string href: pinDetailPopup.detail.href || ""
                focus: true
                anchors.centerIn: parent
                width: Math.min(root.width - 32, 520)
                height: Math.min(root.height - 60, pinDetailColumn.implicitHeight + 2 * padding)
                modal: true
                padding: 16
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle { color: ThemeManager.surfaceColor; radius: 12; border.color: ThemeManager.borderColor }
                contentItem: ColumnLayout {
                    id: pinDetailColumn
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ZIcon {
                            name: pinDetailPopup.detail.type === 3 ? "poll" : (pinDetailPopup.detail.type === 1 ? "file" : "chat")
                            size: 20
                            color: pinDetailPopup.detail.type === 3 ? Theme.onlineGreen
                                   : (pinDetailPopup.detail.type === 1 ? Theme.goldStar : Theme.zaloBlue)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: pinDetailPopup.detail.type === 3 ? I18n.t("Bình chọn đã ghim", "Pinned poll")
                                  : (pinDetailPopup.detail.type === 1 ? I18n.t("Ghi chú đã ghim", "Pinned note")
                                                                      : I18n.t("Tin nhắn đã ghim", "Pinned message"))
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 14
                            color: pinDetailClose.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: "close"; size: 16; color: ThemeManager.iconColor }
                            MouseArea {
                                id: pinDetailClose
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: pinDetailPopup.close()
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (pinDetailPopup.detail.senderName || "").length > 0
                        text: I18n.t("Người gửi: ", "From: ") + (pinDetailPopup.detail.senderName || "")
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                    Item {
                        visible: pinDetailPopup.image.length > 0
                        Layout.preferredWidth: Math.min(pinDetailColumn.width, 360)
                        Layout.preferredHeight: pinDetailImage.status === Image.Ready
                                                ? Math.min(240, pinDetailImage.implicitHeight * width / Math.max(1, pinDetailImage.implicitWidth))
                                                : 96
                        Rectangle {
                            anchors.fill: parent
                            visible: pinDetailImage.status !== Image.Ready
                            radius: 8
                            color: Theme.mediaPlaceholder
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                ZIcon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    name: "image"
                                    size: 28
                                    color: ThemeManager.secondaryTextColor
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: pinDetailImage.status === Image.Loading ? I18n.t("Đang tải ảnh…", "Loading picture…")
                                          : I18n.t("Ảnh không còn khả dụng · Bấm để thử lại", "Picture no longer available · Click to retry")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                        }
                        Image {
                            id: pinDetailImage
                            anchors.fill: parent
                            readonly property bool usable: ZaloClient.brokenMediaRevision >= 0 && !ZaloClient.isMediaBroken(pinDetailPopup.image)
                            visible: status === Image.Ready
                            source: usable ? pinDetailPopup.image : ""
                            sourceSize.width: 720
                            fillMode: Image.PreserveAspectFit
                            horizontalAlignment: Image.AlignLeft
                            asynchronous: true
                            onStatusChanged: if (status === Image.Error) ZaloClient.markMediaBroken(pinDetailPopup.image)
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: pinDetailImage.status === Image.Loading ? Qt.BusyCursor : Qt.PointingHandCursor
                            onClicked: {
                                if (pinDetailImage.status === Image.Ready)
                                    Qt.openUrlExternally(pinDetailPopup.href.length > 0 ? pinDetailPopup.href : pinDetailPopup.image);
                                else if (pinDetailImage.status !== Image.Loading)
                                    ZaloClient.retryMedia(pinDetailPopup.image);
                            }
                        }
                    }
                    Flickable {
                        id: pinDetailFlick
                        visible: pinDetailPopup.fullText.length > 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: Math.min(pinDetailText.implicitHeight, 360)
                        contentWidth: width
                        contentHeight: pinDetailText.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: pinDetailFlick.contentHeight > pinDetailFlick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                        TextEdit {
                            id: pinDetailText
                            width: pinDetailFlick.width - 8
                            text: pinDetailPopup.fullText
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            persistentSelection: true
                            wrapMode: TextEdit.Wrap
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            color: ThemeManager.textColor
                            selectionColor: Theme.zaloBlue
                            selectedTextColor: Theme.textOnColor
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: pinDetailPopup.href.length > 0 && pinDetailPopup.href !== pinDetailPopup.fullText
                        text: pinDetailPopup.href
                        elide: Text.ElideMiddle
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.underline: pinDetailLinkMouse.containsMouse
                        color: Theme.linkColor
                        MouseArea {
                            id: pinDetailLinkMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(pinDetailPopup.href)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: pinDetailPopup.detail.local === false && (pinDetailPopup.detail.msgId || "").length > 0
                        text: I18n.t("Tin nhắn gốc không có trong lịch sử đã tải; nội dung lấy từ danh sách ghim.",
                                     "The original message is not in the loaded history; the content comes from the pinboard.")
                        wrapMode: Text.Wrap
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.italic: true
                        color: ThemeManager.secondaryTextColor
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ZButton {
                            variant: "secondary"
                            text: I18n.t("Bỏ ghim", "Unpin")
                            onClicked: { ZaloClient.pins.unpin(pinDetailPopup.detail.id); pinDetailPopup.close(); }
                        }
                        Item { Layout.fillWidth: true }
                        ZButton {
                            visible: pinDetailPopup.detail.local === true
                            variant: "secondary"
                            text: I18n.t("Xem trong đoạn chat", "Show in chat")
                            onClicked: {
                                if (root.jumpToMessage(pinDetailPopup.detail.msgId || "", pinDetailPopup.detail.cliMsgId || "")) {
                                    pinDetailPopup.close();
                                    pinBar.expanded = false;
                                }
                            }
                        }
                        ZButton {
                            visible: pinDetailPopup.fullText.length > 0
                            variant: "primary"
                            text: pinDetailText.selectedText.length > 0 ? I18n.t("Sao chép đoạn chọn", "Copy selection")
                                                                         : I18n.t("Sao chép", "Copy")
                            onClicked: root.copyPinText(pinDetailText.selectedText.length > 0 ? pinDetailText.selectedText
                                                                                              : pinDetailPopup.fullText)
                        }
                    }
                }
            }
        }
    }

    function openPinDetail(topicId: string): void {
        if (topicId.length === 0) return;
        pinDetailPopupLoader.active = true;
        const host = pinDetailPopupLoader.item as PinDetailHost;
        host.topicId = "";
        host.topicId = topicId;
        root.popupOf(pinDetailPopupLoader).open();
    }

    function copyPinText(text: string): void {
        ZaloClient.copyText(text);
        ZaloClient.showNotice(I18n.t("Đã sao chép", "Copied"));
    }

    Loader {
        id: pinItemMenuLoader
        anchors.fill: parent
        active: false
        sourceComponent: PinMenuHost {
            id: pinMenuHost
            popup: pinItemMenu
            Popup {
                id: pinItemMenu
                margins: 8
                readonly property var topic: pinMenuHost.topic
                readonly property int topicIndex: pinMenuHost.topicIndex
                width: 200
                padding: 6
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle { color: ThemeManager.surfaceColor; radius: 8; border.color: ThemeManager.borderColor }
                contentItem: ColumnLayout {
                    spacing: 2
                    ZMenuRow {
                        visible: (pinItemMenu.topic.msgId || "").length > 0
                        icon: "forward"
                        label: I18n.t("Xem trong đoạn chat", "Show in chat")
                        onTriggered: {
                            pinItemMenu.close();
                            if (root.jumpToMessage(pinItemMenu.topic.msgId || "", pinItemMenu.topic.cliMsgId || ""))
                                pinBar.expanded = false;
                        }
                    }
                    ZMenuRow {
                        visible: (pinItemMenu.topic.title || "").length > 0
                        icon: "copy"
                        label: I18n.t("Sao chép", "Copy")
                        onTriggered: {
                            pinItemMenu.close();
                            root.copyPinText(ZaloClient.pins.detail(pinItemMenu.topic.id).text || pinItemMenu.topic.title);
                        }
                    }
                    ZMenuRow {
                        visible: pinItemMenu.topicIndex > 0
                        icon: "arrow_up"
                        label: I18n.t("Đưa lên đầu", "Pin to top")
                        onTriggered: { ZaloClient.pins.moveToTop(pinItemMenu.topic.id); pinItemMenu.close(); }
                    }
                    ZMenuRow {
                        icon: "pin"
                        danger: true
                        label: I18n.t("Bỏ ghim", "Unpin")
                        onTriggered: { ZaloClient.pins.unpin(pinItemMenu.topic.id); pinItemMenu.close(); }
                    }
                }
            }
        }
    }

    function openPinMenu(topic: var, index: int, anchorItem: Item): void {
        pinItemMenuLoader.active = true;
        const host = pinItemMenuLoader.item as PinMenuHost;
        host.topic = topic;
        host.topicIndex = index;
        const menu = root.popupOf(pinItemMenuLoader);
        const p = root.mapFromItem(anchorItem, anchorItem.width, anchorItem.height);
        root.placePopup(menu, p.x - Math.max(menu.width, menu.implicitWidth), p.y + 4);
        menu.open();
    }

    Loader {
        id: reactionPopupLoader
        anchors.fill: parent
        active: false
        sourceComponent: PopupHost {
            popup: reactionPopup
            Popup {
                id: reactionPopup
                focus: true
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
                                color: active ? ThemeManager.selectedColor : (tabMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor)
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
        }
    }

    component PinMoreButton: Rectangle {
        id: pinMoreBtn
        signal clicked()
        implicitWidth: 28
        implicitHeight: 28
        Layout.preferredWidth: 28
        Layout.preferredHeight: 28
        radius: 14
        color: pinMoreBtnMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
        ZIcon { anchors.centerIn: parent; name: "more_horiz"; size: 18; color: ThemeManager.iconColor }
        MouseArea {
            id: pinMoreBtnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pinMoreBtn.clicked()
        }
    }

    component PinTopicRow: Rectangle {
        id: topicRow
        property var topic: ({})
        property bool hoverable: true
        property bool showMore: true
        signal activated()
        signal moreClicked(Item anchorItem)

        readonly property int topicType: topicRow.topic.type || 2
        readonly property string thumb: topicRow.topic.thumb || ""
        readonly property bool thumbUsable: ZaloClient.brokenMediaRevision >= 0 && !ZaloClient.isMediaBroken(topicRow.thumb)

        function typeTitle(): string {
            if (topicRow.topicType === 1) return I18n.t("Ghi chú", "Note");
            if (topicRow.topicType === 3) return I18n.t("Bình chọn", "Poll");
            return I18n.t("Tin nhắn", "Message");
        }
        function typeColor(): color {
            if (topicRow.topicType === 1) return Theme.goldStar;
            if (topicRow.topicType === 3) return Theme.onlineGreen;
            return Theme.zaloBlue;
        }
        function contentText(): string {
            const title = topicRow.topic.title || "";
            let tag = "";
            switch (topicRow.topic.msgType || 0) {
            case 31: tag = I18n.t("[Tin nhắn thoại]", "[Voice message]"); break;
            case 32: tag = I18n.t("[Hình ảnh]", "[Photo]"); break;
            case 36: tag = I18n.t("[Sticker]", "[Sticker]"); break;
            case 37: tag = I18n.t("[Hình vẽ]", "[Doodle]"); break;
            case 43: tag = I18n.t("[Vị trí]", "[Location]"); break;
            case 44: tag = I18n.t("[Video]", "[Video]"); break;
            case 46: tag = I18n.t("[File]", "[File]"); break;
            case 49: tag = I18n.t("[GIF]", "[GIF]"); break;
            default: break;
            }
            if (tag.length === 0) return title.length > 0 ? title : I18n.t("[Tin nhắn]", "[Message]");
            return title.length > 0 ? tag + " " + title : tag;
        }

        color: topicRow.hoverable && topicRowHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
        HoverHandler { id: topicRowHover }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: topicRow.activated()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 10
            spacing: 12
            ZIcon {
                name: topicRow.topicType === 3 ? "poll" : (topicRow.topicType === 1 ? "file" : "chat")
                size: 20
                color: topicRow.typeColor()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: topicRow.typeTitle()
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        visible: text.length > 0
                        Layout.maximumWidth: 160
                        text: topicRow.topic.senderName ? topicRow.topic.senderName + ":" : ""
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                    Rectangle {
                        visible: topicRow.thumb.length > 0
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        radius: 4
                        clip: true
                        color: Theme.mediaPlaceholder
                        ZIcon {
                            anchors.centerIn: parent
                            visible: topicThumb.status !== Image.Ready
                            name: "image"
                            size: 12
                            color: ThemeManager.secondaryTextColor
                        }
                        Image {
                            id: topicThumb
                            anchors.fill: parent
                            source: topicRow.thumbUsable ? topicRow.thumb : ""
                            sourceSize.width: 36
                            sourceSize.height: 36
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: status === Image.Ready
                            onStatusChanged: if (status === Image.Error) ZaloClient.markMediaBroken(topicRow.thumb)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: topicRow.contentText()
                        textFormat: Text.PlainText
                        maximumLineCount: 1
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        color: ThemeManager.secondaryTextColor
                    }
                }
            }
            PinMoreButton {
                id: topicRowMore
                visible: topicRow.showMore
                onClicked: topicRow.moreClicked(topicRowMore)
            }
        }
    }

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

    Loader {
        id: quickMsgPopupLoader
        anchors.fill: parent
        active: false
        sourceComponent: PopupHost {
            popup: quickMsgPopup
            Popup {
                id: quickMsgPopup
                property bool formOpen: false
                property real editId: 0
                property string editKeyword: ""
                property string editText: ""
                onClosed: quickMsgPopup.formOpen = false
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

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        Layout.topMargin: 2
                        spacing: 6
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Tin nhắn nhanh", "Quick messages")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Rectangle {
                            width: 26
                            height: 26
                            radius: 13
                            color: qmAddMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: "plus"; size: 16; color: Theme.zaloBlue }
                            MouseArea {
                                id: qmAddMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.text: I18n.t("Tạo tin nhắn nhanh", "Create quick message")
                                onClicked: {
                                    quickMsgPopup.editId = 0;
                                    quickMsgPopup.editKeyword = "";
                                    quickMsgPopup.editText = "";
                                    quickMsgPopup.formOpen = !quickMsgPopup.formOpen;
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: ThemeManager.borderColor
                    }

                    ColumnLayout {
                        visible: quickMsgPopup.formOpen
                        Layout.fillWidth: true
                        Layout.leftMargin: 4
                        Layout.rightMargin: 4
                        spacing: 6
                        ZTextField {
                            id: qmKeywordField
                            Layout.fillWidth: true
                            placeholderText: I18n.t("Từ khóa (vd: chao)", "Keyword (e.g. hi)")
                            text: quickMsgPopup.editKeyword
                        }
                        ZTextField {
                            id: qmTextField
                            Layout.fillWidth: true
                            placeholderText: I18n.t("Nội dung tin nhắn", "Message content")
                            text: quickMsgPopup.editText
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Hủy", "Cancel")
                                onClicked: quickMsgPopup.formOpen = false
                            }
                            ZButton {
                                enabled: qmKeywordField.text.trim().length > 0 && qmTextField.text.trim().length > 0
                                text: quickMsgPopup.editId > 0 ? I18n.t("Lưu", "Save") : I18n.t("Tạo", "Create")
                                onClicked: {
                                    if (quickMsgPopup.editId > 0)
                                        ZaloClient.updateQuickMessage(quickMsgPopup.editId, qmKeywordField.text, qmTextField.text);
                                    else
                                        ZaloClient.createQuickMessage(qmKeywordField.text, qmTextField.text);
                                    quickMsgPopup.formOpen = false;
                                }
                            }
                        }
                    }

                    Text {
                        visible: ZaloClient.quickMessages.length === 0 && !quickMsgPopup.formOpen
                        Layout.fillWidth: true
                        Layout.margins: 4
                        wrapMode: Text.Wrap
                        text: I18n.t("Chưa có tin nhắn nhanh. Nhấn + để tạo, rồi gõ \"/\" + từ khóa trong ô soạn tin.",
                                     "No quick messages yet. Tap + to create one, then type \"/\" + keyword in the composer.")
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

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 4
                                spacing: 4
                                ColumnLayout {
                                    Layout.fillWidth: true
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
                                Rectangle {
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    radius: 13
                                    color: qmEditMouse.containsMouse ? ThemeManager.selectedColor : ThemeManager.hoverClearColor
                                    ZIcon { anchors.centerIn: parent; name: "edit"; size: 14; color: ThemeManager.secondaryTextColor }
                                    MouseArea {
                                        id: qmEditMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            quickMsgPopup.editId = qmRow.modelData.id;
                                            quickMsgPopup.editKeyword = qmRow.modelData.keyword;
                                            quickMsgPopup.editText = qmRow.modelData.text;
                                            quickMsgPopup.formOpen = true;
                                        }
                                    }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    radius: 13
                                    color: qmDelMouse.containsMouse ? ThemeManager.selectedColor : ThemeManager.hoverClearColor
                                    ZIcon { anchors.centerIn: parent; name: "trash"; size: 14; color: Theme.badgeRed }
                                    MouseArea {
                                        id: qmDelMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: ZaloClient.deleteQuickMessage(qmRow.modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: reminderPopupLoader
        anchors.fill: parent
        active: false
        sourceComponent: PopupHost {
            popup: reminderPopup
            Popup {
                id: reminderPopup
                onAboutToShow: scheduleContentInput.text = msgInput.text
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
                                color: schedMouse.containsMouse ? ThemeManager.inputFillHoverColor : ThemeManager.inputFillColor
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
        }
    }

    Loader {
        id: messageMenuLoader
        anchors.fill: parent
        active: false
        sourceComponent: PopupHost {
            popup: messageMenu
            Popup {
                id: messageMenu
                margins: 8
                width: Math.max(230, implicitContentWidth + leftPadding + rightPadding)
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
        }
    }

    FileDialog {
        id: saveImageDialog
        title: I18n.t("Lưu ảnh", "Save picture")
        fileMode: FileDialog.SaveFile
        currentFile: "file:///" + ZaloClient.suggestedMediaFileName(root.menuMessage.url || "")
        onAccepted: ZaloClient.saveMediaTo(root.menuMessage.url, selectedFile.toString())
    }

    FileDialog {
        id: imageFileDialog
        title: I18n.t("Chọn hình ảnh để gửi", "Select photo to send")
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Hình ảnh (*.png *.jpg *.jpeg *.gif *.webp *.bmp)", "Tất cả các tệp (*.*)"]
        onAccepted: {
            for (var i = 0; i < selectedFiles.length; i++) {
                root.addDraftFile(selectedFiles[i].toString());
            }
        }
    }

    FileDialog {
        id: documentFileDialog
        title: I18n.t("Chọn tệp tin đính kèm", "Select document to attach")
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Tất cả các tệp (*.*)"]
        onAccepted: {
            for (var j = 0; j < selectedFiles.length; j++) {
                root.addDraftFile(selectedFiles[j].toString());
            }
        }
    }
}
