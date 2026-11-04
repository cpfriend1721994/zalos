pragma Singleton
import QtQuick

QtObject {
    property bool open: false
    property var items: []
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
