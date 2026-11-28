pragma Singleton
import QtQuick
import Zalos

QtObject {
    property var window: null
    signal shortcutsRequested()
    readonly property bool showing: window ? (window.visible && window.visibility !== Window.Minimized) : true
}
