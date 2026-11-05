import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Popup {
    id: root
    objectName: "hiddenPinPopup"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(380, parent ? parent.width - 32 : 380)
    modal: true
    padding: 22
    closePolicy: root.busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    property string flow: ""
    property string step: ""
    property string convId: ""
    property string convName: ""
    property string firstPin: ""
    property string error: ""
    property bool busy: false
    property string pendingOp: ""

    readonly property bool pinStep: root.step === "set" || root.step === "confirm" || root.step === "enterHide"
                                    || root.step === "enterOpen" || root.step === "enterUnhide"
                                    || root.step === "enterOld" || root.step === "setNew" || root.step === "confirmNew"

    function start(flowName: string, id: string, name: string, bypass: bool): void {
        root.flow = flowName;
        root.convId = id;
        root.convName = name;
        root.firstPin = "";
        root.error = "";
        root.busy = false;
        root.pendingOp = "";
        if (flowName === "hide") root.goTo(ZaloClient.hasHiddenPin ? "enterHide" : "intro");
        else if (flowName === "open") root.goTo("enterOpen");
        else if (flowName === "unhide") root.goTo(bypass ? "confirmUnhide" : "enterUnhide");
        else if (flowName === "setPin") root.goTo("set");
        else if (flowName === "changePin") root.goTo("enterOld");
        else if (flowName === "resetPin") root.goTo("resetWarn");
        else return;
        root.open();
    }

    function goTo(next: string): void {
        root.step = next;
        pinInput.text = "";
        if (root.pinStep) Qt.callLater(() => pinInput.forceActiveFocus());
    }

    function fail(message: string): void {
        root.error = message;
        pinInput.text = "";
        pinInput.forceActiveFocus();
    }

    function wrongPin(): void {
        root.fail(I18n.t("Mã PIN không đúng. Vui lòng thử lại.", "Wrong PIN. Please try again."));
    }

    function submit(pin: string): void {
        root.error = "";
        if (root.step === "set" || root.step === "setNew") {
            root.firstPin = pin;
            root.goTo(root.step === "set" ? "confirm" : "confirmNew");
        } else if (root.step === "confirm" || root.step === "confirmNew") {
            if (pin !== root.firstPin) {
                root.firstPin = "";
                root.goTo(root.step === "confirm" ? "set" : "setNew");
                root.wrongPin();
                return;
            }
            root.busy = true;
            root.pendingOp = root.flow === "hide" ? "hide" : "pin";
            ZaloClient.setHiddenPin(pin, root.flow === "hide" ? root.convId : "");
        } else if (!ZaloClient.verifyHiddenPin(pin)) {
            root.wrongPin();
        } else if (root.step === "enterHide") {
            root.busy = true;
            root.pendingOp = "hide";
            ZaloClient.hideConversation(root.convId, true);
        } else if (root.step === "enterOpen") {
            root.close();
            ZaloClient.openHiddenConversation(root.convId, "");
        } else if (root.step === "enterUnhide") {
            root.busy = true;
            root.pendingOp = "unhide";
            ZaloClient.hideConversation(root.convId, false);
        } else if (root.step === "enterOld") {
            root.goTo("setNew");
        }
    }

    function stepTitle(): string {
        switch (root.step) {
        case "intro": return I18n.t("Ẩn trò chuyện", "Hide conversation");
        case "set": return I18n.t("Đặt mã PIN", "Set a PIN");
        case "confirm": return I18n.t("Xác nhận lại mã PIN", "Confirm the PIN");
        case "enterHide": return I18n.t("Nhập mã PIN để ẩn trò chuyện", "Enter the PIN to hide this conversation");
        case "enterOpen": return I18n.t("Nhập mã PIN để mở trò chuyện", "Enter the PIN to open this conversation");
        case "enterUnhide": return I18n.t("Nhập mã PIN để bỏ ẩn trò chuyện", "Enter the PIN to unhide this conversation");
        case "confirmUnhide": return I18n.t("Bỏ ẩn trò chuyện", "Unhide conversation");
        case "success": return I18n.t("Ẩn trò chuyện thành công", "Conversation hidden");
        case "enterOld": return I18n.t("Nhập mã PIN cũ", "Enter your current PIN");
        case "setNew": return I18n.t("Nhập mã PIN mới", "Enter a new PIN");
        case "confirmNew": return I18n.t("Xác nhận lại mã PIN mới", "Confirm the new PIN");
        case "resetWarn": return I18n.t("Xóa mã PIN", "Remove PIN");
        }
        return "";
    }

    function stepBody(): string {
        switch (root.step) {
        case "intro":
            return I18n.t("Trò chuyện sẽ được ẩn khỏi danh sách tin nhắn. Chỉ có bạn mới có thể tìm thấy và mở lại chúng bằng cách sử dụng mã PIN.",
                          "The conversation will be hidden from your message list. Only you can find and reopen it with your PIN.");
        case "set":
        case "confirm":
            return I18n.t("Lưu ý: Ghi nhớ mã PIN để xem lại trò chuyện. Trò chuyện sẽ mất nếu bạn cài đặt lại mã PIN.",
                          "Note: remember your PIN to see the conversation again. Hidden conversations are lost if you reset the PIN.");
        case "confirmUnhide":
            return I18n.t("Bạn có muốn hiện lại trò chuyện %1 ?", "Show the conversation %1 again?").arg(root.convName);
        case "success":
            return I18n.t("Bạn đã ẩn trò chuyện %1 thành công. Để mở các trò chuyện đã ẩn, bạn có thể nhập mã PIN đã cài đặt hoặc tên trò chuyện ẩn tại khung tìm kiếm. Ngoài ra bạn có thể vào Cài đặt > Bảo mật > Thiết lập ẩn trò chuyện để thay đổi hoặc đặt lại mã PIN.",
                          "You hid %1. To open hidden conversations, type your PIN or the conversation's name in the search box. You can change or reset the PIN in Settings > Security > Hidden conversations.").arg(root.convName);
        case "resetWarn":
            return I18n.t("Khi đặt lại mã PIN, tất cả trò chuyện đang được ẩn sẽ bị xoá (bao gồm toàn bộ nội dung cuộc trò chuyện). Bạn có muốn tiếp tục?",
                          "Resetting the PIN deletes every hidden conversation, including all of its messages. Continue?");
        }
        return "";
    }

    Connections {
        target: ZaloClient
        function onHiddenOpFinished(op: string, ok: bool) {
            if (!root.opened || !root.busy) return;
            if (op === "pin" && root.pendingOp === "hide") {
                if (!ok) { root.busy = false; root.goTo("set"); }
                return;
            }
            if (op !== root.pendingOp) return;
            root.busy = false;
            if (op === "hide") {
                if (!ok) {
                    root.fail(I18n.t("Không thể ẩn trò chuyện. Vui lòng thử lại.", "Couldn't hide the conversation. Please try again."));
                } else if (root.step === "confirm") {
                    root.goTo("success");
                } else {
                    ZaloClient.showNotice(I18n.t("Trò chuyện \"%1\" đã được ẩn thành công", "\"%1\" is now hidden").arg(root.convName));
                    root.close();
                }
            } else if (op === "unhide") {
                ZaloClient.showNotice(ok ? I18n.t("Bỏ ẩn trò chuyện thành công", "Conversation unhidden")
                                         : I18n.t("Bỏ ẩn trò chuyện KHÔNG thành công", "Couldn't unhide the conversation"));
                root.close();
            } else if (op === "pin") {
                if (!ok) { root.goTo(root.flow === "changePin" ? "setNew" : "set"); return; }
                ZaloClient.showNotice(root.flow === "changePin" ? I18n.t("Đổi mã PIN thành công", "PIN changed")
                                                                : I18n.t("Cài đặt mã PIN thành công", "PIN set"));
                root.close();
            } else if (op === "reset") {
                root.close();
            }
        }
    }

    background: Rectangle {
        color: ThemeManager.surfaceColor
        radius: 12
        border.color: ThemeManager.borderColor
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            Layout.fillWidth: true
            text: root.stepTitle()
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: 16
            font.bold: true
            color: ThemeManager.textColor
        }

        Text {
            visible: text.length > 0
            Layout.fillWidth: true
            text: root.stepBody()
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: root.step === "resetWarn" ? Theme.badgeRed : ThemeManager.secondaryTextColor
        }

        Item {
            visible: root.pinStep
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 4 * 44 + 3 * 12
            Layout.preferredHeight: 52

            TextInput {
                id: pinInput
                anchors.fill: parent
                opacity: 0
                maximumLength: 4
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
                enabled: !root.busy
                onTextChanged: if (pinInput.text.length === 4) root.submit(pinInput.text)
            }

            Row {
                anchors.centerIn: parent
                spacing: 12
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        width: 44
                        height: 52
                        radius: 8
                        color: ThemeManager.inputFillColor
                        border.width: 1
                        border.color: root.error.length > 0 ? Theme.badgeRed
                                    : (pinInput.activeFocus && index === Math.min(pinInput.text.length, 3)) ? Theme.zaloBlue
                                    : ThemeManager.borderColor
                        Text {
                            anchors.centerIn: parent
                            text: index < pinInput.text.length ? "●" : ""
                            font.pixelSize: 18
                            color: ThemeManager.textColor
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.IBeamCursor
                onClicked: pinInput.forceActiveFocus()
            }
        }

        Text {
            visible: root.error.length > 0
            Layout.fillWidth: true
            text: root.error
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: Theme.fontFamily
            font.pixelSize: 12
            color: Theme.badgeRed
        }

        Text {
            visible: root.step === "enterOpen" || root.step === "enterUnhide"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.StyledText
            text: I18n.t("Quên mã PIN? Bạn phải <a href=\"reset\">cài đặt lại mã.</a>",
                         "Forgot your PIN? You have to <a href=\"reset\">reset it.</a>")
            linkColor: Theme.zaloBlue
            font.family: Theme.fontFamily
            font.pixelSize: 12
            color: ThemeManager.secondaryTextColor
            onLinkActivated: { root.close(); ZaloClient.openSettings("security"); }
        }

        BusyIndicator {
            visible: root.busy
            running: root.busy
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }

        RowLayout {
            visible: !root.pinStep || !root.busy
            Layout.fillWidth: true
            spacing: 10

            Item { Layout.fillWidth: true }

            ZButton {
                visible: root.step !== "success"
                variant: "secondary"
                text: I18n.t("Hủy", "Cancel")
                enabled: !root.busy
                onClicked: root.close()
            }
            ZButton {
                visible: root.step === "intro"
                text: I18n.t("Đặt mã PIN", "Set PIN")
                onClicked: root.goTo("set")
            }
            ZButton {
                visible: root.step === "confirmUnhide"
                text: I18n.t("Bỏ ẩn trò chuyện", "Unhide")
                enabled: !root.busy
                onClicked: {
                    root.busy = true;
                    root.pendingOp = "unhide";
                    ZaloClient.hideConversation(root.convId, false);
                }
            }
            ZButton {
                visible: root.step === "resetWarn"
                variant: "danger"
                text: I18n.t("Xóa mã PIN", "Remove PIN")
                enabled: !root.busy
                onClicked: {
                    root.busy = true;
                    root.pendingOp = "reset";
                    ZaloClient.resetHiddenPin();
                }
            }
            ZButton {
                visible: root.step === "success"
                text: I18n.t("Đóng", "Close")
                onClicked: root.close()
            }
        }
    }
}
