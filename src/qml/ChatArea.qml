pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Templates as T
import Zalos
import "MediaSource.js" as MediaSource

Rectangle {
    id: root
    readonly property bool compactWindow: (Window.window ? Window.window.width : 1000) < 720
    readonly property bool shortWindow: (Window.window ? Window.window.height : 800) < 600
    readonly property bool windowShowing: UiWindow.showing
    objectName: "chatArea"
    color: ThemeManager.chatBackgroundColor

    property bool showInChatSearch: false
    property bool isFormattingActive: false
    property bool isEditorExpanded: false
    onIsFormattingActiveChanged: {
        if (messageList.stickToBottom || messageList.distanceToEnd < 48) {
            Qt.callLater(root.followEnd);
        }
    }
    onIsEditorExpandedChanged: {
        if (messageList.stickToBottom || messageList.distanceToEnd < 48) {
            Qt.callLater(root.followEnd);
        }
    }
    function followEnd(): void {
        if (messageList && typeof messageList.scrollToEnd === "function") messageList.scrollToEnd();
    }
    function dayLabel(ts: real): string {
        if (!ts || ts < 946684800000) return "";
        const d = new Date(ts);
        const today = new Date();
        today.setHours(0, 0, 0, 0);
        const day = new Date(d);
        day.setHours(0, 0, 0, 0);
        const diff = Math.round((today - day) / 86400000);
        if (diff === 0) return I18n.t("Hôm nay", "Today");
        if (diff === 1) return I18n.t("Hôm qua", "Yesterday");
        return Qt.formatDate(d, "dd/MM/yyyy");
    }

    property real floatingDateTs: -1
    function updateFloatingDate(distance: real): void {
        if (distance <= 48 || messageList.count === 0) {
            floatingDatePill.shown = false;
            return;
        }
        const viewTop = messageList.contentY + floatingDatePill.anchors.topMargin;
        const row = messageList.itemAt(messageList.width / 2, viewTop + floatingDatePill.height + 4);
        if (!row) return;
        const ts = row.ts || 0;
        if (ts !== root.floatingDateTs) {
            const label = root.dayLabel(ts);
            if (label.length === 0) return;
            root.floatingDateTs = ts;
            floatingDatePill.label = label;
        }
        const separatorAtTop = row.dateSeparator && row.y >= viewTop - 8;
        floatingDatePill.shown = !separatorAtTop;
        floatingDateTimer.restart();
    }

    function followEndIfSticky(): void {
        if (messageList && messageList.stickToBottom) root.followEnd();
    }
    readonly property int infoPanelWidth: root.compactWindow ? width : Math.min(280, width)
    readonly property bool infoOverlay: ZaloClient.showConversationInfo && (root.compactWindow || width < 620)
    readonly property bool headerOverflow: root.compactWindow
    readonly property bool headerSearchOverflow: width < 420
    readonly property bool composerOverflow: width < 480
    readonly property int composerLineHeight: Math.ceil(composerMetrics.height)
    readonly property real composerMinTextHeight: composerLineHeight * (root.shortWindow ? 1 : 3) + 16
    readonly property real composerMaxTextHeight: composerLineHeight * (root.shortWindow ? 4 : 8) + 16
    FontMetrics { id: composerMetrics; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSubtitle }
    property list<string> searchHits: []
    property var searchHitDetails: []
    property int searchCursor: -1
    readonly property string searchFocusId: root.searchCursor >= 0 && root.searchCursor < root.searchHits.length
                                            ? root.searchHits[root.searchCursor] : ""

    function showSearchHit(): void {
        const id = root.searchFocusId;
        if (id.length === 0 || !ZaloClient.revealMessage(id)) return;
        messageList.stickToBottom = false;
        root.revealMessageAtRow(ZaloClient.messageModel.rowOfMessage(id));
        root.flashMessage(id);
    }
    property string menuMessageId: ""
    property string menuMessageText: ""
    property bool menuMessageIsMine: false
    property string menuMessageType: ""
    property string menuMessageUrl: ""

    function runSearch(): void {
        const pre = inChatSearchInput.preeditText;
        const pos = Math.max(0, Math.min(inChatSearchInput.cursorPosition, inChatSearchInput.text.length));
        const q = (pre && pre.length > 0)
                  ? inChatSearchInput.text.substring(0, pos) + pre + inChatSearchInput.text.substring(pos)
                  : inChatSearchInput.text;
        const details = ZaloClient.searchConversationHits(q);
        const ids = [];
        for (let i = 0; i < details.length; ++i) ids.push(details[i].msgId);
        root.searchHitDetails = details;
        root.searchHits = ids;
        root.searchCursor = root.searchHits.length > 0 ? root.searchHits.length - 1 : -1;
        if (root.searchHits.length === 0) searchResultsPopup.close();
        root.showSearchHit();
    }

    function searchHitDate(ts: real): string {
        if (ts < 946684800000) return "";
        const when = new Date(ts);
        return when.toDateString() === new Date().toDateString() ? Qt.formatTime(when, "HH:mm")
                                                                 : Qt.formatDate(when, "dd/MM/yy");
    }

    function openSearchHitAt(listIndex: int): void {
        const cursor = root.searchHits.length - 1 - listIndex;
        if (cursor < 0 || cursor >= root.searchHits.length) return;
        root.searchCursor = cursor;
        searchResultsPopup.close();
        root.showSearchHit();
    }

    function stepSearch(delta: int): void {
        if (root.searchHits.length === 0) return;
        root.searchCursor = (root.searchCursor + delta + root.searchHits.length) % root.searchHits.length;
        root.showSearchHit();
    }

    function openSearch(): void {
        root.foundMsgId = "";
        root.showInChatSearch = true;
        inChatSearchInput.forceActiveFocus();
        inChatSearchInput.selectAll();
    }

    function closeSearch(): void {
        searchResultsPopup.close();
        inChatSearchInput.text = "";
        root.searchHitDetails = [];
        root.searchHits = [];
        root.searchCursor = -1;
        root.showInChatSearch = false;
    }

    function handleEscape(): bool {
        if (root.selectionMode) { root.exitSelection(); return true; }
        if (searchResultsPopup.visible) { searchResultsPopup.close(); return true; }
        if (root.showInChatSearch) { root.closeSearch(); return true; }
        if (ZaloClient.replyTarget.id !== undefined) { ZaloClient.clearReplyTarget(); return true; }
        return false;
    }

    function compactBack(): bool {
        if (!mainWindow.isChatFocused || mainWindow.anyModalOpen || ZaloClient.appLock.locked || ImageViewerState.open) return false;
        if (ZaloClient.showConversationInfo) {
            ZaloClient.showConversationInfo = false;
            return true;
        }
        ZaloClient.openWelcomeScreen();
        return true;
    }

    Keys.onEscapePressed: (event) => { event.accepted = root.compactBack(); }

    property string reactionMsgId: ""
    property var reactionInfo: ({ total: 0, known: false, groups: [], people: [] })
    property string reactionTab: ""
    function openReactionDetails(msgId: string, anchor: Item): void {
        root.reactionMsgId = msgId;
        root.reactionTab = "";
        root.reactionInfo = ZaloClient.reactionDetails(msgId);
        reactionPopup.open();
    }

    property list<string> seenDetailsUids: []
    function openSeenDetails(uids: list<string>): void {
        root.seenDetailsUids = uids || [];
        seenPopup.open();
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
        const info = ZaloClient.messageModel.getMessageById(msg.id || "");
        if (info.hasMenu !== true) return;
        messageMenu.canReact = info.canReact === true;
        messageMenu.canReply = info.canReply === true;
        messageMenu.canForward = info.canForward === true;
        messageMenu.canCopy = info.canCopy === true;
        root.menuMessageId = msg.id || "";
        root.menuMessageText = msg.text || "";
        root.menuMessageIsMine = msg.isMine === true;
        root.menuMessageType = msg.messageType || "";
        root.menuMessageUrl = msg.url || "";
        messageMenu.canModerate = ZaloClient.canModerateCurrent();
        const p = root.mapFromItem(item, mx, my);
        root.placePopup(messageMenu, p.x, p.y);
        messageMenu.open();
    }

    property string draftConvId: ""
    property double convSwitchAt: 0
    property bool selectionMode: false
    property list<string> selectedIds: []

    function enterSelection(msgId: string): void {
        root.selectedIds = msgId.length > 0 ? [msgId] : [];
        root.selectionMode = true;
    }
    function exitSelection(): void {
        root.selectionMode = false;
        root.selectedIds = [];
    }
    function toggleSelected(msgId: string): void {
        const next = root.selectedIds.slice();
        const at = next.indexOf(msgId);
        if (at >= 0) next.splice(at, 1);
        else if (next.length < 100) next.push(msgId);
        root.selectedIds = next;
    }

    Timer {
        id: scrollAnchorTimer
        interval: 400
        onTriggered: {
            const convId = ZaloClient.currentConversationId;
            if (convId.length === 0 || Date.now() - root.convSwitchAt < 1000) return;
            if (messageList.stickToBottom || messageList.distanceToEnd < 48) {
                ZaloClient.setScrollAnchor(convId, "");
                return;
            }
            const idx = messageList.indexAt(20, messageList.contentY + messageList.topMargin + 4);
            const item = idx >= 0 ? messageList.itemAtIndex(idx) : null;
            if (item && item.msgId) ZaloClient.setScrollAnchor(convId, item.msgId);
        }
    }
    Connections {
        target: messageList
        function onContentYChanged(): void { scrollAnchorTimer.restart(); }
    }
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
        ZaloClient.composer.resetPreedit();
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
        root.revealMessageAtRow(row);
        root.flashMessage(ZaloClient.messageModel.messageIdAt(row) || msgId);
        return true;
    }
    function flashMessage(msgId: string): void {
        root.flashMsgId = "";
        root.flashMsgId = msgId;
        flashTimer.restart();
    }
    Timer { id: flashTimer; interval: 900; onTriggered: root.flashMsgId = "" }
    property string foundMsgId: ""
    function revealSearchResult(msgId: string): void {
        if (root.jumpToMessage(msgId, "")) root.foundMsgId = ZaloClient.messageModel.messageIdAt(ZaloClient.messageModel.rowOfMessage(msgId)) || msgId;
    }

    function revealMessageAtRow(row: int): void {
        root.revealMessageAttempt(row, 0);
    }
    function revealMessageAttempt(row: int, attempt: int): void {
        if (row < 0 || row >= messageList.count) return;
        const item = messageList.itemAtIndex(row);
        if (!item) {
            if (attempt >= 5) return;
            messageList.positionViewAtIndex(row, ListView.Center);
            Qt.callLater(() => root.revealMessageAttempt(row, attempt + 1));
            return;
        }
        const overlayTop = (pinBar.visible ? pinBar.height : 0) + (root.showInChatSearch ? 42 + 12 : 0) + 12;
        const visibleHeight = Math.max(0, messageList.height - overlayTop - messageList.bottomMargin);
        const offset = Math.min(Math.max(0, (visibleHeight - item.height) / 2), visibleHeight / 3);
        const minY = messageList.originY - messageList.topMargin;
        const maxY = Math.max(minY, messageList.originY + messageList.contentHeight + messageList.bottomMargin
                                    - messageList.height);
        messageList.contentY = Math.max(minY, Math.min(item.y - overlayTop - offset, maxY));
        if (attempt < 2) Qt.callLater(() => root.revealMessageAttempt(row, attempt + 1));
    }

    function revealReactionTarget(msgId: string): bool {
        ZaloClient.revealMessage(msgId);
        const row = ZaloClient.messageModel.rowOfMessage(msgId);
        ZaloClient.markReactionsSeen(msgId);
        if (row < 0) return false;
        messageList.stickToBottom = false;
        root.revealMessageAtRow(row);
        root.flashMessage(msgId);
        return true;
    }
    function goToNextUnreadReaction(): void {
        for (let attempt = 0; attempt < 20; ++attempt) {
            const id = ZaloClient.nextUnreadReactionMsgId();
            if (id.length === 0 || root.revealReactionTarget(id)) return;
        }
    }
    Connections {
        target: ZaloClient.messageSearch
        function onCountChanged(): void {
            if (ZaloClient.messageSearch.count === 0) root.foundMsgId = "";
        }
    }
    Connections {
        target: ZaloClient
        function onReactionJumpRequested(msgId: string): void {
            root.revealReactionTarget(msgId);
        }
    }

    function jumpToBottom(): void {
        messageList.stickToBottom = true;
        messageList.scrollToEnd();
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

    function composerComposedText(): string {
        const t = msgInput.text;
        const pre = msgInput.preeditText;
        if (!pre || pre.length === 0) return t;
        const pos = Math.max(0, Math.min(msgInput.cursorPosition, t.length));
        return t.substring(0, pos) + pre + t.substring(pos);
    }

    function handleComposerSubmit(): void {
        if (root.suggestItems.length > 0) {
            root.acceptSuggestion(root.suggestIndex);
            return;
        }
        root.sendComposer();
    }

    function handleComposerReturn(event: var): void {
        event.accepted = true;
        ZaloClient.composer.flushPreedit();
        if (event.modifiers & Qt.ShiftModifier) {
            msgInput.insert(msgInput.cursorPosition, "\n");
            return;
        }
        root.handleComposerSubmit();
    }

    function sendComposer(): void {
        ZaloClient.composer.flushPreedit();
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
                const urls = [];
                for (let i = 0; i < attCount; ++i) urls.push(draftAttachmentsModel.get(i).url);
                ZaloClient.sendPhotoAlbum(urls);
            }
            draftAttachmentsModel.clear();
        } else {
            const mentions = ZaloClient.currentConversationIsGroup ? root.collectMentions(text) : [];
            ZaloClient.sendComposedMessage(text, mentions, ZaloClient.composer.styles());
        }
        ZaloClient.composer.resetPreedit();
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
    property string lastSuggestText: ""
    property int lastSuggestCursor: -1

    function foldText(t: string): string {
        return t.toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/đ/g, "d");
    }

    function closeSuggestions(): void {
        if (root.suggestMode === "" && root.suggestItems.length === 0 && root.suggestStart < 0) return;
        root.suggestMode = "";
        root.suggestItems = [];
        root.suggestStart = -1;
        root.suggestIndex = 0;
        root.lastSuggestText = "";
        root.lastSuggestCursor = -1;
    }

    function updateSuggestions(): void {
        const composed = root.composerComposedText();
        const curPos = msgInput.cursorPosition + (msgInput.preeditText ? msgInput.preeditText.length : 0);
        if (composed === root.lastSuggestText && curPos === root.lastSuggestCursor) return;
        root.lastSuggestText = composed;
        root.lastSuggestCursor = curPos;
        const before = composed.substring(0, curPos);
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
                const memberFolded = (m.foldedName !== undefined && m.foldedName !== null) ? m.foldedName : root.foldText(m.name);
                if (q.length === 0 || memberFolded.indexOf(q) >= 0) {
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
        const t = root.composerComposedText();
        const cursor = msgInput.cursorPosition + (msgInput.preeditText ? msgInput.preeditText.length : 0);
        ZaloClient.composer.resetPreedit();
        if (root.suggestMode === "mention") {
            const label = "@" + item.name;
            const newPos = root.suggestStart + label.length + 1;
            msgInput.text = t.substring(0, root.suggestStart) + label + " " + t.substring(cursor);
            Qt.callLater(() => {
                msgInput.cursorPosition = newPos;
            });
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

    Shortcut {
        sequences: ZaloClient.shortcuts.keySequences("chat.find")
        enabled: root.visible
        onActivated: root.openSearch()
    }
    Shortcut {
        sequence: "Esc"
        enabled: root.visible && !mainWindow.anyModalOpen && (root.selectionMode || root.showInChatSearch || ZaloClient.replyTarget.id !== undefined)
        onActivated: root.handleEscape()
    }
    Connections {
        target: ZaloClient
        function onRevealRequested(msgId: string): void { Qt.callLater(root.revealSearchResult, msgId); }
        function onSwipeBackRequested(): void { root.compactBack(); }
        function onCurrentConversationChanged(): void {
            root.foundMsgId = "";
            if (ZaloClient.currentConversationId.length > 0 && ZaloClient.currentConversationId === root.draftConvId) return;
            root.closeSearch();
            root.exitSelection();
            root.mentionPicks = [];
            root.closeSuggestions();
            draftAttachmentsModel.clear();
            root.swapDraft();
            root.newBelow = 0;
            pinBar.expanded = false;
            const markerRow = ZaloClient.messageModel.unreadMarkerRow();
            root.convSwitchAt = Date.now();
            const anchorId = markerRow < 0 ? ZaloClient.scrollAnchor(ZaloClient.currentConversationId) : "";
            const anchorRow = anchorId.length > 0 ? ZaloClient.messageModel.rowOfMessage(anchorId) : -1;
            messageList.stickToBottom = markerRow < 0 && anchorRow < 0;
            messageList.endReached = false;
            if (markerRow >= 0) Qt.callLater(() => messageList.positionViewAtIndex(markerRow, ListView.Beginning));
            else if (anchorRow >= 0) Qt.callLater(() => messageList.positionViewAtIndex(anchorRow, ListView.Beginning));
        }
        function onNavigationTabChanged(): void {
            if (ZaloClient.currentNavigationTab !== "messages" || root.draftConvId.length === 0) return;
            const d = ZaloClient.draftFor(root.draftConvId);
            if (d !== msgInput.text) {
                root.loadingDraft = true;
                msgInput.text = d;
                msgInput.cursorPosition = msgInput.text.length;
                root.loadingDraft = false;
            }
        }
        function onGroupMembersChanged(groupId: string) {
            if (root.suggestMode === "mention" && groupId === ZaloClient.currentConversationId) {
                root.lastSuggestText = "";
                root.lastSuggestCursor = -1;
                root.updateSuggestions();
            }
        }
        function onClipboardAttachmentsReady(items: var): void {
            if (!items) return;
            for (let i = 0; i < items.length; ++i) root.addDraftAttachment(items[i]);
        }
    }

    Connections {
        target: ZaloClient.composer
        function onSubmitRequested() { root.handleComposerSubmit(); }
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

    function changeIndent(delta: int): void {
        ZaloClient.composer.changeIndent(delta, msgInput.selectionStart, msgInput.selectionEnd);
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

                    ZIconButton {
                        id: btnBack
                        visible: root.compactWindow
                        iconName: "arrow_left"
                        iconSize: 16
                        tooltip: I18n.t("Quay lại danh sách hội thoại", "Back to conversations")
                        tooltipDelay: 0
                        onClicked: ZaloClient.openWelcomeScreen()
                    }

                    ZAvatar {
                        id: headerAvatar
                        size: 40
                        avatarUrl: ZaloClient.currentConversationAvatar
                        name: ZaloClient.currentConversationName
                        isOnline: ZaloClient.currentConversationIsOnline
                        isGroup: ZaloClient.currentConversationIsGroup
                        isVerified: ZaloClient.currentConversationIsOfficial || ZaloClient.currentConversationIsCloud
                        memberCount: ZaloClient.currentConversationMemberCount
                        memberAvatars: ZaloClient.currentConversationIsGroup ? ZaloClient.groupComposite(ZaloClient.currentConversationId, ZaloClient.currentConversationMemberCount) : []
                        showStatusDot: !ZaloClient.currentConversationIsGroup && !ZaloClient.currentConversationIsOfficial && !ZaloClient.currentConversationIsCloud

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
                            textFormat: Text.PlainText
                            font.family: Theme.fontFamily
                            font.weight: Font.DemiBold
                            font.pixelSize: Theme.fontSubtitle
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
                            Layout.fillWidth: true
                            spacing: 5
                            visible: !ZaloClient.currentConversationIsCloud && (ZaloClient.currentConversationIsGroup || ZaloClient.currentConversationIsOnline
                                     || ZaloClient.currentConversationStatus.length > 0 || ZaloClient.currentConversationHasPresence)

                            ZIcon {
                                visible: !ZaloClient.currentConversationIsGroup && ZaloClient.currentConversationIsOnline
                                name: "dot"
                                size: 7
                                color: Theme.onlineGreen
                            }

                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
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
                                font.pixelSize: Theme.fontCaption
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

                        ZIconButton {
                            id: btnAddMember
                            visible: ZaloClient.currentConversationCanCreateGroup && !root.headerOverflow
                            iconName: "user_add"
                            iconSize: 20
                            tooltip: ZaloClient.currentConversationIsGroup
                                ? I18n.t("Thêm thành viên vào nhóm", "Add member to group")
                                : I18n.t("Tạo nhóm với người này", "Create group with this person")
                            onClicked: ZaloClient.currentConversationIsGroup ? ZaloClient.openAddMemberModal() : ZaloClient.openCreateGroup()
                        }

                        ZIconButton {
                            id: btnSearch
                            visible: !root.headerSearchOverflow
                            iconName: "search"
                            iconColor: root.showInChatSearch ? Theme.zaloBlue : ThemeManager.textColor
                            highlighted: root.showInChatSearch
                            pressTint: false
                            tooltip: I18n.t("Tìm kiếm trong tin nhắn", "Search messages")
                            onClicked: root.showInChatSearch ? root.closeSearch() : root.openSearch()
                        }

                        ZIconButton {
                            id: btnVoiceCall
                            visible: ZaloClient.currentConversationCanCall
                            iconName: "phone"
                            tooltip: ZaloClient.currentConversationIsGroup
                                ? I18n.t("Cuộc gọi thoại nhóm", "Group voice call")
                                : I18n.t("Cuộc gọi thoại", "Voice call")
                            onClicked: {
                                if (ZaloClient.currentConversationIsGroup) ZaloClient.startGroupCall(false);
                                else ZaloClient.startVoiceCall();
                            }
                        }

                        ZIconButton {
                            id: btnVideoCall
                            visible: ZaloClient.currentConversationCanCall
                            iconName: "video"
                            tooltip: ZaloClient.currentConversationIsGroup
                                ? I18n.t("Cuộc gọi video nhóm", "Group video call")
                                : I18n.t("Cuộc gọi video", "Video call")
                            onClicked: {
                                if (ZaloClient.currentConversationIsGroup) ZaloClient.startGroupCall(true);
                                else ZaloClient.startVideoCall();
                            }
                        }

                        ZIconButton {
                            id: btnInfo
                            visible: !root.headerOverflow
                            iconName: "sidebar_panel"
                            iconColor: ZaloClient.showConversationInfo ? Theme.zaloBlue : ThemeManager.textColor
                            highlighted: ZaloClient.showConversationInfo
                            pressTint: false
                            tooltip: I18n.t("Thông tin hội thoại", "Conversation info")
                            onClicked: ZaloClient.toggleConversationInfo()
                        }

                        ZIconButton {
                            id: btnHeaderMore
                            objectName: "headerMoreButton"
                            visible: root.headerOverflow
                            iconName: "more_horiz"
                            tooltip: I18n.t("Thêm", "More")
                            onClicked: {
                                const p = root.mapFromItem(btnHeaderMore, btnHeaderMore.width, btnHeaderMore.height);
                                root.placePopup(headerMoreMenu, p.x - Math.max(headerMoreMenu.width, headerMoreMenu.implicitWidth), p.y + 4);
                                headerMoreMenu.open();
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: inChatSearchBar
                objectName: "inChatSearchBar"
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
                        objectName: "inChatSearchInput"
                        Layout.fillWidth: true
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        clip: true

                        onTextChanged: searchDebounce.restart()
                        onPreeditTextChanged: searchDebounce.restart()
                        Keys.onReturnPressed: function(event) {
                            event.accepted = true;
                            Qt.inputMethod.commit();
                            root.stepSearch((event.modifiers & Qt.ShiftModifier) ? 1 : -1);
                        }
                        Keys.onEnterPressed: function(event) {
                            event.accepted = true;
                            Qt.inputMethod.commit();
                            root.stepSearch((event.modifiers & Qt.ShiftModifier) ? 1 : -1);
                        }
                        Keys.onEscapePressed: {
                            if (searchResultsPopup.visible) searchResultsPopup.close();
                            else root.closeSearch();
                        }
                        Timer { id: searchDebounce; interval: 200; onTriggered: root.runSearch() }

                        Text {
                            text: I18n.t("Tìm tin nhắn trong cuộc trò chuyện (không cần dấu)...",
                                         "Search messages in this conversation...")
                            color: ThemeManager.secondaryTextColor
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            visible: !inChatSearchInput.text && !inChatSearchInput.preeditText && !inChatSearchInput.activeFocus
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        visible: inChatSearchInput.text.trim().length > 0 || inChatSearchInput.preeditText.trim().length > 0
                        text: root.searchHits.length === 0
                              ? I18n.t("Không có kết quả trong tin đã tải", "No results in loaded messages")
                              : (root.searchHits.length - root.searchCursor) + "/" + root.searchHits.length
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
                        color: root.searchHits.length === 0 ? ThemeManager.secondaryTextColor : Theme.zaloBlue
                    }

                    Repeater {
                        model: [ { icon: "arrow_up", step: -1, vi: "Kết quả trước", en: "Previous result" },
                                 { icon: "arrow_down", step: 1, vi: "Kết quả sau", en: "Next result" } ]
                        Rectangle {
                            id: searchStepBtn
                            required property var modelData
                            visible: root.searchHits.length > 1
                            width: 24
                            height: 24
                            radius: 12
                            color: stepMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: searchStepBtn.modelData.icon; size: 14; color: ThemeManager.secondaryTextColor }
                            MouseArea {
                                id: stepMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 400
                                ToolTip.text: I18n.t(searchStepBtn.modelData.vi, searchStepBtn.modelData.en)
                                onClicked: root.stepSearch(searchStepBtn.modelData.step)
                            }
                        }
                    }

                    ZIconButton {
                        objectName: "searchResultsButton"
                        visible: root.searchHits.length > 0
                        width: 24
                        height: 24
                        radius: 12
                        iconName: "eye"
                        iconSize: 16
                        highlighted: searchResultsPopup.visible
                        iconColor: searchResultsPopup.visible ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                        tooltip: searchResultsPopup.visible ? I18n.t("Ẩn danh sách kết quả", "Hide result list")
                                                            : I18n.t("Xem danh sách kết quả", "Show result list")
                        onClicked: {
                            if (searchResultsPopup.visible) searchResultsPopup.close();
                            else searchResultsPopup.open();
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

                Popup {
                    id: searchResultsPopup
                    objectName: "searchResultsPopup"
                    y: inChatSearchBar.height
                    width: inChatSearchBar.width
                    height: Math.min(resultsHeader.implicitHeight + searchResultsList.contentHeight + 12,
                                     Math.max(160, root.height * 0.55))
                    padding: 0
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    background: Rectangle {
                        color: ThemeManager.surfaceColor
                        border.color: ThemeManager.borderColor
                        border.width: 1
                    }
                    onOpened: searchResultsList.positionViewAtIndex(Math.max(0, root.searchHits.length - 1 - root.searchCursor),
                                                                     ListView.Contain)

                    contentItem: ColumnLayout {
                        spacing: 0

                        Text {
                            id: resultsHeader
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            Layout.topMargin: 8
                            Layout.bottomMargin: 4
                            text: I18n.t("%1 kết quả trong cuộc trò chuyện", "%1 results in this conversation").arg(root.searchHits.length)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            color: ThemeManager.secondaryTextColor
                        }

                        ListView {
                            id: searchResultsList
                            objectName: "searchResultsList"
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: searchResultsPopup.visible ? root.searchHitDetails.length : 0
                            delegate: Rectangle {
                                id: resultRow
                                required property int index
                                readonly property var hit: root.searchHitDetails[root.searchHitDetails.length - 1 - resultRow.index] || ({})
                                readonly property bool current: root.searchHits.length - 1 - root.searchCursor === resultRow.index
                                width: ListView.view.width
                                height: 60
                                color: resultRow.current ? ThemeManager.selectedColor
                                       : (resultHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)
                                HoverHandler { id: resultHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: root.openSearchHitAt(resultRow.index) }
                                SearchHitRow {
                                    anchors.fill: parent
                                    avatarUrl: resultRow.hit.avatar || ""
                                    title: resultRow.hit.senderName || ""
                                    subtitle: resultRow.hit.text || ""
                                    dateText: root.searchHitDate(resultRow.hit.ts || 0)
                                }
                            }
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
                    readonly property string shortcutKey: ZaloClient.shortcuts.keys("privacy.antiPeeking")[0] || "Ctrl+Shift+H"
                    text: I18n.t("Đang bật chống nhìn lén · rê chuột vào tin nhắn để xem (%1 để tắt)",
                                 "Anti-peeking is on · hover a message to read it (%1 to turn off)").arg(shortcutKey)
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.zaloBlueTonalText
                }
                Text {
                    text: I18n.t("Tắt", "Turn off")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
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
                visible: messageList.count === 0 && !ZaloClient.isLoadingHistory
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
                        font.weight: Font.DemiBold
                        font.pixelSize: Theme.fontTitle
                        color: ThemeManager.textColor
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: I18n.t("Chưa có tin nhắn nào trong cuộc trò chuyện này.", "No messages in this conversation yet.")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
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
                            Text { text: "👋"; font.pixelSize: Theme.fontBodyLarge }
                            Text {
                                text: I18n.t("Gửi lời chào", "Say hello")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.DemiBold
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

                    ZButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: ZaloClient.currentConversationIsGroup
                                 && !ZaloClient.currentConversationIsCloud
                                 && !ZaloClient.isLoadingHistory
                                 && ZaloClient.mobileSyncState !== 4
                        variant: "tonal"
                        text: I18n.t("Đồng bộ tin nhắn từ điện thoại", "Sync messages from phone")
                        onClicked: ZaloClient.acceptSyncPrompt()
                    }
                }
            }

            DropArea {
                id: dropArea
                anchors.fill: parent
                z: 50
                keys: ["text/uri-list", "text/plain"]
                onDropped: function(drop) {
                    if (drop.hasUrls && drop.urls.length > 0) {
                        for (let i = 0; i < drop.urls.length; ++i) root.addDraftFile(drop.urls[i].toString());
                    } else if (drop.hasText && drop.text.length > 0) {
                        msgInput.insert(msgInput.cursorPosition, drop.text);
                    }
                    msgInput.forceActiveFocus();
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
                        text: I18n.t("Thả tệp, ảnh hoặc chữ vào đây để thêm vào tin nhắn", "Drop files, photos or text here to add them to your message")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitleLarge
                        font.weight: Font.DemiBold
                        color: Theme.zaloBlue
                    }
                }
            }

            Image {
                id: bgImage
                parent: messageList.parent
                z: -1
                anchors.fill: messageList
                clip: true
                visible: status === Image.Ready && ZaloClient.currentConversationBackground.length > 0
                opacity: ZaloClient.chatBackgroundOpacity
                source: ZaloClient.currentConversationBackground.length > 0
                        ? "image://cover/" + encodeURIComponent(ZaloClient.currentConversationBackground) : ""
                sourceSize.width: 96
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }

            ChatJumpDownButton {
                id: jumpDownButton
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

            ChatReactionJumpButton {
                id: mentionJumpButton
                objectName: "mentionJumpButton"
                parent: messageList.parent
                z: 10
                anchors.right: messageList.right
                anchors.bottom: messageList.bottom
                anchors.rightMargin: 16
                anchors.bottomMargin: (jumpDownButton.shown ? 14 + jumpDownButton.height + 18 : 14)
                                      + (reactionJumpButton.shown ? reactionJumpButton.height + 12 : 0)
                count: ZaloClient.currentMentionIds.length
                emoji: "@"
                shown: messageList.count > 0 && ZaloClient.currentMentionIds.length > 0
                onClicked: {
                    const id = ZaloClient.currentMentionIds[0];
                    root.jumpToMessage(id, "");
                    ZaloClient.consumeMention(id);
                }
            }

            ChatReactionJumpButton {
                id: reactionJumpButton
                parent: messageList.parent
                z: 10
                anchors.right: messageList.right
                anchors.bottom: messageList.bottom
                anchors.rightMargin: 16
                anchors.bottomMargin: jumpDownButton.shown ? 14 + jumpDownButton.height + 18 : 14
                count: ZaloClient.currentUnreadReactionCount
                emoji: ZaloClient.currentUnreadReactionIcon
                shown: messageList.count > 0 && ZaloClient.currentUnreadReactionCount > 0
                onClicked: root.goToNextUnreadReaction()
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
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.Normal
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
                                font.pixelSize: Theme.fontBodyLarge
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
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.Normal
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
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
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

            Rectangle {
                id: floatingDatePill
                objectName: "floatingDatePill"
                property string label: ""
                property bool shown: false
                z: 5
                anchors.top: messageList.top
                anchors.topMargin: pinBar.visible ? 56 + 8 : 8
                anchors.horizontalCenter: messageList.horizontalCenter
                width: floatingDateText.implicitWidth + 20
                height: 22
                radius: 10
                color: Theme.timestampPill
                opacity: shown ? 1 : 0
                visible: opacity > 0 && label.length > 0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                Text {
                    id: floatingDateText
                    anchors.centerIn: parent
                    text: floatingDatePill.label
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }

                Timer {
                    id: floatingDateTimer
                    interval: 1200
                    onTriggered: floatingDatePill.shown = false
                }
            }

            ListView {
                id: messageList
                objectName: "messageList"
                anchors.fill: parent
                clip: true
                model: ZaloClient.messageModel
                reuseItems: true
                cacheBuffer: 280
                spacing: 0
                topMargin: pinBar.visible ? 16 + 56 : 16
                bottomMargin: 5

                ScrollBar.vertical: ScrollBar {
                    id: verticalScrollBar
                    objectName: "messageScrollBar"
                    policy: ScrollBar.AsNeeded
                }

                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 150; easing.type: Easing.OutQuad }
                }

                property bool autoScrolling: false
                function scrollToEnd(): void {
                    messageList.autoScrolling = true;
                    messageList.positionViewAtEnd();
                    const end = messageList.originY + messageList.contentHeight + messageList.bottomMargin - messageList.height;
                    if (end > messageList.contentY) messageList.contentY = end;
                    messageList.autoScrolling = false;
                    messageList.stickToBottom = true;
                    messageList.endReached = true;
                }

                property bool stickToBottom: true
                property bool endReached: false
                property int countBeforeReset: 0
                readonly property real distanceToEnd: Math.max(0, originY + contentHeight + bottomMargin - height - contentY)
                property real lastContentY: 0
                property bool resetting: false
                onContentYChanged: {
                    const previous = lastContentY;
                    lastContentY = contentY;
                    if (autoScrolling || resetting || count === 0) return;
                    const distance = Math.max(0, originY + contentHeight + bottomMargin - height - contentY);
                    if (distance <= 2) endReached = true;
                    if (contentY < previous - 0.5 && distance > 2 && endReached) stickToBottom = false;
                    else if (distance <= 2 || (contentY > previous + 0.5 && distance < 48)) stickToBottom = true;
                    root.updateFloatingDate(distance);
                }
                onAtYEndChanged: if (atYEnd) root.newBelow = 0
                onCountChanged: {
                    if (stickToBottom) Qt.callLater(root.followEndIfSticky);
                }
                onContentHeightChanged: {
                    if (stickToBottom && !moving) Qt.callLater(root.followEndIfSticky);
                }
                onHeightChanged: {
                    if (stickToBottom || distanceToEnd < 48) {
                        messageList.scrollToEnd();
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
                    function onModelAboutToBeReset() {
                        messageList.countBeforeReset = messageList.count;
                        messageList.resetting = true;
                    }
                    function onModelReset() {
                        Qt.callLater(() => { messageList.resetting = false; });
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
                        visible: ZaloClient.isLoadingHistory
                        running: visible && root.visible && root.windowShowing
                    }
                }

                delegate: ChatMessageRow {
                    listWidth: messageList.width
                    selectionMode: root.selectionMode
                    selected: root.selectionMode && root.selectedIds.indexOf(msgId) >= 0
                    onSelectionToggled: (id) => root.toggleSelected(id)
                    searchFocusId: root.searchFocusId.length > 0 ? root.searchFocusId : root.foundMsgId
                    flashMsgId: root.flashMsgId
                    onFocusComposerRequested: msgInput.forceActiveFocus()
                    onOpenMessageMenuRequested: (item, msg, mx, my) => root.openMessageMenu(item, msg, mx, my)
                    onOpenReactionDetailsRequested: (msgId, anchor) => root.openReactionDetails(msgId, anchor)
                    onOpenSeenDetailsRequested: (uids) => root.openSeenDetails(uids)
                    onOpenViewerRequested: (msgId) => root.openViewer(msgId)
                    onJumpToMessageRequested: (msgId, cliMsgId) => root.jumpToMessage(msgId, cliMsgId)
                }
            }

            Rectangle {
                id: typingIndicatorPill
                objectName: "typingPill"
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 16
                anchors.bottomMargin: 3
                implicitWidth: typingRow.implicitWidth + 20
                width: implicitWidth
                height: 22
                radius: 11
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1
                z: 10

                readonly property bool isPartnerTyping: ZaloClient.currentConversationIsTyping
                visible: opacity > 0
                opacity: isPartnerTyping ? 1.0 : 0.0
                Behavior on opacity { NumberAnimation { duration: 180 } }

                RowLayout {
                    id: typingRow
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 10
                    spacing: 5

                    Row {
                        spacing: 3
                        Layout.alignment: Qt.AlignVCenter

                        Repeater {
                            model: 3
                            Rectangle {
                                id: dot
                                required property int index
                                readonly property int dotIndex: index
                                width: 4
                                height: 4
                                radius: 2
                                color: Theme.zaloBlue

                                SequentialAnimation on y {
                                    running: typingIndicatorPill.isPartnerTyping && root.windowShowing
                                    loops: Animation.Infinite
                                    PauseAnimation { duration: dot.dotIndex * 140 }
                                    NumberAnimation { from: 0; to: -3; duration: 240; easing.type: Easing.OutQuad }
                                    NumberAnimation { from: -3; to: 0; duration: 240; easing.type: Easing.InQuad }
                                    PauseAnimation { duration: (2 - dot.dotIndex) * 140 }
                                }
                            }
                        }
                    }

                    Text {
                        text: ZaloClient.currentConversationStatus.length > 0 ? ZaloClient.currentConversationStatus : I18n.t("Đang soạn tin...", "Typing...")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
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
                        id: scheduledRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        ZIcon { name: "reminder"; size: 14; color: scheduledRow.modelData.state === "failed" ? Theme.badgeRed : Theme.zaloBlue }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.Normal
                            color: ThemeManager.textColor
                            text: (scheduledRow.modelData.state === "failed" ? I18n.t("Gửi lỗi · ", "Failed · ")
                                   : scheduledRow.modelData.state === "sending" ? I18n.t("Đang gửi · ", "Sending · ") : "")
                                  + Qt.formatDateTime(new Date(scheduledRow.modelData.sendAt), "HH:mm dd/MM") + " — " + scheduledRow.modelData.text
                        }
                        Text {
                            visible: scheduledRow.modelData.state !== "sending"
                            text: I18n.t("Hủy", "Cancel")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: Theme.zaloBlue
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.cancelScheduledMessage(scheduledRow.modelData.id)
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
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                            text: I18n.t("Trả lời ", "Replying to ") + (ZaloClient.replyTarget.senderName || "")
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
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
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
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
                                font.pixelSize: Theme.fontCaption
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
                            color: removeDraftMouse.containsMouse ? Theme.badgeRed : "#99000000"
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
                    font.pixelSize: Theme.fontBody
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
                    font.pixelSize: Theme.fontBody
                    color: ThemeManager.secondaryTextColor
                }
                ZButton {
                    visible: ZaloClient.composerLockDeletable
                    variant: "secondary"
                    text: I18n.t("Xóa trò chuyện", "Delete conversation")
                    onClicked: ZaloClient.deleteConversationWithUndo(ZaloClient.currentConversationId)
                }
            }
        }

        Rectangle {
            id: selectionBar
            objectName: "selectionBar"
            visible: root.selectionMode
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: ThemeManager.surfaceColor
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: ThemeManager.dividerColor }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                ZIconButton {
                    iconName: "close"
                    iconSize: 16
                    tooltip: I18n.t("Thoát chế độ chọn", "Exit selection")
                    onClicked: root.exitSelection()
                }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Đã chọn %1", "%1 selected").arg(root.selectedIds.length)
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBodyLarge
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                }
                ZIconButton {
                    objectName: "selectionCopyButton"
                    enabled: root.selectedIds.length > 0
                    iconName: "copy"
                    tooltip: I18n.t("Sao chép", "Copy")
                    onClicked: {
                        if (ZaloClient.copyMessages(root.selectedIds).length > 0) ZaloClient.showNotice(I18n.t("Đã sao chép", "Copied"));
                        root.exitSelection();
                    }
                }
                ZIconButton {
                    objectName: "selectionForwardButton"
                    enabled: root.selectedIds.length > 0
                    iconName: "forward"
                    tooltip: I18n.t("Chuyển tiếp", "Forward")
                    onClicked: {
                        ZaloClient.openForwardModalMulti(root.selectedIds);
                        root.exitSelection();
                    }
                }
                ZIconButton {
                    objectName: "selectionDeleteButton"
                    enabled: root.selectedIds.length > 0
                    iconName: "trash"
                    tooltip: I18n.t("Xóa ở phía tôi", "Delete for me")
                    onClicked: deleteSelectedDialog.open()
                }
            }
        }

        Rectangle {
            id: composerBox
            objectName: "composerBox"
            visible: ZaloClient.composerLockReason.length === 0 && !root.selectionMode
            Layout.fillWidth: true
            readonly property real textAreaHeight: root.isEditorExpanded
                ? Math.min(230, Math.max(110, Math.floor(root.height * 0.38)))
                : Math.max(root.composerMinTextHeight, Math.min(root.composerMaxTextHeight, msgInput.implicitHeight + 16))
            readonly property real calculatedHeight: 32 + 1 + (root.isFormattingActive ? 34 : 0) + (ZaloClient.composerUrgency > 0 ? 26 : 0) + textAreaHeight + 32
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
                            running: ZaloClient.voiceRecorder.recording && root.visible && root.windowShowing
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
                        font.pixelSize: Theme.fontBodyLarge
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

                        ZIconButton {
                            id: tbSticker
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "sticker"
                            iconColor: root.emojiPickerOpen ? Theme.zaloBlue : (tbSticker.pressed ? Theme.zaloBlueHover : (tbSticker.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor))
                            tooltip: I18n.t("Biểu tượng cảm xúc (Emoji/Stickers)", "Stickers & Emojis")
                            property bool wasOpenOnPress: false
                            onPressStarted: wasOpenOnPress = root.emojiPickerOpen
                            onClicked: {
                                if (!wasOpenOnPress) {
                                    root.emojiPickerOpen = true;
                                }
                            }
                        }

                        ZIconButton {
                            id: tbImage
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "image"
                            iconColor: tbImage.pressed ? Theme.zaloBlueHover : (tbImage.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Gửi hình ảnh", "Send image")
                            onClicked: imageFileDialog.open()
                        }

                        ZIconButton {
                            id: tbClip
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "clip"
                            iconColor: tbClip.pressed ? Theme.zaloBlueHover : (tbClip.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Đính kèm tệp tin", "Attach file")
                            onClicked: documentFileDialog.open()
                        }

                        ZIconButton {
                            id: tbSnip
                            visible: !root.composerOverflow
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "snip"
                            readonly property string snipShortcut: ZaloClient.shortcuts.keys("app.screenshot")[0] || "Ctrl+Alt+S"
                            iconColor: tbSnip.pressed ? Theme.zaloBlueHover : (tbSnip.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Chụp ảnh màn hình (%1)", "Capture screenshot (%1)").arg(tbSnip.snipShortcut)
                            onClicked: ZaloClient.requestScreenshot()
                        }

                        ZIconButton {
                            id: tbMention
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            visible: ZaloClient.currentConversationIsGroup && !root.composerOverflow
                            iconName: "mention"
                            iconColor: tbMention.pressed ? Theme.zaloBlueHover : (tbMention.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Nhắc tên thành viên (@)", "Mention a member (@)")
                            onClicked: {
                                msgInput.forceActiveFocus();
                                msgInput.insert(msgInput.cursorPosition, "@");
                            }
                        }

                        ZIconButton {
                            id: tbCard
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            visible: ZaloClient.currentConversationId.length > 0 && !root.composerOverflow
                            iconName: "contacts"
                            iconColor: tbCard.pressed ? Theme.zaloBlueHover : (tbCard.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Gửi danh thiếp", "Send a contact card")
                            onClicked: ZaloClient.openSendCardPicker()
                        }

                        ZIconButton {
                            id: tbPoll
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            visible: ZaloClient.currentConversationIsGroup && !root.composerOverflow
                            iconName: "poll"
                            iconColor: tbPoll.pressed ? Theme.zaloBlueHover : (tbPoll.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Tạo cuộc bình chọn", "Create poll")
                            onClicked: ZaloClient.openCreatePollModal()
                        }

                        ZIconButton {
                            id: tbReminder
                            visible: !root.composerOverflow
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "reminder"
                            iconColor: tbReminder.pressed ? Theme.zaloBlueHover : (tbReminder.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Hẹn giờ gửi tin nhắn", "Schedule message")
                            onClicked: reminderPopup.open()
                        }

                        ZIconButton {
                            id: tbQuickMsg
                            visible: !root.composerOverflow
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "quick_message"
                            iconColor: tbQuickMsg.pressed ? Theme.zaloBlueHover : (tbQuickMsg.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Tin nhắn nhanh", "Quick message")
                            onClicked: quickMsgPopup.open()
                        }

                        ZIconButton {
                            id: tbVoice
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "mic"
                            opacity: ZaloClient.voiceRecorder.available ? 1.0 : 0.45
                            iconColor: tbVoice.pressed ? Theme.zaloBlueHover : (tbVoice.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            readonly property string voiceShortcut: ZaloClient.shortcuts.primaryText("composer.voice")
                            tooltip: ZaloClient.voiceRecorder.available
                                ? I18n.t("Ghi âm tin nhắn thoại", "Record a voice message") + (tbVoice.voiceShortcut.length > 0 ? " (" + tbVoice.voiceShortcut + ")" : "")
                                : (ZaloClient.voiceRecorder.unavailableReason === "no_microphone"
                                   ? I18n.t("Không tìm thấy micro", "No microphone found")
                                   : (ZaloClient.voiceRecorder.unavailableReason === "no_multimedia"
                                      ? I18n.t("Máy này chưa hỗ trợ ghi âm", "This device does not support voice recording")
                                      : I18n.t("Chưa ghi âm được trên máy này", "Voice recording is not available on this device yet")))
                            onClicked: ZaloClient.voiceRecorder.start()
                        }

                        ZIconButton {
                            id: tbFormat
                            visible: !root.composerOverflow
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "format_text"
                            iconColor: root.isFormattingActive ? Theme.zaloBlue : (tbFormat.pressed ? Theme.zaloBlueHover : (tbFormat.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor))
                            tooltip: I18n.t("Định dạng tin nhắn", "Format message")
                            onClicked: root.isFormattingActive = !root.isFormattingActive
                        }

                        ZIconButton {
                            id: tbUrgency
                            objectName: "urgencyButton"
                            visible: !root.composerOverflow
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "bell"
                            iconColor: ZaloClient.composerUrgency === 2 ? Theme.badgeRed
                                     : ZaloClient.composerUrgency === 1 ? "#F59E0B"
                                     : (tbUrgency.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: I18n.t("Đánh dấu tin Quan trọng / Khẩn cấp", "Mark as Important / Urgent")
                            onClicked: ZaloClient.setComposerUrgency((ZaloClient.composerUrgency + 1) % 3)
                        }

                        ZIconButton {
                            id: tbMore
                            objectName: "composerMoreButton"
                            visible: root.composerOverflow
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: "more_horiz"
                            iconColor: composerMoreMenu.opened ? Theme.zaloBlue : (tbMore.pressed ? Theme.zaloBlueHover : (tbMore.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor))
                            tooltip: I18n.t("Thêm công cụ", "More tools")
                            onClicked: {
                                const p = root.mapFromItem(tbMore, 0, 0);
                                root.placePopup(composerMoreMenu, p.x, p.y - Math.max(composerMoreMenu.height, composerMoreMenu.implicitHeight) - 4);
                                composerMoreMenu.open();
                            }
                        }

                        Item { Layout.fillWidth: true }

                        ZIconButton {
                            id: tbExpand
                            visible: !root.composerOverflow
                            width: 28
                            height: 28
                            radius: 4
                            color: "transparent"
                            pressedScale: 0.94
                            iconName: root.isEditorExpanded ? "collapse" : "expand"
                            iconColor: tbExpand.pressed ? Theme.zaloBlueHover : (tbExpand.hovered ? Theme.zaloBlue : ThemeManager.secondaryTextColor)
                            tooltip: root.isEditorExpanded ? I18n.t("Thu gọn ô soạn thảo", "Collapse editor") : I18n.t("Mở rộng ô soạn thảo", "Expand editor")
                            onClicked: root.isEditorExpanded = !root.isEditorExpanded
                        }
                    }
                }

                Rectangle {
                    objectName: "urgencyChip"
                    visible: ZaloClient.composerUrgency > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 26 : 0
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 12
                        spacing: 6
                        Rectangle {
                            implicitWidth: urgencyChipText.implicitWidth + 12
                            implicitHeight: 20
                            radius: 10
                            color: ZaloClient.composerUrgency === 2 ? Theme.badgeRed : "#F59E0B"
                            Text {
                                id: urgencyChipText
                                anchors.centerIn: parent
                                text: ZaloClient.composerUrgency === 2 ? I18n.t("Tin nhắn Khẩn cấp", "Urgent message")
                                                                       : I18n.t("Tin nhắn Quan trọng", "Important message")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontCaption
                                font.weight: Font.DemiBold
                                color: "#FFFFFF"
                            }
                        }
                        ZIconButton {
                            iconName: "close"
                            iconSize: 12
                            tooltip: I18n.t("Bỏ đánh dấu", "Remove mark")
                            onClicked: ZaloClient.setComposerUrgency(0)
                        }
                        Item { Layout.fillWidth: true }
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
                                { st: "ind_inc", icon: "format_indent_increase", vi: "Tăng thụt lề", en: "Increase indent" },
                                { st: "ind_dec", icon: "format_indent_decrease", vi: "Giảm thụt lề", en: "Decrease indent" },
                                { st: "undo", icon: "undo", vi: "Hoàn tác (Ctrl+Z)", en: "Undo (Ctrl+Z)" },
                                { st: "redo", icon: "redo", vi: "Làm lại (Ctrl+Y)", en: "Redo (Ctrl+Y)" },
                                { st: "clear", icon: "format_clear", vi: "Xóa định dạng", en: "Clear formatting" }
                            ]
                            delegate: Rectangle {
                                id: fmtButton
                                required property var modelData
                                readonly property var fmtState: ZaloClient.composer.active
                                readonly property bool on: fmtButton.modelData.st === "lst_1" || fmtButton.modelData.st === "lst_2"
                                                           ? fmtButton.fmtState.list === fmtButton.modelData.st
                                                           : fmtButton.modelData.st === "ind_inc" ? Number(fmtButton.fmtState.indent || 0) > 0
                                                           : fmtButton.modelData.st === "size" ? (fmtButton.fmtState.size || "").length > 0
                                                           : fmtButton.modelData.st === "color" ? (fmtButton.fmtState.color || "").length > 0
                                                           : fmtButton.fmtState[fmtButton.modelData.st] === true
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
                                    color: fmtButton.modelData.st === "color" && (fmtButton.fmtState.color || "").length > 2
                                           ? "#" + fmtButton.fmtState.color.substring(2)
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
                                        else if (st === "ind_inc") root.changeIndent(1);
                                        else if (st === "ind_dec") root.changeIndent(-1);
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
                            objectName: "msgInput"
                            wrapMode: TextEdit.Wrap
                            color: ThemeManager.textColor
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSubtitle
                            persistentSelection: true
                            Component.onCompleted: ZaloClient.composer.attach(msgInput.textDocument, msgInput)
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
                                    if (ZaloClient.pasteClipboardToDraft()) {
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
                                if (!root.loadingDraft) {
                                    draftSaveTimer.restart();
                                    if (msgInput.activeFocus && (msgInput.text.length > 0 || msgInput.preeditText.length > 0)) {
                                        ZaloClient.notifyTyping();
                                    }
                                }
                            }
                            onPreeditTextChanged: {
                                root.updateSuggestions();
                                if (!root.loadingDraft && msgInput.activeFocus && msgInput.preeditText.length > 0) {
                                    ZaloClient.notifyTyping();
                                }
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
                                root.handleComposerReturn(event);
                            }
                            Keys.onEnterPressed: function(event) {
                                root.handleComposerReturn(event);
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
                        margins: 8
                        visible: root.suggestItems.length > 0 && msgInput.activeFocus
                        x: 12
                        y: Math.max(-composerBox.y + 8, -height - 8)
                        width: Math.min(360, parent.width - 24)
                        height: Math.min(suggestList.contentHeight, Math.max(120, root.height - 96), 8 * 44) + 8
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
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
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
                                            textFormat: Text.PlainText
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontBody
                                            font.weight: (root.suggestMode === "quick") ? Font.DemiBold : Font.Normal
                                            color: root.suggestMode === "quick" ? Theme.zaloBlue : ThemeManager.textColor
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            visible: text.length > 0
                                            Layout.fillWidth: true
                                            text: root.suggestMode === "mention" ? ((sugRow.modelData && sugRow.modelData.hint) || "") : ((sugRow.modelData && sugRow.modelData.text) || "")
                                            textFormat: Text.PlainText
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontCaption
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
                            Layout.fillWidth: true
                            text: I18n.t("Nhấn Enter để gửi, Shift + Enter để xuống dòng", "Press Enter to send, Shift + Enter for new line")
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontCaption
                            color: ThemeManager.secondaryTextColor
                            visible: root.width >= 420 && (msgInput.activeFocus || msgInput.text.length > 0 || msgInput.preeditText.length > 0)
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            visible: msgInput.text.trim() === "" && msgInput.preeditText.trim() === "" && draftAttachmentsModel.count === 0
                            width: 30
                            height: 30
                            radius: 15
                            color: "transparent"
                            scale: likeBtnMouse.pressed ? 0.88 : (likeBtnMouse.containsMouse ? 1.1 : 1.0)
                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }

                            ZIcon {
                                visible: ZaloClient.defaultReaction === "like"
                                anchors.centerIn: parent
                                name: "thumbs_up"
                                size: 18
                                color: Theme.zaloBlue
                            }

                            Text {
                                visible: ZaloClient.defaultReaction !== "like"
                                anchors.centerIn: parent
                                text: ZaloClient.defaultReactionEmoji
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTitleLarge
                                renderType: Text.NativeRendering
                            }

                            MouseArea {
                                id: likeBtnMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: I18n.t("Gửi nhanh", "Quick send") + " " + ZaloClient.defaultReactionEmoji
                                onClicked: ZaloClient.sendMessage(ZaloClient.defaultReactionEmoji)
                            }
                        }

                        Rectangle {
                            visible: msgInput.text.trim() !== "" || msgInput.preeditText.trim() !== "" || draftAttachmentsModel.count > 0
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

    Item {
        objectName: "conversationInfoSlot"
        visible: ZaloClient.showConversationInfo && !root.infoOverlay
        Layout.fillHeight: true
        Layout.preferredWidth: visible ? root.infoPanelWidth : 0
    }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.infoOverlay
        color: "#40000000"
        z: 40
        MouseArea {
            anchors.fill: parent
            onClicked: ZaloClient.showConversationInfo = false
        }
    }

    Loader {
        id: conversationInfoLoader
        objectName: "conversationInfoLoader"
        active: ZaloClient.showConversationInfo
        visible: active
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: root.infoPanelWidth
        z: 41
        source: active ? Qt.resolvedUrl("ConversationInfoPanel.qml") : ""
        onLoaded: if (item) item.objectName = "conversationInfoPanel"
    }

    property bool emojiPickerOpen: false
    onEmojiPickerOpenChanged: {
        if (emojiPickerOpen) emojiPopup.open();
        else emojiPopup.close();
    }
    Popup {
        id: emojiPopup
        parent: root
        x: 16
        y: Math.max(16, root.height - composerBox.height - 6 - height)
        width: Math.min(380, root.width - 32)
        height: Math.min(root.height - 32, 420)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.emojiPickerOpen = false
        background: Item {}
        contentItem: Loader {
            id: emojiPickerLoader
            active: root.emojiPickerOpen
            anchors.fill: parent
            source: active ? Qt.resolvedUrl("EmojiPickerPopup.qml") : ""
        }
    }
    Connections {
        target: emojiPickerLoader.item
        ignoreUnknownSignals: true
        function onEmojiSelected(emoji: string): void {
            msgInput.insert(msgInput.cursorPosition, emoji);
            msgInput.forceActiveFocus();
        }
        function onStickerSelected(sticker: var): void {
            ZaloClient.stickers.send(sticker);
            root.emojiPickerOpen = false;
            msgInput.forceActiveFocus();
        }
        function onGifSelected(url: string): void {
            ZaloClient.sendGifFromUrl(url);
            root.emojiPickerOpen = false;
            msgInput.forceActiveFocus();
        }
    }

    PinDetailHost {
            id: pinDetailHost
            popup: pinDetailPopup
            Popup {
                id: pinDetailPopup
                objectName: "pinDetailPopup"
                parent: T.Overlay.overlay
                onAboutToShow: {
                    pinDetailText.deselect();
                    pinDetailFlick.contentY = 0;
                }
                readonly property var detail: pinDetailHost.detail
                readonly property string fullText: pinDetailPopup.detail.text || ""
                readonly property var images: pinDetailPopup.detail.images || []
                readonly property string image: ZaloClient.brokenMediaRevision >= 0 ? pinDetailPopup.firstUsable(pinDetailPopup.images) : ""
                readonly property bool imageShown: pinDetailImage.status === Image.Ready && pinDetailImage.source.toString().length > 0
                function firstUsable(urls: var): string {
                    for (let i = 0; i < urls.length; ++i) {
                        if (!ZaloClient.isMediaBroken(urls[i])) return urls[i];
                    }
                    return "";
                }
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
                            font.pixelSize: Theme.fontSubtitle
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
                    ZButton {
                        visible: pinDetailPopup.detail.type === 3 && Number(pinDetailPopup.detail.pollId || 0) > 0
                        Layout.fillWidth: true
                        variant: "tonal"
                        text: I18n.t("Xem bình chọn", "View poll")
                        onClicked: {
                            ZaloClient.openPollDialog(Number(pinDetailPopup.detail.pollId), ZaloClient.currentConversationId, false);
                            pinDetailPopup.close();
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (pinDetailPopup.detail.senderName || "").length > 0
                        text: I18n.t("Người gửi: ", "From: ") + (pinDetailPopup.detail.senderName || "")
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: ThemeManager.secondaryTextColor
                    }
                    Item {
                        visible: pinDetailPopup.images.length > 0
                        Layout.preferredWidth: Math.min(pinDetailColumn.width, 360)
                        Layout.preferredHeight: pinDetailPopup.imageShown
                                                ? Math.min(240, pinDetailImage.implicitHeight * width / Math.max(1, pinDetailImage.implicitWidth))
                                                : 96
                        Rectangle {
                            anchors.fill: parent
                            visible: !pinDetailPopup.imageShown
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
                                    font.pixelSize: Theme.fontSmall
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                        }
                        Image {
                            id: pinDetailImage
                            anchors.fill: parent
                            visible: pinDetailPopup.imageShown
                            source: pinDetailPopup.image.length > 0 ? MediaSource.toSource(pinDetailPopup.image) : ""
                            sourceSize.width: 720
                            fillMode: Image.PreserveAspectFit
                            horizontalAlignment: Image.AlignLeft
                            asynchronous: true
                            onStatusChanged: {
                                if (status !== Image.Error) return;
                                const failed = pinDetailPopup.image;
                                Qt.callLater(() => ZaloClient.markMediaBroken(failed));
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: pinDetailImage.status === Image.Loading ? Qt.BusyCursor : Qt.PointingHandCursor
                            onClicked: {
                                if (pinDetailPopup.imageShown)
                                    ZaloClient.openLink(pinDetailPopup.href.length > 0 ? pinDetailPopup.href : pinDetailPopup.image);
                                else if (pinDetailImage.status !== Image.Loading)
                                    for (let i = 0; i < pinDetailPopup.images.length; ++i) ZaloClient.retryMedia(pinDetailPopup.images[i]);
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
                            text: "<style>a{color:" + Theme.linkColor + ";text-decoration:none}</style>" + ZaloClient.linkifyText(pinDetailPopup.fullText)
                            textFormat: TextEdit.RichText
                            onLinkActivated: function(link) { ZaloClient.openLink(link); }
                            HoverHandler { cursorShape: pinDetailText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor }
                            readOnly: true
                            selectByMouse: true
                            persistentSelection: true
                            wrapMode: TextEdit.Wrap
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBodyLarge
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
                        font.pixelSize: Theme.fontSmall
                        font.underline: pinDetailLinkMouse.containsMouse
                        color: Theme.linkColor
                        MouseArea {
                            id: pinDetailLinkMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ZaloClient.openLink(pinDetailPopup.href)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: pinDetailPopup.detail.local === false && (pinDetailPopup.detail.msgId || "").length > 0
                        text: I18n.t("Tin nhắn gốc không có trong lịch sử đã tải; nội dung lấy từ danh sách ghim.",
                                     "The original message is not in the loaded history; the content comes from the pinboard.")
                        wrapMode: Text.Wrap
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontCaption
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

    function openPinDetail(topicId: string): void {
        if (topicId.length === 0) return;
        pinDetailHost.topicId = "";
        pinDetailHost.topicId = topicId;
        pinDetailPopup.open();
    }

    function copyPinText(text: string): void {
        ZaloClient.copyText(text);
        ZaloClient.showNotice(I18n.t("Đã sao chép", "Copied"));
    }

    Popup {
        id: headerMoreMenu
        objectName: "headerMoreMenu"
        margins: 8
        width: 220
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: ThemeManager.surfaceColor; radius: 8; border.color: ThemeManager.borderColor }
        contentItem: ColumnLayout {
            spacing: 2
            ZMenuRow {
                visible: root.headerSearchOverflow
                icon: "search"
                label: I18n.t("Tìm kiếm trong tin nhắn", "Search messages")
                onTriggered: {
                    headerMoreMenu.close();
                    root.openSearch();
                }
            }
            ZMenuRow {
                visible: ZaloClient.currentConversationCanCreateGroup
                icon: "user_add"
                label: ZaloClient.currentConversationIsGroup
                    ? I18n.t("Thêm thành viên vào nhóm", "Add member to group")
                    : I18n.t("Tạo nhóm với người này", "Create group with this person")
                onTriggered: {
                    headerMoreMenu.close();
                    if (ZaloClient.currentConversationIsGroup) ZaloClient.openAddMemberModal();
                    else ZaloClient.openCreateGroup();
                }
            }
            ZMenuRow {
                icon: "sidebar_panel"
                label: I18n.t("Thông tin hội thoại", "Conversation info")
                onTriggered: {
                    headerMoreMenu.close();
                    ZaloClient.toggleConversationInfo();
                }
            }
        }
    }

    Popup {
        id: composerMoreMenu
        objectName: "composerMoreMenu"
        margins: 8
        width: 240
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: ThemeManager.surfaceColor; radius: 8; border.color: ThemeManager.borderColor }
        contentItem: ColumnLayout {
            spacing: 2
            ZMenuRow {
                icon: "snip"
                label: I18n.t("Chụp ảnh màn hình", "Capture screenshot")
                onTriggered: {
                    composerMoreMenu.close();
                    ZaloClient.requestScreenshot();
                }
            }
            ZMenuRow {
                visible: ZaloClient.currentConversationIsGroup
                icon: "mention"
                label: I18n.t("Nhắc tên thành viên (@)", "Mention a member (@)")
                onTriggered: {
                    composerMoreMenu.close();
                    msgInput.forceActiveFocus();
                    msgInput.insert(msgInput.cursorPosition, "@");
                }
            }
            ZMenuRow {
                visible: ZaloClient.currentConversationId.length > 0
                icon: "contacts"
                label: I18n.t("Gửi danh thiếp", "Send a contact card")
                onTriggered: {
                    composerMoreMenu.close();
                    ZaloClient.openSendCardPicker();
                }
            }
            ZMenuRow {
                visible: ZaloClient.currentConversationIsGroup
                icon: "poll"
                label: I18n.t("Tạo cuộc bình chọn", "Create poll")
                onTriggered: {
                    composerMoreMenu.close();
                    ZaloClient.openCreatePollModal();
                }
            }
            ZMenuRow {
                icon: "bell"
                label: ZaloClient.composerUrgency === 0 ? I18n.t("Đánh dấu tin Quan trọng", "Mark as Important")
                     : ZaloClient.composerUrgency === 1 ? I18n.t("Đánh dấu tin Khẩn cấp", "Mark as Urgent")
                     : I18n.t("Bỏ đánh dấu Khẩn cấp", "Remove Urgent mark")
                onTriggered: {
                    composerMoreMenu.close();
                    ZaloClient.setComposerUrgency((ZaloClient.composerUrgency + 1) % 3);
                }
            }
            ZMenuRow {
                icon: "reminder"
                label: I18n.t("Hẹn giờ gửi tin nhắn", "Schedule message")
                onTriggered: {
                    composerMoreMenu.close();
                    reminderPopup.open();
                }
            }
            ZMenuRow {
                icon: "quick_message"
                label: I18n.t("Tin nhắn nhanh", "Quick message")
                onTriggered: {
                    composerMoreMenu.close();
                    quickMsgPopup.open();
                }
            }
            ZMenuRow {
                icon: "format_text"
                label: root.isFormattingActive ? I18n.t("Tắt định dạng tin nhắn", "Hide message formatting")
                                               : I18n.t("Định dạng tin nhắn", "Format message")
                onTriggered: {
                    composerMoreMenu.close();
                    root.isFormattingActive = !root.isFormattingActive;
                }
            }
            ZMenuRow {
                icon: root.isEditorExpanded ? "collapse" : "expand"
                label: root.isEditorExpanded ? I18n.t("Thu gọn ô soạn thảo", "Collapse editor")
                                             : I18n.t("Mở rộng ô soạn thảo", "Expand editor")
                onTriggered: {
                    composerMoreMenu.close();
                    root.isEditorExpanded = !root.isEditorExpanded;
                }
            }
        }
    }

    PinMenuHost {
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

    function openPinMenu(topic: var, index: int, anchorItem: Item): void {
        pinMenuHost.topic = topic;
        pinMenuHost.topicIndex = index;
        const p = root.mapFromItem(anchorItem, anchorItem.width, anchorItem.height);
        root.placePopup(pinItemMenu, p.x - Math.max(pinItemMenu.width, pinItemMenu.implicitWidth), p.y + 4);
        pinItemMenu.open();
    }

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
                            font.pixelSize: Theme.fontSubtitle
                            font.weight: Font.DemiBold
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
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: parent.active ? Font.DemiBold : Font.Normal
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
                        font.pixelSize: Theme.fontSmall
                        color: ThemeManager.secondaryTextColor
                    }
                    ListView {
                        id: reactionList
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: {
                            const all = root.reactionInfo.people;
                            if (root.reactionTab.length === 0) return all;
                            const matches = [];
                            for (let i = 0; i < all.length; ++i) {
                                if ((all[i].perEmoji[root.reactionTab] || 0) > 0) matches.push(all[i]);
                            }
                            return matches;
                        }
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
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.Normal
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
                                    font.pixelSize: Theme.fontSubtitle
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                        }
                    }
                }
            }

            Popup {
                id: seenPopup
                focus: true
                anchors.centerIn: parent
                width: Math.min(root.width - 32, 340)
                height: Math.min(root.height - 60, 400)
                modal: true
                padding: 14
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle { color: ThemeManager.surfaceColor; radius: 12; border.color: ThemeManager.borderColor }
                contentItem: ColumnLayout {
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Người đã xem (%1)", "Seen by (%1)").arg(root.seenDetailsUids.length)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSubtitle
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }
                        Rectangle {
                            width: 26
                            height: 26
                            radius: 13
                            color: seenCloseMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: "close"; size: 14; color: ThemeManager.iconColor }
                            MouseArea { id: seenCloseMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: seenPopup.close() }
                        }
                    }
                    ListView {
                        id: seenList
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: root.seenDetailsUids
                        delegate: Rectangle {
                            id: seenPersonRow
                            required property string modelData
                            readonly property string uid: seenPersonRow.modelData
                            readonly property string name: ZaloClient.displayNameOf(uid) || uid
                            readonly property string avatar: ZaloClient.avatarOf(uid)
                            width: seenList.width
                            height: 46
                            radius: 8
                            color: seenPersonHover.hovered ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                            HoverHandler { id: seenPersonHover }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: 10
                                ZAvatar {
                                    size: 34
                                    avatarUrl: seenPersonRow.avatar
                                    name: seenPersonRow.name
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: seenPersonRow.name
                                    elide: Text.ElideRight
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.Normal
                                    color: ThemeManager.textColor
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    seenPopup.close();
                                    ZaloClient.viewProfile(seenPersonRow.uid);
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
        readonly property var thumbUrls: ZaloClient.pins.pins.length >= 0 ? ZaloClient.pins.previewUrls(topicRow.topic.id || "") : []
        readonly property string thumb: ZaloClient.brokenMediaRevision >= 0 ? topicRow.firstUsable(topicRow.thumbUrls) : ""
        readonly property bool thumbShown: topicThumb.status === Image.Ready && topicThumb.source.toString().length > 0

        function firstUsable(urls: var): string {
            for (let i = 0; i < urls.length; ++i) {
                if (!ZaloClient.isMediaBroken(urls[i])) return urls[i];
            }
            return "";
        }
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
                    font.pixelSize: Theme.fontBody
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
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: ThemeManager.secondaryTextColor
                    }
                    Rectangle {
                        objectName: "pinTopicThumb"
                        visible: topicRow.thumbUrls.length > 0
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        radius: 4
                        clip: true
                        color: Theme.mediaPlaceholder
                        ZIcon {
                            objectName: "pinTopicThumbGlyph"
                            anchors.centerIn: parent
                            visible: !topicRow.thumbShown
                            name: "image"
                            size: 12
                            color: ThemeManager.secondaryTextColor
                        }
                        Image {
                            id: topicThumb
                            anchors.fill: parent
                            source: topicRow.thumb.length > 0 ? MediaSource.toSource(topicRow.thumb) : ""
                            sourceSize.width: 36
                            sourceSize.height: 36
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: topicRow.thumbShown
                            onStatusChanged: {
                                if (status !== Image.Error) return;
                                const failed = topicRow.thumb;
                                Qt.callLater(() => ZaloClient.markMediaBroken(failed));
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: topicRow.contentText()
                        textFormat: Text.PlainText
                        maximumLineCount: 1
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
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
        margins: 8
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

            Popup {
                id: quickMsgPopup
                objectName: "quickMsgPopup"
                property bool formOpen: false
                property real editId: 0
                function openQuickForm(id: real, keyword: string, content: string): void {
                    quickMsgPopup.editId = id;
                    qmKeywordField.text = keyword;
                    qmTextField.text = content;
                    quickMsgPopup.formOpen = true;
                }
                function closeQuickForm(): void {
                    quickMsgPopup.formOpen = false;
                    quickMsgPopup.editId = 0;
                    qmKeywordField.text = "";
                    qmTextField.text = "";
                }
                onClosed: quickMsgPopup.closeQuickForm()
                margins: 8
                width: Math.min(320, root.width - 24)
                height: Math.min(quickMsgCol.implicitHeight + 16, Math.max(160, root.height - 40))
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

                contentItem: Flickable {
                    clip: true
                    contentWidth: width
                    contentHeight: quickMsgCol.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: quickMsgCol
                        width: parent.width
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
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
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
                                    if (quickMsgPopup.formOpen && quickMsgPopup.editId === 0)
                                        quickMsgPopup.closeQuickForm();
                                    else
                                        quickMsgPopup.openQuickForm(0, "", "");
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
                            objectName: "qmKeywordField"
                            Layout.fillWidth: true
                            placeholderText: I18n.t("Từ khóa (vd: chao)", "Keyword (e.g. hi)")
                        }
                        ZTextField {
                            id: qmTextField
                            objectName: "qmTextField"
                            Layout.fillWidth: true
                            placeholderText: I18n.t("Nội dung tin nhắn", "Message content")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Hủy", "Cancel")
                                onClicked: quickMsgPopup.closeQuickForm()
                            }
                            ZButton {
                                enabled: qmKeywordField.text.trim().length > 0 && qmTextField.text.trim().length > 0
                                text: quickMsgPopup.editId > 0 ? I18n.t("Lưu", "Save") : I18n.t("Tạo", "Create")
                                onClicked: {
                                    if (quickMsgPopup.editId > 0)
                                        ZaloClient.updateQuickMessage(quickMsgPopup.editId, qmKeywordField.text, qmTextField.text);
                                    else
                                        ZaloClient.createQuickMessage(qmKeywordField.text, qmTextField.text);
                                    quickMsgPopup.closeQuickForm();
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
                        font.pixelSize: Theme.fontSmall
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
                                        textFormat: Text.PlainText
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Font.DemiBold
                                        color: Theme.zaloBlue
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qmRow.modelData.text + (qmRow.modelData.hasMedia ? I18n.t("  (kèm ảnh: chưa hỗ trợ gửi)", "  (with photo: not sent yet)") : "")
                                        textFormat: Text.PlainText
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
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
                                        onClicked: quickMsgPopup.openQuickForm(qmRow.modelData.id, qmRow.modelData.keyword, qmRow.modelData.text)
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

            Popup {
                id: reminderPopup
                onAboutToShow: scheduleContentInput.text = msgInput.text
                margins: 8
                width: Math.min(320, root.width - 24)
                height: Math.min(reminderCol.implicitHeight + 24, Math.max(180, root.height - 40))
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

                contentItem: Flickable {
                    clip: true
                    contentWidth: width
                    contentHeight: reminderCol.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: reminderCol
                        width: parent.width
                        spacing: 8

                    Text {
                        text: I18n.t("Hẹn giờ gửi tin nhắn", "Schedule message")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                        color: ThemeManager.textColor
                    }

                    ZTextField {
                        id: scheduleContentInput
                        Layout.fillWidth: true
                        placeholderText: I18n.t("Nội dung tin nhắn...", "Message...")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
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
                        font.pixelSize: Theme.fontCaption
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
                                id: schedPresetRow
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
                                    text: I18n.t(schedPresetRow.modelData.vi, schedPresetRow.modelData.en)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontCaption
                                    color: ThemeManager.textColor
                                }

                                MouseArea {
                                    id: schedMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: reminderPopup.schedule(schedPresetRow.modelData.hour >= 0 ? reminderPopup.nextAt(schedPresetRow.modelData.hour)
                                                                                          : Date.now() + schedPresetRow.modelData.offset)
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
                        font.pixelSize: Theme.fontCaption
                        color: ThemeManager.secondaryTextColor
                    }
                    }
                }
            }

            Popup {
                id: messageMenu
                property bool canModerate: false
                property bool canReact: false
                property bool canReply: false
                property bool canForward: false
                property bool canCopy: false
                margins: 8
                width: Math.max(230, implicitContentWidth + leftPadding + rightPadding)
                padding: 6
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle {
                    color: ThemeManager.surfaceColor
                    radius: 8
                    border.color: ThemeManager.borderColor
                }

                readonly property string msgId: root.menuMessageId || ""
                readonly property bool isServerMessage: msgId.length > 0 && !msgId.startsWith("local-")

                contentItem: ColumnLayout {
                    spacing: 2

                    RowLayout {
                        objectName: "menuReactionStrip"
                        visible: messageMenu.canReact
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
                                Text { anchors.centerIn: parent; text: reactItem.modelData.emoji; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTitle }
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
                        visible: root.menuMessageType === "image" && (root.menuMessageUrl || "").length > 0
                        icon: "zoom_in"
                        label: I18n.t("Xem ảnh", "View picture")
                        onTriggered: { root.openViewer(messageMenu.msgId); messageMenu.close(); }
                    }
                    ZMenuRow {
                        visible: (root.menuMessageType === "video"
                                  || (root.menuMessageType === "file"
                                      && ZaloClient.isVideoFileName(root.menuMessageText || root.menuMessageUrl || "")))
                                 && (root.menuMessageUrl || "").length > 0
                        icon: "play"
                        label: I18n.t("Xem video", "Watch video")
                        onTriggered: { root.openViewer(messageMenu.msgId); messageMenu.close(); }
                    }
                    ZMenuRow {
                        visible: root.menuMessageType === "image" && (root.menuMessageUrl || "").length > 0
                        icon: "copy"
                        label: I18n.t("Sao chép ảnh", "Copy picture")
                        onTriggered: { ZaloClient.copyImage(root.menuMessageUrl); messageMenu.close(); }
                    }
                    ZMenuRow {
                        visible: root.menuMessageType === "image" && (root.menuMessageUrl || "").length > 0
                        icon: "download"
                        label: I18n.t("Lưu ảnh về máy", "Save picture")
                        onTriggered: { saveImageDialog.open(); messageMenu.close(); }
                    }
                    ZMenuRow {
                        objectName: "selectMessagesRow"
                        visible: messageMenu.isServerMessage
                        icon: "check"
                        label: I18n.t("Chọn nhiều tin nhắn", "Select messages")
                        onTriggered: { root.enterSelection(messageMenu.msgId); messageMenu.close(); }
                    }
                    ZMenuRow {
                        objectName: "saveFileAsRow"
                        visible: (root.menuMessageType === "file" || root.menuMessageType === "video")
                                 && (root.menuMessageUrl || "").length > 0
                        icon: "download"
                        label: I18n.t("Lưu thành…", "Save as…")
                        onTriggered: { saveFileDialog.open(); messageMenu.close(); }
                    }
                    ZMenuRow {
                        objectName: "menuReplyRow"
                        visible: messageMenu.canReply
                        icon: "reply"
                        label: I18n.t("Trả lời", "Reply")
                        onTriggered: { ZaloClient.setReplyTarget(messageMenu.msgId); messageMenu.close(); msgInput.forceActiveFocus(); }
                    }
                    ZMenuRow {
                        visible: messageMenu.canCopy && (root.menuMessageText || "").length > 0
                        icon: "copy"
                        label: I18n.t("Sao chép", "Copy")
                        onTriggered: { ZaloClient.copyText(root.menuMessageText); messageMenu.close(); }
                    }
                    ZMenuRow {
                        objectName: "menuForwardRow"
                        visible: messageMenu.canForward
                        icon: "forward"
                        label: I18n.t("Chuyển tiếp", "Forward")
                        onTriggered: { ZaloClient.openForwardModal(messageMenu.msgId, root.menuMessageText || ""); messageMenu.close(); }
                    }
                    ZMenuRow {
                        id: pinMenuRow
                        readonly property bool pinned: ZaloClient.pins.pins.length >= 0 && ZaloClient.pins.isPinned(messageMenu.msgId)
                        visible: messageMenu.canReply || pinMenuRow.pinned
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
                        visible: messageMenu.isServerMessage && root.menuMessageIsMine === true
                                 && ZaloClient.canRecall(messageMenu.msgId)
                        icon: "undo"
                        danger: true
                        label: I18n.t("Thu hồi", "Recall")
                        onTriggered: { ZaloClient.recallMessage(messageMenu.msgId); messageMenu.close(); }
                    }
                    ZMenuRow {
                        visible: messageMenu.isServerMessage && root.menuMessageIsMine !== true
                        icon: "shield"
                        danger: true
                        label: I18n.t("Báo cáo vi phạm", "Report abuse")
                        onTriggered: {
                            ZaloClient.reportAbuse(ZaloClient.currentConversationId, ZaloClient.currentConversationIsGroup, 0, root.menuMessageText || "");
                            messageMenu.close();
                        }
                    }
                    ZMenuRow {
                        objectName: "deleteForEveryoneRow"
                        visible: messageMenu.isServerMessage && messageMenu.canModerate && root.menuMessageIsMine !== true
                        icon: "trash"
                        danger: true
                        label: I18n.t("Xóa với mọi người", "Delete for everyone")
                        onTriggered: { deleteEveryoneDialog.msgId = messageMenu.msgId; messageMenu.close(); deleteEveryoneDialog.open(); }
                    }
                    ZMenuRow {
                        visible: messageMenu.isServerMessage
                        icon: "trash"
                        danger: true
                        label: I18n.t("Xóa chỉ ở phía tôi", "Delete for me")
                        onTriggered: { ZaloClient.deleteMessageForMe(messageMenu.msgId); messageMenu.close(); }
                    }
                }
            }

    ZFilePicker {
        id: saveImageDialog
        title: I18n.t("Lưu ảnh", "Save picture")
        fileMode: ZFilePicker.SaveFile
        currentFile: ZaloClient.suggestedMediaSaveUrl(root.menuMessageUrl || "")
        onAccepted: ZaloClient.saveMediaTo(root.menuMessageUrl, selectedFile.toString())
    }

    ZConfirmDialog {
        id: deleteEveryoneDialog
        objectName: "deleteEveryoneDialog"
        property string msgId: ""
        danger: true
        title: I18n.t("Xóa tin nhắn với mọi người?", "Delete for everyone?")
        okText: I18n.t("Xóa", "Delete")
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: I18n.t("Tin nhắn sẽ bị xóa khỏi nhóm với tất cả thành viên.", "The message is removed from the group for every member.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: ThemeManager.textColor
        }
        onAccepted: ZaloClient.deleteMessageForEveryone(deleteEveryoneDialog.msgId)
    }

    ZConfirmDialog {
        id: deleteSelectedDialog
        objectName: "deleteSelectedDialog"
        danger: true
        title: I18n.t("Xóa %1 tin nhắn ở phía bạn?", "Delete %1 messages for you?").arg(root.selectedIds.length)
        okText: I18n.t("Xóa", "Delete")
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: I18n.t("Người khác vẫn thấy các tin nhắn này.", "Other people still see these messages.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: ThemeManager.textColor
        }
        onAccepted: {
            ZaloClient.deleteMessagesForMe(root.selectedIds);
            root.exitSelection();
        }
    }

    ZFilePicker {
        id: saveFileDialog
        title: I18n.t("Lưu tệp", "Save file")
        fileMode: ZFilePicker.SaveFile
        currentFile: root.menuMessageType === "file" && (root.menuMessageText || "").length > 0
                     ? "file://" + ZaloClient.downloadDirectory + "/" + root.menuMessageText
                     : ZaloClient.suggestedMediaSaveUrl(root.menuMessageUrl || "")
        onAccepted: ZaloClient.saveMediaTo(root.menuMessageUrl, selectedFile.toString())
    }

    ZFilePicker {
        id: imageFileDialog
        title: I18n.t("Chọn hình ảnh để gửi", "Select photo to send")
        fileMode: ZFilePicker.OpenFiles
        nameFilters: [I18n.t("Hình ảnh (*.png *.jpg *.jpeg *.gif *.webp *.bmp)", "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"), I18n.t("Tất cả các tệp (*.*)", "All files (*.*)")]
        onAccepted: {
            for (var i = 0; i < selectedFiles.length; i++) {
                root.addDraftFile(selectedFiles[i].toString());
            }
        }
    }

    ZFilePicker {
        id: documentFileDialog
        title: I18n.t("Chọn tệp tin đính kèm", "Select document to attach")
        fileMode: ZFilePicker.OpenFiles
        nameFilters: [I18n.t("Tất cả các tệp (*.*)", "All files (*.*)")]
        onAccepted: {
            for (var j = 0; j < selectedFiles.length; j++) {
                root.addDraftFile(selectedFiles[j].toString());
            }
        }
    }

    MouseArea {
        objectName: "compactBackArea"
        anchors.fill: parent
        z: 100
        acceptedButtons: Qt.BackButton
        onClicked: root.compactBack()
    }
}
