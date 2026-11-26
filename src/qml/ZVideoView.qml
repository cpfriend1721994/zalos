import QtQuick
import QtMultimedia
import Zalos

Item {
    id: root
    property int fillMode: VideoOutput.PreserveAspectFit
    readonly property bool useSoftware: ZaloClient.softwareRenderer || GraphicsInfo.api === GraphicsInfo.Software
    readonly property QtObject output: viewLoader.item
    readonly property QtObject videoSink: viewLoader.item ? viewLoader.item.videoSink : null

    Loader {
        id: viewLoader
        anchors.fill: parent
        sourceComponent: root.useSoftware ? softwareComp : hardwareComp
    }

    Component {
        id: hardwareComp
        VideoOutput {
            fillMode: root.fillMode
        }
    }

    Component {
        id: softwareComp
        ZSoftwareVideoItem {
            fillMode: root.fillMode
        }
    }
}
