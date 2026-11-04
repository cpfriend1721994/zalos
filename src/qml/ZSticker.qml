import QtQuick
import Zalos

Item {
    id: root
    property var info: ({})
    property string fallbackUrl: ""
    property bool playing: true

    readonly property string stillUrl: (root.info && root.info.url) ? root.info.url : root.fallbackUrl
    readonly property string animatedUrl: (root.info && root.info.animatedUrl) ? root.info.animatedUrl : ""
    readonly property string spriteUrl: (root.info && root.info.spriteUrl) ? root.info.spriteUrl : ""
    readonly property int frames: (root.info && root.info.frames) ? root.info.frames : 1
    readonly property int duration: (root.info && root.info.duration) ? root.info.duration : 100
    readonly property bool spriteMode: root.animatedUrl.length === 0 && root.spriteUrl.length > 0 && root.frames > 1
    readonly property int status: animLoader.item && animLoader.item.status === Image.Ready ? Image.Ready
                                  : spriteImage.status === Image.Ready ? Image.Ready : stillImage.status
    property int frame: 0
    readonly property real side: Math.min(width, height)

    Image {
        id: stillImage
        anchors.fill: parent
        source: root.stillUrl
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.ceil(root.width * (Screen.devicePixelRatio || 1))
        visible: !(animLoader.item && animLoader.item.status === Image.Ready)
                 && !(root.spriteMode && spriteImage.status === Image.Ready)
    }

    Loader {
        id: animLoader
        anchors.fill: parent
        active: root.animatedUrl.length > 0
        sourceComponent: AnimatedImage {
            source: root.animatedUrl
            cache: false
            fillMode: Image.PreserveAspectFit
            playing: root.playing && visible
            visible: status === Image.Ready
        }
    }

    Item {
        anchors.centerIn: parent
        width: root.side
        height: root.side
        clip: true
        visible: root.spriteMode && spriteImage.status === Image.Ready
        Image {
            id: spriteImage
            source: root.spriteMode ? root.spriteUrl : ""
            asynchronous: true
            cache: true
            height: root.side
            width: root.side * root.frames
            x: -root.frame * root.side
            fillMode: Image.Stretch
            sourceSize.height: Math.ceil(root.side * (Screen.devicePixelRatio || 1))
        }
    }
    Timer {
        interval: root.duration
        repeat: true
        running: root.spriteMode && root.playing && root.visible && spriteImage.status === Image.Ready
        onTriggered: root.frame = (root.frame + 1) % root.frames
    }
}
