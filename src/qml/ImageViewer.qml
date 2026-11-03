import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Zalos

// Full-window photo viewer (Zalo PC media viewer): zoom (wheel, +/-, double click), drag to pan, rotate,
// previous/next with a thumbnail strip, copy, save, open original. Esc or a click on the backdrop closes.
Rectangle {
    id: root
    color: Qt.rgba(0, 0, 0, 0.92)
    focus: true

    readonly property var items: ImageViewerState.items
    readonly property int count: items.length
    readonly property var current: count > 0 ? items[ImageViewerState.index] : ({})
    property real zoom: 1.0
    property real angle: 0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 8.0

    function resetView(): void {
        zoom = 1.0;
        angle = 0;
        photo.x = 0;
        photo.y = 0;
    }
    function go(delta: int): void {
        if (count < 2) return;
        ImageViewerState.index = (ImageViewerState.index + delta + count) % count;
        resetView();
    }
    function zoomBy(factor: real): void {
        zoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor));
        if (zoom <= 1.0) { photo.x = 0; photo.y = 0; }
    }
    function timeText(ts: real): string {
        if (!ts) return "";
        return Qt.formatDateTime(new Date(ts), "HH:mm dd/MM/yyyy");
    }

    Component.onCompleted: forceActiveFocus()
    Keys.onEscapePressed: ImageViewerState.close()
    Keys.onLeftPressed: go(-1)
    Keys.onRightPressed: go(1)
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) { zoomBy(1.25); event.accepted = true; }
        else if (event.key === Qt.Key_Minus) { zoomBy(0.8); event.accepted = true; }
        else if (event.key === Qt.Key_0) { resetView(); event.accepted = true; }
        else if (event.key === Qt.Key_R) { angle += (event.modifiers & Qt.ShiftModifier) ? -90 : 90; event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) { ZaloClient.copyImage(current.url); event.accepted = true; }
        else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_S) { saveDialog.open(); event.accepted = true; }
    }

    // Backdrop: a click outside the picture closes the viewer.
    MouseArea {
        anchors.fill: parent
        onClicked: ImageViewerState.close()
    }

    // ---- Picture area -------------------------------------------------------------------------
    Item {
        id: stage
        anchors.top: topBar.bottom
        anchors.bottom: strip.visible ? strip.top : parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 16
        clip: true

        Image {
            id: photo
            width: stage.width
            height: stage.height
            source: root.current.url || ""
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

            // Only the painted picture swallows clicks; empty letterbox space still closes the viewer.
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
                enabled: root.zoom > 1.0
                target: photo
            }
        }

        WheelHandler {
            target: null
            onWheel: function(event) { root.zoomBy(event.angleDelta.y > 0 ? 1.15 : 1 / 1.15); }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: photo.status === Image.Loading
            visible: running
        }
        Text {
            anchors.centerIn: parent
            visible: photo.status === Image.Error
            text: I18n.t("Không tải được ảnh", "Couldn't load the picture")
            color: "#CFD3D8"
            font.family: Theme.fontFamily
            font.pixelSize: 14
        }

        // Previous / next
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

    // ---- Top bar: who/when, counter, actions ----------------------------------------------------
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
        color: Qt.rgba(0, 0, 0, 0.35)
        MouseArea { anchors.fill: parent } // clicks on the bar do not close the viewer

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

            ToolButtonIcon { icon: "zoom_out"; tip: I18n.t("Thu nhỏ (−)", "Zoom out (−)"); onClicked: root.zoomBy(0.8) }
            ToolButtonIcon { label: Math.round(root.zoom * 100) + "%"; tip: I18n.t("Vừa khung (0)", "Fit (0)"); onClicked: root.resetView() }
            ToolButtonIcon { icon: "zoom_in"; tip: I18n.t("Phóng to (+)", "Zoom in (+)"); onClicked: root.zoomBy(1.25) }
            ToolButtonIcon { icon: "rotate"; tip: I18n.t("Xoay (R)", "Rotate (R)"); onClicked: root.angle += 90 }
            ToolButtonIcon { icon: "copy"; tip: I18n.t("Sao chép ảnh (Ctrl+C)", "Copy picture (Ctrl+C)"); onClicked: ZaloClient.copyImage(root.current.url) }
            ToolButtonIcon { icon: "download"; tip: I18n.t("Lưu ảnh (Ctrl+S)", "Save picture (Ctrl+S)"); onClicked: saveDialog.open() }
            ToolButtonIcon {
                visible: String(root.current.url || "").startsWith("http")
                icon: "open_external"
                tip: I18n.t("Mở ảnh gốc trong trình duyệt", "Open original in browser")
                onClicked: Qt.openUrlExternally(root.current.url)
            }
            ToolButtonIcon { icon: "close"; tip: I18n.t("Đóng (Esc)", "Close (Esc)"); onClicked: ImageViewerState.close() }
        }
    }

    // ---- Thumbnail strip --------------------------------------------------------------------------
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
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { ImageViewerState.index = index; root.resetView(); }
            }
        }
    }

    FileDialog {
        id: saveDialog
        title: I18n.t("Lưu ảnh", "Save picture")
        fileMode: FileDialog.SaveFile
        currentFile: "file:///" + ZaloClient.suggestedMediaFileName(root.current.url || "")
        nameFilters: [I18n.t("Hình ảnh (*.jpg *.jpeg *.png *.webp *.gif)", "Images (*.jpg *.jpeg *.png *.webp *.gif)")]
        onAccepted: ZaloClient.saveMediaTo(root.current.url, selectedFile.toString())
    }
}
