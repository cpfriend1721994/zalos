import QtQuick
import QtQuick.Window
import Zalos
import "MediaSource.js" as MediaSource

Item {
    id: root
    readonly property bool windowShowing: Window.window ? (Window.window.visible && Window.window.visibility !== Window.Minimized) : true
    property url source: ""
    readonly property AnimatedImage anim: animLoader.item as AnimatedImage
    readonly property bool animReady: root.anim !== null && root.anim.status === Image.Ready
    readonly property int status: root.animReady ? Image.Ready : img.status
    property alias sourceSize: img.sourceSize
    property alias fillMode: img.fillMode
    readonly property real implicitImageWidth: (root.animReady && root.anim.implicitWidth > 0)
                                               ? root.anim.implicitWidth : img.implicitWidth
    readonly property real implicitImageHeight: (root.animReady && root.anim.implicitHeight > 0)
                                                ? root.anim.implicitHeight : img.implicitHeight
    property url animatedSource: ""
    property real radius: 8
    property bool topOnly: false
    property bool cache: true
    property bool errorGlyph: true
    readonly property bool gpu: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property bool ready: (img.status === Image.Ready && img.source.toString().length > 0) || root.animReady
    readonly property real screenDpr: Screen.devicePixelRatio || 1
    property bool maskArmed: false
    readonly property bool masked: gpu && maskLoader.status === Loader.Ready

    onReadyChanged: if (ready) maskArmed = true
    onAnimReadyChanged: if (!animReady && (img.status === Image.Null || img.status === Image.Error)) maskArmed = false
    onWidthChanged: {
        if (masked && !contentLayer.live) contentLayer.scheduleUpdate();
        if (masked) maskLayer.scheduleUpdate();
    }
    onHeightChanged: {
        if (masked && !contentLayer.live) contentLayer.scheduleUpdate();
        if (masked) maskLayer.scheduleUpdate();
    }
    onRadiusChanged: if (masked) maskLayer.scheduleUpdate()
    onTopOnlyChanged: if (masked) maskLayer.scheduleUpdate()
    onScreenDprChanged: if (masked) maskLayer.scheduleUpdate()
    onMaskedChanged: {
        if (masked && !contentLayer.live) contentLayer.scheduleUpdate();
        if (masked) maskLayer.scheduleUpdate();
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Theme.mediaPlaceholder
        visible: !root.ready
        ZIcon {
            objectName: "roundedImageErrorGlyph"
            anchors.centerIn: parent
            visible: root.errorGlyph && img.status === Image.Error && !root.animReady && root.width >= 24 && root.height >= 24
            name: "image"
            size: Math.min(24, Math.round(Math.min(root.width, root.height) * 0.45))
            color: ThemeManager.secondaryTextColor
        }
    }
    Item {
        id: content
        anchors.fill: parent
        Image {
            id: img
            anchors.fill: parent
            source: MediaSource.toSource(root.source)
            asynchronous: true
            cache: root.cache
            smooth: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 1024
            sourceSize.height: 2048
            visible: !root.animReady
            onStatusChanged: {
                if (status === Image.Ready && root.masked) {
                    if (!contentLayer.live) contentLayer.scheduleUpdate();
                    maskLayer.scheduleUpdate();
                } else if ((status === Image.Null || status === Image.Error) && !root.animReady) {
                    root.maskArmed = false;
                }
            }
            onSourceChanged: if (root.masked && !contentLayer.live) contentLayer.scheduleUpdate()
        }
        Loader {
            id: animLoader
            anchors.fill: parent
            active: root.animatedSource.toString().length > 0
            sourceComponent: AnimatedImage {
                source: root.animatedSource
                cache: false
                smooth: true
                fillMode: img.fillMode
                playing: status === Image.Ready && root.visible && root.windowShowing
                visible: status === Image.Ready
            }
        }
    }
    ShaderEffectSource {
        id: contentLayer
        anchors.fill: content
        sourceItem: root.masked ? content : null
        hideSource: root.masked
        live: root.animReady
        visible: false
    }
    Item {
        id: maskShape
        anchors.fill: parent
        visible: root.masked
        Rectangle {
            anchors.fill: parent
            radius: root.radius
            Rectangle {
                visible: root.topOnly
                anchors.bottom: parent.bottom
                width: parent.width
                height: root.radius
            }
        }
    }
    ShaderEffectSource {
        id: maskLayer
        anchors.fill: maskShape
        sourceItem: root.masked ? maskShape : null
        hideSource: root.masked
        live: false
        visible: false
    }
    Loader {
        id: maskLoader
        anchors.fill: content
        active: ZaloClient.effectsAvailable && root.gpu && root.radius > 0 && (root.ready || root.maskArmed)
        visible: root.ready
        source: "ZMaskEffect.qml"
        onLoaded: {
            maskLoader.item.source = contentLayer;
            maskLoader.item.maskSource = maskLayer;
            if (!contentLayer.live) contentLayer.scheduleUpdate();
            maskLayer.scheduleUpdate();
        }
    }
}
