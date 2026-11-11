import QtQuick
import Zalos
import "MediaSource.js" as MediaSource

Item {
    id: root
    property url source: ""
    property alias status: img.status
    property alias sourceSize: img.sourceSize
    property alias fillMode: img.fillMode
    property alias implicitImageWidth: img.implicitWidth
    property alias implicitImageHeight: img.implicitHeight
    property url animatedSource: ""
    property real radius: 8
    property bool topOnly: false
    readonly property bool gpu: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property bool masked: gpu && maskLoader.status === Loader.Ready

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Theme.mediaPlaceholder
        visible: img.status !== Image.Ready
    }
    Item {
        id: content
        anchors.fill: parent
        layer.enabled: root.masked
        visible: !root.masked
        Image {
            id: img
            anchors.fill: parent
            source: MediaSource.toSource(root.source)
            asynchronous: true
            cache: true
            smooth: true
            fillMode: Image.PreserveAspectCrop
        }
        Loader {
            anchors.fill: parent
            active: root.animatedSource.toString().length > 0
            sourceComponent: AnimatedImage {
                source: root.animatedSource
                cache: false
                sourceSize.width: Math.ceil(root.width * (Screen.devicePixelRatio || 1))
                smooth: true
                fillMode: img.fillMode
                playing: visible
                visible: status === Image.Ready
            }
        }
    }
    Item {
        id: maskShape
        anchors.fill: parent
        layer.enabled: root.masked
        visible: false
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
    Loader {
        id: maskLoader
        anchors.fill: content
        active: root.gpu
        source: "ZMaskEffect.qml"
        onLoaded: {
            maskLoader.item.source = content;
            maskLoader.item.maskSource = maskShape;
        }
    }
}
