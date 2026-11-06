import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import Zalos

Rectangle {
    id: root
    color: Qt.rgba(0, 0, 0, 0.92)
    focus: true

    readonly property var items: ImageViewerState.items
    readonly property int count: items.length
    readonly property var current: count > 0 ? items[ImageViewerState.index] : ({})
    readonly property bool isVideo: !!(root.current && root.current.isVideo)
    property real zoom: 1.0
    property real angle: 0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 8.0
    property var videoPlayer: null
    readonly property bool videoPlaying: !!videoPlayer && videoPlayer.playbackState === MediaPlayer.PlayingState
    readonly property bool videoLoading: !!videoPlayer && (videoPlayer.mediaStatus === MediaPlayer.LoadingMedia
                                                           || videoPlayer.mediaStatus === MediaPlayer.BufferingMedia)
    readonly property bool videoFailed: !!videoPlayer && videoPlayer.error !== MediaPlayer.NoError
    readonly property bool videoMuted: !videoPlayer || videoPlayer.audioOutput.muted
    readonly property real videoPosition: videoPlayer ? videoPlayer.position : 0
    readonly property real videoDuration: videoPlayer ? videoPlayer.duration : 0
    readonly property bool videoSeekable: !!videoPlayer && videoPlayer.seekable

    function resetView(): void {
        zoom = 1.0;
        angle = 0;
        photo.x = 0;
        photo.y = 0;
    }
    function go(delta: int): void {
        if (count < 2) return;
        stopVideo();
        ImageViewerState.index = (ImageViewerState.index + delta + count) % count;
        resetView();
    }
    function zoomBy(factor: real): void {
        if (root.isVideo) return;
        zoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor));
        if (zoom <= 1.0) { photo.x = 0; photo.y = 0; }
    }
    function timeText(ts: real): string {
        if (!ts) return "";
        return Qt.formatDateTime(new Date(ts), "HH:mm dd/MM/yyyy");
    }
    function formatDuration(ms: real): string {
        if (!ms || ms < 0) return "00:00";
        const totalSec = Math.floor(ms / 1000);
        const m = Math.floor(totalSec / 60);
        const s = totalSec % 60;
        return (m < 10 ? "0" + m : String(m)) + ":" + (s < 10 ? "0" + s : String(s));
    }
    function toggleVideoPlayback(): void {
        if (!root.isVideo || !videoPlayer) return;
        if (videoPlayer.playbackState === MediaPlayer.PlayingState) videoPlayer.pause();
        else videoPlayer.play();
    }
    function stopVideo(): void { if (videoPlayer) videoPlayer.stop(); }
    function loadVideo(sourceUrl: url): void {
        if (!videoPlayer) videoPlayer = videoPlayerComponent.createObject(root);
        if (!videoPlayer) return;
        videoPlayer.source = sourceUrl;
        videoPlayer.play();
    }

    onCurrentChanged: {
        stopVideo();
        if (root.isVideo && root.current.url) loadVideo(root.current.url);
        else if (videoPlayer) videoPlayer.source = "";
    }

    Component.onCompleted: {
        forceActiveFocus();
        if (root.isVideo && root.current.url) loadVideo(root.current.url);
    }
    Component.onDestruction: stopVideo()
    Keys.onEscapePressed: { stopVideo(); ImageViewerState.close(); }
    Keys.onLeftPressed: go(-1)
    Keys.onRightPressed: go(1)
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Space && root.isVideo) { root.toggleVideoPlayback(); event.accepted = true; }
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) { zoomBy(1.25); event.accepted = true; }
        else if (event.key === Qt.Key_Minus) { zoomBy(0.8); event.accepted = true; }
        else if (event.key === Qt.Key_0) { resetView(); event.accepted = true; }
        else if (event.key === Qt.Key_R && !root.isVideo) { angle += (event.modifiers & Qt.ShiftModifier) ? -90 : 90; event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C && !root.isVideo) { ZaloClient.copyImage(current.url); event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_S) { saveDialog.open(); event.accepted = true; }
    }

    Component {
        id: videoPlayerComponent
        MediaPlayer {
            audioOutput: AudioOutput { muted: false }
            videoOutput: videoOut
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: { stopVideo(); ImageViewerState.close(); }
    }

    Item {
        id: stage
        anchors.top: topBar.bottom
        anchors.bottom: videoControls.visible ? videoControls.top : (strip.visible ? strip.top : parent.bottom)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        clip: true

        Image {
            id: photo
            visible: !root.isVideo
            width: stage.width
            height: stage.height
            source: !root.isVideo ? (root.current.url || "") : ""
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            sourceSize.width: 4096
            sourceSize.height: 4096
            scale: root.zoom
            rotation: root.angle
            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            Behavior on rotation { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

            MouseArea {
                anchors.centerIn: parent
                width: photo.paintedWidth
                height: photo.paintedHeight
                acceptedButtons: Qt.LeftButton
                cursorShape: root.zoom > 1.0 ? (dragHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
                onDoubleClicked: root.zoom > 1.0 ? root.resetView() : root.zoomBy(2.0)
            }
            DragHandler {
                id: dragHandler
                enabled: !root.isVideo && root.zoom > 1.0
                target: photo
            }
        }

        VideoOutput {
            id: videoOut
            visible: root.isVideo
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggleVideoPlayback()
            }

            Rectangle {
                visible: root.isVideo && !root.videoPlaying && !root.videoFailed
                anchors.centerIn: parent
                width: 64
                height: 64
                radius: 32
                color: Qt.rgba(0, 0, 0, 0.6)
                ZIcon { anchors.centerIn: parent; name: "play"; size: 32; color: "#FFFFFF" }
            }
        }

        WheelHandler {
            enabled: !root.isVideo
            target: null
            onWheel: function(event) { root.zoomBy(event.angleDelta.y > 0 ? 1.15 : 1 / 1.15); }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: (!root.isVideo && photo.status === Image.Loading) || (root.isVideo && root.videoLoading)
            visible: running
        }
        ColumnLayout {
            anchors.centerIn: parent
            visible: (!root.isVideo && photo.status === Image.Error) || (root.isVideo && root.videoFailed)
            spacing: 10
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.isVideo ? I18n.t("Không phát được video trực tiếp trong ứng dụng", "Couldn't play the video in-app")
                                   : I18n.t("Không tải được ảnh", "Couldn't load the picture")
                color: "#CFD3D8"
                font.family: Theme.fontFamily
                font.pixelSize: 14
            }
            ZButton {
                visible: root.isVideo && String(root.current.url || "").length > 0
                Layout.alignment: Qt.AlignHCenter
                text: I18n.t("Mở bằng trình phát ngoài", "Open in external player")
                onClicked: Qt.openUrlExternally(root.current.url)
            }
        }

        Repeater {
            model: [ { step: -1, icon: "chevron_left", left: true }, { step: 1, icon: "chevron_right", left: false } ]
            Rectangle {
                required property var modelData
                visible: root.count > 1
                width: 44
                height: 44
                radius: 22
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: modelData.left ? parent.left : undefined
                anchors.right: modelData.left ? undefined : parent.right
                color: navMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.28) : Qt.rgba(1, 1, 1, 0.14)
                ZIcon { anchors.centerIn: parent; name: modelData.icon; size: 24; color: "#FFFFFF" }
                MouseArea {
                    id: navMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.go(modelData.step)
                }
            }
        }
    }

    Rectangle {
        id: videoControls
        visible: root.isVideo
        anchors.bottom: strip.visible ? strip.top : parent.bottom
        anchors.bottomMargin: strip.visible ? 8 : 16
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 48, 640)
        height: 44
        radius: 22
        color: Qt.rgba(0, 0, 0, 0.65)
        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 14
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 16
                color: playBtnMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : ThemeManager.hoverClearColor
                ZIcon {
                    anchors.centerIn: parent
                    name: root.videoPlaying ? "pause" : "play"
                    size: 18
                    color: "#FFFFFF"
                }
                MouseArea {
                    id: playBtnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleVideoPlayback()
                }
            }

            Text {
                text: root.formatDuration(root.videoPosition) + " / " + root.formatDuration(root.videoDuration)
                color: "#E5E7EB"
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, root.videoDuration)
                value: pressed ? value : root.videoPosition
                onMoved: {
                    if (root.videoSeekable) videoPlayer.position = value;
                }
                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    implicitWidth: 200
                    implicitHeight: 4
                    width: seekSlider.availableWidth
                    height: implicitHeight
                    radius: 2
                    color: Qt.rgba(1, 1, 1, 0.25)
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        color: Theme.zaloBlue
                        radius: 2
                    }
                }
                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    implicitWidth: 12
                    implicitHeight: 12
                    radius: 6
                    color: "#FFFFFF"
                }
            }

            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 16
                color: muteBtnMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : ThemeManager.hoverClearColor
                ZIcon {
                    anchors.centerIn: parent
                    name: root.videoMuted ? "volume_off" : "volume"
                    size: 18
                    color: "#FFFFFF"
                }
                MouseArea {
                    id: muteBtnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (videoPlayer) videoPlayer.audioOutput.muted = !videoPlayer.audioOutput.muted
                }
            }
        }
    }

    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
        color: Qt.rgba(0, 0, 0, 0.35)
        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 12
            spacing: 6

            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Text {
                    text: root.current.isMine ? I18n.t("Bạn", "You") : (root.current.senderName || "")
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: root.timeText(root.current.ts || 0)
                    color: "#AEB4BC"
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
            }

            Text {
                visible: root.count > 1
                text: (ImageViewerState.index + 1) + " / " + root.count
                color: "#CFD3D8"
                font.family: Theme.fontFamily
                font.pixelSize: 13
                Layout.rightMargin: 12
            }

            component ToolButtonIcon: Rectangle {
                id: tb
                property string icon: ""
                property string tip: ""
                property string label: ""
                signal clicked()
                Layout.preferredHeight: 36
                Layout.preferredWidth: label.length > 0 ? labelText.implicitWidth + 20 : 36
                radius: 18
                color: tbMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : ThemeManager.hoverClearColor
                ZIcon { visible: tb.icon.length > 0; anchors.centerIn: parent; name: tb.icon; size: 20; color: "#FFFFFF" }
                Text {
                    id: labelText
                    visible: tb.label.length > 0
                    anchors.centerIn: parent
                    text: tb.label
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
                MouseArea {
                    id: tbMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: containsMouse && tb.tip.length > 0
                    ToolTip.delay: 400
                    ToolTip.text: tb.tip
                    onClicked: tb.clicked()
                }
            }

            ToolButtonIcon { visible: !root.isVideo; icon: "zoom_out"; tip: I18n.t("Thu nhỏ (−)", "Zoom out (−)"); onClicked: root.zoomBy(0.8) }
            ToolButtonIcon { visible: !root.isVideo; label: Math.round(root.zoom * 100) + "%"; tip: I18n.t("Vừa khung (0)", "Fit (0)"); onClicked: root.resetView() }
            ToolButtonIcon { visible: !root.isVideo; icon: "zoom_in"; tip: I18n.t("Phóng to (+)", "Zoom in (+)"); onClicked: root.zoomBy(1.25) }
            ToolButtonIcon { visible: !root.isVideo; icon: "rotate"; tip: I18n.t("Xoay (R)", "Rotate (R)"); onClicked: root.angle += 90 }
            ToolButtonIcon { visible: !root.isVideo; icon: "copy"; tip: I18n.t("Sao chép ảnh (Ctrl+C)", "Copy picture (Ctrl+C)"); onClicked: ZaloClient.copyImage(root.current.url) }
            ToolButtonIcon {
                icon: "download"
                tip: root.isVideo ? I18n.t("Lưu video (Ctrl+S)", "Save video (Ctrl+S)")
                                  : I18n.t("Lưu ảnh (Ctrl+S)", "Save picture (Ctrl+S)")
                onClicked: saveDialog.open()
            }
            ToolButtonIcon {
                visible: String(root.current.url || "").startsWith("http")
                icon: "open_external"
                tip: root.isVideo ? I18n.t("Mở video trong trình duyệt", "Open video in browser")
                                  : I18n.t("Mở ảnh gốc trong trình duyệt", "Open original in browser")
                onClicked: Qt.openUrlExternally(root.current.url)
            }
            ToolButtonIcon { icon: "close"; tip: I18n.t("Đóng (Esc)", "Close (Esc)"); onClicked: { stopVideo(); ImageViewerState.close(); } }
        }
    }

    ListView {
        id: strip
        visible: root.count > 1
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 48, root.count * 64)
        height: 56
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        model: root.items
        currentIndex: ImageViewerState.index
        highlightMoveDuration: 150
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            required property var modelData
            required property int index
            width: 56
            height: 56
            radius: 6
            color: "transparent"
            border.width: index === ImageViewerState.index ? 2 : 0
            border.color: "#FFFFFF"
            opacity: index === ImageViewerState.index ? 1.0 : 0.55
            Image {
                anchors.fill: parent
                anchors.margins: 2
                source: modelData.thumb || modelData.url
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 112
                sourceSize.height: 112
            }
            Rectangle {
                visible: !!(modelData.isVideo)
                anchors.centerIn: parent
                width: 22
                height: 22
                radius: 11
                color: Qt.rgba(0, 0, 0, 0.6)
                ZIcon { anchors.centerIn: parent; name: "play"; size: 12; color: "#FFFFFF" }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { stopVideo(); ImageViewerState.index = index; root.resetView(); }
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: root.isVideo ? I18n.t("Lưu video", "Save video") : I18n.t("Lưu ảnh", "Save picture")
        fileMode: FileDialog.SaveFile
        currentFile: "file:///" + ZaloClient.suggestedMediaFileName(root.current.url || "")
        nameFilters: root.isVideo
            ? [I18n.t("Video (*.mp4 *.mov *.webm *.mkv)", "Videos (*.mp4 *.mov *.webm *.mkv)"),
               I18n.t("Tất cả tệp (*)", "All files (*)")]
            : [I18n.t("Hình ảnh (*.jpg *.jpeg *.png *.webp *.gif)", "Images (*.jpg *.jpeg *.png *.webp *.gif)"),
               I18n.t("Tất cả tệp (*)", "All files (*)")]
        onAccepted: ZaloClient.saveMediaTo(root.current.url, selectedFile.toString())
    }
}
