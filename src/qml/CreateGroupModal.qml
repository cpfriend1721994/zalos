pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    visible: ZaloClient.showCreateGroupModal
    anchors.fill: parent
    z: 200

    property var selectedMembers: ({})
    property int selectedCount: 0
    property string filterText: ""
    property var friendList: []
    property var filteredFriends: []
    property bool creating: false
    property string resultMessage: ""
    property bool resultOk: false
    readonly property int minMembers: 2
    readonly property bool canCreate: root.selectedCount >= root.minMembers && !root.creating

    function updateFilteredFriends(): void {
        var all = root.friendList || [];
        var q = root.filterText.trim().toLowerCase();
        if (q.length === 0) {
            root.filteredFriends = all;
            return;
        }
        var res = [];
        for (var i = 0; i < all.length; ++i) {
            var item = all[i];
            if (item && item.name && item.name.toLowerCase().indexOf(q) !== -1) {
                res.push(item);
            }
        }
        root.filteredFriends = res;
    }

    function isMemberSelected(id: string): bool {
        return Boolean(root.selectedMembers && root.selectedMembers[id]);
    }

    function toggleMember(id: string): void {
        var s = Object.assign({}, root.selectedMembers || {});
        if (s[id]) {
            delete s[id];
        } else {
            s[id] = true;
        }
        root.selectedMembers = s;
        root.selectedCount = Object.keys(s).length;
    }

    function resetState(): void {
        root.selectedMembers = ({});
        root.selectedCount = 0;
        root.filterText = "";
        root.creating = false;
        root.resultMessage = "";
        root.resultOk = false;
        groupNameInput.text = "";
        memberSearchInput.text = "";
        root.friendList = ZaloClient.friendsForCard();
        root.updateFilteredFriends();
    }

    function closeModal(): void {
        root.resetState();
        ZaloClient.closeCreateGroup();
    }

    function submit(): void {
        if (!root.canCreate)
            return;
        root.creating = true;
        root.resultMessage = "";
        ZaloClient.createGroup(groupNameInput.text.trim(), Object.keys(root.selectedMembers));
    }

    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (visible)
            root.resetState();
    }

    Connections {
        target: ZaloClient
        function onGroupCreateFinished(ok: bool, message: string): void {
            if (!root.creating)
                return;
            root.creating = false;
            root.resultOk = ok;
            if (ok) {
                root.closeModal();
            } else {
                root.resultMessage = (message && message.length > 0)
                        ? message
                        : I18n.t("Không thể tạo nhóm. Vui lòng thử lại.",
                                 "Could not create the group. Please try again.");
            }
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
        id: modalCard
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        height: Math.min(root.height - 32, 560)
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
                    text: I18n.t("Tạo nhóm", "Create group")
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    width: 28
                    height: 28
                    radius: 14
                    color: closeHover.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

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
                Layout.preferredHeight: 1
                color: ThemeManager.borderColor
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    radius: 20
                    color: (ThemeManager.hoverColor)
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    ZIcon {
                        anchors.centerIn: parent
                        name: "group_add"
                        size: 20
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 8
                    color: (ThemeManager.hoverColor)
                    border.color: groupNameInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                    border.width: groupNameInput.activeFocus ? 2 : 1

                    TextInput {
                        id: groupNameInput
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        clip: true

                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            text: I18n.t("Đặt tên nhóm", "Group name")
                            color: ThemeManager.secondaryTextColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            visible: !groupNameInput.text && !groupNameInput.activeFocus
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                radius: 8
                color: (ThemeManager.hoverColor)
                border.color: memberSearchInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    ZIcon {
                        name: "search"
                        size: 15
                        color: ThemeManager.secondaryTextColor
                    }

                    TextInput {
                        id: memberSearchInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        verticalAlignment: TextInput.AlignVCenter
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        selectByMouse: true
                        clip: true
                        onTextChanged: {
                            root.filterText = text;
                            root.updateFilteredFriends();
                        }

                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            text: I18n.t("Tìm bạn bè theo tên", "Search friends by name")
                            color: ThemeManager.secondaryTextColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            visible: !memberSearchInput.text && !memberSearchInput.activeFocus
                        }
                    }
                }
            }

            ListView {
                id: memberListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 80
                clip: true
                spacing: 2
                model: root.filteredFriends
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: memberRow
                    required property var modelData
                    readonly property string memberId: modelData.id || ""
                    readonly property string memberName: modelData.name || ""
                    readonly property string memberAvatar: modelData.avatar || ""
                    readonly property bool selected: root.isMemberSelected(memberRow.memberId)
                    width: memberListView.width
                    height: 52
                    radius: 8
                    color: itemMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            radius: 10
                            border.color: memberRow.selected ? Theme.zaloBlue : ThemeManager.borderColor
                            border.width: 2
                            color: memberRow.selected ? Theme.zaloBlue : "transparent"

                            ZIcon {
                                visible: memberRow.selected
                                anchors.centerIn: parent
                                name: "check"
                                size: 14
                                color: "#FFFFFF"
                            }
                        }

                        ZAvatar {
                            size: 36
                            avatarUrl: memberRow.memberAvatar
                            name: memberRow.memberName
                        }

                        Text {
                            Layout.fillWidth: true
                            text: memberRow.memberName
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleMember(memberRow.memberId)
                    }
                }

                ColumnLayout {
                    visible: memberListView.count === 0
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 32, 320)
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: (root.friendList && root.friendList.length === 0)
                              ? I18n.t("Chưa có bạn bè nào", "No friends yet")
                              : I18n.t("Không tìm thấy bạn bè phù hợp", "No matching friends")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: true
                        color: ThemeManager.textColor
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        visible: root.friendList && root.friendList.length === 0
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: I18n.t("Hãy kết bạn trước khi tạo nhóm.", "Add some friends before creating a group.")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Text {
                visible: root.resultMessage.length > 0
                Layout.fillWidth: true
                text: root.resultMessage
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: root.resultOk ? Theme.onlineGreen : Theme.badgeRed
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: ThemeManager.borderColor
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.selectedCount >= root.minMembers
                          ? I18n.t("Đã chọn " + root.selectedCount + " bạn", root.selectedCount + " friends selected")
                          : I18n.t("Chọn ít nhất " + root.minMembers + " bạn", "Select at least " + root.minMembers + " friends")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ThemeManager.secondaryTextColor
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: cancelLabel.implicitWidth + 28
                    Layout.preferredHeight: 36
                    radius: 8
                    color: btnCancelMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    Text {
                        id: cancelLabel
                        anchors.centerIn: parent
                        text: I18n.t("Hủy", "Cancel")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: ThemeManager.textColor
                    }

                    MouseArea {
                        id: btnCancelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.closeModal()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: submitLabel.implicitWidth + 28
                    Layout.preferredHeight: 36
                    radius: 8
                    color: root.canCreate ? (btnSubmitMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue)
                                          : (ThemeManager.dividerColor)

                    Text {
                        id: submitLabel
                        anchors.centerIn: parent
                        text: root.creating ? I18n.t("Đang tạo...", "Creating...") : I18n.t("Tạo nhóm", "Create group")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: true
                        color: root.canCreate ? "#FFFFFF" : "#94A3B8"
                    }

                    MouseArea {
                        id: btnSubmitMouse
                        anchors.fill: parent
                        enabled: root.canCreate
                        hoverEnabled: true
                        cursorShape: root.canCreate ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.submit()
                    }
                }
            }
        }
    }
}
