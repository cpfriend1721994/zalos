pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia
import Zalos
import "MediaSource.js" as MediaSource

Rectangle {
    id: root
    objectName: "imageViewerRoot"
    color: "#EB000000"
    focus: true

    readonly property int count: ImageViewerState.count
    readonly property string currentUrl: ImageViewerState.currentUrl
    readonly property bool isVideo: ImageViewerState.currentIsVideo
    readonly property bool isAnimated: ImageViewerState.currentIsAnimated
    property real zoom: 1.0
    property real angle: 0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 8.0
    property bool annotating: false
    property string currentDrawTool: "pen"
    property color drawColor: "#FF3B30"
    property var strokes: []
    property var currentStroke: null

    function copyCurrentImage(): void {
        if (root.strokes.length > 0 && !root.isVideo) {
            photo.grabToImage(function(result) {
                const tempPath = ZaloClient.annotatedImageTempPath()
                if (result.saveToFile(tempPath)) ZaloClient.copyImage("file://" + tempPath)
            })
        } else {
            ZaloClient.copyImage(root.currentUrl)
        }
    }
    property MediaPlayer videoPlayer: null
    property bool videoFetching: false
    property bool videoFetchError: false
    readonly property bool videoPlaying: !!videoPlayer && videoPlayer.playbackState === MediaPlayer.PlayingState
    readonly property bool videoLoading: root.videoFetching || (!!videoPlayer && (videoPlayer.mediaStatus === MediaPlayer.LoadingMedia
                                                           || videoPlayer.mediaStatus === MediaPlayer.BufferingMedia))
    readonly property bool videoFailed: (root.isVideo && !ZaloClient.multimediaAvailable)
                                        || root.videoFetchError
                                        || (!!videoPlayer && videoPlayer.error !== MediaPlayer.NoError)
    readonly property bool videoMuted: !videoPlayer || videoPlayer.audioOutput.muted
    readonly property real videoPosition: videoPlayer ? videoPlayer.position : 0
    readonly property real videoDuration: videoPlayer ? videoPlayer.duration : 0
    readonly property bool videoSeekable: !!videoPlayer && videoPlayer.seekable

    function requestCanvasRepaint(): void {
        if (drawCanvas.visible) {
            drawCanvas.requestPaint();
        }
    }

    function resetView(): void {
        zoom = 1.0;
        angle = 0;
        photo.x = 0;
        photo.y = 0;
        strokes = [];
        annotating = false;
        requestCanvasRepaint();
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
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
    function toggleVideoPlayback(): void {
        if (!root.isVideo || !videoPlayer) return;
        if (videoPlayer.playbackState === MediaPlayer.PlayingState) videoPlayer.pause();
        else videoPlayer.play();
    }
    function stopVideo(): void { if (videoPlayer) videoPlayer.stop(); }
    function releaseVideo(): void {
        videoFetching = false;
        videoFetchError = false;
        if (!videoPlayer) return;
        videoPlayer.stop();
        videoPlayer.source = "";
    }
    function loadVideo(sourceUrl: string): void {
        if (!ZaloClient.multimediaAvailable) return;
        videoFetchError = false;
        const lower = sourceUrl.toLowerCase();
        if (lower.startsWith("http://") || lower.startsWith("https://")) {
            videoFetching = true;
            ZaloClient.prepareVideoPlayback(sourceUrl);
            return;
        }
        videoFetching = false;
        if (!videoPlayer) videoPlayer = videoPlayerComponent.createObject(root) as MediaPlayer;
        if (!videoPlayer) return;
        videoPlayer.source = sourceUrl;
        videoPlayer.play();
    }

    onCurrentUrlChanged: {
        root.stopVideo();
        if (root.isVideo && root.currentUrl.length > 0) {
            root.loadVideo(root.currentUrl);
        } else if (videoPlayer) {
            releaseVideo();
            videoPlayer.destroy();
            videoPlayer = null;
        }
    }

    Component.onCompleted: {
        forceActiveFocus();
        if (root.isVideo && root.currentUrl) loadVideo(root.currentUrl);
    }
    Component.onDestruction: releaseVideo()
    Keys.onEscapePressed: { root.stopVideo(); ImageViewerState.close(); }
    Keys.onLeftPressed: go(-1)
    Keys.onRightPressed: go(1)
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Space && root.isVideo) { root.toggleVideoPlayback(); event.accepted = true; }
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) { zoomBy(1.25); event.accepted = true; }
        else if (event.key === Qt.Key_Minus) { zoomBy(0.8); event.accepted = true; }
        else if (event.key === Qt.Key_0) { resetView(); event.accepted = true; }
        else if (event.key === Qt.Key_R && !root.isVideo) { angle += (event.modifiers & Qt.ShiftModifier) ? -90 : 90; event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C && !root.isVideo) { root.copyCurrentImage(); event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_P && !root.isVideo) { ZaloClient.printMedia(root.currentUrl); event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_S) { saveDialog.open(); event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z && root.annotating) {
            if (root.strokes.length > 0) {
                root.strokes = root.strokes.slice(0, -1);
                root.requestCanvasRepaint();
            }
            event.accepted = true;
        }
    }

    Component {
        id: videoPlayerComponent
        MediaPlayer {
            audioOutput: AudioOutput { muted: false }
            videoOutput: videoOut.item ? videoOut.item.output : null
        }
    }

    Connections {
        target: ZaloClient
        function onVideoPlaybackReady(originalUrl: string, localFileUrl: string, error: string): void {
            if (!root.isVideo || originalUrl !== root.currentUrl) return;
            root.videoFetching = false;
            if (error.length > 0 || localFileUrl.length === 0) {
                root.videoFetchError = true;
                return;
            }
            root.videoFetchError = false;
            if (!root.videoPlayer) root.videoPlayer = videoPlayerComponent.createObject(root) as MediaPlayer;
            if (!root.videoPlayer) return;
            root.videoPlayer.source = localFileUrl;
            root.videoPlayer.play();
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: { root.stopVideo(); ImageViewerState.close(); }
    }

    Item {
        id: stage
        anchors.top: topBar.bottom
        anchors.bottom: videoControls.visible ? videoControls.top : (strip.visible ? strip.top : parent.bottom)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        clip: true

        Item {
            id: photo
            visible: !root.isVideo
            width: stage.width
            height: stage.height
            readonly property bool animReady: animPhoto.source.toString().length > 0 && animPhoto.status === Image.Ready
            readonly property real paintedWidth: animReady ? animPhoto.paintedWidth : stillPhoto.paintedWidth
            readonly property real paintedHeight: animReady ? animPhoto.paintedHeight : stillPhoto.paintedHeight
            readonly property int status: animReady ? Image.Ready : stillPhoto.status
            scale: root.zoom
            rotation: root.angle
            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            Behavior on rotation { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

            Image {
                id: stillPhoto
                anchors.fill: parent
                visible: !photo.animReady
                source: !root.isVideo ? MediaSource.toSource(root.currentUrl) : ""
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                sourceSize.width: 2048
                sourceSize.height: 2048
            }

            AnimatedImage {
                id: animPhoto
                anchors.fill: parent
                source: (!root.isVideo && root.isAnimated && root.currentUrl.length > 0) ? root.currentUrl : ""
                cache: false
                smooth: true
                fillMode: Image.PreserveAspectFit
                playing: status === Image.Ready && root.visible && !root.annotating
                visible: status === Image.Ready
            }

            MouseArea {
                anchors.centerIn: parent
                width: photo.paintedWidth
                height: photo.paintedHeight
                acceptedButtons: Qt.LeftButton
                enabled: !root.annotating
                cursorShape: root.zoom > 1.0 ? (dragHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
                onDoubleClicked: root.zoom > 1.0 ? root.resetView() : root.zoomBy(2.0)
            }
            DragHandler {
                id: dragHandler
                enabled: !root.isVideo && root.zoom > 1.0 && !root.annotating
                target: photo
            }

            Canvas {
                id: drawCanvas
                anchors.centerIn: parent
                width: photo.paintedWidth
                height: photo.paintedHeight
                visible: root.annotating && !root.isVideo && photo.status === Image.Ready
                z: 2
                onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        for (var i = 0; i < root.strokes.length; i++) {
                            var s = root.strokes[i]
                            ctx.strokeStyle = s.color
                            ctx.fillStyle = s.color
                            ctx.lineWidth = 3
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            if (s.type === "pen" && s.points && s.points.length > 1) {
                                ctx.beginPath()
                                ctx.moveTo(s.points[0].x, s.points[0].y)
                                for (var j = 1; j < s.points.length; j++) ctx.lineTo(s.points[j].x, s.points[j].y)
                                ctx.stroke()
                            } else if (s.type === "rect") {
                                ctx.strokeRect(s.x, s.y, s.w, s.h)
                            } else if (s.type === "arrow") {
                                ctx.beginPath()
                                ctx.moveTo(s.x1, s.y1)
                                ctx.lineTo(s.x2, s.y2)
                                ctx.stroke()
                                var angle = Math.atan2(s.y2 - s.y1, s.x2 - s.x1)
                                ctx.beginPath()
                                ctx.moveTo(s.x2, s.y2)
                                ctx.lineTo(s.x2 - 14 * Math.cos(angle - Math.PI / 6), s.y2 - 14 * Math.sin(angle - Math.PI / 6))
                                ctx.lineTo(s.x2 - 14 * Math.cos(angle + Math.PI / 6), s.y2 - 14 * Math.sin(angle + Math.PI / 6))
                                ctx.closePath()
                                ctx.fill()
                            } else if (s.type === "text") {
                                ctx.font = "bold 16px sans-serif"
                                ctx.fillText(s.text, s.x, s.y)
                            }
                        }
                        if (root.currentStroke) {
                            var cs = root.currentStroke
                            ctx.strokeStyle = cs.color
                            ctx.fillStyle = cs.color
                            ctx.lineWidth = 3
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            if (cs.type === "pen" && cs.points && cs.points.length > 1) {
                                ctx.beginPath()
                                ctx.moveTo(cs.points[0].x, cs.points[0].y)
                                for (var k = 1; k < cs.points.length; k++) ctx.lineTo(cs.points[k].x, cs.points[k].y)
                                ctx.stroke()
                            } else if (cs.type === "rect") {
                                ctx.strokeRect(cs.x, cs.y, cs.w, cs.h)
                            } else if (cs.type === "arrow") {
                                ctx.beginPath()
                                ctx.moveTo(cs.x1, cs.y1)
                                ctx.lineTo(cs.x2, cs.y2)
                                ctx.stroke()
                            }
                        }
                    }

                    MouseArea {
                        id: drawMouse
                        anchors.fill: parent
                        enabled: root.annotating && !root.isVideo
                        cursorShape: Qt.CrossCursor
                        preventStealing: true
                        onPressed: function(mouse) {
                            if (root.currentDrawTool === "pen") {
                                root.currentStroke = { type: "pen", color: root.drawColor.toString(), points: [{x: mouse.x, y: mouse.y}] }
                            } else if (root.currentDrawTool === "rect") {
                                root.currentStroke = { type: "rect", color: root.drawColor.toString(), startX: mouse.x, startY: mouse.y, x: mouse.x, y: mouse.y, w: 0, h: 0 }
                            } else if (root.currentDrawTool === "arrow") {
                                root.currentStroke = { type: "arrow", color: root.drawColor.toString(), x1: mouse.x, y1: mouse.y, x2: mouse.x, y2: mouse.y }
                            } else if (root.currentDrawTool === "text") {
                                textInputDialog.targetX = mouse.x;
                                textInputDialog.targetY = mouse.y;
                                textInputDialog.open();
                            }
                        }
                        onPositionChanged: function(mouse) {
                            if (!root.currentStroke) return
                            if (root.currentDrawTool === "pen") {
                                const pts = root.currentStroke.points
                                const last = pts[pts.length - 1]
                                if (Math.abs(last.x - mouse.x) + Math.abs(last.y - mouse.y) < 2) return
                                pts.push({x: mouse.x, y: mouse.y})
                            } else if (root.currentDrawTool === "rect") {
                                root.currentStroke.x = Math.min(root.currentStroke.startX, mouse.x)
                                root.currentStroke.y = Math.min(root.currentStroke.startY, mouse.y)
                                root.currentStroke.w = Math.abs(mouse.x - root.currentStroke.startX)
                                root.currentStroke.h = Math.abs(mouse.y - root.currentStroke.startY)
                            } else if (root.currentDrawTool === "arrow") {
                                root.currentStroke.x2 = mouse.x
                                root.currentStroke.y2 = mouse.y
                            }
                            drawCanvas.requestPaint()
                        }
                        onReleased: function(mouse) {
                            if (root.currentStroke) {
                                root.strokes = root.strokes.concat([root.currentStroke])
                                root.currentStroke = null
                                drawCanvas.requestPaint()
                            }
                        }
                    }
                }
            }

        Loader {
            id: videoOut
            visible: root.isVideo
            anchors.fill: parent
            active: ZaloClient.multimediaAvailable
            source: active ? "ZVideoView.qml" : ""
        }

        Item {
            visible: root.isVideo && ZaloClient.multimediaAvailable
            anchors.fill: videoOut

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
                color: "#99000000"
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
            visible: (!root.isVideo && photo.status === Image.Loading) || (root.isVideo && root.videoLoading)
            running: visible && (Window.window ? (Window.window.visible && Window.window.visibility !== Window.Minimized) : true)
        }
        ColumnLayout {
            anchors.centerIn: parent
            visible: (!root.isVideo && photo.status === Image.Error) || (root.isVideo && root.videoFailed)
            spacing: 10
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.isVideo ? (ZaloClient.multimediaAvailable
                                      ? I18n.t("Không phát được video trực tiếp trong ứng dụng", "Couldn't play the video in-app")
                                      : I18n.t("Máy thiếu thành phần phát video (Qt Multimedia)", "This system lacks the video playback components (Qt Multimedia)"))
                                   : I18n.t("Không tải được ảnh", "Couldn't load the picture")
                color: "#CFD3D8"
                font.family: Theme.fontFamily
                font.pixelSize: 14
            }
            ZButton {
                visible: root.isVideo && root.currentUrl.length > 0
                Layout.alignment: Qt.AlignHCenter
                text: I18n.t("Mở bằng trình phát ngoài", "Open in external player")
                onClicked: ZaloClient.openExternalUrl(root.currentUrl)
            }
        }

        Repeater {
            model: [ { step: -1, icon: "chevron_left", left: true }, { step: 1, icon: "chevron_right", left: false } ]
            Rectangle {
                id: navBtn
                required property var modelData
                visible: root.count > 1
                width: 44
                height: 44
                radius: 22
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: navBtn.modelData.left ? parent.left : undefined
                anchors.right: navBtn.modelData.left ? undefined : parent.right
                color: navMouse.containsMouse ? "#47FFFFFF" : "#24FFFFFF"
                ZIcon { anchors.centerIn: parent; name: navBtn.modelData.icon; size: 24; color: "#FFFFFF" }
                MouseArea {
                    id: navMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.go(navBtn.modelData.step)
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
        color: "#A6000000"
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
                color: playBtnMouse.containsMouse ? "#2EFFFFFF" : ThemeManager.hoverClearColor
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
                    color: "#40FFFFFF"
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
                color: muteBtnMouse.containsMouse ? "#2EFFFFFF" : ThemeManager.hoverClearColor
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
        color: "#59000000"
        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 12
            spacing: 6

            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                Text {
                    text: ImageViewerState.currentIsMine ? I18n.t("Bạn", "You") : ImageViewerState.currentSenderName
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: root.timeText(ImageViewerState.currentTs)
                    color: "#AEB4BC"
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
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
                property bool active: false
                signal clicked()
                implicitHeight: 36
                implicitWidth: label.length > 0 ? labelText.implicitWidth + 20 : 36
                width: implicitWidth
                height: implicitHeight
                Layout.preferredHeight: implicitHeight
                Layout.preferredWidth: implicitWidth
                radius: 18
                color: active ? Theme.zaloBlue : (tbMouse.containsMouse ? "#2EFFFFFF" : ThemeManager.hoverClearColor)
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

            Flickable {
                id: toolsFlick
                objectName: "imageViewerToolsFlick"
                Layout.fillWidth: true
                Layout.preferredWidth: toolsStrip.implicitWidth
                Layout.minimumWidth: 0
                Layout.preferredHeight: 36
                Layout.maximumWidth: toolsStrip.implicitWidth
                clip: true
                contentWidth: toolsStrip.implicitWidth
                contentHeight: 36
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentWidth > width

                Row {
                    id: toolsStrip
                    objectName: "imageViewerToolsStrip"
                    height: 36
                    spacing: 4

                    ToolButtonIcon { visible: !root.isVideo; icon: "zoom_out"; tip: I18n.t("Thu nhỏ (−)", "Zoom out (−)"); onClicked: root.zoomBy(0.8) }
                    ToolButtonIcon { visible: !root.isVideo; label: Math.round(root.zoom * 100) + "%"; tip: I18n.t("Vừa khung (0)", "Fit (0)"); onClicked: root.resetView() }
                    ToolButtonIcon { visible: !root.isVideo; icon: "zoom_in"; tip: I18n.t("Phóng to (+)", "Zoom in (+)"); onClicked: root.zoomBy(1.25) }
                    ToolButtonIcon { visible: !root.isVideo; icon: "rotate"; tip: I18n.t("Xoay (R)", "Rotate (R)"); onClicked: root.angle += 90 }
                    ToolButtonIcon {
                        visible: !root.isVideo
                        icon: "edit"
                        active: root.annotating
                        tip: root.annotating ? I18n.t("Ẩn thanh công cụ vẽ", "Hide drawing tools") : I18n.t("Vẽ & Ghi chú", "Annotate")
                        onClicked: root.annotating = !root.annotating
                    }
                    ToolButtonIcon {
                        visible: !root.isVideo
                        icon: "copy"
                        tip: root.strokes.length > 0 ? I18n.t("Sao chép ảnh kèm ghi chú (Ctrl+C)", "Copy picture with notes (Ctrl+C)") : I18n.t("Sao chép ảnh (Ctrl+C)", "Copy picture (Ctrl+C)")
                        onClicked: root.copyCurrentImage()
                    }
                    ToolButtonIcon {
                        visible: !root.isVideo
                        icon: "send"
                        tip: I18n.t("In ảnh (Ctrl+P)", "Print picture (Ctrl+P)")
                        onClicked: ZaloClient.printMedia(root.currentUrl)
                    }
                    ToolButtonIcon {
                        icon: "download"
                        tip: root.isVideo ? I18n.t("Lưu video (Ctrl+S)", "Save video (Ctrl+S)")
                                          : I18n.t("Lưu ảnh (Ctrl+S)", "Save picture (Ctrl+S)")
                        onClicked: saveDialog.open()
                    }
                    ToolButtonIcon {
                        visible: root.currentUrl.startsWith("http")
                        icon: "open_external"
                        tip: root.isVideo ? I18n.t("Mở video trong trình duyệt", "Open video in browser")
                                          : I18n.t("Mở ảnh gốc trong trình duyệt", "Open original in browser")
                        onClicked: ZaloClient.openExternalUrl(root.currentUrl)
                    }
                }
            }
            ToolButtonIcon { icon: "close"; tip: I18n.t("Đóng (Esc)", "Close (Esc)"); onClicked: { root.stopVideo(); ImageViewerState.close(); } }
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
        model: ImageViewerState.items
        currentIndex: ImageViewerState.index
        highlightMoveDuration: 150
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            id: thumbTile
            required property var modelData
            required property int index
            width: 56
            height: 56
            radius: 6
            color: "transparent"
            border.width: thumbTile.index === ImageViewerState.index ? 2 : 0
            border.color: "#FFFFFF"
            opacity: thumbTile.index === ImageViewerState.index ? 1.0 : 0.55
            Image {
                anchors.fill: parent
                anchors.margins: 2
                source: MediaSource.toSource(thumbTile.modelData.thumb || thumbTile.modelData.url)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 112
                sourceSize.height: 112
            }
            Rectangle {
                visible: !!(thumbTile.modelData.isVideo)
                anchors.centerIn: parent
                width: 22
                height: 22
                radius: 11
                color: "#99000000"
                ZIcon { anchors.centerIn: parent; name: "play"; size: 12; color: "#FFFFFF" }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { root.stopVideo(); ImageViewerState.index = thumbTile.index; root.resetView(); }
            }
        }
    }

    ZFilePicker {
        id: saveDialog
        title: root.isVideo ? I18n.t("Lưu video", "Save video") : I18n.t("Lưu ảnh", "Save picture")
        fileMode: ZFilePicker.SaveFile
        currentFile: ZaloClient.suggestedMediaSaveUrl(root.currentUrl)
        nameFilters: root.isVideo
            ? [I18n.t("Video (*.mp4 *.mov *.webm *.mkv)", "Videos (*.mp4 *.mov *.webm *.mkv)"),
               I18n.t("Tất cả tệp (*)", "All files (*)")]
            : [I18n.t("Hình ảnh (*.jpg *.jpeg *.png *.webp *.gif)", "Images (*.jpg *.jpeg *.png *.webp *.gif)"),
               I18n.t("Tất cả tệp (*)", "All files (*)")]
        onAccepted: {
            const dest = selectedFile.toString()
            if (root.strokes.length > 0 && !root.isVideo) {
                photo.grabToImage(function(result) {
                    const tempPath = ZaloClient.annotatedImageTempPath()
                    if (result.saveToFile(tempPath)) ZaloClient.saveMediaTo("file://" + tempPath, dest)
                    else ZaloClient.saveMediaTo(root.currentUrl, dest)
                })
            } else {
                ZaloClient.saveMediaTo(root.currentUrl, dest)
            }
        }
    }

    Rectangle {
        id: annotationBar
        anchors.top: topBar.bottom
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        z: 10
        visible: root.annotating && !root.isVideo
        height: 44
        width: Math.min(parent.width - 24, annotateRow.implicitWidth + 28)
        radius: 22
        clip: true
        color: "#E0141414"
        border.color: "#26FFFFFF"
        border.width: 1
        MouseArea { anchors.fill: parent }

        Flickable {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            contentWidth: annotateRow.implicitWidth
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentWidth > width

            Row {
                id: annotateRow
                objectName: "imageViewerAnnotateRow"
                anchors.verticalCenter: parent.verticalCenter
                height: 32
                spacing: 8

                component AnnotateToolBtn: Rectangle {
                    id: atb
                    property string tool: ""
                    property string icon: ""
                    property string tip: ""
                    y: Math.round((annotateRow.height - height) / 2)
                    width: 32
                    height: 32
                    radius: 16
                    color: root.currentDrawTool === atb.tool ? Theme.zaloBlue : (atbMouse.containsMouse ? "#26FFFFFF" : ThemeManager.hoverClearColor)
                    ZIcon { anchors.centerIn: parent; name: atb.icon; size: 18; color: "#FFFFFF" }
                    MouseArea {
                        id: atbMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        ToolTip.visible: containsMouse && atb.tip.length > 0
                        ToolTip.delay: 300
                        ToolTip.text: atb.tip
                        onClicked: root.currentDrawTool = atb.tool
                    }
                }

                AnnotateToolBtn { tool: "pen"; icon: "edit"; tip: I18n.t("Bút vẽ tự do", "Freehand pen") }
                AnnotateToolBtn { tool: "rect"; icon: "expand"; tip: I18n.t("Vẽ khung chữ nhật", "Rectangle frame") }
                AnnotateToolBtn { tool: "arrow"; icon: "forward"; tip: I18n.t("Vẽ mũi tên", "Arrow") }
                AnnotateToolBtn { tool: "text"; icon: "format_text"; tip: I18n.t("Chèn chữ", "Text note") }

                Rectangle { y: Math.round((annotateRow.height - height) / 2); width: 1; height: 24; color: "#33FFFFFF" }

                Repeater {
                    model: ["#FF3B30", "#FFCC00", "#34C759", "#007AFF", "#FFFFFF"]
                    delegate: Rectangle {
                        id: swatchBtn
                        required property string modelData
                        y: Math.round((annotateRow.height - height) / 2)
                        width: 22
                        height: 22
                        radius: 11
                        color: swatchBtn.modelData
                        border.color: root.drawColor === swatchBtn.modelData ? "#FFFFFF" : "#4C000000"
                        border.width: root.drawColor === swatchBtn.modelData ? 2 : 1
                        scale: root.drawColor === swatchBtn.modelData ? 1.15 : 1.0
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.drawColor = swatchBtn.modelData
                        }
                    }
                }

                Rectangle { y: Math.round((annotateRow.height - height) / 2); width: 1; height: 24; color: "#33FFFFFF" }

                Rectangle {
                    y: Math.round((annotateRow.height - height) / 2)
                    width: 32
                    height: 32
                    radius: 16
                    color: undoMouse.containsMouse ? "#26FFFFFF" : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "undo"; size: 18; color: root.strokes.length > 0 ? "#FFFFFF" : "#777777" }
                    MouseArea {
                        id: undoMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.strokes.length > 0
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 300
                        ToolTip.text: I18n.t("Hoàn tác (Ctrl+Z)", "Undo (Ctrl+Z)")
                        onClicked: {
                            root.strokes = root.strokes.slice(0, -1)
                            root.requestCanvasRepaint()
                        }
                    }
                }

                Rectangle {
                    y: Math.round((annotateRow.height - height) / 2)
                    width: 32
                    height: 32
                    radius: 16
                    color: clearMouse.containsMouse ? "#26FFFFFF" : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "trash"; size: 18; color: root.strokes.length > 0 ? "#FFFFFF" : "#777777" }
                    MouseArea {
                        id: clearMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.strokes.length > 0
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 300
                        ToolTip.text: I18n.t("Xóa toàn bộ nét vẽ", "Clear all drawings")
                        onClicked: {
                            root.strokes = []
                            root.requestCanvasRepaint()
                        }
                    }
                }
            }
        }
    }

    ZConfirmDialog {
        id: textInputDialog
        property real targetX: 0
        property real targetY: 0
        width: 300
        title: I18n.t("Nhập ghi chú", "Enter note")
        okText: I18n.t("Thêm", "Add")
        onOpened: annotationField.forceActiveFocus()
        ZTextField {
            id: annotationField
            Layout.fillWidth: true
            placeholderText: I18n.t("Nội dung ghi chú...", "Note text...")
            onAccepted: textInputDialog.accept()
        }
        onAccepted: {
            if (annotationField.text.trim().length > 0) {
                root.strokes = root.strokes.concat([{
                    type: "text",
                    color: root.drawColor.toString(),
                    text: annotationField.text.trim(),
                    x: textInputDialog.targetX,
                    y: textInputDialog.targetY
                }])
                root.requestCanvasRepaint()
            }
            annotationField.text = ""
        }
        onRejected: annotationField.text = ""
    }
}
