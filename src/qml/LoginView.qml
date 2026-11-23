pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Templates as T
import Zalos

Rectangle {
    id: root
    objectName: "loginViewRoot"
    color: ThemeManager.chatBackgroundColor

    property int activeTab: 0
    property bool showPassword: false
    property bool rememberLogin: true
    property bool qrZoomOpen: false
    property bool loginNoticeVisible: false

    Component.onCompleted: ZaloClient.clearLoginError()

    Timer {
        id: loginNoticeTimer
        interval: 3000
        repeat: false
        onTriggered: {
            root.loginNoticeVisible = false;
            ZaloClient.clearLoginError();
        }
    }

    Connections {
        target: ZaloClient
        function onHasLoginErrorChanged(): void {
            if (ZaloClient.hasLoginError) {
                root.loginNoticeVisible = true;
                loginNoticeTimer.restart();
            } else if (!ZaloClient.isLoggingIn) {
                root.loginNoticeVisible = false;
                loginNoticeTimer.stop();
            }
        }
        function onLoginStatusMessageChanged(): void {
            if (ZaloClient.hasLoginError && ZaloClient.loginStatusMessage.length > 0) {
                root.loginNoticeVisible = true;
                loginNoticeTimer.restart();
            }
        }
        function onIsLoggingInChanged(): void {
            if (ZaloClient.isLoggingIn) {
                root.loginNoticeVisible = true;
                loginNoticeTimer.stop();
            } else if (!ZaloClient.hasLoginError) {
                root.loginNoticeVisible = false;
            }
        }
    }

    readonly property bool isWide: root.width >= 680
    readonly property int requiredContentHeight: Math.ceil(mainContainer.implicitHeight + 48)
    readonly property bool qrReady: ((ZaloClient.qrImageUrl.length > 0) || ZaloClient.qrSize > 0) && ZaloClient.qrSecondsRemaining > 0
    readonly property bool phoneFormValid: phoneInput.text.trim().length >= 8 && passInput.text.length >= 6

    onQrReadyChanged: if (!qrReady) qrZoomOpen = false
    onQrZoomOpenChanged: {
        if (qrZoomOpen) qrZoomPopup.open();
        else qrZoomPopup.close();
    }

    function switchTab(tab: int): void {
        root.activeTab = tab;
        ZaloClient.clearLoginError();
    }

    function paintQrMatrix(ctx, w: real, h: real): void {
        ctx.reset();
        var size = ZaloClient.qrSize;
        var matrix = ZaloClient.qrMatrix;
        if (!matrix || size <= 0 || matrix.length < size * size) {
            ctx.fillStyle = "#FFFFFF";
            ctx.fillRect(0, 0, w, h);
            return;
        }
        var quiet = 4;
        var totalModules = size + 2 * quiet;
        var cellSize = w / totalModules;
        ctx.fillStyle = "#FFFFFF";
        ctx.fillRect(0, 0, w, h);

        function isFinderEye(x, y) {
            if (x < 7 && y < 7) return true;
            if (x >= size - 7 && y < 7) return true;
            if (x < 7 && y >= size - 7) return true;
            return false;
        }
        function fillRoundedRect(x, y, rw, rh, r, color) {
            ctx.fillStyle = color;
            ctx.beginPath();
            ctx.moveTo(x + r, y);
            ctx.lineTo(x + rw - r, y);
            ctx.arcTo(x + rw, y, x + rw, y + r, r);
            ctx.lineTo(x + rw, y + rh - r);
            ctx.arcTo(x + rw, y + rh, x + rw - r, y + rh, r);
            ctx.lineTo(x + r, y + rh);
            ctx.arcTo(x, y + rh, x, y + rh - r, r);
            ctx.lineTo(x, y + r);
            ctx.arcTo(x, y, x + r, y, r);
            ctx.closePath();
            ctx.fill();
        }
        ctx.fillStyle = "#000000";
        for (var y = 0; y < size; y++) {
            for (var x = 0; x < size; x++) {
                if (!isFinderEye(x, y) && matrix[y * size + x]) {
                    ctx.fillRect(
                        Math.floor((x + quiet) * cellSize),
                        Math.floor((y + quiet) * cellSize),
                        Math.ceil(cellSize),
                        Math.ceil(cellSize)
                    );
                }
            }
        }
        function drawZaloEye(mx, my) {
            var ex = (mx + quiet) * cellSize;
            var ey = (my + quiet) * cellSize;
            var outerSize = 7 * cellSize;
            var middleOffset = 1 * cellSize;
            var middleSize = 5 * cellSize;
            var innerOffset = 2 * cellSize;
            var innerSize = 3 * cellSize;
            var radius = cellSize * 2.2;
            fillRoundedRect(ex, ey, outerSize, outerSize, radius, "#0068FF");
            fillRoundedRect(ex + middleOffset, ey + middleOffset, middleSize, middleSize, radius * 0.7, "#FFFFFF");
            fillRoundedRect(ex + innerOffset, ey + innerOffset, innerSize, innerSize, radius * 0.45, "#0068FF");
        }
        drawZaloEye(0, 0);
        drawZaloEye(size - 7, 0);
        drawZaloEye(0, size - 7);
    }

    component LinkText: Text {
        signal activated()
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.underline: linkMouse.containsMouse
        color: Theme.zaloBlue

        MouseArea {
            id: linkMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.activated()
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        implicitWidth: root.width
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            implicitWidth: root.width
            width: scroll.availableWidth
            implicitHeight: Math.max(root.height, mainContainer.implicitHeight + 48)
            height: implicitHeight

            ColumnLayout {
                id: mainContainer
                width: Math.max(0, Math.min(parent.width - 32, root.isWide ? 760 : 420))
                x: Math.floor((parent.width - width) / 2)
                y: Math.max(24, Math.floor((parent.height - implicitHeight) / 2))
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        text: "Zalos"
                        color: Theme.zaloBlue
                        font.family: Theme.fontFamily
                        font.weight: Font.DemiBold
                        font.pixelSize: 28
                    }

                    Item { Layout.fillWidth: true }

                    RowLayout {
                        spacing: 12

                        Text {
                            text: "Tiếng Việt"
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: (I18n.currentLanguage === "vi") ? Font.DemiBold : Font.Normal
                            color: I18n.currentLanguage === "vi" ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { ZaloClient.setAppLanguage("vi"); I18n.setLanguage("vi"); }
                            }
                        }

                        Text {
                            text: "English"
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: (I18n.currentLanguage === "en") ? Font.DemiBold : Font.Normal
                            color: I18n.currentLanguage === "en" ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { ZaloClient.setAppLanguage("en"); I18n.setLanguage("en"); }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: cardContent.implicitHeight + 40
                    radius: 16
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    ColumnLayout {
                        id: cardContent
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 20
                        spacing: 16

                        RowLayout {
                            visible: !root.isWide
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: 8
                                color: root.activeTab === 0 ? Theme.zaloBlue : ThemeManager.hoverColor

                                Text {
                                    anchors.centerIn: parent
                                    text: I18n.t("Mã QR", "QR Code")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: root.activeTab === 0 ? "#FFFFFF" : ThemeManager.textColor
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.switchTab(0)
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: 8
                                color: root.activeTab === 1 ? Theme.zaloBlue : ThemeManager.hoverColor

                                Text {
                                    anchors.centerIn: parent
                                    text: I18n.t("Số điện thoại", "Phone number")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    color: root.activeTab === 1 ? "#FFFFFF" : ThemeManager.textColor
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.switchTab(1)
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.isWide ? 3 : 1
                            columnSpacing: 24
                            rowSpacing: 16

                            // Layer 1: QR sign-in
                            ColumnLayout {
                                id: qrColumn
                                visible: root.isWide || root.activeTab === 0
                                Layout.fillWidth: true
                                Layout.preferredWidth: root.isWide ? 280 : -1
                                Layout.alignment: Qt.AlignTop
                                spacing: 12

                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: I18n.t("Đăng nhập qua mã QR", "Sign in with QR code")
                                    font.family: Theme.fontFamily
                                    font.weight: Font.DemiBold
                                    font.pixelSize: 15
                                    color: ThemeManager.textColor
                                }

                                ColumnLayout {
                                    visible: ZaloClient.qrLoginStep === 0
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: 224
                                        Layout.preferredHeight: qrCardColumn.implicitHeight + 20
                                        radius: 12
                                        color: "#FFFFFF"
                                        border.color: qrHoverArea.containsMouse ? Theme.zaloBlue : "#E2E8F0"
                                        border.width: qrHoverArea.containsMouse ? 2 : 1

                                        ColumnLayout {
                                            id: qrCardColumn
                                            anchors.top: parent.top
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.margins: 10
                                            spacing: 6

                                            Item {
                                                Layout.alignment: Qt.AlignHCenter
                                                Layout.preferredWidth: 204
                                                Layout.preferredHeight: 204

                                                Image {
                                                    id: qrServerImage
                                                    anchors.fill: parent
                                                    visible: ZaloClient.qrImageUrl.length > 0 && status === Image.Ready
                                                    source: ZaloClient.qrImageUrl
                                                    fillMode: Image.PreserveAspectFit
                                                    smooth: true
                                                    antialiasing: true
                                                }

                                                Canvas {
                                                    id: qrCanvas
                                                    anchors.fill: parent
                                                    visible: !qrServerImage.visible
                                                    antialiasing: true

                                                    Connections {
                                                        target: ZaloClient
                                                        function onQrMatrixChanged(): void { qrCanvas.requestPaint(); }
                                                        function onQrCodeChanged(): void { qrCanvas.requestPaint(); }
                                                    }

                                                    Component.onCompleted: requestPaint()
                                                    onPaint: root.paintQrMatrix(getContext("2d"), width, height)
                                                }

                                                Text {
                                                    anchors.centerIn: parent
                                                    width: parent.width - 24
                                                    visible: !qrServerImage.visible && ZaloClient.qrSize <= 0 && ZaloClient.qrSecondsRemaining > 0
                                                    horizontalAlignment: Text.AlignHCenter
                                                    wrapMode: Text.WordWrap
                                                    text: I18n.t("Đang tải mã QR...", "Loading QR code...")
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 12
                                                    color: "#64748B"
                                                }

                                                MouseArea {
                                                    id: qrHoverArea
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    acceptedButtons: root.qrReady ? Qt.LeftButton : Qt.NoButton
                                                    cursorShape: root.qrReady ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                    ToolTip.visible: containsMouse && ZaloClient.qrSecondsRemaining > 0
                                                    ToolTip.text: root.qrReady
                                                                  ? I18n.t("Bấm để phóng to mã QR", "Click to zoom QR code")
                                                                  : I18n.t("Mở Zalo trên điện thoại và quét mã này", "Open Zalo on your phone and scan this code")
                                                    onClicked: if (root.qrReady) root.qrZoomOpen = true
                                                }

                                                Rectangle {
                                                    anchors.fill: parent
                                                    visible: ZaloClient.qrSecondsRemaining <= 0
                                                    color: "#E5FFFFFF"
                                                    radius: 8

                                                    ColumnLayout {
                                                        anchors.centerIn: parent
                                                        width: parent.width - 24
                                                        spacing: 8

                                                        Text {
                                                            Layout.alignment: Qt.AlignHCenter
                                                            text: "⌛"
                                                            font.pixelSize: 28
                                                        }

                                                        Text {
                                                            Layout.fillWidth: true
                                                            horizontalAlignment: Text.AlignHCenter
                                                            wrapMode: Text.WordWrap
                                                            text: I18n.t("Mã QR đã hết hạn", "QR code expired")
                                                            font.family: Theme.fontFamily
                                                            font.pixelSize: 12
                                                            font.weight: Font.DemiBold
                                                            color: Theme.badgeRed
                                                        }

                                                        Rectangle {
                                                            Layout.alignment: Qt.AlignHCenter
                                                            Layout.preferredWidth: refreshLabel.implicitWidth + 28
                                                            Layout.preferredHeight: 30
                                                            radius: 6
                                                            color: refreshMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue

                                                            Text {
                                                                id: refreshLabel
                                                                anchors.centerIn: parent
                                                                text: I18n.t("Lấy mã mới", "Get new code")
                                                                color: "#FFFFFF"
                                                                font.family: Theme.fontFamily
                                                                font.pixelSize: 11
                                                                font.weight: Font.DemiBold
                                                            }

                                                            MouseArea {
                                                                id: refreshMouse
                                                                anchors.fill: parent
                                                                hoverEnabled: true
                                                                cursorShape: Qt.PointingHandCursor
                                                                onClicked: ZaloClient.refreshQrCode()
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.WordWrap
                                                text: I18n.t("Bấm để phóng to mã QR", "Click to zoom QR code")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: "#0068FF"

                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: root.qrReady ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                    onClicked: if (root.qrReady) root.qrZoomOpen = true
                                                }
                                            }
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WordWrap
                                        text: ZaloClient.qrSecondsRemaining > 0
                                              ? I18n.t("Mã có hiệu lực trong ", "Code valid for ") + ZaloClient.qrSecondsRemaining + I18n.t(" giây", " s")
                                              : I18n.t("Mã đã hết hạn, vui lòng bấm \"Lấy mã mới\"", "The code has expired. Please click \"Get new code\"")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        font.weight: (ZaloClient.qrSecondsRemaining <= 0) ? Font.DemiBold : Font.Normal
                                        color: ZaloClient.qrSecondsRemaining > 0 ? ThemeManager.secondaryTextColor : Theme.badgeRed
                                    }
                                }

                                ColumnLayout {
                                    visible: ZaloClient.qrLoginStep === 1
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: 224
                                        Layout.preferredHeight: scannedColumn.implicitHeight + 36
                                        radius: 12
                                        color: ThemeManager.surfaceColor
                                        border.color: Theme.zaloBlue
                                        border.width: 1

                                        ColumnLayout {
                                            id: scannedColumn
                                            anchors.centerIn: parent
                                            width: parent.width - 28
                                            spacing: 10

                                            ZAvatar {
                                                Layout.alignment: Qt.AlignHCenter
                                                Layout.preferredWidth: 64
                                                Layout.preferredHeight: 64
                                                size: 64
                                                avatarUrl: ZaloClient.scannedUserAvatar
                                                name: ZaloClient.scannedUserName
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                visible: ZaloClient.scannedUserName.length > 0
                                                horizontalAlignment: Text.AlignHCenter
                                                elide: Text.ElideRight
                                                text: ZaloClient.scannedUserName
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                color: ThemeManager.textColor
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.WordWrap
                                                text: I18n.t("Xác nhận đăng nhập trên điện thoại của bạn", "Confirm the login on your phone")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: ThemeManager.secondaryTextColor
                                            }
                                        }
                                    }

                                    LinkText {
                                        text: I18n.t("Quay lại", "Go back")
                                        onActivated: ZaloClient.cancelQrScan()
                                    }
                                }
                            }

                            Rectangle {
                                visible: root.isWide
                                Layout.preferredWidth: 1
                                Layout.fillHeight: true
                                color: ThemeManager.borderColor
                            }

                            // Layer 2: Browser session + Phone login directly visible
                            ColumnLayout {
                                id: directAuthColumn
                                visible: root.isWide || root.activeTab === 1
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: 14

                                Text {
                                    Layout.fillWidth: true
                                    text: I18n.t("Phương thức khác", "Other sign-in methods")
                                    font.family: Theme.fontFamily
                                    font.weight: Font.DemiBold
                                    font.pixelSize: 15
                                    color: ThemeManager.textColor
                                }

                                ZButton {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    variant: "tonal"
                                    text: I18n.t("Đăng nhập bằng phiên trình duyệt", "Sign in with browser session")
                                    onClicked: {
                                        ZaloClient.clearLoginError();
                                        ZaloClient.syncFromBrowserSession();
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: ThemeManager.borderColor
                                    }

                                    Text {
                                        text: I18n.t("hoặc số điện thoại", "or phone number")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeManager.secondaryTextColor
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 1
                                        color: ThemeManager.borderColor
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 42
                                        radius: 8
                                        color: ThemeManager.hoverColor
                                        border.color: phoneInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                                        border.width: 1

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12
                                            spacing: 8

                                            Text {
                                                text: "+84"
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                color: ThemeManager.textColor
                                                font.weight: Font.DemiBold
                                            }

                                            Rectangle {
                                                Layout.preferredWidth: 1
                                                Layout.preferredHeight: 18
                                                color: ThemeManager.borderColor
                                            }

                                            TextInput {
                                                id: phoneInput
                                                Layout.fillWidth: true
                                                text: ""
                                                color: ThemeManager.textColor
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                inputMethodHints: Qt.ImhDialableCharactersOnly
                                                clip: true
                                                Keys.onReturnPressed: passInput.forceActiveFocus()

                                                Text {
                                                    text: I18n.t("Số điện thoại", "Phone number")
                                                    color: ThemeManager.secondaryTextColor
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 13
                                                    visible: !phoneInput.text && !phoneInput.activeFocus
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 42
                                        radius: 8
                                        color: ThemeManager.hoverColor
                                        border.color: passInput.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                                        border.width: 1

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12
                                            spacing: 8

                                            TextInput {
                                                id: passInput
                                                Layout.fillWidth: true
                                                text: ""
                                                echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                                                color: ThemeManager.textColor
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                clip: true

                                                Text {
                                                    text: I18n.t("Mật khẩu", "Password")
                                                    color: ThemeManager.secondaryTextColor
                                                    font.family: Theme.fontFamily
                                                    font.pixelSize: 13
                                                    visible: !passInput.text && !passInput.activeFocus
                                                }

                                                Keys.onReturnPressed: btnSubmitPhone.clicked()
                                            }

                                            Item {
                                                Layout.preferredWidth: 24
                                                Layout.preferredHeight: 24

                                                ZIcon {
                                                    anchors.centerIn: parent
                                                    name: root.showPassword ? "eye" : "eye_off"
                                                    size: 16
                                                    color: ThemeManager.secondaryTextColor
                                                }

                                                MouseArea {
                                                    id: eyeMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.showPassword = !root.showPassword
                                                    ToolTip.visible: containsMouse
                                                    ToolTip.text: root.showPassword ? I18n.t("Ẩn mật khẩu", "Hide password") : I18n.t("Hiện mật khẩu", "Show password")
                                                }
                                            }
                                        }
                                    }

                                    ZCheckBox {
                                        Layout.fillWidth: true
                                        checked: root.rememberLogin
                                        text: I18n.t("Ghi nhớ đăng nhập trên thiết bị này", "Remember me on this device")
                                        onToggled: (val) => root.rememberLogin = val
                                    }

                                    ZButton {
                                        id: btnSubmitPhone
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 40
                                        variant: "primary"
                                        enabled: root.phoneFormValid && !ZaloClient.isLoggingIn
                                        text: ZaloClient.isLoggingIn
                                              ? I18n.t("Đang xác thực thông tin...", "Verifying...")
                                              : I18n.t("Đăng nhập với mật khẩu", "Sign in with password")
                                        onClicked: {
                                            if (!root.phoneFormValid || ZaloClient.isLoggingIn) {
                                                return;
                                            }
                                            ZaloClient.loginWithPhone(phoneInput.text.trim(), passInput.text, root.rememberLogin);
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

    Rectangle {
        visible: root.loginNoticeVisible && (ZaloClient.isLoggingIn || ZaloClient.hasLoginError)
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 20
        width: Math.min(parent.width - 32, Math.max(260, statusRow.implicitWidth + 28))
        height: statusRow.implicitHeight + 16
        radius: 10
        z: 50
        color: ZaloClient.hasLoginError ? ThemeManager.errorBackgroundColor : Theme.zaloBlueLight
        border.color: ZaloClient.hasLoginError ? Theme.badgeRed : Theme.zaloBlue
        border.width: 1

        RowLayout {
            id: statusRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8

            Text {
                Layout.alignment: Qt.AlignTop
                text: ZaloClient.hasLoginError ? "⚠️" : "🔄"
                font.pixelSize: 13
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: ZaloClient.loginStatusMessage.length > 0
                      ? ZaloClient.loginStatusMessage
                      : I18n.t("Đang xác thực tài khoản...", "Verifying your account...")
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: ZaloClient.hasLoginError ? Theme.badgeRed : Theme.zaloBlueTonalText
            }
        }
    }

    Popup {
        id: qrZoomPopup
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.qrZoomOpen = false
        onOpened: qrZoomCanvas.requestPaint()
        background: Rectangle {
            radius: 16
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1
        }
        contentItem: ColumnLayout {
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        Layout.fillWidth: true
                        text: I18n.t("Quét mã QR để đăng nhập", "Scan QR code to sign in")
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: ThemeManager.textColor
                    }

                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: qrCloseMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            font.pixelSize: 14
                            color: ThemeManager.secondaryTextColor
                        }

                        MouseArea {
                            id: qrCloseMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.qrZoomOpen = false
                        }
                    }
                }

                Rectangle {
                    readonly property int qrBoxSize: Math.max(240, Math.min(root.width - 80, root.height - 180, 380))
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: qrBoxSize + 24
                    Layout.preferredHeight: qrBoxSize + 24
                    radius: 14
                    color: "#FFFFFF"
                    border.color: "#E2E8F0"
                    border.width: 1

                    Image {
                        id: qrZoomServerImage
                        anchors.centerIn: parent
                        width: parent.width - 24
                        height: parent.height - 24
                        visible: ZaloClient.qrImageUrl.length > 0 && status === Image.Ready
                        source: ZaloClient.qrImageUrl
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        antialiasing: true
                    }

                    Canvas {
                        id: qrZoomCanvas
                        anchors.centerIn: parent
                        width: parent.width - 24
                        height: parent.height - 24
                        visible: !qrZoomServerImage.visible
                        antialiasing: true

                        Connections {
                            target: ZaloClient
                            function onQrMatrixChanged(): void { qrZoomCanvas.requestPaint(); }
                            function onQrCodeChanged(): void { qrZoomCanvas.requestPaint(); }
                        }

                        Component.onCompleted: requestPaint()
                        onPaint: root.paintQrMatrix(getContext("2d"), width, height)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: ZaloClient.qrSecondsRemaining > 0
                          ? I18n.t("Mã có hiệu lực trong ", "Code valid for ") + ZaloClient.qrSecondsRemaining + I18n.t(" giây", " s")
                          : I18n.t("Mã đã hết hạn", "Code expired")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    color: ZaloClient.qrSecondsRemaining > 0 ? ThemeManager.secondaryTextColor : Theme.badgeRed
                }
            }
    }
}
