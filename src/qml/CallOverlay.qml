import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import Zalos

Rectangle {
    id: root
    readonly property var call: ZaloClient.calls
    readonly property string phase: call ? call.phase : ""
    readonly property bool peerVideoShown: !!(call && call.isVideo && call.peerVideoActive && !call.peerCamOff)
    visible: phase.length > 0
    color: "#000000"
    z: 2000

    property double now: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: root.phase === "connected"
        onTriggered: root.now = Date.now()
    }

    function durationText(): string {
        if (!call || call.since <= 0) return "";
        var s = Math.max(0, Math.floor((root.now - call.since) / 1000));
        var m = Math.floor(s / 60);
        return ("0" + m).slice(-2) + ":" + ("0" + (s % 60)).slice(-2);
    }

    function phaseText(): string {
        var isVid = !!(call && call.isVideo);
        var isGrp = !!(call && call.isGroup);
        switch (root.phase) {
        case "outgoing":
            if (isGrp) return isVid ? I18n.t("Đang gọi video nhóm...", "Calling group video...") : I18n.t("Đang gọi nhóm...", "Calling group...");
            return isVid ? I18n.t("Đang gọi video...", "Calling video...") : I18n.t("Đang gọi...", "Calling...");
        case "incoming":
            if (isGrp) return isVid ? I18n.t("Cuộc gọi video nhóm đến", "Incoming group video call") : I18n.t("Cuộc gọi nhóm đến", "Incoming group call");
            return isVid ? I18n.t("Cuộc gọi video đến", "Incoming video call") : I18n.t("Cuộc gọi thoại đến", "Incoming audio call");
        case "connecting": return isVid ? I18n.t("Đang kết nối video...", "Connecting video...") : I18n.t("Đang kết nối...", "Connecting...");
        case "ended":
            if (isGrp) return I18n.t("Cuộc gọi nhóm đã kết thúc", "Group call ended");
            return isVid ? I18n.t("Cuộc gọi video đã kết thúc", "Video call ended") : I18n.t("Cuộc gọi đã kết thúc", "Call ended");
        }
        return "";
    }

    MouseArea { anchors.fill: parent }

    Timer {
        interval: 2500
        running: root.phase === "ended"
        onTriggered: {
            if (root.call) root.call.hangup();
        }
    }

    Timer {
        interval: 4500
        running: root.phase === "ended"
        onTriggered: {
            if (root.call) root.call.hangup();
        }
    }

    Item {
        id: stage
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true

        Image {
            id: bgAvatar
            anchors.fill: parent
            anchors.margins: -60
            fillMode: Image.PreserveAspectCrop
            source: root.call && root.call.peerAvatar.length > 0
                    ? "image://avatar/" + encodeURIComponent(root.call.peerAvatar) : ""
            sourceSize.width: 256
            sourceSize.height: 256
            visible: false
        }
        Loader {
            id: blurLoader
            anchors.fill: bgAvatar
            active: GraphicsInfo.api !== GraphicsInfo.Software
            visible: bgAvatar.status === Image.Ready
            source: "ZBlurEffect.qml"
            onLoaded: blurLoader.item.source = bgAvatar
        }

        VideoOutput {
            id: peerVideo
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            visible: root.peerVideoShown
            Component.onCompleted: if (root.call) root.call.setPeerVideoSink(peerVideo.videoSink)
        }

        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: 17
            anchors.topMargin: 8
            visible: root.phase === "connected"
            text: root.durationText()
            color: "#42D58C"
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.bold: true
        }

        ColumnLayout {
            anchors.centerIn: parent
            visible: !root.peerVideoShown
            width: Math.min(parent.width - 32, 420)
            spacing: 10

            Item {
                id: avatarBox
                readonly property int d: root.phase === "connected" ? 160 : 125
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: d
                Layout.preferredHeight: d
                ZAvatar {
                    anchors.fill: parent
                    size: avatarBox.d
                    avatarUrl: root.call ? root.call.peerAvatar : ""
                    name: root.call ? root.call.peerName : ""
                }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 6
                    border.color: Qt.rgba(150 / 255, 150 / 255, 150 / 255, 0.55)
                    visible: root.phase !== "connected"
                }
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: root.call ? root.call.peerName : ""
                elide: Text.ElideRight
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: root.phaseText()
                color: "#CFD3D8"
                font.family: Theme.fontFamily
                font.pixelSize: 16
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                visible: !!(root.call && root.call.peerMuted && root.phase === "connected")
                Layout.preferredHeight: 26
                Layout.preferredWidth: noteText.implicitWidth + 20
                radius: 13
                color: Qt.rgba(0, 0, 0, 0.7)
                Text {
                    id: noteText
                    anchors.centerIn: parent
                    text: I18n.t("Người kia đang tắt micro", "The other person is muted")
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
            }

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                visible: !!(root.call && root.call.isVideo && root.call.peerCamOff && root.phase === "connected")
                Layout.preferredHeight: 26
                Layout.preferredWidth: camNoteText.implicitWidth + 20
                radius: 13
                color: Qt.rgba(0, 0, 0, 0.7)
                Text {
                    id: camNoteText
                    anchors.centerIn: parent
                    text: I18n.t("Người kia đã tắt camera", "The other person turned off camera")
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
            }

            // Group Members Carousel / Row
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                visible: !!(root.call && root.call.isGroup && root.call.groupMembers && root.call.groupMembers.length > 0)

                Repeater {
                    model: root.call && root.call.groupMembers ? root.call.groupMembers.slice(0, 6) : []
                    delegate: Column {
                        spacing: 4
                        Item {
                            width: 48
                            height: 48
                            anchors.horizontalCenter: parent.horizontalCenter
                            ZAvatar {
                                anchors.fill: parent
                                size: 48
                                avatarUrl: modelData.avatar || ""
                                name: modelData.name || ""
                            }
                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                anchors.bottom: parent.bottom
                                anchors.right: parent.right
                                color: modelData.state === "connected" ? "#34C759" : (modelData.state === "calling" ? "#FF9500" : "#8E8E93")
                                border.width: 2
                                border.color: "#000000"
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: (modelData.name || "").split(" ").slice(-1)[0]
                            font.pixelSize: 11
                            color: "#FFFFFF"
                            elide: Text.ElideRight
                            width: 56
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }

        Rectangle {
            id: selfView
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 16
            anchors.bottomMargin: 96
            width: 200
            height: 112
            radius: 8
            color: "#000000"
            border.color: Qt.rgba(1, 1, 1, 0.25)
            visible: !!(root.call && root.call.isVideo && root.call.cameraOn && root.phase !== "ended")
            clip: true
            VideoOutput {
                id: selfVideo
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectCrop
                transform: Scale { origin.x: selfVideo.width / 2; xScale: -1 }
                Component.onCompleted: if (root.call) root.call.setLocalVideoSink(selfVideo.videoSink)
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            width: 240
            height: videoNote.implicitHeight + 20
            radius: 8
            color: Qt.rgba(0, 0, 0, 0.7)
            visible: !!(root.call && root.call.isVideo && root.phase !== "ended" && videoNote.text.length > 0)

            Text {
                id: videoNote
                anchors.centerIn: parent
                width: parent.width - 20
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: {
                    const c = root.call;
                    if (!c) return "";
                    if (!c.videoSupported)
                        return I18n.t("Máy chưa có OpenH264 nên không gửi và xem được hình. Cài gói openh264 (Fedora) hoặc libopenh264 (Ubuntu/Debian) rồi mở lại Zalos. Cuộc gọi này chỉ có âm thanh.",
                                      "OpenH264 is not installed, so no pictures are sent or shown. Install openh264 (Fedora) or libopenh264 (Ubuntu/Debian) and restart Zalos. This call is audio only.");
                    if (c.cameraError === "no_camera" || (!c.cameraAvailable && !c.cameraOn))
                        return I18n.t("Không tìm thấy camera: người kia chỉ nghe thấy tiếng.", "No camera found: the other person only hears you.");
                    if (c.cameraError.length > 0 && c.cameraError !== "no_encoder")
                        return I18n.t("Không mở được camera (", "Couldn't open the camera (") + c.cameraError
                               + I18n.t("). Camera có thể đang được ứng dụng khác dùng.", "). Another app may be using it.");
                    return "";
                }
                color: "#CFD3D8"
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
        }
    }

    component PillButton: Item {
        id: pbRoot
        width: 80
        height: 56
        property alias tip: pb.tip
        property alias icon: pb.icon
        property alias on: pb.on
        property alias enabled: pb.enabled
        signal clicked()

        Rectangle {
            id: pb
            anchors.centerIn: parent
            opacity: enabled ? 1.0 : 0.4
            property string icon: ""
            property string tip: ""
            property bool on: false
            width: 80
            height: 40
            radius: 20
            color: pbMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.26) : Qt.rgba(1, 1, 1, 0.16)
            border.color: Qt.rgba(1, 1, 1, 0.18)
            border.width: 1
            ZIcon {
                anchors.centerIn: parent
                name: pb.icon
                size: 22
                color: pb.on ? "#E5383B" : "#FFFFFF"
            }
            MouseArea {
                id: pbMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: pbRoot.clicked()
            }
            ToolTip.visible: pbMouse.containsMouse && tip.length > 0
            ToolTip.delay: 400
            ToolTip.text: tip
        }
    }

    component CircleButton: Rectangle {
        id: rb
        property string icon: ""
        property string tip: ""
        property color tint: "#E5383B"
        signal clicked()
        width: 56
        height: 56
        radius: 28
        color: rbMouse.containsMouse ? Qt.lighter(rb.tint, 1.15) : rb.tint
        ZIcon {
            anchors.centerIn: parent
            name: rb.icon
            size: 28
            color: "#FFFFFF"
        }
        MouseArea {
            id: rbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: rb.clicked()
        }
        ToolTip.visible: rbMouse.containsMouse && tip.length > 0
        ToolTip.delay: 400
        ToolTip.text: tip
    }

    Rectangle {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 120
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.45) }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 28
            spacing: 20
            baselineOffset: 0

            CircleButton {
                visible: root.phase === "incoming"
                icon: "call_end"
                tip: I18n.t("Từ chối", "Decline")
                onClicked: root.call.reject()
            }
            CircleButton {
                visible: root.phase === "incoming" && !!root.call && root.call.canAnswer && root.call.isVideo
                icon: "phone"
                tint: "#0068FF"
                tip: I18n.t("Trả lời không camera", "Answer without camera")
                onClicked: root.call.accept(true)
            }
            CircleButton {
                visible: root.phase === "incoming" && !!root.call && root.call.canAnswer
                icon: root.call && root.call.isVideo ? "camera" : "phone"
                tint: "#2BB673"
                tip: root.call && root.call.isVideo ? I18n.t("Trả lời bằng video", "Answer with video") : I18n.t("Trả lời", "Answer")
                onClicked: root.call.accept(false)
            }
            PillButton {
                visible: root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing"
                icon: root.call && root.call.muted ? "mic_off" : "mic"
                on: !!(root.call && root.call.muted)
                tip: root.call && root.call.muted ? I18n.t("Bật micro", "Unmute") : I18n.t("Tắt micro", "Mute")
                onClicked: root.call.setMuted(!root.call.muted)
            }
            PillButton {
                visible: root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing"
                icon: root.call && root.call.speakerOff ? "volume_off" : "volume"
                on: !!(root.call && root.call.speakerOff)
                tip: root.call && root.call.speakerOff ? I18n.t("Bật loa", "Speaker on") : I18n.t("Tắt loa", "Speaker off")
                onClicked: root.call.setSpeakerOff(!root.call.speakerOff)
            }
            PillButton {
                visible: !!(root.call && root.call.isVideo) && (root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing")
                enabled: !!(root.call && (root.call.cameraAvailable || root.call.cameraOn))
                icon: root.call && root.call.cameraOn ? "camera" : "camera_off"
                on: !(root.call && root.call.cameraOn)
                tip: root.call && root.call.cameraOn ? I18n.t("Tắt camera", "Turn off camera") : I18n.t("Bật camera", "Turn on camera")
                onClicked: root.call.setCamera(!root.call.cameraOn)
            }
            CircleButton {
                visible: root.phase !== "incoming"
                icon: root.phase === "ended" ? "close" : "call_end"
                tip: root.phase === "ended" ? I18n.t("Đóng", "Close") : I18n.t("Kết thúc", "End call")
                onClicked: root.call.hangup()
            }
        }
    }
}
