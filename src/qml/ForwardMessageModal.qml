import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    visible: ZaloClient.showForwardModal
    anchors.fill: parent
    z: 220

    property string filterText: ""
    readonly property string mode: ZaloClient.forwardMode
    property var sentIds: ({})
    property var friends: []

    function closeModal(): void {
        ZaloClient.closeForwardModal()
        filterInput.text = ""
    }

    function matches(name: string): bool {
        return root.filterText.length === 0 || (name || "").toLowerCase().indexOf(root.filterText) !== -1;
    }

    function sendTo(id: string, name: string): void {
        if (id.length === 0 || root.sentIds[id]) return;
        if (root.mode === "card") ZaloClient.sendContactCard(id, ZaloClient.forwardCardUid, "");
        else if (root.mode === "pickContact") ZaloClient.sendContactCard(ZaloClient.currentConversationId, id, name);
        else ZaloClient.forwardMessage(id, ZaloClient.forwardSourceMsgId);
        const next = Object.assign({}, root.sentIds);
        next[id] = true;
        root.sentIds = next;
        if (root.mode === "pickContact") root.closeModal();
    }

    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (visible) {
            filterInput.text = ""
            root.sentIds = ({})
            root.friends = root.mode === "pickContact" ? ZaloClient.friendsForCard() : []
            filterInput.forceActiveFocus()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.visible ? 0.5 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        MouseArea {
            anchors.fill: parent
            onClicked: root.closeModal()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        height: Math.min(root.height - 40, 560)
        scale: root.visible ? 1.0 : 0.94
        opacity: root.visible ? 1.0 : 0.0
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.mode === "card" ? I18n.t("Chia sẻ danh thiếp", "Share contact card")
                          : root.mode === "pickContact" ? I18n.t("Gửi danh thiếp", "Send a contact card")
                          : I18n.t("Chuyển tiếp tin nhắn", "Forward message")
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: 28
                    height: 28
                    radius: 14
                    color: closeHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                    ZIcon {
                        anchors.centerIn: parent
                        name: "close"
                        size: 16
                        color: ThemeManager.secondaryTextColor
                    }

                    MouseArea {
                        id: closeHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeModal()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: Math.min(68, previewCol.implicitHeight + 16)
                radius: 6
                color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.03)
                border.color: ThemeManager.borderColor
                border.width: 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    radius: 1.5
                    color: ThemeManager.accentColor
                }

                ColumnLayout {
                    id: previewCol
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 2

                    Text {
                        text: root.mode === "message" ? I18n.t("Nội dung tin nhắn:", "Message content:")
                                                      : I18n.t("Danh thiếp:", "Contact card:")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }

                    Text {
                        Layout.fillWidth: true
                        text: ZaloClient.forwardSourcePreview.length > 0
                              ? ZaloClient.forwardSourcePreview
                              : I18n.t("[Tin nhắn / Tệp đính kèm]", "[Message / Attachment]")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 36
                radius: 6
                color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.04)
                border.color: filterInput.activeFocus ? ThemeManager.accentColor : ThemeManager.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    ZIcon {
                        name: "search"
                        size: 16
                        color: ThemeManager.secondaryTextColor
                    }

                    TextInput {
                        id: filterInput
                        Layout.fillWidth: true
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                        selectionColor: ThemeManager.accentColor
                        selectByMouse: true
                        clip: true
                        onTextChanged: root.filterText = text.trim().toLowerCase()

                        Text {
                            anchors.fill: parent
                            text: root.mode === "pickContact" ? I18n.t("Tìm bạn bè...", "Search friends...")
                                                              : I18n.t("Tìm kiếm hội thoại hoặc liên hệ...", "Search conversations or contacts...")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.secondaryTextColor
                            visible: !filterInput.text && !filterInput.activeFocus
                        }
                    }

                    Rectangle {
                        visible: filterInput.text.length > 0
                        width: 18
                        height: 18
                        radius: 9
                        color: clearHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                        ZIcon {
                            anchors.centerIn: parent
                            name: "close"
                            size: 12
                            color: ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            id: clearHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: filterInput.text = ""
                        }
                    }
                }
            }

            Text {
                text: root.mode === "pickContact" ? I18n.t("Bạn bè", "Friends") : I18n.t("Gần đây", "Recent")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.bold: true
                color: ThemeManager.secondaryTextColor
                Layout.topMargin: 4
            }

            component TargetRow: Rectangle {
                id: rowItem
                property string rowId: ""
                property string rowName: ""
                property string rowAvatar: ""
                property string rowSub: ""
                property bool rowIsGroup: false
                property var rowMembers: []
                property int rowMemberCount: 0
                property bool rowHidden: false
                readonly property bool sent: root.sentIds[rowItem.rowId] === true
                radius: 8
                color: rowHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                visible: !rowItem.rowHidden && root.matches(rowItem.rowName)
                height: visible ? 52 : 0

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10
                    ZAvatar {
                        size: 38
                        avatarUrl: rowItem.rowAvatar
                        name: rowItem.rowName
                        isGroup: rowItem.rowIsGroup
                        memberCount: rowItem.rowMemberCount
                        memberAvatars: rowItem.rowMembers
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: rowItem.rowName
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: rowItem.rowSub.length > 0
                            Layout.fillWidth: true
                            text: rowItem.rowSub
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                            elide: Text.ElideRight
                        }
                    }
                    ZButton {
                        variant: rowItem.sent ? "secondary" : "primary"
                        enabled: !rowItem.sent
                        text: rowItem.sent ? I18n.t("Đã gửi", "Sent") : I18n.t("Gửi", "Send")
                        onClicked: root.sendTo(rowItem.rowId, rowItem.rowName)
                    }
                }
                MouseArea {
                    id: rowHover
                    anchors.fill: parent
                    z: -1
                    hoverEnabled: true
                    onClicked: root.sendTo(rowItem.rowId, rowItem.rowName)
                }
            }

            ListView {
                id: targetList
                visible: root.mode !== "pickContact"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.mode !== "pickContact" ? ZaloClient.conversationModel : null
                spacing: 4
                ScrollBar.vertical: ScrollBar { active: true; width: 6 }
                delegate: TargetRow {
                    required property var model
                    width: targetList.width
                    rowId: model.id
                    rowName: model.name || (model.isGroup ? I18n.t("Nhóm", "Group") : I18n.t("Người dùng Zalo", "Zalo user"))
                    rowAvatar: model.avatarUrl || ""
                    rowIsGroup: model.isGroup
                    rowMemberCount: model.memberCount
                    rowHidden: model.isHidden
                    rowMembers: model.memberAvatars || []
                    rowSub: model.lastMessage || (model.isGroup ? I18n.t("Nhóm", "Group") : I18n.t("Cá nhân", "Direct"))
                }
            }

            ListView {
                id: friendList
                visible: root.mode === "pickContact"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.friends
                spacing: 4
                ScrollBar.vertical: ScrollBar { active: true; width: 6 }
                delegate: TargetRow {
                    required property var modelData
                    width: friendList.width
                    rowId: modelData.id
                    rowName: modelData.name
                    rowAvatar: modelData.avatar || ""
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.friends.length === 0
                    text: I18n.t("Chưa có bạn bè nào", "No friends yet")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 80
                    height: 32
                    radius: 6
                    color: closeBtnHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: I18n.t("Đóng", "Close")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                    }

                    MouseArea {
                        id: closeBtnHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeModal()
                    }
                }
            }
        }
    }
}
