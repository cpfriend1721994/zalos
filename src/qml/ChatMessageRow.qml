import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos
import "MediaSource.js" as MediaSource

Item {

    required property real listWidth
    required property string searchFocusId
    required property string flashMsgId

    signal focusComposerRequested()
    signal openMessageMenuRequested(Item item, var msg, real mx, real my)
    signal openReactionDetailsRequested(string msgId, Item anchor)
    signal openSeenDetailsRequested(var uids)
    signal openViewerRequested(string msgId)
    signal jumpToMessageRequested(string msgId, string cliMsgId)

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
        if (ext === "doc" || ext === "docx") return "#2B579A";
        if (ext === "xls" || ext === "xlsx" || ext === "csv") return "#217346";
        if (ext === "ppt" || ext === "pptx") return "#D24726";
        if (ext === "pdf") return "#E2574C";
        if (ext === "gif" || ext === "jpg" || ext === "jpeg" || ext === "png" || ext === "svg" || ext === "webp") return "#2E9E6B";
        if (ext === "zip" || ext === "rar" || ext === "7z" || ext === "tar" || ext === "gz") return "#8E6E53";
        if (ext === "m4a" || ext === "mp4" || ext === "mov" || ext === "avi" || ext === "mkv") return "#7E57C2";
        if (ext === "mp3" || ext === "mpg" || ext === "wav" || ext === "flac") return "#EC407A";
        if (ext === "txt" || ext === "log" || ext === "md") return "#607D8B";
        return "#8A94A6";
    }

    function statusLabel(status: string): string {
        if (status === "sending") return I18n.t("Đang gửi", "Sending");
        if (status === "seen") return I18n.t("Đã xem", "Seen");
        if (status === "failed") return I18n.t("Gửi lỗi", "Failed");
        return I18n.t("Đã gửi", "Sent");
    }

    function seenTooltipText(seenUids: var): string {
        if (!seenUids || seenUids.length === 0) return "";
        const names = [];
        for (let i = 0; i < seenUids.length; ++i) {
            const uid = seenUids[i];
            const name = ZaloClient.displayNameOf(uid);
            names.push(name && name.length > 0 ? name : uid);
        }
        return I18n.t("Đã xem: ", "Seen by: ") + names.join(", ");
    }
    id: msgRow
    objectName: "messageRow"
    width: msgRow.listWidth
    visible: !msgRow.inMediaRun
    height: msgRow.inMediaRun ? 0 : rowCol.implicitHeight + (msgRow.lastInGroup ? 10 : 2)
    readonly property bool inViewport: {
        const lv = msgRow.ListView.view;
        return !lv || (msgRow.y + msgRow.height >= lv.contentY && msgRow.y <= lv.contentY + lv.height);
    }
    readonly property bool unseenReactionShown: msgRow.isMine && msgRow.visible && msgRow.inViewport
                                               && msgRow.Window.active
                                               && ZaloClient.currentUnreadReactionIds.indexOf(msgRow.msgId) >= 0
    onUnseenReactionShownChanged: {
        if (!unseenReactionShown) return;
        const seenId = msgRow.msgId;
        Qt.callLater(function() { ZaloClient.markReactionsSeen(seenId); });
    }

    required property string messageType
    required property bool isMine
    required property string text
    required property string quoteText
    required property string msgId
    required property string href
    required property string mediaUrl
    required property var mediaRun
    required property bool dateSeparator
    required property bool firstInGroup
    required property bool inMediaRun
    required property bool isRecalled
    required property bool lastInGroup
    required property bool showStatus
    required property bool unreadMarker
    required property string quoteMsgId
    required property string quoteSender
    required property string reaction
    required property string richText
    required property string senderAvatar
    required property string senderId
    required property string senderName
    required property string status
    required property string time
    required property real fileSize
    required property real ts
    required property list<string> seenUids
    required property var extra
    required property var pollData
    required property bool pollCard

    ListView.onReused: msgText.deselect()

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
    readonly property bool hasCard: isFile || isLink || isVoice || isCall || isContactCard || isLocation
                                    || isBankCard
    readonly property bool bubbleless: (isPhoto && text.length === 0 && quoteText.length === 0) || isSticker
    readonly property real maxBubble: Math.min(msgRow.listWidth * 0.7, 560)
    readonly property bool isSearchHit: msgRow.searchFocusId.length > 0 && msgId === msgRow.searchFocusId
    readonly property var menuInfo: ({ id: msgRow.msgId, text: msgRow.text, isMine: msgRow.isMine,
                                       messageType: msgRow.messageType,
                                       url: msgRow.href.length > 0 ? msgRow.href : msgRow.mediaUrl })

    HoverHandler { id: rowHover }

    Rectangle {
        anchors.fill: parent
        z: -1
        radius: 6
        color: Theme.zaloBlueLight
        opacity: msgRow.flashMsgId.length > 0 && msgRow.msgId === msgRow.flashMsgId ? 0.7 : 0
        Behavior on opacity { NumberAnimation { duration: 300 } }
    }

    ColumnLayout {
        id: rowCol
        width: parent.width
        spacing: 4

        Loader {
            active: msgRow.dateSeparator
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
                        text: msgRow.dateLabel(msgRow.ts)
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
            }
        }

        Loader {
            active: msgRow.unreadMarker
            visible: active
            Layout.fillWidth: true
            Layout.topMargin: 4
            sourceComponent: ChatUnreadBar {
            }
        }

        Text {
            id: systemNoticeText
            visible: msgRow.isSystem
            Layout.fillWidth: true
            Layout.leftMargin: 40
            Layout.rightMargin: 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: systemLinkMouse.enabled ? msgRow.text : ZaloClient.linkifyText(msgRow.text)
            textFormat: systemLinkMouse.enabled ? Text.PlainText : Text.StyledText
            linkColor: Theme.linkColor
            onLinkActivated: function(link) { ZaloClient.openLink(link); }
            HoverHandler { cursorShape: systemNoticeText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.underline: systemLinkMouse.enabled && systemLinkMouse.containsMouse
            color: systemLinkMouse.enabled ? Theme.zaloBlue : ThemeManager.secondaryTextColor

            MouseArea {
                id: systemLinkMouse
                anchors.fill: parent
                enabled: msgRow.isSystem && !!msgRow.extra.reminderEventId
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
                readonly property bool suggest: msgRow.extra.suggestStickers === true
                readonly property var stickers: (msgRow.extra.stickers && msgRow.extra.stickers.length > 0)
                                                ? msgRow.extra.stickers
                                                : (ecardCard.suggest ? ZaloClient.stickers.birthdayStickers : [])
                readonly property string desc: ecardCard.suggest && ecardCard.stickers.length === 0 ? "" : (msgRow.extra.desc || "")
                implicitWidth: Math.min(340, msgRow.listWidth - 48)
                implicitHeight: ecardCol.implicitHeight + 24
                radius: 12
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1
                function prefetch(): void {
                    if (ecardCard.suggest) ZaloClient.stickers.loadBirthdayStickers();
                    else if (ecardCard.stickers.length > 0) ZaloClient.prefetchStickers(ecardCard.stickers);
                }
                Component.onCompleted: ecardCard.prefetch()
                onStickersChanged: if (!ecardCard.suggest && ecardCard.stickers.length > 0) ZaloClient.prefetchStickers(ecardCard.stickers)

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
                        avatarUrl: msgRow.senderAvatar
                        name: msgRow.senderName
                        showStatusDot: false
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: msgRow.text
                        textFormat: Text.PlainText
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: ThemeManager.textColor
                    }

                    Text {
                        id: ecardDescText
                        visible: ecardCard.desc.length > 0
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: ZaloClient.linkifyText(ecardCard.desc)
                        textFormat: Text.StyledText
                        linkColor: Theme.linkColor
                        onLinkActivated: function(link) { ZaloClient.openLink(link); }
                        HoverHandler { cursorShape: ecardDescText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
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

        Loader {
            active: msgRow.isPoll && !msgRow.isRecalled
            visible: active
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            sourceComponent: ChatPollBlock {
                isMine: msgRow.isMine
                senderName: msgRow.senderName
                pollData: msgRow.pollData
                extra: msgRow.extra
                showCard: msgRow.pollCard
                question: msgRow.text
                availableWidth: msgRow.listWidth
            }
        }

        RowLayout {
            id: bubbleRow
            visible: !msgRow.isSystem && !msgRow.isEcard && !msgRow.isPoll
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
                    active: msgRow.firstInGroup
                    sourceComponent: Item {
                        ZAvatar {
                            anchors.fill: parent
                            size: 32
                            avatarUrl: (msgRow.senderAvatar && msgRow.senderAvatar.length > 0)
                                       ? msgRow.senderAvatar
                                       : (ZaloClient.currentConversationIsGroup ? "" : ZaloClient.currentConversationAvatar)
                            name: (msgRow.senderName && msgRow.senderName.length > 0) ? msgRow.senderName
                                  : (ZaloClient.currentConversationIsGroup ? "" : ZaloClient.currentConversationName)
                            showStatusDot: false
                        }
                        MouseArea {
                            visible: msgRow.senderId.length > 0
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ZaloClient.viewProfile(msgRow.senderId)
                        }
                    }
                }
            }

            ColumnLayout {
                id: contentCol
                Layout.maximumWidth: msgRow.maxBubble
                Layout.rightMargin: msgRow.mine ? 24 : 0
                opacity: (ZaloClient.antiPeeking && !rowHover.hovered) ? 0.08 : 1.0
                Behavior on opacity { NumberAnimation { duration: 150 } }
                spacing: 0

                Text {
                    id: senderNameText
                    visible: !msgRow.mine && ZaloClient.currentConversationIsGroup && msgRow.firstInGroup
                             && (msgRow.senderName.length > 0 || (msgRow.senderId.length > 0 && msgRow.senderId !== "system"))
                    Layout.bottomMargin: 4
                    text: msgRow.senderName.length > 0 ? msgRow.senderName : I18n.t("Người dùng Zalo", "Zalo user")
                    textFormat: Text.PlainText
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: senderNameMouse.containsMouse ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                    MouseArea {
                        id: senderNameMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { if (msgRow.senderId.length > 0) ZaloClient.viewProfile(msgRow.senderId); }
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
                        readonly property real maxW: Math.max(150, Math.min(msgRow.maxBubble, msgRow.listWidth - 110))
                        readonly property real fit: Math.min(maxW / srcW, 380 / srcH, 1.0)
                        implicitWidth: Math.max(120, Math.round(srcW * fit))
                        implicitHeight: Math.max(48, Math.round(srcH * fit))

                        ZRoundedImage {
                            id: mediaImg
                            anchors.fill: parent
                            cache: false
                            errorGlyph: false
                            source: msgRow.mediaUrl
                            animatedSource: msgRow.extra.animatedUrl ? msgRow.extra.animatedUrl : ""
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 640
                            sourceSize.height: 1280
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
                                text: mediaImg.status === Image.Error && ZaloClient.isMediaTooLarge(msgRow.mediaUrl)
                                      ? I18n.t("Ảnh quá lớn để hiển thị", "Photo too large to display")
                                      : (msgRow.messageType === "video" ? I18n.t("Video đã hết hạn", "Video expired")
                                                                        : I18n.t("Ảnh đã hết hạn", "Photo expired"))
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
                            visible: msgRow.messageType === "video" && mediaImg.status !== Image.Error
                            anchors.centerIn: parent
                            width: 44
                            height: 44
                            radius: 22
                            color: "#8C000000"
                            ZIcon { anchors.centerIn: parent; name: "play"; size: 26; color: "#FFFFFF" }
                        }
                        MouseArea {
                            id: mediaMouse
                            objectName: "mediaClickArea"
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            ToolTip.visible: containsMouse && msgRow.messageType === "video"
                            ToolTip.delay: 600
                            ToolTip.text: I18n.t("Xem video", "Watch video")
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    msgRow.openMessageMenuRequested(mediaMouse, msgRow.menuInfo, mouse.x, mouse.y);
                                } else {
                                    msgRow.openViewerRequested(msgRow.msgId);
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
                            info: msgRow.extra.sticker ? msgRow.extra.sticker : ({})
                            fallbackUrl: msgRow.mediaUrl
                            playing: msgRow.visible && msgRow.inViewport
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
                                          : I18n.t("Sticker", "Sticker")
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
                                    msgRow.openMessageMenuRequested(stickerBox, msgRow.menuInfo, mouse.x, mouse.y);
                                } else if (stickerImage.status === Image.Ready) {
                                    ImageViewerState.show([{ msgId: msgRow.msgId, url: msgRow.mediaUrl, thumb: msgRow.mediaUrl,
                                                             senderName: msgRow.senderName, isMine: msgRow.isMine, ts: msgRow.ts }], 0);
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
                        readonly property real maxW: Math.max(150, Math.min(msgRow.maxBubble, msgRow.listWidth - 140))
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
                                objectName: "mediaRunTile"
                                required property var modelData
                                readonly property bool sticker: tile.modelData.messageType === "sticker"
                                width: tile.sticker ? 130 : 150
                                height: tile.sticker ? 130 : 150
                                ZRoundedImage {
                                    id: tileImage
                                    visible: !tile.sticker
                                    anchors.fill: parent
                                    cache: false
                                    source: tile.sticker ? "" : tile.modelData.mediaUrl
                                    animatedSource: !tile.sticker && tile.modelData.animatedUrl ? tile.modelData.animatedUrl : ""
                                    errorGlyph: false
                                    sourceSize.width: 320
                                }
                                ColumnLayout {
                                    objectName: "mediaRunExpiredHint"
                                    visible: !tile.sticker && tileImage.status === Image.Error
                                    anchors.centerIn: parent
                                    spacing: 6
                                    ZIcon {
                                        Layout.alignment: Qt.AlignHCenter
                                        name: tile.modelData.messageType === "video" ? "video" : "image"
                                        size: 24
                                        color: ThemeManager.secondaryTextColor
                                    }
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: tile.modelData.messageType === "video" ? I18n.t("Video đã hết hạn", "Video expired")
                                                                                     : I18n.t("Ảnh đã hết hạn", "Photo expired")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                ZSticker {
                                    id: tileSticker
                                    visible: tile.sticker
                                    anchors.fill: parent
                                    info: tile.sticker && tile.modelData.sticker ? tile.modelData.sticker : ({})
                                    fallbackUrl: tile.sticker ? tile.modelData.mediaUrl : ""
                                    playing: visible && msgRow.inViewport
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
                                            msgRow.openMessageMenuRequested(tile, { id: t.msgId, text: "", isMine: t.isMine, messageType: t.messageType,
                                                                         url: t.href.length > 0 ? t.href : t.mediaUrl }, mouse.x, mouse.y);
                                        } else if (tile.sticker) {
                                            if (tileSticker.status === Image.Ready)
                                                ImageViewerState.show([{ msgId: t.msgId, url: t.mediaUrl, thumb: t.mediaUrl,
                                                                         senderName: t.senderName, isMine: t.isMine, ts: t.ts }], 0);
                                        } else {
                                            msgRow.openViewerRequested(t.msgId);
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
                            if (!msgRow.isRecalled) msgRow.openMessageMenuRequested(bubbleRect, msgRow.menuInfo, mouse.x, mouse.y);
                        }
                    }

                    ColumnLayout {
                        id: bubbleInner
                        x: 12
                        y: msgRow.isPhoto ? 7 : 8
                        width: bubbleRect.width - 24
                        spacing: 4

                        Loader {
                            active: msgRow.quoteMsgId.length > 0 || msgRow.quoteText.length > 0
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
                                        text: msgRow.quoteSender
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: msgRow.quoteText
                                        textFormat: Text.PlainText
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
                                    onClicked: { if (msgRow.quoteMsgId.length > 0) msgRow.jumpToMessageRequested(msgRow.quoteMsgId, ""); }
                                }
                            }
                        }

                        Loader {
                            active: msgRow.isFile
                            visible: active
                            sourceComponent: Item {
                                id: fileCardBox
                                implicitWidth: Math.min(352, bubbleRect.innerMax)
                                implicitHeight: fileCard.implicitHeight
                                readonly property string fileUrl: msgRow.href.length > 0 ? msgRow.href : msgRow.mediaUrl
                                readonly property bool isVideoFile: fileCard.ext === "mp4" || fileCard.ext === "mov" || fileCard.ext === "webm" || fileCard.ext === "mkv" || fileCard.ext === "m4v"
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
                                    color: msgRow.fileColor(fileCard.ext)
                                    Text {
                                        anchors.centerIn: parent
                                        width: parent.width - 4
                                        horizontalAlignment: Text.AlignHCenter
                                        text: (fileCard.ext || "file").toUpperCase().substring(0, 4)
                                        color: "#FFFFFF"
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: msgRow.text
                                        textFormat: Text.PlainText
                                        maximumLineCount: 1
                                        elide: Text.ElideMiddle
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: msgRow.fileSizeText(msgRow.fileSize)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                Rectangle {
                                    visible: fileCardBox.isVideoFile
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    radius: 4
                                    color: fileOpenMouse.containsMouse && !fileDownloadMouse.containsMouse
                                        ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                    ZIcon { anchors.centerIn: parent; name: "play"; size: 20; color: ThemeManager.accentColor }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    radius: 4
                                    z: 2
                                    color: (fileCardBox.isVideoFile ? fileDownloadMouse.containsMouse : fileOpenMouse.containsMouse)
                                        ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                    ZIcon { anchors.centerIn: parent; name: "download"; size: 20; color: ThemeManager.secondaryTextColor }
                                    MouseArea {
                                        id: fileDownloadMouse
                                        anchors.fill: parent
                                        enabled: fileCardBox.isVideoFile
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        ToolTip.visible: containsMouse
                                        ToolTip.delay: 500
                                        ToolTip.text: I18n.t("Tải về và mở (lưu trong Tải xuống/Zalos)", "Download and open (saved in Downloads/Zalos)")
                                        onClicked: ZaloClient.openFileMessage(fileCardBox.fileUrl, msgRow.text)
                                    }
                                }
                            }
                                MouseArea {
                                    id: fileOpenMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    ToolTip.visible: containsMouse && !(fileCardBox.isVideoFile && fileDownloadMouse.containsMouse)
                                    ToolTip.delay: 500
                                    ToolTip.text: fileCardBox.isVideoFile
                                        ? I18n.t("Xem video", "Watch video")
                                        : I18n.t("Tải về và mở (lưu trong Tải xuống/Zalos)", "Download and open (saved in Downloads/Zalos)")
                                    onClicked: {
                                        if (fileCardBox.isVideoFile && fileCardBox.fileUrl.length > 0) {
                                            msgRow.openViewerRequested(msgRow.msgId);
                                        } else {
                                            ZaloClient.openFileMessage(fileCardBox.fileUrl, msgRow.text);
                                        }
                                    }
                                }
                            }
                        }

                        Loader {
                            active: msgRow.isContactCard && !msgRow.isRecalled
                            visible: active
                            Layout.preferredWidth: Math.min(280, bubbleRect.innerMax)
                            sourceComponent: ColumnLayout {
                                id: contactCard
                                readonly property string uid: msgRow.extra.uid || ""
                                readonly property string phone: msgRow.extra.phone || ""
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
                                                textFormat: Text.PlainText
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
                                                      : (msgRow.extra.official ? I18n.t("Official Account", "Official Account")
                                                                              : I18n.t("Danh thiếp Zalo", "Zalo contact card"))
                                                textFormat: Text.PlainText
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
                            active: msgRow.isLink && !msgRow.isRecalled
                            visible: active
                            Layout.preferredWidth: Math.min(320, bubbleRect.innerMax)
                            sourceComponent: ColumnLayout {
                                spacing: 4
                                Image {
                                    visible: msgRow.mediaUrl.length > 0 && status !== Image.Error
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: visible ? 150 : 0
                                    source: msgRow.isLink ? MediaSource.toSource(msgRow.mediaUrl) : ""
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    sourceSize.width: 320
                                    clip: true
                                }
                                Text {
                                    id: linkCardTitle
                                    Layout.fillWidth: true
                                    text: msgRow.href.length > 0 ? msgRow.text : ZaloClient.linkifyText(msgRow.text)
                                    textFormat: msgRow.href.length > 0 ? Text.PlainText : Text.StyledText
                                    linkColor: Theme.linkColor
                                    onLinkActivated: function(link) { ZaloClient.openLink(link); }
                                    HoverHandler { cursorShape: linkCardTitle.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: ThemeManager.textColor
                                }
                                Text {
                                    id: linkCardDesc
                                    visible: msgRow.isLink && (msgRow.extra.desc || "").length > 0
                                    Layout.fillWidth: true
                                    text: !msgRow.isLink ? "" : (msgRow.href.length > 0 ? (msgRow.extra.desc || "") : ZaloClient.linkifyText(msgRow.extra.desc || ""))
                                    textFormat: msgRow.href.length > 0 ? Text.PlainText : Text.StyledText
                                    linkColor: Theme.linkColor
                                    onLinkActivated: function(link) { ZaloClient.openLink(link); }
                                    HoverHandler { cursorShape: linkCardDesc.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
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
                                    text: msgRow.isLink ? (msgRow.extra.domain || msgRow.href) : ""
                                    textFormat: Text.PlainText
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
                            active: msgRow.isBankCard && !msgRow.isRecalled
                            visible: active
                            Layout.preferredWidth: Math.min(300, bubbleRect.innerMax)
                            sourceComponent: ColumnLayout {
                                id: bankCardBox
                                spacing: 8
                                readonly property string bankName: (msgRow.isBankCard && msgRow.extra.bankName) ? String(msgRow.extra.bankName) : ""
                                readonly property int binBank: (msgRow.isBankCard && msgRow.extra.binBank) ? Number(msgRow.extra.binBank) : 0
                                readonly property string accNum: (msgRow.isBankCard && msgRow.extra.accountNumber) ? String(msgRow.extra.accountNumber) : ""
                                readonly property string accHolder: (msgRow.isBankCard && msgRow.extra.accountHolder) ? String(msgRow.extra.accountHolder) : ""
                                readonly property bool hasDetails: accNum.length > 0

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Rectangle {
                                        Layout.preferredWidth: 40
                                        Layout.preferredHeight: 40
                                        radius: 20
                                        color: Theme.zaloBlueLight
                                        ZIcon { anchors.centerIn: parent; name: "bank"; size: 20; color: Theme.zaloBlueTonalText }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text {
                                                Layout.fillWidth: true
                                                text: bankCardBox.bankName.length > 0 ? bankCardBox.bankName
                                                                                      : I18n.t("Thông tin chuyển khoản", "Bank transfer details")
                                                elide: Text.ElideRight
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: ThemeManager.textColor
                                            }
                                            Rectangle {
                                                visible: bankCardBox.binBank > 0
                                                radius: 4
                                                color: ThemeManager.hoverColor
                                                implicitWidth: binLbl.implicitWidth + 8
                                                implicitHeight: binLbl.implicitHeight + 4
                                                Text {
                                                    id: binLbl
                                                    anchors.centerIn: parent
                                                    text: "BIN " + bankCardBox.binBank
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 10
                                                    color: ThemeManager.secondaryTextColor
                                                }
                                            }
                                        }
                                        Text {
                                            visible: bankCardBox.accHolder.length > 0
                                            Layout.fillWidth: true
                                            text: bankCardBox.accHolder
                                            elide: Text.ElideRight
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 12
                                            color: ThemeManager.secondaryTextColor
                                        }
                                        Text {
                                            visible: bankCardBox.hasDetails
                                            Layout.fillWidth: true
                                            text: bankCardBox.accNum
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 15
                                            font.weight: Font.Bold
                                            color: Theme.zaloBlueTonalText
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            visible: !bankCardBox.hasDetails && msgRow.text !== I18n.t("Thông tin chuyển khoản", "Bank transfer details")
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
                                            visible: !bankCardBox.hasDetails
                                            Layout.fillWidth: true
                                            text: msgRow.isBankCard ? (msgRow.extra.desc || "") : ""
                                            wrapMode: Text.Wrap
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 12
                                            color: ThemeManager.secondaryTextColor
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: bankCardBox.hasDetails
                                    Layout.fillWidth: true
                                    height: 30
                                    radius: 6
                                    color: copyBankMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlueLight
                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        ZIcon {
                                            name: "copy"
                                            size: 14
                                            color: copyBankMouse.containsMouse ? "#FFFFFF" : Theme.zaloBlueTonalText
                                        }
                                        Text {
                                            text: I18n.t("Sao chép số tài khoản", "Copy account number")
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                            color: copyBankMouse.containsMouse ? "#FFFFFF" : Theme.zaloBlueTonalText
                                        }
                                    }
                                    MouseArea {
                                        id: copyBankMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            ZaloClient.copyText(bankCardBox.accNum);
                                            ZaloClient.showNotice(I18n.t("Đã sao chép số tài khoản", "Account number copied"));
                                        }
                                    }
                                }
                            }
                        }

                        Loader {
                            active: msgRow.isLocation && !msgRow.isRecalled
                            visible: active
                            Layout.preferredWidth: Math.min(300, bubbleRect.innerMax)
                            sourceComponent: ColumnLayout {
                                id: locCard
                                spacing: 8
                                readonly property bool hasCoords: !!(msgRow.extra && msgRow.extra.hasCoords)
                                readonly property real lat: locCard.hasCoords ? Number(msgRow.extra.lat || 0) : 0
                                readonly property real lng: locCard.hasCoords ? Number(msgRow.extra.lng || 0) : 0
                                readonly property bool hasThumb: msgRow.mediaUrl.length > 0
                                property bool thumbFailed: false
                                readonly property string thumbKey: msgRow.mediaUrl
                                onThumbKeyChanged: locCard.thumbFailed = false
                                readonly property bool thumbOk: locCard.hasThumb && !(locCard.thumbFailed && locCard.hasCoords)
                                readonly property bool hasMap: locCard.hasThumb || locCard.hasCoords
                                readonly property string markerAvatar: (msgRow.senderAvatar && msgRow.senderAvatar.length > 0)
                                                                       ? msgRow.senderAvatar
                                                                       : (msgRow.mine ? ZaloClient.currentUserAvatar
                                                                                      : (ZaloClient.currentConversationIsGroup ? "" : ZaloClient.currentConversationAvatar))
                                readonly property string markerName: (msgRow.senderName && msgRow.senderName.length > 0)
                                                                     ? msgRow.senderName
                                                                     : (msgRow.mine ? ZaloClient.currentUserName
                                                                                    : (ZaloClient.currentConversationIsGroup ? "" : ZaloClient.currentConversationName))
                                function osmTileXf(lon: real): real {
                                    return (lon + 180.0) / 360.0 * 32768.0;
                                }
                                function osmTileYf(latitude: real): real {
                                    const clamped = Math.max(-85.0511, Math.min(85.0511, latitude));
                                    const rad = clamped * Math.PI / 180.0;
                                    return (1.0 - Math.log(Math.tan(rad) + 1.0 / Math.cos(rad)) / Math.PI) / 2.0 * 32768.0;
                                }

                                Rectangle {
                                    id: mapBox
                                    visible: locCard.hasMap
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 156
                                    radius: 8
                                    clip: true
                                    color: ThemeManager.cardBackgroundColor
                                    border.color: ThemeManager.borderColor
                                    border.width: 1

                                    ZRoundedImage {
                                        visible: locCard.thumbOk
                                        anchors.fill: parent
                                        radius: 8
                                        source: locCard.thumbOk ? msgRow.mediaUrl : ""
                                        onStatusChanged: if (status === Image.Error) locCard.thumbFailed = true
                                    }

                                    Item {
                                        id: osmGrid
                                        visible: !locCard.thumbOk && locCard.hasCoords
                                        anchors.fill: parent
                                        readonly property real xf: locCard.osmTileXf(locCard.lng)
                                        readonly property real yf: locCard.osmTileYf(locCard.lat)
                                        readonly property int tx: Math.floor(osmGrid.xf)
                                        readonly property int ty: Math.floor(osmGrid.yf)
                                        readonly property real fx: osmGrid.xf - osmGrid.tx
                                        readonly property real fy: osmGrid.yf - osmGrid.ty
                                        Repeater {
                                            model: !locCard.thumbOk && locCard.hasCoords ? 9 : 0
                                            Image {
                                                required property int index
                                                readonly property int dx: (index % 3) - 1
                                                readonly property int dy: Math.floor(index / 3) - 1
                                                readonly property int tileX: (osmGrid.tx + dx + 32768) % 32768
                                                readonly property int tileY: Math.max(0, Math.min(32767, osmGrid.ty + dy))
                                                x: Math.round(mapBox.width / 2 - osmGrid.fx * 256 + dx * 256)
                                                y: Math.round(mapBox.height / 2 - osmGrid.fy * 256 + dy * 256)
                                                width: 256
                                                height: 256
                                                asynchronous: true
                                                cache: true
                                                source: "image://media/" + encodeURIComponent(
                                                    "https://tile.openstreetmap.org/15/" + tileX + "/" + tileY + ".png")
                                            }
                                        }
                                    }

                                    Column {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.verticalCenter
                                        anchors.bottomMargin: -4
                                        spacing: 0
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 36
                                            height: 36
                                            radius: 18
                                            color: Theme.zaloBlue
                                            border.color: "#FFFFFF"
                                            border.width: 2
                                            ZAvatar {
                                                visible: locCard.markerAvatar.length > 0 || locCard.markerName.length > 0
                                                anchors.centerIn: parent
                                                size: 30
                                                avatarUrl: locCard.markerAvatar
                                                name: locCard.markerName
                                            }
                                            ZIcon {
                                                visible: locCard.markerAvatar.length === 0 && locCard.markerName.length === 0
                                                anchors.centerIn: parent
                                                name: "location"
                                                size: 18
                                                color: "#FFFFFF"
                                            }
                                        }
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: 8
                                            height: 8
                                            radius: 4
                                            color: Theme.zaloBlue
                                            border.color: "#FFFFFF"
                                            border.width: 1.5
                                        }
                                    }

                                    Rectangle {
                                        visible: !locCard.thumbOk && locCard.hasCoords
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 4
                                        width: osmAttrText.implicitWidth + 8
                                        height: osmAttrText.implicitHeight + 4
                                        radius: 4
                                        color: Qt.rgba(0, 0, 0, 0.55)
                                        Text {
                                            id: osmAttrText
                                            anchors.centerIn: parent
                                            text: "© OSM"
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 9
                                            color: "#FFFFFF"
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Rectangle {
                                        visible: !locCard.hasMap
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
                                            textFormat: Text.PlainText
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
                                            text: msgRow.isLocation ? (msgRow.extra.desc || msgRow.extra.coords || "") : ""
                                            textFormat: Text.PlainText
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
                                }

                                TapHandler {
                                    onTapped: if (msgRow.href.length > 0) ZaloClient.openExternalUrl(msgRow.href)
                                }
                                HoverHandler { cursorShape: msgRow.href.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                            }
                        }

                        Loader {
                            active: msgRow.isVoice && !msgRow.isRecalled
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
                                    readonly property int secs: msgRow.isVoice ? Math.round((msgRow.extra.durationMs || 0) / 1000) : 0
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
                                    color: msgRow.isCall && msgRow.extra.missed ? Theme.badgeRed : ThemeManager.hoverColor
                                    ZIcon {
                                        anchors.centerIn: parent
                                        name: msgRow.isCall && msgRow.extra.video ? "video" : "phone"
                                        size: 16
                                        color: msgRow.isCall && msgRow.extra.missed ? "#FFFFFF" : ThemeManager.textColor
                                    }
                                }
                                ColumnLayout {
                                    spacing: 0
                                    Text {
                                        text: msgRow.text
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: msgRow.isCall && msgRow.extra.missed ? Theme.badgeRed : ThemeManager.textColor
                                    }
                                    Text {
                                        visible: text.length > 0
                                        text: msgRow.isCall ? (msgRow.extra.duration || "") : ""
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
                                            if (msgRow.extra.video) ZaloClient.startVideoCall();
                                            else ZaloClient.startVoiceCall();
                                        }
                                    }
                                }
                            }
                        }

                        TextEdit {
                            id: msgText
                            visible: !msgRow.hasCard && msgRow.text.length > 0 && !msgRow.isRecalled
                            Layout.preferredWidth: Math.min(implicitWidth, bubbleRect.innerMax)
                            Layout.fillWidth: msgRow.isPhoto
                            readOnly: true
                            selectByMouse: true
                            persistentSelection: false
                            textFormat: TextEdit.RichText
                            wrapMode: TextEdit.Wrap
                            text: "<style>a{color:" + Theme.linkColor + ";text-decoration:none}</style>" + msgRow.richText
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
                                    ZaloClient.scanAndOpenUrl(link);
                                }
                            }
                            HoverHandler { cursorShape: msgText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor }
                        }
                        Text {
                            visible: msgRow.isRecalled
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
                    readonly property bool canReact: !msgRow.isRecalled && !msgRow.isSystem && !msgRow.isEcard && !msgRow.isPoll
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
                    Layout.fillWidth: true
                    implicitHeight: msgRow.reaction.length > 0 ? 12 : 0
                    z: 2
                    Row {
                        id: reactionPills
                        x: Math.max(0, reactionRow.contentEdge - width - 6)
                        y: -10
                        spacing: 4
                        Loader {
                            active: msgRow.reaction.length > 0
                            visible: active
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
                                    text: msgRow.reaction
                                    font.pixelSize: 12
                                    color: ThemeManager.textColor
                                }
                                MouseArea {
                                    id: reactionPillMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: msgRow.openReactionDetailsRequested(msgRow.msgId, parent)
                                }
                                ToolTip.visible: reactionPillMouse.containsMouse
                                ToolTip.delay: 400
                                ToolTip.text: I18n.t("Xem ai đã thả cảm xúc", "See who reacted")
                            }
                        }
                        Loader {
                            active: reactionRow.canReact && rowHover.hovered
                            visible: active
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

                Loader {
                    active: msgRow.lastInGroup || msgRow.showStatus
                    visible: active
                    Layout.alignment: msgRow.mine ? Qt.AlignRight : Qt.AlignLeft
                    Layout.topMargin: 4
                    sourceComponent: RowLayout {
                        spacing: 6
                        Text {
                            id: messageTimeLabel
                            visible: msgRow.lastInGroup
                            text: msgRow.time
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
                            ToolTip.text: msgRow.fullTimestamp(msgRow.ts, msgRow.time)
                        }
                        Row {
                            id: groupSeenRow
                            readonly property var rawUids: msgRow.seenUids ? msgRow.seenUids : []
                            readonly property var uids: rawUids.slice().reverse()
                            visible: msgRow.mine && ZaloClient.currentConversationIsGroup && groupSeenRow.uids.length > 0
                            spacing: -6
                            Layout.alignment: Qt.AlignVCenter
                            height: 16

                            Repeater {
                                model: groupSeenRow.uids.slice(0, 4)
                                delegate: Rectangle {
                                    id: seenAvatarItem
                                    required property string modelData
                                    required property int index
                                    readonly property string avatarUrl: ZaloClient.avatarOf(seenAvatarItem.modelData)
                                    z: 10 - index
                                    width: 16
                                    height: 16
                                    radius: 8
                                    color: ThemeManager.chatBubbleMine
                                    border.color: ThemeManager.surfaceColor
                                    border.width: 1
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: seenAvatarItem.avatarUrl.length > 0
                                            ? "image://avatar/" + encodeURIComponent(seenAvatarItem.avatarUrl) : ""
                                        sourceSize.width: 16
                                        sourceSize.height: 16
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    ZIcon {
                                        anchors.centerIn: parent
                                        visible: seenAvatarItem.avatarUrl.length === 0
                                        name: "user"
                                        size: 10
                                        color: ThemeManager.textColor
                                    }
                                }
                            }

                            Rectangle {
                                visible: groupSeenRow.uids.length > 4
                                z: 20
                                width: 16
                                height: 16
                                radius: 8
                                color: ThemeManager.hoverColor
                                border.color: ThemeManager.surfaceColor
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: "+" + Math.max(0, groupSeenRow.uids.length - 4)
                                    font.pixelSize: 8
                                    font.weight: Font.DemiBold
                                    color: ThemeManager.textColor
                                }
                            }

                            HoverHandler {
                                id: groupSeenHover
                                cursorShape: Qt.PointingHandCursor
                            }

                            TapHandler {
                                onTapped: msgRow.openSeenDetailsRequested(groupSeenRow.uids)
                            }

                            ToolTip.visible: groupSeenHover.hovered
                            ToolTip.delay: 200
                            ToolTip.text: msgRow.seenTooltipText(groupSeenRow.uids)
                        }
                        Text {
                            visible: msgRow.showStatus && (!ZaloClient.currentConversationIsGroup || !msgRow.seenUids || msgRow.seenUids.length === 0)
                            text: msgRow.statusLabel(msgRow.status)
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: msgRow.status === "failed" ? Theme.badgeRed
                                   : msgRow.status === "seen" ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                        }
                    }
                }

            }

            Item { visible: !msgRow.mine; Layout.fillWidth: true }
        }
    }

    Loader {
        objectName: "messageActions"
        active: rowHover.hovered && !msgRow.isRecalled && !msgRow.isSystem && !msgRow.isEcard && !msgRow.isPoll && !msgRow.msgId.startsWith("local-")
        y: bubbleRow.y + (senderNameText.visible ? (senderNameText.height + 4) : 0) + 2
        x: Math.max(8, Math.min(msgRow.width - width - 8,
                                msgRow.mine ? contentCol.x + Math.min(reactionRow.contentLeft, contentCol.width) - width - 8
                                            : contentCol.x + Math.max(reactionRow.contentEdge, 0) + 8))
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
                        id: actionBtn
                        required property var modelData
                        width: 26
                        height: 24
                        radius: 4
                        color: actMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                        ZIcon { anchors.centerIn: parent; name: actionBtn.modelData.icon; size: 16; color: ThemeManager.secondaryTextColor }
                        MouseArea {
                            id: actMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 400
                            ToolTip.text: I18n.t(actionBtn.modelData.vi, actionBtn.modelData.en)
                            onClicked: function(mouse) {
                                if (actionBtn.modelData.act === "reply") {
                                    ZaloClient.setReplyTarget(msgRow.msgId);
                                    msgRow.focusComposerRequested();
                                } else if (actionBtn.modelData.act === "forward") {
                                    ZaloClient.openForwardModal(msgRow.msgId, msgRow.text);
                                } else {
                                    msgRow.openMessageMenuRequested(actMouse, msgRow.menuInfo, mouse.x, mouse.y);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

