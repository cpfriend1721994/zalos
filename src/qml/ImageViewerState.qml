pragma Singleton
import QtQuick

QtObject {
    id: root
    property bool open: false
    property var items: []
    property int index: 0
    property int count: 0
    property string currentUrl: ""
    property string currentSenderName: ""
    property bool currentIsMine: false
    property real currentTs: 0
    property bool currentIsVideo: false
    property bool currentIsAnimated: false

    onIndexChanged: syncCurrent()

    function isAnimatedUrl(u: string): bool {
        if (!u) return false;
        const clean = u.split("?")[0].split("#")[0].toLowerCase();
        return clean.endsWith(".gif") || clean.endsWith("_gif")
            || clean.indexOf("zgif") >= 0 || clean.indexOf("zalo-gif") >= 0;
    }

    function syncCurrent(): void {
        count = items ? items.length : 0;
        if (count > 0 && index >= 0 && index < count) {
            const cur = items[index] || ({});
            const nextUrl = cur.url ? String(cur.url) : "";
            const nextIsVideo = !!cur.isVideo;
            if (currentIsVideo !== nextIsVideo) currentUrl = "";
            currentIsVideo = nextIsVideo;
            currentIsAnimated = !nextIsVideo && (!!cur.animated || isAnimatedUrl(nextUrl));
            currentSenderName = cur.senderName ? String(cur.senderName) : "";
            currentIsMine = !!cur.isMine;
            currentTs = Number(cur.ts || 0);
            currentUrl = nextUrl;
        } else {
            currentUrl = "";
            currentSenderName = "";
            currentIsMine = false;
            currentTs = 0;
            currentIsVideo = false;
            currentIsAnimated = false;
        }
    }

    function show(list: var, start: int): void {
        items = list || [];
        index = Math.max(0, Math.min(start, items.length - 1));
        syncCurrent();
        open = items.length > 0;
    }

    function close(): void {
        open = false;
        items = [];
        syncCurrent();
    }
}
