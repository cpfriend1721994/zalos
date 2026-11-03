import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    visible: ZaloClient.showCreateGroupModal
    anchors.fill: parent
    z: 200

    // Selected friend ids (map id -> true) and derived count
    property var selectedMembers: ({})
    property int selectedCount: 0
    property string filterText: ""
    property bool creating: false
    property string resultMessage: ""
    property bool resultOk: false
    // Zalo requires at least 3 people including the current user
    readonly property int minMembers: 2
    readonly property bool canCreate: root.selectedCount >= root.minMembers && !root.creating

    function isMemberSelected(id: string): bool {
        return Boolean(root.selectedMembers && root.selectedMembers[id]);
    }

    function toggleMember(id) {
        var s = Object.assign({}, root.selectedMembers || {});
        if (s[id]) {
            delete s[id];
        } else {
            s[id] = true;
        }
        root.selectedMembers = s;
        root.selectedCount = Object.keys(s).length;
    }

    function matchesFilter(name: var): bool {
        var q = root.filterText.trim().toLowerCase();
        if (q.length === 0)
            return true;
        return String(name || "").toLowerCase().indexOf(q) !== -1;
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
    }

    function closeModal(): void {
        root.resetState();
        ZaloClient.closeCreateGroup();
    }

    function submit() {
        if (!root.canCreate)
            return;
        root.creating = true;
        root.resultMessage = "";
        ZaloClient.createGroup(groupNameInput.text.trim(), Object.keys(root.selectedMembers));
    }

    // Created on demand by a Loader in Main.qml, already visible: run the same init.
    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (visible)
            root.resetState();
    }

    Connections {
        target: ZaloClient
        function onGroupCreateFinished(ok, message) {
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

    // Backdrop Dim
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

    // Modal Card
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

        // Swallow clicks so the backdrop does not close the modal
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            // Header Row
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

            // Group Name Row
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

            // Member Search Field
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
                        onTextChanged: root.filterText = text

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

            // Friend list with checkboxes (friends only, filtered by name)
            ListView {
                id: memberListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 80
                clip: true
                spacing: 2
                model: ZaloClient.contactModel
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // Count of delegates actually shown (after friend/name filtering)
                property int shownCount: 0
                function recountShown() {
                    var n = 0;
                    for (var i = 0; i < memberListView.contentItem.children.length; ++i) {
                        var c = memberListView.contentItem.children[i];
                        if (c && c.isMemberRow === true && c.visible)
                            n++;
                    }
                    memberListView.shownCount = n;
                }
                onCountChanged: Qt.callLater(recountShown)
                Connections {
                    target: root
                    function onFilterTextChanged() { Qt.callLater(memberListView.recountShown) }
                    function onVisibleChanged() { Qt.callLater(memberListView.recountShown) }
                }

                delegate: Rectangle {
                    id: memberRow
                    readonly property bool isMemberRow: true
                    readonly property bool shown: !model.isGroup && root.matchesFilter(model.name)
                    width: memberListView.width
                    height: shown ? 52 : 0
                    visible: shown
                    radius: 8
                    color: itemMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 12

                        // Checkbox
                        Rectangle {
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            radius: 10
                            border.color: root.isMemberSelected(model.id) ? Theme.zaloBlue : ThemeManager.borderColor
                            border.width: 2
                            color: root.isMemberSelected(model.id) ? Theme.zaloBlue : "transparent"

                            ZIcon {
                                visible: root.isMemberSelected(model.id)
                                anchors.centerIn: parent
                                name: "check"
                                size: 14
                                color: "#FFFFFF"
                            }
                        }

                        ZAvatar {
                            size: 36
                            avatarUrl: model.avatarUrl || ""
                            name: model.name || ""
                        }

                        Text {
                            Layout.fillWidth: true
                            text: model.name || ""
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        enabled: memberRow.shown
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleMember(model.id)
                    }
                }

                // Empty State
                ColumnLayout {
                    visible: memberListView.shownCount === 0
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 32, 320)
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: memberListView.count === 0
                              ? I18n.t("Chưa có bạn bè nào", "No friends yet")
                              : I18n.t("Không tìm thấy bạn bè phù hợp", "No matching friends")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: true
                        color: ThemeManager.textColor
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        visible: memberListView.count === 0
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

            // Inline error from the server
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

            // Footer: selection hint (left), actions flush right
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
