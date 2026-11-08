pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    objectName: "loginViewRoot"
    color: ThemeManager.isDarkMode ? "#121416" : "#E8F3FF"

    property int activeTab: 0
    property bool showPassword: false
    property bool rememberLogin: true

    readonly property bool phoneFormValid: phoneInput.text.trim().length >= 8 && passInput.text.length >= 6

    function switchTab(tab: int): void {
        root.activeTab = tab;
        ZaloClient.clearLoginError();
    }

    component PromoBanner: Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 64
        radius: 12
        color: (ThemeManager.cardBackgroundColor)
        border.color: ThemeManager.borderColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 36
                radius: 6
                color: (ThemeManager.dividerColor)

                Text {
                    anchors.centerIn: parent
                    text: "💻"
                    font.pixelSize: 20
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Nâng cao hiệu quả công việc với Zalo PC", "Work smarter with Zalo PC")
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    font.bold: true
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: I18n.t("Gửi file lớn lên đến 1 GB, chụp màn hình, gọi video", "Send files up to 1 GB, take screenshots, make video calls")
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    color: ThemeManager.secondaryTextColor
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                Layout.preferredWidth: Math.max(68, downloadLabel.implicitWidth + 20)
                Layout.preferredHeight: 30
                radius: 6
                color: downloadMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue

                Text {
                    id: downloadLabel
                    anchors.centerIn: parent
                    text: I18n.t("Tải ngay", "Download")
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.bold: true
                    color: "#FFFFFF"
                }

                MouseArea {
                    id: downloadMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally("https://zalo.me/pc")
                }
            }
        }
    }

    component LinkText: Text {
        signal activated()
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: 12
        font.bold: true
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
            implicitHeight: Math.max(root.height, mainContainer.implicitHeight + 40)
            height: implicitHeight

            ColumnLayout {
                id: mainContainer
                width: Math.max(0, Math.min(parent.width - 32, 420))
                x: Math.floor((parent.width - width) / 2)
                y: Math.max(20, Math.floor((parent.height - implicitHeight) / 2))
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "Zalo"
                        color: Theme.zaloBlue
                        font.family: Theme.fontFamily
                        font.bold: true
                        font.pixelSize: 38
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: I18n.t("Đăng nhập tài khoản Zalo", "Sign in to your Zalo account")
                        color: ThemeManager.textColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: I18n.t("để kết nối với ứng dụng Zalo Web", "to connect with Zalo Web")
                        color: ThemeManager.secondaryTextColor
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: cardContent.implicitHeight + 36
                    radius: 16
                    color: ThemeManager.surfaceColor
                    border.color: ThemeManager.borderColor
                    border.width: 1

                    ColumnLayout {
                        id: cardContent
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 18
                        spacing: 14

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            spacing: 8

                            Item {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                            }

                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: root.activeTab === 0
                                      ? I18n.t("Đăng nhập qua mã QR", "Sign in with QR code")
                                      : I18n.t("Đăng nhập với số điện thoại", "Sign in with phone number")
                                font.family: Theme.fontFamily
                                font.bold: true
                                font.pixelSize: 15
                                color: ThemeManager.textColor
                            }

                            Rectangle {
                                id: btnMenu
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                radius: width / 2
                                color: btnMenuMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor
                                border.color: ThemeManager.borderColor
                                border.width: 1

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3

                                    Rectangle { width: 14; height: 1.5; color: ThemeManager.textColor; radius: 1 }
                                    Rectangle { width: 14; height: 1.5; color: ThemeManager.textColor; radius: 1 }
                                    Rectangle { width: 14; height: 1.5; color: ThemeManager.textColor; radius: 1 }
                                }

                                MouseArea {
                                    id: btnMenuMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: menuPopup.open()
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: I18n.t("Tùy chọn đăng nhập", "Sign-in options")
                                }

                                Menu {
                                    id: menuPopup
                                    x: btnMenu.width - width
                                    y: btnMenu.height + 4

                                    MenuItem {
                                        text: I18n.t("Đăng nhập qua mã QR", "Sign in with QR code")
                                        onTriggered: root.switchTab(0)
                                    }
                                    MenuItem {
                                        text: I18n.t("Đăng nhập với số điện thoại", "Sign in with phone number")
                                        onTriggered: root.switchTab(1)
                                    }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: I18n.t("Đăng nhập bằng phiên trình duyệt", "Sign in with browser session")
                                        onTriggered: ZaloClient.syncFromBrowserSession()
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: ThemeManager.borderColor
                        }

                        StackLayout {
                            id: tabStack
                            currentIndex: root.activeTab
                            Layout.fillWidth: true
                            Layout.preferredHeight: root.activeTab === 0 ? tab0Layout.implicitHeight : tab1Layout.implicitHeight

                            ColumnLayout {
                                id: tab0Layout
                                Layout.fillWidth: true
                                spacing: 12

                                ColumnLayout {
                                    visible: ZaloClient.qrLoginStep === 0
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: visible ? implicitHeight : 0
                                    spacing: 12

                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: 236
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
                                                Layout.preferredWidth: 216
                                                Layout.preferredHeight: 216

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
                                                        function onQrMatrixChanged() { qrCanvas.requestPaint(); }
                                                        function onQrCodeChanged() { qrCanvas.requestPaint(); }
                                                    }

                                                    Component.onCompleted: requestPaint()

                                                    onPaint: {
                                                        var ctx = getContext("2d");
                                                        ctx.reset();

                                                        var size = ZaloClient.qrSize;
                                                        var matrix = ZaloClient.qrMatrix;

                                                        if (!matrix || size <= 0 || matrix.length < size * size) {
                                                            ctx.fillStyle = "#FFFFFF";
                                                            ctx.fillRect(0, 0, width, height);
                                                            return;
                                                        }

                                                        var quiet = 4;
                                                        var totalModules = size + 2 * quiet;
                                                        var cellSize = width / totalModules;

                                                        ctx.fillStyle = "#FFFFFF";
                                                        ctx.fillRect(0, 0, width, height);

                                                        function isFinderEye(x, y) {
                                                            if (x < 7 && y < 7) return true;
                                                            if (x >= size - 7 && y < 7) return true;
                                                            if (x < 7 && y >= size - 7) return true;
                                                            return false;
                                                        }

                                                        function fillRoundedRect(x, y, w, h, r, color) {
                                                            ctx.fillStyle = color;
                                                            ctx.beginPath();
                                                            ctx.moveTo(x + r, y);
                                                            ctx.lineTo(x + w - r, y);
                                                            ctx.arcTo(x + w, y, x + w, y + r, r);
                                                            ctx.lineTo(x + w, y + h - r);
                                                            ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
                                                            ctx.lineTo(x + r, y + h);
                                                            ctx.arcTo(x, y + h, x, y + h - r, r);
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
                                                    acceptedButtons: Qt.NoButton
                                                    ToolTip.visible: containsMouse && ZaloClient.qrSecondsRemaining > 0
                                                    ToolTip.text: I18n.t("Mở Zalo trên điện thoại và quét mã này", "Open Zalo on your phone and scan this code")
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
                                                            font.bold: true
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
                                                                font.bold: true
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
                                                text: I18n.t("Chỉ dùng để đăng nhập", "For sign-in only")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                font.bold: true
                                                color: "#0068FF"
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                horizontalAlignment: Text.AlignHCenter
                                                wrapMode: Text.WordWrap
                                                text: I18n.t("Zalo trên máy tính", "Zalo on desktop")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: "#334155"
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
                                        font.bold: ZaloClient.qrSecondsRemaining <= 0
                                        color: ZaloClient.qrSecondsRemaining > 0 ? ThemeManager.secondaryTextColor : Theme.badgeRed
                                    }

                                    Rectangle {
                                        visible: ZaloClient.isLoggingIn || (root.activeTab === 0 && ZaloClient.hasLoginError)
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: visible ? statusRow.implicitHeight + 16 : 0
                                        radius: 8
                                        color: ZaloClient.hasLoginError ? (ThemeManager.errorBackgroundColor) : (ThemeManager.isDarkMode ? "#1E3A5F" : Theme.zaloBlueLight)
                                        border.color: ZaloClient.hasLoginError ? Theme.badgeRed : Theme.zaloBlue
                                        border.width: 1

                                        RowLayout {
                                            id: statusRow
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12
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
                                                font.pixelSize: 11
                                                font.bold: true
                                                color: ZaloClient.hasLoginError ? Theme.badgeRed : Theme.zaloBlue
                                            }
                                        }
                                    }

                                    LinkText {
                                        text: I18n.t("Đăng nhập bằng phiên trình duyệt", "Sign in with browser session")
                                        onActivated: ZaloClient.syncFromBrowserSession()
                                    }
                                }

                                ColumnLayout {
                                    visible: ZaloClient.qrLoginStep === 1
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: visible ? implicitHeight : 0
                                    spacing: 14

                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: 236
                                        Layout.preferredHeight: scannedColumn.implicitHeight + 40
                                        radius: 12
                                        color: ThemeManager.surfaceColor
                                        border.color: Theme.zaloBlue
                                        border.width: 1

                                        ColumnLayout {
                                            id: scannedColumn
                                            anchors.centerIn: parent
                                            width: parent.width - 32
                                            spacing: 12

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
                                                font.bold: true
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

                                PromoBanner {}
                            }

                            ColumnLayout {
                                id: tab1Layout
                                Layout.fillWidth: true
                                spacing: 12

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 42
                                    radius: 8
                                    color: (ThemeManager.hoverColor)
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
                                            font.bold: true
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
                                    color: (ThemeManager.hoverColor)
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

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Rectangle {
                                        Layout.preferredWidth: 18
                                        Layout.preferredHeight: 18
                                        Layout.alignment: Qt.AlignTop
                                        radius: 4
                                        color: root.rememberLogin ? Theme.zaloBlue : "transparent"
                                        border.color: root.rememberLogin ? Theme.zaloBlue : ThemeManager.borderColor
                                        border.width: 1.5

                                        Text {
                                            anchors.centerIn: parent
                                            text: "✓"
                                            color: "#FFFFFF"
                                            font.pixelSize: 11
                                            font.bold: true
                                            visible: root.rememberLogin
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.rememberLogin = !root.rememberLogin
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: I18n.t("Ghi nhớ đăng nhập trên thiết bị này", "Remember me on this device")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.textColor

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.rememberLogin = !root.rememberLogin
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: root.activeTab === 1 && ZaloClient.hasLoginError
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: visible ? errorRow.implicitHeight + 16 : 0
                                    radius: 8
                                    color: (ThemeManager.errorBackgroundColor)
                                    border.color: Theme.badgeRed
                                    border.width: 1

                                    RowLayout {
                                        id: errorRow
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        spacing: 8

                                        Text {
                                            Layout.alignment: Qt.AlignTop
                                            text: "⚠️"
                                            font.pixelSize: 14
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: ZaloClient.loginStatusMessage.length > 0
                                                  ? ZaloClient.loginStatusMessage
                                                  : I18n.t("Đăng nhập thất bại. Vui lòng kiểm tra lại thông tin.", "Sign-in failed. Please check your details and try again.")
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 11
                                            font.bold: true
                                            color: Theme.badgeRed
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }

                                Rectangle {
                                    id: btnSubmitPhone
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 42
                                    radius: 8
                                    color: root.phoneFormValid && !ZaloClient.isLoggingIn
                                           ? (btnSubmitMouse.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue)
                                           : (ThemeManager.accentTonalColor)

                                    signal clicked()
                                    onClicked: {
                                        if (!root.phoneFormValid || ZaloClient.isLoggingIn) {
                                            return;
                                        }
                                        ZaloClient.loginWithPhone(phoneInput.text.trim(), passInput.text, root.rememberLogin);
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        width: parent.width - 24
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                        text: ZaloClient.isLoggingIn
                                              ? I18n.t("Đang xác thực thông tin...", "Verifying...")
                                              : I18n.t("Đăng nhập với mật khẩu", "Sign in with password")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: "#FFFFFF"
                                    }

                                    MouseArea {
                                        id: btnSubmitMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: root.phoneFormValid ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: btnSubmitPhone.clicked()
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.WordWrap
                                    text: I18n.t("Zalo Web hiện chỉ hỗ trợ đăng nhập bằng mã QR hoặc phiên trình duyệt.", "Zalo Web currently supports QR code or browser-session sign-in only.")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: ThemeManager.secondaryTextColor
                                }

                                PromoBanner {}

                                LinkText {
                                    text: I18n.t("Đăng nhập không dùng mật khẩu (quét mã QR)", "Sign in without a password (scan QR code)")
                                    onActivated: root.switchTab(0)
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 16

                    Text {
                        text: "Tiếng Việt"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.bold: I18n.currentLanguage === "vi"
                        color: I18n.currentLanguage === "vi" ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: I18n.setLanguage("vi")
                        }
                    }

                    Text {
                        text: "English"
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.bold: I18n.currentLanguage === "en"
                        color: I18n.currentLanguage === "en" ? Theme.zaloBlue : ThemeManager.secondaryTextColor

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: I18n.setLanguage("en")
                        }
                    }
                }
            }
        }
    }
}
