import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    visible: ZaloClient.showAddFriendModal
    anchors.fill: parent
    z: 200

    // Search / request state
    property bool searching: false
    property bool hasResult: false
    property var foundProfile: ({})
    property string searchError: ""
    property bool requestPending: false
    property string requestMessage: ""
    property bool requestOk: false

    function resetState(): void {
        root.searching = false;
        root.hasResult = false;
        root.foundProfile = ({});
        root.searchError = "";
        root.requestPending = false;
        root.requestMessage = "";
        root.requestOk = false;
        phoneInput.text = "";
        greetingInput.text = "";
    }

    function closeModal(): void {
        root.resetState();
        ZaloClient.closeAddFriend();
    }

    function defaultGreeting(): string {
        return I18n.t("Xin chào, mình là ", "Hi, I'm ") + ZaloClient.currentUserName;
    }

    function triggerSearch() {
        var digits = phoneInput.text.replace(/[^0-9+]/g, "");
        root.hasResult = false;
        root.requestMessage = "";
        if (digits.length === 0) {
            root.searchError = I18n.t("Vui lòng nhập số điện thoại để tìm kiếm.",
                                      "Please enter a phone number to search.");
            return;
        }
        if (digits.replace("+", "").length < 9) {
            root.searchError = I18n.t("Số điện thoại không hợp lệ (cần ít nhất 9 số).",
                                      "Invalid phone number (at least 9 digits required).");
            return;
        }
        root.searchError = "";
        root.searching = true;
        ZaloClient.findUserByPhone(digits);
    }

    // Created on demand by a Loader in Main.qml, already visible: run the same init.
    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (visible) {
            root.resetState();
            phoneInput.forceActiveFocus();
        }
    }

    Connections {
        target: ZaloClient
        function onUserSearchFinished(ok, profile, message) {
            if (!root.searching)
                return;
            root.searching = false;
            if (ok && profile && profile.uid) {
                root.foundProfile = profile;
                root.hasResult = true;
                root.searchError = "";
                root.requestMessage = "";
                greetingInput.text = root.defaultGreeting();
            } else {
                root.hasResult = false;
                root.searchError = (message && message.length > 0)
                        ? message
                        : I18n.t("Không tìm thấy người dùng với số điện thoại này.",
                                 "No user found with this phone number.");
            }
        }
        function onFriendRequestFinished(ok, message) {
            if (!root.requestPending)
                return;
            root.requestPending = false;
            root.requestOk = ok;
            root.requestMessage = (message && message.length > 0)
                    ? message
                    : (ok ? I18n.t("Đã gửi lời mời kết bạn.", "Friend request sent.")
                          : I18n.t("Không thể gửi lời mời kết bạn.", "Could not send friend request."));
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
        id: card
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 440)
        height: Math.min(root.height - 32, cardLayout.implicitHeight + 40)
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
            id: cardLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            // Header Row
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Thêm bạn", "Add friend")
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

            // Scrollable body
            ScrollView {
                id: bodyScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: bodyLayout.implicitHeight
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    id: bodyLayout
                    width: bodyScroll.availableWidth
                    spacing: 14

                    // Input Row: Country Code + Phone Number Input + Search
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 38
                            radius: 8
                            color: (ThemeManager.hoverColor)
                            border.color: ThemeManager.borderColor
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "+84"
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.bold: true
                                color: ThemeManager.textColor
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80
                            Layout.preferredHeight: 38
                            radius: 8
                            color: (ThemeManager.hoverColor)
                            border.color: phoneInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                            border.width: phoneInput.activeFocus ? 2 : 1

                            TextInput {
                                id: phoneInput
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                color: ThemeManager.textColor
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                verticalAlignment: TextInput.AlignVCenter
                                inputMethodHints: Qt.ImhDialableCharactersOnly
                                selectByMouse: true
                                clip: true
                                enabled: !root.searching

                                Text {
                                    anchors.fill: parent
                                    verticalAlignment: Text.AlignVCenter
                                    text: I18n.t("Số điện thoại", "Phone number")
                                    color: ThemeManager.secondaryTextColor
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    visible: !phoneInput.text && !phoneInput.activeFocus
                                }

                                Keys.onReturnPressed: root.triggerSearch()
                                Keys.onEnterPressed: root.triggerSearch()
                            }
                        }

                        // Search Button
                        Rectangle {
                            Layout.preferredWidth: searchLabel.implicitWidth + 28
                            Layout.preferredHeight: 38
                            radius: 8
                            opacity: root.searching ? 0.7 : 1.0
                            color: btnSearchMouse.containsMouse && !root.searching ? Theme.zaloBlueHover : Theme.zaloBlue

                            Text {
                                id: searchLabel
                                anchors.centerIn: parent
                                text: root.searching ? I18n.t("Đang tìm...", "Searching...")
                                                     : I18n.t("Tìm kiếm", "Search")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.bold: true
                                color: "#FFFFFF"
                            }

                            MouseArea {
                                id: btnSearchMouse
                                anchors.fill: parent
                                enabled: !root.searching
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.triggerSearch()
                            }
                        }
                    }

                    // Busy indicator while searching
                    RowLayout {
                        visible: root.searching
                        Layout.fillWidth: true
                        spacing: 8

                        BusyIndicator {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            running: root.searching
                        }

                        Text {
                            Layout.fillWidth: true
                            text: I18n.t("Đang tìm người dùng...", "Looking up user...")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Error Banner
                    Rectangle {
                        visible: root.searchError.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: errorText.implicitHeight + 16
                        radius: 6
                        color: (ThemeManager.errorBackgroundColor)
                        border.color: Theme.badgeRed
                        border.width: 1

                        Text {
                            id: errorText
                            anchors.fill: parent
                            anchors.margins: 8
                            verticalAlignment: Text.AlignVCenter
                            text: root.searchError
                            color: Theme.badgeRed
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Search Result Card
                    Rectangle {
                        visible: root.hasResult
                        Layout.fillWidth: true
                        Layout.preferredHeight: resultLayout.implicitHeight + 24
                        radius: 10
                        color: (ThemeManager.cardBackgroundColor)
                        border.color: ThemeManager.borderColor
                        border.width: 1

                        ColumnLayout {
                            id: resultLayout
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                ZAvatar {
                                    size: 48
                                    avatarUrl: root.foundProfile.avatar || ""
                                    name: root.foundProfile.name || ""
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: root.foundProfile.name || ""
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 15
                                    font.bold: true
                                    color: ThemeManager.textColor
                                    elide: Text.ElideRight
                                }
                            }

                            // Greeting message
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                radius: 8
                                color: (ThemeManager.surfaceColor)
                                border.color: greetingInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: greetingInput.activeFocus ? 2 : 1

                                TextInput {
                                    id: greetingInput
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    color: ThemeManager.textColor
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    verticalAlignment: TextInput.AlignVCenter
                                    selectByMouse: true
                                    clip: true

                                    Text {
                                        anchors.fill: parent
                                        verticalAlignment: Text.AlignVCenter
                                        text: I18n.t("Lời chào", "Greeting message")
                                        color: ThemeManager.secondaryTextColor
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        visible: !greetingInput.text && !greetingInput.activeFocus
                                    }
                                }
                            }

                            // Inline request result
                            Text {
                                visible: root.requestMessage.length > 0
                                Layout.fillWidth: true
                                text: root.requestMessage
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: root.requestOk ? Theme.onlineGreen : Theme.badgeRed
                                wrapMode: Text.WordWrap
                            }

                            // Action buttons (flush right)
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Item { Layout.fillWidth: true }

                                Rectangle {
                                    Layout.preferredWidth: msgLabel.implicitWidth + 28
                                    Layout.preferredHeight: 34
                                    radius: 8
                                    color: btnMsgMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor
                                    border.color: ThemeManager.borderColor
                                    border.width: 1

                                    Text {
                                        id: msgLabel
                                        anchors.centerIn: parent
                                        text: I18n.t("Nhắn tin", "Chat")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        color: ThemeManager.textColor
                                    }

                                    MouseArea {
                                        id: btnMsgMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            var p = root.foundProfile;
                                            ZaloClient.openConversationWithContact(p.uid, p.name || "", p.avatar || "");
                                            root.closeModal();
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: addLabel.implicitWidth + 28
                                    Layout.preferredHeight: 34
                                    radius: 8
                                    opacity: (root.requestPending || root.requestOk) ? 0.7 : 1.0
                                    color: btnAddMouse.containsMouse && btnAddMouse.enabled ? Theme.zaloBlueHover : Theme.zaloBlue

                                    Text {
                                        id: addLabel
                                        anchors.centerIn: parent
                                        text: root.requestPending ? I18n.t("Đang gửi...", "Sending...")
                                              : (root.requestOk ? I18n.t("Đã gửi lời mời", "Request sent")
                                                                : I18n.t("Kết bạn", "Add friend"))
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: "#FFFFFF"
                                    }

                                    MouseArea {
                                        id: btnAddMouse
                                        anchors.fill: parent
                                        enabled: !root.requestPending && !root.requestOk
                                        hoverEnabled: true
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            root.requestMessage = "";
                                            root.requestPending = true;
                                            ZaloClient.sendFriendRequest(root.foundProfile.uid, greetingInput.text);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
