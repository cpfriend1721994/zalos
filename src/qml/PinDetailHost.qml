import QtQuick
import Zalos

PopupHost {
    property string topicId: ""
    readonly property var detail: topicId.length > 0 ? ZaloClient.pins.detail(topicId) : ({})
}
