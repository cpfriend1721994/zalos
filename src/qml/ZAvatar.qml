import QtQuick
import Zalos

Item {
    id: root
    width: size
    height: size
    implicitWidth: size
    implicitHeight: size

    property string avatarUrl: ""
    property string name: ""
    property int size: 40
    property bool isOnline: false
    property int memberCount: 0
    property bool isGroup: memberCount > 0
    property bool showStatusDot: false
    property var memberAvatars: []

    readonly property bool hasImage: avatarImage.status === Image.Ready
    readonly property bool imagePending: root.avatarUrl.length > 0 && avatarImage.status === Image.Loading
    readonly property color placeholderColor: ThemeManager.isDarkMode ? "#3B4048" : "#E4E7EC"
    readonly property bool composite: root.isGroup && root.avatarUrl.length === 0 && root.memberAvatars.length > 0
    readonly property int shownMembers: root.memberCount > 4 ? 3 : Math.min(4, root.memberAvatars.length)
    readonly property bool countCell: root.memberCount > 4

    function paletteColor(key: string): color {
        var palette = ["#0284C7", "#059669", "#EA580C", "#DC2626", "#7C3AED", "#2563EB", "#0D9488"];
        var hash = 0;
        for (var i = 0; i < key.length; i++) hash = (hash * 31 + key.charCodeAt(i)) % palette.length;
        return key.length > 0 ? palette[Math.abs(hash)] : ThemeManager.inputFillHoverColor;
    }

    function initials(text: string): string {
        var trimmed = (text || "").trim();
        if (trimmed.length === 0) return "";
        var parts = trimmed.split(/\s+/).filter(function(p) { return p.length > 0; });
        if (parts.length === 0) return "";
        if (parts.length === 1) return parts[0].charAt(0).toUpperCase();
        return (parts[0].charAt(0) + parts[parts.length - 1].charAt(0)).toUpperCase();
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: !root.hasImage && !root.composite
        color: root.imagePending ? root.placeholderColor : root.paletteColor(root.name || "")

        Text {
            anchors.centerIn: parent
            visible: !root.imagePending && !root.isGroup && text.length > 0
            text: root.initials(root.name)
            font.family: Theme.fontFamily
            font.bold: true
            font.pixelSize: Math.max(9, Math.floor(root.size * (text.length > 1 ? 0.36 : 0.42)))
            color: "#FFFFFF"
        }

        ZIcon {
            anchors.centerIn: parent
            visible: !root.imagePending && (root.isGroup || root.initials(root.name).length === 0)
            name: root.isGroup ? "contacts" : "user"
            size: Math.max(12, Math.floor(root.size * 0.5))
            color: "#FFFFFF"
        }
    }

    Image {
        id: avatarImage
        anchors.fill: parent
        asynchronous: true
        cache: true
        smooth: true
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.ceil(root.size * (Screen.devicePixelRatio || 1))
        sourceSize.height: Math.ceil(root.size * (Screen.devicePixelRatio || 1))
        source: root.avatarUrl.length > 0 ? "image://avatar/" + encodeURIComponent(root.avatarUrl) : ""
        visible: root.hasImage
        opacity: root.hasImage ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    Rectangle {
        visible: root.showStatusDot && root.isOnline && !root.isGroup
        width: Math.max(11, Math.round(root.size * 0.35))
        height: width
        radius: width / 2
        color: Theme.onlineGreen
        border.color: ThemeManager.surfaceColor
        border.width: root.size >= 40 ? 2 : 1.5
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.bottomMargin: -Math.round(width * 0.15)
        anchors.rightMargin: -Math.round(width * 0.15)
    }

    Item {
        id: compositeBox
        anchors.fill: parent
        visible: root.composite
        readonly property int cells: root.shownMembers + (root.countCell ? 1 : 0)
        readonly property real cell: cells <= 2 ? root.size * 0.62 : root.size * 0.54
        function cellX(i: int): real {
            if (cells <= 2) return i === 0 ? 0 : root.size - cell;
            if (cells === 3) return i === 0 ? (root.size - cell) / 2 : (i === 1 ? 0 : root.size - cell);
            return i % 2 === 0 ? 0 : root.size - cell;
        }
        function cellY(i: int): real {
            if (cells <= 2) return i === 0 ? 0 : root.size - cell;
            if (cells === 3) return i === 0 ? 0 : root.size - cell;
            return i < 2 ? 0 : root.size - cell;
        }

        Repeater {
            model: root.composite ? root.memberAvatars.slice(0, root.shownMembers) : []
            delegate: Rectangle {
                id: memberCell
                required property var modelData
                required property int index
                x: compositeBox.cellX(memberCell.index)
                y: compositeBox.cellY(memberCell.index)
                width: compositeBox.cell
                height: compositeBox.cell
                readonly property bool unnamed: (memberCell.modelData.name || "").length === 0
                readonly property bool pending: memberImage.status === Image.Loading
                radius: width / 2
                color: memberCell.pending ? root.placeholderColor
                       : memberCell.unnamed ? (ThemeManager.isDarkMode ? "#4A5361" : "#B8C1CC")
                                            : root.paletteColor(memberCell.modelData.name)
                border.color: ThemeManager.surfaceColor
                border.width: 1
                ZIcon {
                    anchors.centerIn: parent
                    visible: memberCell.unnamed && memberImage.status !== Image.Ready && !memberCell.pending
                    name: "user"
                    size: Math.max(8, Math.floor(compositeBox.cell * 0.6))
                    color: "#FFFFFF"
                }
                Text {
                    anchors.centerIn: parent
                    visible: !memberCell.unnamed && memberImage.status !== Image.Ready && !memberCell.pending
                    text: root.initials(memberCell.modelData.name).charAt(0)
                    font.family: Theme.fontFamily
                    font.bold: true
                    font.pixelSize: Math.max(7, Math.floor(compositeBox.cell * 0.45))
                    color: "#FFFFFF"
                }
                Image {
                    id: memberImage
                    anchors.fill: parent
                    anchors.margins: 1
                    asynchronous: true
                    cache: true
                    smooth: true
                    sourceSize.width: Math.ceil(compositeBox.cell * (Screen.devicePixelRatio || 1))
                    sourceSize.height: Math.ceil(compositeBox.cell * (Screen.devicePixelRatio || 1))
                    source: (memberCell.modelData.avatar || "").length > 0
                            ? "image://avatar/" + encodeURIComponent(memberCell.modelData.avatar) : ""
                    visible: status === Image.Ready
                }
            }
        }
        Rectangle {
            visible: root.countCell
            x: compositeBox.cellX(3)
            y: compositeBox.cellY(3)
            width: compositeBox.cell
            height: compositeBox.cell
            radius: width / 2
            color: ThemeManager.isDarkMode ? "#3B4048" : "#E4E7EC"
            border.color: ThemeManager.surfaceColor
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: root.memberCount > 99 ? "99+" : root.memberCount
                font.family: Theme.fontFamily
                font.pixelSize: Math.max(7, Math.floor(compositeBox.cell * (root.memberCount > 99 ? 0.32 : 0.42)))
                color: ThemeManager.isDarkMode ? "#DCE1E8" : "#4A5361"
            }
        }
    }
}
