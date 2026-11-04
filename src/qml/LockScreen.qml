import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    color: ThemeManager.backgroundColor
    property bool failed: false

    function submit(): void {
        if (passField.text.length === 0) return;
        root.failed = !ZaloClient.appLock.unlock(passField.text);
        passField.text = "";
        if (root.failed) passField.forceActiveFocus();
    }

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (wheel) => wheel.accepted = true }
    Component.onCompleted: passField.forceActiveFocus()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        spacing: 14

        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/Zalos/assets/icons/zalos.svg"
            sourceSize.width: Math.ceil(72 * Screen.devicePixelRatio)
            sourceSize.height: Math.ceil(72 * Screen.devicePixelRatio)
            width: 72
            height: 72
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: I18n.t("Zalos đã khóa", "Zalos is locked")
            color: ThemeManager.textColor
            font.family: Theme.fontFamily
            font.pixelSize: 20
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: I18n.t("Nhập mã khóa để tiếp tục", "Enter your passcode to continue")
            color: ThemeManager.secondaryTextColor
            font.family: Theme.fontFamily
            font.pixelSize: 13
        }
        ZTextField {
            id: passField
            Layout.fillWidth: true
            echoMode: TextInput.Password
            enabled: ZaloClient.appLock.retryAfterSeconds === 0
            placeholderText: I18n.t("Mã khóa", "Passcode")
            font.family: Theme.fontFamily
            font.pixelSize: 15
            onAccepted: root.submit()
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: text.length > 0
            text: ZaloClient.appLock.retryAfterSeconds > 0
                  ? I18n.t("Sai quá nhiều lần. Thử lại sau ", "Too many attempts. Try again in ")
                    + ZaloClient.appLock.retryAfterSeconds + I18n.t(" giây", " seconds")
                  : (root.failed ? I18n.t("Mã khóa không đúng", "Wrong passcode") : "")
            color: Theme.badgeRed
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
        ZButton {
            Layout.fillWidth: true
            text: I18n.t("Mở khóa", "Unlock")
            enabled: passField.enabled && passField.text.length > 0
            onClicked: root.submit()
        }
    }
}
