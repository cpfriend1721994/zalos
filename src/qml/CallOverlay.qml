pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import Zalos

Item {
    id: root
    objectName: "callOverlayRoot"
    property bool minimized: false
    property bool splitView: false
    property bool deviceMenuOpen: false
    onDeviceMenuOpenChanged: if (deviceMenuOpen && ZaloClient.calls) ZaloClient.calls.refreshDevicesAsync()
    property real pipX: -1
    property real pipY: 56
    readonly property string phase: ZaloClient.calls ? ZaloClient.calls.phase : ""
    readonly property bool screenShareOn: !!(ZaloClient.calls && ZaloClient.calls.screenShareOn)
    readonly property bool peerVideoShown: !!(ZaloClient.calls && ZaloClient.calls.peerVideoActive
                                              && (ZaloClient.calls.peerSharing || !ZaloClient.calls.peerCamOff))
    readonly property bool hasLocalVideo: !!(ZaloClient.calls && (ZaloClient.calls.cameraOn || ZaloClient.calls.screenShareOn))
    readonly property bool hasAnyVideo: root.peerVideoShown || root.hasLocalVideo
    readonly property bool isSplitActive: !root.minimized && root.splitView && root.peerVideoShown && root.hasLocalVideo
    visible: phase.length > 0
    z: 2000

    onPhaseChanged: {
        if (phase === "incoming" || phase === "ended" || phase === "") {
            root.minimized = false;
            root.splitView = false;
            root.deviceMenuOpen = false;
        }
    }
    onScreenShareOnChanged: if (screenShareOn) root.minimized = true
    onMinimizedChanged: if (minimized) root.deviceMenuOpen = false

    property double now: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: root.phase === "connected"
        onTriggered: root.now = Date.now()
    }

    function durationText(): string {
        if (!ZaloClient.calls || ZaloClient.calls.since <= 0) return "";
        var s = Math.max(0, Math.floor((root.now - ZaloClient.calls.since) / 1000));
        var m = Math.floor(s / 60);
        return ("0" + m).slice(-2) + ":" + ("0" + (s % 60)).slice(-2);
    }

    function clockText(seconds: int): string {
        var s = Math.max(0, seconds);
        var m = Math.floor(s / 60);
        return ("0" + m).slice(-2) + ":" + ("0" + (s % 60)).slice(-2);
    }

    function joinNames(list: var): string {
        var out = [];
        if (list) {
            for (var i = 0; i < list.length; ++i)
                out.push(list[i] ? String(list[i]) : I18n.t("Thành viên", "Participant"));
        }
        return out.join(", ");
    }

    function statusLabel(code: string, params: var): string {
        var p = params || {};
        var inviter = p.inviter ? String(p.inviter) : I18n.t("Ai đó", "Someone");
        switch (code) {
        case "ringing":
        case "groupRinging": return I18n.t("Đang đổ chuông...", "Ringing...");
        case "endedWithDuration": return I18n.t("Cuộc gọi đã kết thúc", "Call ended") + " · " + clockText(Number(p.duration || 0));
        case "disconnected": return I18n.t("Mất kết nối với bộ gọi điện", "Lost connection to the call engine");
        case "recipientUnreachable": return I18n.t("Người nhận không liên lạc được", "The recipient can't be reached");
        case "recipientNoAnswer": return I18n.t("Người nhận không trả lời", "No answer");
        case "recipientDeclined": return I18n.t("Người nhận đã từ chối cuộc gọi", "The recipient declined the call");
        case "callFailed": return I18n.t("Không gọi được", "Couldn't place the call");
        case "serverUnreachable": return I18n.t("Không kết nối được máy chủ cuộc gọi", "Couldn't reach the call server");
        case "groupUnsupported": return I18n.t("Gọi nhóm chưa được hỗ trợ trên Linux", "Group calls aren't supported on Linux yet");
        case "groupCallIgnored": return I18n.t("Đã bỏ qua cuộc gọi nhóm", "Group call ignored");
        case "groupCallFailed": return I18n.t("Không gọi được nhóm", "Couldn't start the group call");
        case "groupCallFailedReason": return I18n.t("Không gọi được nhóm: ", "Couldn't start the group call: ") + String(p.message || "");
        case "groupJoinRejected": return I18n.t("Không vào được cuộc gọi nhóm (máy chủ từ chối)", "Couldn't join the group call (refused by the server)");
        case "groupDeclined": return I18n.t("Đã từ chối cuộc gọi nhóm", "Group call declined");
        case "groupKicked": return I18n.t("Bạn đã bị mời ra khỏi cuộc gọi", "You were removed from the call");
        case "groupNoAnswer": return I18n.t("Không ai trả lời", "Nobody answered");
        case "groupCancelled": return I18n.t("Đã huỷ cuộc gọi nhóm", "Group call cancelled");
        case "groupWaitingOthers": return I18n.t("Đang chờ người khác...", "Waiting for others...");
        case "groupParticipants":
            return I18n.t("%1 người: Bạn, %2", "%1 people: You, %2").arg(Number(p.count || 0)).arg(joinNames(p.names));
        case "incomingGroupInvite": return I18n.t("%1 mời bạn vào cuộc gọi nhóm", "%1 invited you to a group call").arg(inviter);
        case "incomingGroupUnsupported":
            return I18n.t("%1 đang gọi nhóm. Gọi nhóm chưa được hỗ trợ trên Linux.",
                          "%1 is calling the group. Group calls aren't supported on Linux yet.").arg(inviter);
        case "audioMicSpeakerFailed":
            return I18n.t("Không mở được micro và loa. Hãy kiểm tra thiết bị âm thanh trong cài đặt hệ thống.",
                          "Couldn't open the microphone and speaker. Check the sound devices in your system settings.");
        case "audioMicFailed":
            return I18n.t("Không mở được micro, người kia không nghe thấy bạn. Hãy kiểm tra micro trong cài đặt hệ thống.",
                          "Couldn't open the microphone, so the other person can't hear you. Check the microphone in your system settings.");
        case "audioSpeakerFailed":
            return I18n.t("Không phát được âm thanh ra loa. Hãy kiểm tra loa trong cài đặt hệ thống.",
                          "Couldn't play sound on the speaker. Check the speaker in your system settings.");
        }
        return "";
    }

    function phaseText(): string {
        var isVid = !!(ZaloClient.calls && ZaloClient.calls.isVideo);
        var isGrp = !!(ZaloClient.calls && ZaloClient.calls.isGroup);
        if (root.phase === "connected" && !isGrp) return "";
        var label = ZaloClient.calls && ZaloClient.calls.statusCode.length > 0 ? statusLabel(ZaloClient.calls.statusCode, ZaloClient.calls.statusParams) : "";
        if (label.length > 0) return label;
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

    MouseArea {
        anchors.fill: parent
        enabled: !root.minimized
        visible: !root.minimized
    }

    Timer {
        interval: 2500
        running: root.phase === "ended"
        onTriggered: {
            if (ZaloClient.calls) ZaloClient.calls.hangup();
        }
    }

    Timer {
        interval: 4500
        running: root.phase === "ended"
        onTriggered: {
            if (ZaloClient.calls) ZaloClient.calls.hangup();
        }
    }

    component PillButton: Item {
        id: pbRoot
        property real pillWidth: Math.min(76, Math.max(44, Math.floor((root.width - 72) / 7)))
        width: pillWidth
        height: 56
        property alias tip: pb.tip
        property alias icon: pb.icon
        property alias on: pb.on
        signal clicked()

        Rectangle {
            id: pb
            anchors.centerIn: parent
            opacity: enabled ? 1.0 : 0.4
            property string icon: ""
            property string tip: ""
            property bool on: false
            width: pbRoot.pillWidth
            height: 40
            radius: 20
            color: pbMouse.containsMouse ? "#42FFFFFF" : "#29FFFFFF"
            border.color: "#2EFFFFFF"
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

    component MiniButton: Rectangle {
        id: mb
        property string icon: ""
        property string tip: ""
        property bool on: false
        property bool destructive: false
        signal clicked()
        width: 32
        height: 32
        radius: 16
        opacity: enabled ? 1.0 : 0.4
        color: mb.destructive
               ? (mbMouse.containsMouse ? "#F25558" : "#E5383B")
               : (mbMouse.containsMouse ? "#42FFFFFF" : "#24FFFFFF")
        ZIcon {
            anchors.centerIn: parent
            name: mb.icon
            size: 16
            color: (!mb.destructive && mb.on) ? "#E5383B" : "#FFFFFF"
        }
        MouseArea {
            id: mbMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mb.clicked()
        }
        ToolTip.visible: mbMouse.containsMouse && mb.tip.length > 0
        ToolTip.delay: 400
        ToolTip.text: mb.tip
    }

    component DeviceOptionRow: Rectangle {
        id: devRow
        property string labelText: ""
        property bool selected: false
        signal picked()
        implicitHeight: 32
        height: 32
        radius: 6
        color: devMouse.containsMouse ? "#28FFFFFF" : (devRow.selected ? "#290068FF" : "#00000000")

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: devRow.labelText
                elide: Text.ElideRight
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: devRow.selected ? Font.DemiBold : Font.Normal
            }
            ZIcon {
                visible: devRow.selected
                name: "check"
                size: 14
                color: "#4DA3FF"
            }
        }
        MouseArea {
            id: devMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: devRow.picked()
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
        id: callCard
        objectName: "callCard"
        x: root.minimized ? (root.pipX < 0 ? Math.max(12, root.width - width - 20)
                                           : Math.max(8, Math.min(root.pipX, root.width - width - 8)))
                          : 0
        y: root.minimized ? Math.max(8, Math.min(root.pipY, root.height - height - 8)) : 0
        width: root.minimized ? 300 : root.width
        height: root.minimized ? (root.hasAnyVideo ? 200 : 96) : root.height
        radius: root.minimized ? 14 : 0
        color: root.minimized ? "#F2181C24" : "#000000"
        border.width: root.minimized ? 1 : 0
        border.color: "#45FFFFFF"
        clip: true

        MouseArea {
            anchors.fill: parent
            z: 1
            enabled: root.minimized
            cursorShape: root.minimized ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor
            property real pressMouseX: 0
            property real pressMouseY: 0
            property real startPipX: 0
            property real startPipY: 0
            onPressed: function(mouse) {
                const pt = mapToItem(root, mouse.x, mouse.y);
                startPipX = callCard.x;
                startPipY = callCard.y;
                pressMouseX = pt.x;
                pressMouseY = pt.y;
            }
            onPositionChanged: function(mouse) {
                if (!pressed || !root.minimized) return;
                const pt = mapToItem(root, mouse.x, mouse.y);
                root.pipX = Math.max(8, Math.min(startPipX + (pt.x - pressMouseX), root.width - callCard.width - 8));
                root.pipY = Math.max(8, Math.min(startPipY + (pt.y - pressMouseY), root.height - callCard.height - 8));
            }
            onDoubleClicked: if (root.minimized) root.minimized = false
        }

        Item {
            id: stage
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.minimized ? 44 : 0
            clip: true

            Item {
                id: bgBlurContainer
                width: 256
                height: 256
                anchors.centerIn: parent
                scale: Math.max((stage.width + 120) / 256, (stage.height + 120) / 256, 1)
                visible: !root.minimized && !root.peerVideoShown && bgAvatar.status === Image.Ready

                Image {
                    id: bgAvatar
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    source: (!root.minimized && !root.peerVideoShown && root.visible && root.phase !== ""
                             && root.phase !== "ended" && ZaloClient.calls && ZaloClient.calls.peerAvatar.length > 0)
                            ? "image://avatar/" + encodeURIComponent(ZaloClient.calls.peerAvatar) : ""
                    sourceSize.width: 256
                    sourceSize.height: 256
                    visible: !ZaloClient.effectsAvailable || blurLoader.status !== Loader.Ready
                }
                Loader {
                    id: blurLoader
                    anchors.fill: bgAvatar
                    active: ZaloClient.effectsAvailable && !root.minimized && !root.peerVideoShown
                            && bgAvatar.status === Image.Ready && GraphicsInfo.api !== GraphicsInfo.Software
                    visible: active && status === Loader.Ready
                    source: "ZBlurEffect.qml"
                    onLoaded: blurLoader.item.source = bgAvatar
                }
            }

            Loader {
                id: peerVideo
                x: 0
                y: 0
                width: root.isSplitActive ? stage.width / 2 : stage.width
                height: stage.height
                visible: root.peerVideoShown
                active: ZaloClient.multimediaAvailable
                source: active ? "ZVideoView.qml" : ""
                onLoaded: if (ZaloClient.calls) ZaloClient.calls.setPeerVideoSink(peerVideo.item.videoSink)
            }

            Text {
                visible: !ZaloClient.multimediaAvailable && !!ZaloClient.calls && ZaloClient.calls.peerVideoActive && !root.minimized
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 120
                width: Math.min(parent.width - 48, 420)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: I18n.t("Máy thiếu thành phần hiển thị video (Qt Multimedia) nên chỉ nghe được tiếng.",
                             "This system lacks the video display components (Qt Multimedia), so the call is audio-only.")
                font.family: Theme.fontFamily
                font.pixelSize: 13
                color: "#FFFFFF"
            }

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 17
                anchors.topMargin: 8
                visible: !root.minimized && root.phase === "connected"
                text: root.durationText()
                color: "#42D58C"
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                visible: root.minimized && !root.hasAnyVideo
                ZAvatar {
                    size: 36
                    avatarUrl: ZaloClient.calls ? ZaloClient.calls.peerAvatar : ""
                    name: ZaloClient.calls ? ZaloClient.calls.peerName : ""
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: ZaloClient.calls ? ZaloClient.calls.peerName : ""
                        elide: Text.ElideRight
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.phase === "connected" ? root.durationText() : root.phaseText()
                        elide: Text.ElideRight
                        color: root.phase === "connected" ? "#42D58C" : "#CFD3D8"
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: !root.minimized && !root.peerVideoShown
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
                    avatarUrl: ZaloClient.calls ? ZaloClient.calls.peerAvatar : ""
                    name: ZaloClient.calls ? ZaloClient.calls.peerName : ""
                }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 6
                    border.color: "#8C969696"
                    visible: root.phase !== "connected"
                }
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: ZaloClient.calls ? ZaloClient.calls.peerName : ""
                elide: Text.ElideRight
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: 20
                font.weight: Font.DemiBold
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
                id: noticeStrip
                readonly property string label: ZaloClient.calls && ZaloClient.calls.noticeCode.length > 0 && root.phase !== "ended"
                                                ? root.statusLabel(ZaloClient.calls.noticeCode, ZaloClient.calls.noticeParams) : ""
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: Math.min(420, root.width - 32)
                Layout.preferredWidth: noticeText.implicitWidth + 24
                Layout.preferredHeight: noticeText.implicitHeight + 12
                visible: label.length > 0
                radius: 10
                color: "#D9D93333"
                Text {
                    id: noticeText
                    anchors.fill: parent
                    anchors.margins: 6
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    text: noticeStrip.label
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                visible: !!(ZaloClient.calls && ZaloClient.calls.peerMuted && root.phase === "connected")
                Layout.preferredHeight: 26
                Layout.preferredWidth: noteText.implicitWidth + 20
                radius: 13
                color: "#B2000000"
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
                visible: !!(ZaloClient.calls && ZaloClient.calls.isVideo && ZaloClient.calls.peerCamOff && root.phase === "connected")
                Layout.preferredHeight: 26
                Layout.preferredWidth: camNoteText.implicitWidth + 20
                radius: 13
                color: "#B2000000"
                Text {
                    id: camNoteText
                    anchors.centerIn: parent
                    text: I18n.t("Người kia đã tắt camera", "The other person turned off camera")
                    color: "#FFFFFF"
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: parent.width
                clip: true
                spacing: 12
                visible: !!(ZaloClient.calls && ZaloClient.calls.isGroup && ZaloClient.calls.groupMembers && ZaloClient.calls.groupMembers.length > 0)

                Repeater {
                    model: ZaloClient.calls && ZaloClient.calls.groupMembers ? ZaloClient.calls.groupMembers.slice(0, 6) : []
                    delegate: Column {
                        id: partCol
                        required property var modelData
                        spacing: 4
                        Item {
                            width: 48
                            height: 48
                            anchors.horizontalCenter: parent.horizontalCenter
                            ZAvatar {
                                anchors.fill: parent
                                size: 48
                                avatarUrl: partCol.modelData.avatar || ""
                                name: partCol.modelData.name || ""
                            }
                            Rectangle {
                                width: 14
                                height: 14
                                radius: 7
                                anchors.bottom: parent.bottom
                                anchors.right: parent.right
                                color: partCol.modelData.state === "connected" ? "#34C759" : (partCol.modelData.state === "calling" ? "#FF9500" : "#8E8E93")
                                border.width: 2
                                border.color: "#000000"
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: (partCol.modelData.name || "").split(" ").slice(-1)[0]
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
            x: root.isSplitActive
               ? stage.width / 2
               : (parent.width - width - (root.minimized ? (root.peerVideoShown ? 6 : 0) : 16))
            y: root.isSplitActive
               ? 0
               : (parent.height - height - (root.minimized ? (root.peerVideoShown ? 6 : 0) : 96))
            width: root.isSplitActive
                   ? stage.width / 2
                   : (root.minimized ? (root.peerVideoShown ? 84 : stage.width) : 200)
            height: root.isSplitActive
                    ? stage.height
                    : (root.minimized ? (root.peerVideoShown ? 48 : stage.height) : 112)
            radius: (root.isSplitActive || (root.minimized && !root.peerVideoShown)) ? 0 : 8
            color: "#000000"
            border.width: (root.isSplitActive || (root.minimized && !root.peerVideoShown)) ? 0 : 1
            border.color: "#40FFFFFF"
            visible: !!(ZaloClient.calls && (ZaloClient.calls.cameraOn || ZaloClient.calls.screenShareOn) && root.phase !== "ended")
            clip: true
            Loader {
                id: selfVideo
                anchors.fill: parent
                active: ZaloClient.multimediaAvailable
                source: active ? "ZVideoView.qml" : ""
                transform: Scale { origin.x: selfVideo.width / 2; xScale: ZaloClient.calls && ZaloClient.calls.screenShareOn ? 1 : -1 }
                onLoaded: {
                    selfVideo.item.fillMode = Qt.binding(function() {
                        return ZaloClient.calls && ZaloClient.calls.screenShareOn ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop;
                    });
                    if (ZaloClient.calls) ZaloClient.calls.setLocalVideoSink(selfVideo.item.videoSink);
                }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 12
            height: 26
            width: peerShareLabel.implicitWidth + 24
            radius: 13
            color: "#B2000000"
            visible: !root.minimized && !!(ZaloClient.calls && ZaloClient.calls.peerSharing && root.phase === "connected")
            Text {
                id: peerShareLabel
                anchors.centerIn: parent
                text: I18n.t("Người kia đang chia sẻ màn hình", "The other person is sharing their screen")
                color: "#FFFFFF"
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }

        Row {
            id: topActions
            z: 5
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 8
            visible: !root.minimized && root.phase !== "incoming" && root.phase !== "ended"

            Rectangle {
                id: callLayoutBtn
                objectName: "callLayoutBtn"
                visible: root.peerVideoShown && root.hasLocalVideo
                height: 34
                width: layoutRow.implicitWidth + 20
                radius: 17
                color: layoutMouse.containsMouse ? "#45FFFFFF" : "#29FFFFFF"
                border.color: "#35FFFFFF"
                border.width: 1
                RowLayout {
                    id: layoutRow
                    anchors.centerIn: parent
                    spacing: 6
                    ZIcon { name: "sidebar_panel"; size: 16; color: "#FFFFFF" }
                    Text {
                        text: root.splitView ? I18n.t("Khung chuẩn", "Standard") : I18n.t("Chia đôi", "Split")
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
                MouseArea {
                    id: layoutMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.splitView = !root.splitView
                }
            }

            Rectangle {
                id: callHideTrayBtn
                objectName: "callHideTrayBtn"
                height: 34
                width: hideTrayRow.implicitWidth + 20
                radius: 17
                color: hideTrayMouse.containsMouse ? "#45FFFFFF" : "#29FFFFFF"
                border.color: "#35FFFFFF"
                border.width: 1
                RowLayout {
                    id: hideTrayRow
                    anchors.centerIn: parent
                    spacing: 6
                    ZIcon { name: "arrow_down"; size: 16; color: "#FFFFFF" }
                    Text {
                        text: ZaloClient.trayAvailable ? I18n.t("Ẩn xuống khay", "To tray")
                                                       : I18n.t("Ẩn xuống tác vụ", "Minimize")
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
                MouseArea {
                    id: hideTrayMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ZaloClient.hideWindowToTray()
                }
            }

            Rectangle {
                id: callMinimizeBtn
                objectName: "callMinimizeBtn"
                height: 34
                width: minRow.implicitWidth + 20
                radius: 17
                color: minMouse.containsMouse ? "#45FFFFFF" : "#29FFFFFF"
                border.color: "#35FFFFFF"
                border.width: 1
                RowLayout {
                    id: minRow
                    anchors.centerIn: parent
                    spacing: 6
                    ZIcon { name: "pip"; size: 16; color: "#FFFFFF" }
                    Text {
                        text: I18n.t("Thu nhỏ", "Minimize")
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }
                MouseArea {
                    id: minMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.minimized = true
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            anchors.topMargin: topActions.visible ? 56 : 16
            width: 240
            height: videoNote.implicitHeight + 20
            radius: 8
            color: "#B2000000"
            visible: !root.minimized && !!(ZaloClient.calls && (ZaloClient.calls.isVideo || ZaloClient.calls.screenShareOn || ZaloClient.calls.cameraError.length > 0)
                        && root.phase !== "ended" && videoNote.text.length > 0)

            Text {
                id: videoNote
                anchors.centerIn: parent
                width: parent.width - 20
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: {
                    const c = ZaloClient.calls;
                    if (!c) return "";
                    if (!c.videoSupported)
                        return I18n.t("Máy chưa có OpenH264 nên không gửi và xem được hình. Cài gói openh264 (Fedora) hoặc libopenh264 (Ubuntu/Debian) rồi mở lại Zalos. Cuộc gọi này chỉ có âm thanh.",
                                      "OpenH264 is not installed, so no pictures are sent or shown. Install openh264 (Fedora) or libopenh264 (Ubuntu/Debian) and restart Zalos. This call is audio only.");
                    if (c.cameraError === "no_screen")
                        return I18n.t("Không tìm thấy màn hình để chia sẻ.", "No screen found to share.");
                    if (c.isVideo && (c.cameraError === "no_camera" || (!c.cameraAvailable && !c.cameraOn && !c.screenShareOn)))
                        return I18n.t("Không tìm thấy camera: người kia chỉ nghe thấy tiếng.", "No camera found: the other person only hears you.");
                    if (c.cameraError.length > 0 && c.cameraError !== "no_encoder" && c.cameraError !== "no_camera")
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

    Rectangle {
        id: bar
        z: 7
        visible: !root.minimized
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 120
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#73000000" }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 28
            spacing: root.width < 560 ? 8 : 16
            baselineOffset: 0

            CircleButton {
                visible: root.phase === "incoming"
                icon: "call_end"
                tip: I18n.t("Từ chối", "Decline")
                onClicked: ZaloClient.calls.reject()
            }
            CircleButton {
                visible: root.phase === "incoming" && !!ZaloClient.calls && ZaloClient.calls.canAnswer && ZaloClient.calls.isVideo
                icon: "phone"
                tint: "#0068FF"
                tip: I18n.t("Trả lời không camera", "Answer without camera")
                onClicked: ZaloClient.calls.accept(true)
            }
            CircleButton {
                visible: root.phase === "incoming" && !!ZaloClient.calls && ZaloClient.calls.canAnswer
                icon: ZaloClient.calls && ZaloClient.calls.isVideo ? "camera" : "phone"
                tint: "#2BB673"
                tip: ZaloClient.calls && ZaloClient.calls.isVideo ? I18n.t("Trả lời bằng video", "Answer with video") : I18n.t("Trả lời", "Answer")
                onClicked: ZaloClient.calls.accept(false)
            }
            PillButton {
                visible: root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing"
                icon: ZaloClient.calls && ZaloClient.calls.muted ? "mic_off" : "mic"
                on: !!(ZaloClient.calls && ZaloClient.calls.muted)
                tip: ZaloClient.calls && ZaloClient.calls.muted ? I18n.t("Bật micro", "Unmute") : I18n.t("Tắt micro", "Mute")
                onClicked: ZaloClient.calls.setMuted(!ZaloClient.calls.muted)
            }
            PillButton {
                visible: root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing"
                icon: ZaloClient.calls && ZaloClient.calls.speakerOff ? "volume_off" : "volume"
                on: !!(ZaloClient.calls && ZaloClient.calls.speakerOff)
                tip: ZaloClient.calls && ZaloClient.calls.speakerOff ? I18n.t("Bật loa", "Speaker on") : I18n.t("Tắt loa", "Speaker off")
                onClicked: ZaloClient.calls.setSpeakerOff(!ZaloClient.calls.speakerOff)
            }
            PillButton {
                visible: !!(ZaloClient.calls && ZaloClient.calls.isVideo) && (root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing")
                enabled: !!(ZaloClient.calls && (ZaloClient.calls.cameraAvailable || ZaloClient.calls.cameraOn))
                icon: ZaloClient.calls && ZaloClient.calls.cameraOn ? "camera" : "camera_off"
                on: !(ZaloClient.calls && ZaloClient.calls.cameraOn)
                tip: ZaloClient.calls && ZaloClient.calls.cameraOn ? I18n.t("Tắt camera", "Turn off camera") : I18n.t("Bật camera", "Turn on camera")
                onClicked: ZaloClient.calls.setCamera(!ZaloClient.calls.cameraOn)
            }
            PillButton {
                visible: !!(ZaloClient.calls && !ZaloClient.calls.isGroup && ZaloClient.calls.videoSupported) && root.phase === "connected"
                enabled: !!(ZaloClient.calls && (ZaloClient.calls.screenShareAvailable || ZaloClient.calls.screenShareOn))
                icon: ZaloClient.calls && ZaloClient.calls.screenShareOn ? "stop_screen_share" : "screen_share"
                on: !!(ZaloClient.calls && ZaloClient.calls.screenShareOn)
                tip: ZaloClient.calls && ZaloClient.calls.screenShareOn ? I18n.t("Dừng chia sẻ màn hình", "Stop sharing screen") : I18n.t("Chia sẻ màn hình", "Share screen")
                onClicked: ZaloClient.calls.setScreenShare(!ZaloClient.calls.screenShareOn)
            }
            PillButton {
                objectName: "callSettingsBtn"
                visible: root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing"
                icon: "settings"
                on: root.deviceMenuOpen
                tip: I18n.t("Thiết bị & âm lượng", "Devices & volume")
                onClicked: root.deviceMenuOpen = !root.deviceMenuOpen
            }
            CircleButton {
                visible: root.phase !== "incoming"
                icon: root.phase === "ended" ? "close" : "call_end"
                tip: root.phase === "ended" ? I18n.t("Đóng", "Close") : I18n.t("Kết thúc", "End call")
                onClicked: ZaloClient.calls.hangup()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 6
        enabled: !root.minimized && root.deviceMenuOpen
        visible: enabled
        onClicked: root.deviceMenuOpen = false
    }

    Rectangle {
        id: deviceMenuCard
        objectName: "callDeviceMenu"
        z: 7
        visible: !root.minimized && root.deviceMenuOpen && root.phase !== "ended" && root.phase !== "incoming"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: bar.top
        anchors.bottomMargin: 8
        width: Math.min(340, parent.width - 32)
        height: Math.min(380, Math.max(120, root.height - bar.height - 28), devCol.implicitHeight + 24)
        radius: 12
        color: "#EB151A22"
        border.color: "#45FFFFFF"
        border.width: 1
        clip: true

        Flickable {
            id: devFlick
            anchors.fill: parent
            anchors.margins: 12
            contentHeight: devCol.implicitHeight
            clip: true

            ColumnLayout {
                id: devCol
                width: devFlick.width
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    ZIcon { name: "mic"; size: 15; color: "#9DB2CE" }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.t("Micro", "Microphone")
                        color: "#9DB2CE"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                DeviceOptionRow {
                    Layout.fillWidth: true
                    labelText: I18n.t("Mặc định hệ thống", "System default")
                    selected: ZaloClient.calls ? ZaloClient.calls.selectedMicId.length === 0 : true
                    onPicked: if (ZaloClient.calls) ZaloClient.calls.selectAudioDevice("mic", "")
                }

                Repeater {
                    model: root.deviceMenuOpen && ZaloClient.calls ? ZaloClient.calls.audioInputDeviceList : []
                    delegate: DeviceOptionRow {
                        id: micOpt
                        required property var modelData
                        Layout.fillWidth: true
                        labelText: String(micOpt.modelData.name || "")
                        selected: ZaloClient.calls ? ZaloClient.calls.selectedMicId === String(micOpt.modelData.id || "") : false
                        onPicked: if (ZaloClient.calls) ZaloClient.calls.selectAudioDevice("mic", String(micOpt.modelData.id || ""))
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 6
                    Text {
                        Layout.fillWidth: true
                        text: I18n.t("Âm lượng micro", "Microphone volume")
                        color: "#CFD3D8"
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }
                    Rectangle {
                        id: micModeToggle
                        objectName: "callMicModeToggle"
                        property bool autoGain: ZaloClient.calls ? ZaloClient.calls.micAutoGain : true
                        implicitWidth: micModeRow.implicitWidth + 14
                        implicitHeight: 20
                        radius: 10
                        color: autoGain ? "#2A0068FF" : "#18FFFFFF"
                        border.color: autoGain ? "#664C9BFF" : "#30FFFFFF"
                        border.width: 1
                        RowLayout {
                            id: micModeRow
                            anchors.centerIn: parent
                            spacing: 4
                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                color: micModeToggle.autoGain ? "#4CD964" : "#9DB2CE"
                            }
                            Text {
                                text: micModeToggle.autoGain
                                      ? I18n.t("Tự động", "Auto")
                                      : I18n.t("Thủ công", "Manual")
                                color: micModeToggle.autoGain ? "#DCEBFF" : "#CFD3D8"
                                font.family: Theme.fontFamily
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: if (ZaloClient.calls) ZaloClient.calls.setMicAutoGain(!micModeToggle.autoGain)
                        }
                    }
                    Text {
                        text: String(ZaloClient.calls ? ZaloClient.calls.micVolume : 100) + "%"
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Item {
                    id: micVolumeSlider
                    objectName: "callMicVolumeSlider"
                    Layout.fillWidth: true
                    height: 20

                    Rectangle {
                        id: micTrack
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 6
                        radius: 3
                        color: "#38FFFFFF"

                        Rectangle {
                            height: parent.height
                            width: Math.max(0, Math.min(1, (ZaloClient.calls ? ZaloClient.calls.micVolume : 100) / 100.0)) * parent.width
                            radius: 3
                            color: "#0068FF"
                        }
                    }

                    Rectangle {
                        width: 14
                        height: 14
                        radius: 7
                        anchors.verticalCenter: parent.verticalCenter
                        x: Math.max(0, Math.min(parent.width - width,
                                                ((ZaloClient.calls ? ZaloClient.calls.micVolume : 100) / 100.0) * (parent.width - width)))
                        color: "#FFFFFF"
                        border.color: "#0068FF"
                        border.width: 2
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        function updateVol(mx: real): void {
                            if (!ZaloClient.calls || width <= 0) return;
                            var pct = Math.round(Math.max(0, Math.min(1, mx / width)) * 100);
                            ZaloClient.calls.setMicVolume(pct);
                        }
                        onPressed: function(mouse) { updateVol(mouse.x); }
                        onPositionChanged: function(mouse) { if (pressed) updateVol(mouse.x); }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 6
                    radius: 3
                    color: "#22FFFFFF"
                    clip: true

                    Rectangle {
                        id: micMeterFill
                        objectName: "callMicMeterFill"
                        height: parent.height
                        readonly property int dbov: ZaloClient.calls ? Math.max(0, Math.min(60, ZaloClient.calls.micLevel)) : 60
                        width: Math.max(0, Math.min(1, (60 - micMeterFill.dbov) / 60.0)) * parent.width
                        radius: 3
                        color: micMeterFill.dbov <= 9 ? "#EF4444" : "#22C55E"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    height: 1
                    color: "#24FFFFFF"
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    spacing: 6
                    ZIcon { name: "volume"; size: 15; color: "#9DB2CE" }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.t("Loa", "Speaker")
                        color: "#9DB2CE"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                DeviceOptionRow {
                    Layout.fillWidth: true
                    labelText: I18n.t("Mặc định hệ thống", "System default")
                    selected: ZaloClient.calls ? ZaloClient.calls.selectedSpeakerId.length === 0 : true
                    onPicked: if (ZaloClient.calls) ZaloClient.calls.selectAudioDevice("speaker", "")
                }

                Repeater {
                    model: root.deviceMenuOpen && ZaloClient.calls ? ZaloClient.calls.audioOutputDeviceList : []
                    delegate: DeviceOptionRow {
                        id: spkOpt
                        required property var modelData
                        Layout.fillWidth: true
                        labelText: String(spkOpt.modelData.name || "")
                        selected: ZaloClient.calls ? ZaloClient.calls.selectedSpeakerId === String(spkOpt.modelData.id || "") : false
                        onPicked: if (ZaloClient.calls) ZaloClient.calls.selectAudioDevice("speaker", String(spkOpt.modelData.id || ""))
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    height: 1
                    color: "#24FFFFFF"
                    visible: !!(ZaloClient.calls && ZaloClient.calls.isVideo && !ZaloClient.calls.screenShareOn)
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    spacing: 6
                    visible: !!(ZaloClient.calls && ZaloClient.calls.isVideo && !ZaloClient.calls.screenShareOn)
                    ZIcon { name: "camera"; size: 15; color: "#9DB2CE" }
                    Text {
                        Layout.fillWidth: true
                        text: I18n.t("Camera", "Camera")
                        color: "#9DB2CE"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                }

                DeviceOptionRow {
                    Layout.fillWidth: true
                    visible: !!(ZaloClient.calls && ZaloClient.calls.isVideo && !ZaloClient.calls.screenShareOn)
                    labelText: I18n.t("Tự động (bỏ camera hồng ngoại)", "Automatic (skip IR camera)")
                    selected: ZaloClient.calls ? ZaloClient.calls.selectedCameraId.length === 0 : true
                    onPicked: if (ZaloClient.calls) ZaloClient.calls.selectCameraDevice("")
                }

                Repeater {
                    model: (root.deviceMenuOpen && ZaloClient.calls && ZaloClient.calls.isVideo && !ZaloClient.calls.screenShareOn)
                           ? ZaloClient.calls.cameraDeviceList : []
                    delegate: DeviceOptionRow {
                        id: camOpt
                        required property var modelData
                        Layout.fillWidth: true
                        labelText: String(camOpt.modelData.name || "")
                        selected: ZaloClient.calls ? ZaloClient.calls.selectedCameraId === String(camOpt.modelData.id || "") : false
                        onPicked: if (ZaloClient.calls) ZaloClient.calls.selectCameraDevice(String(camOpt.modelData.id || ""))
                    }
                }
            }
        }
    }

    Rectangle {
        id: miniBar
        z: 5
        visible: root.minimized
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        color: "#D912161E"

        Row {
            anchors.centerIn: parent
            spacing: 10
            baselineOffset: 0

            MiniButton {
                visible: root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing"
                icon: ZaloClient.calls && ZaloClient.calls.muted ? "mic_off" : "mic"
                on: !!(ZaloClient.calls && ZaloClient.calls.muted)
                tip: ZaloClient.calls && ZaloClient.calls.muted ? I18n.t("Bật micro", "Unmute") : I18n.t("Tắt micro", "Mute")
                onClicked: ZaloClient.calls.setMuted(!ZaloClient.calls.muted)
            }
            MiniButton {
                visible: !!(ZaloClient.calls && ZaloClient.calls.isVideo) && (root.phase === "connected" || root.phase === "connecting" || root.phase === "outgoing")
                enabled: !!(ZaloClient.calls && (ZaloClient.calls.cameraAvailable || ZaloClient.calls.cameraOn))
                icon: ZaloClient.calls && ZaloClient.calls.cameraOn ? "camera" : "camera_off"
                on: !(ZaloClient.calls && ZaloClient.calls.cameraOn)
                tip: ZaloClient.calls && ZaloClient.calls.cameraOn ? I18n.t("Tắt camera", "Turn off camera") : I18n.t("Bật camera", "Turn on camera")
                onClicked: ZaloClient.calls.setCamera(!ZaloClient.calls.cameraOn)
            }
            MiniButton {
                visible: !!(ZaloClient.calls && !ZaloClient.calls.isGroup && ZaloClient.calls.videoSupported) && root.phase === "connected"
                enabled: !!(ZaloClient.calls && (ZaloClient.calls.screenShareAvailable || ZaloClient.calls.screenShareOn))
                icon: ZaloClient.calls && ZaloClient.calls.screenShareOn ? "stop_screen_share" : "screen_share"
                on: !!(ZaloClient.calls && ZaloClient.calls.screenShareOn)
                tip: ZaloClient.calls && ZaloClient.calls.screenShareOn ? I18n.t("Dừng chia sẻ màn hình", "Stop sharing screen") : I18n.t("Chia sẻ màn hình", "Share screen")
                onClicked: ZaloClient.calls.setScreenShare(!ZaloClient.calls.screenShareOn)
            }
            MiniButton {
                objectName: "callExpandBtn"
                icon: "open_in_full"
                tip: I18n.t("Phóng to", "Expand")
                onClicked: root.minimized = false
            }
            MiniButton {
                icon: root.phase === "ended" ? "close" : "call_end"
                destructive: true
                tip: root.phase === "ended" ? I18n.t("Đóng", "Close") : I18n.t("Kết thúc", "End call")
                onClicked: ZaloClient.calls.hangup()
            }
        }
    }
    }
}
