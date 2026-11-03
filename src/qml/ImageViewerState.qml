pragma Singleton
import QtQuick

// Which photos the image viewer shows. ChatArea calls show(); Main.qml creates ImageViewer only while open.
QtObject {
    property bool open: false
    property var items: []   // [{msgId, url, thumb, senderName, isMine, ts}] from MessageModel.mediaItems()
    property int index: 0

    function show(list: var, start: int): void {
        items = list;
        index = Math.max(0, Math.min(start, list.length - 1));
        open = list.length > 0;
    }

    function close(): void {
        open = false;
        items = [];
    }
}
