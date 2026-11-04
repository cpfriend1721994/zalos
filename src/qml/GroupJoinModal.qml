import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    anchors.fill: parent
    readonly property var preview: ZaloClient.groupAdmin.preview
    readonly property bool hasGroup: (root.preview.groupId || "").length > 0

    function close(): void { ZaloClient.groupAdmin.closeJoin(); }

    Component.onCompleted: {
        linkField.text = root.preview.link || "";
        if (linkField.text.length === 0) linkField.forceActiveFocus();
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.5
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 420)
        height: Math.min(root.height - 40, content.implicitHeight + 40)
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZIcon { name: "link"; size: 20; color: Theme.zaloBlue }
                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Tham gia nhóm bằng link", "Join a group by link")
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: closeMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "close"; size: 16; color: ThemeManager.iconColor }
                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ZTextField {
                    id: linkField
                    Layout.fillWidth: true
                    placeholderText: "https://zalo.me/g/…"
                    onAccepted: ZaloClient.groupAdmin.previewLink(text)
                }
                ZButton {
                    variant: "secondary"
                    text: I18n.t("Xem", "Open")
                    enabled: linkField.text.trim().length > 0 && !root.preview.loading
                    onClicked: ZaloClient.groupAdmin.previewLink(linkField.text)
                }
            }

            Text {
                visible: root.preview.loading === true
                text: I18n.t("Đang tải thông tin nhóm…", "Loading the group…")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.secondaryTextColor
            }
            Text {
                visible: (root.preview.error || "").length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.preview.error || ""
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: Theme.badgeRed
            }

            ColumnLayout {
                visible: root.hasGroup
                Layout.fillWidth: true
                spacing: 8
                ZAvatar {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 72
                    size: 72
                    avatarUrl: root.preview.avatar || ""
                    name: root.preview.name || ""
                    showStatusDot: false
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: root.preview.name || ""
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: I18n.t("%1 thành viên", "%1 members").arg(root.preview.totalMember || 0)
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                }
                Text {
                    visible: (root.preview.desc || "").length > 0
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    text: root.preview.desc || ""
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    color: ThemeManager.textColor
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: -6
                    Repeater {
                        model: root.preview.members || []
                        ZAvatar {
                            required property var modelData
                            width: 28
                            height: 28
                            size: 28
                            avatarUrl: modelData.avatar
                            name: modelData.name
                            showStatusDot: false
                        }
                    }
                }
                Text {
                    visible: root.preview.needsApproval === true && root.preview.isMember !== true
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: I18n.t("Nhóm cần trưởng hoặc phó nhóm duyệt trước khi bạn vào nhóm.",
                                 "The owner or a deputy must approve you before you join.")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                }
                ZButton {
                    Layout.fillWidth: true
                    enabled: root.preview.joining !== true
                    text: root.preview.isMember === true ? I18n.t("Mở nhóm", "Open group")
                          : root.preview.joining === true ? I18n.t("Đang tham gia…", "Joining…")
                          : root.preview.needsApproval === true ? I18n.t("Gửi yêu cầu tham gia", "Ask to join")
                          : I18n.t("Tham gia nhóm", "Join group")
                    onClicked: ZaloClient.groupAdmin.join()
                }
            }
        }
    }
}
