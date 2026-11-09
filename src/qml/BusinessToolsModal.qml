pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    anchors.fill: parent

    function close(): void {
        ZaloClient.closeBusinessTools();
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.5
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Rectangle {
        id: card
        width: Math.min(parent.width - 32, 620)
        height: Math.min(parent.height - 40, 580)
        anchors.centerIn: parent
        color: ThemeManager.surfaceColor
        radius: 12
        border.color: ThemeManager.borderColor
        border.width: 1
        clip: true

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                height: 54
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 10

                    Rectangle {
                        width: 32
                        height: 32
                        radius: 16
                        color: ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.4, 0.9, 0.25) : Theme.zaloBlueLight
                        ZIcon {
                            anchors.centerIn: parent
                            name: "biz_tools"
                            size: 18
                            color: ThemeManager.isDarkMode ? "#45A3DD" : Theme.zaloBlueTonalText
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: I18n.t("Công cụ zBusiness", "zBusiness Tools")
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            font.bold: true
                            color: ThemeManager.textColor
                        }
                        Text {
                            text: I18n.t("Hồ sơ kinh doanh, danh mục sản phẩm và tin nhắn tự động", "Business profile, product catalog and auto reply")
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: closeMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                        ZIcon {
                            anchors.centerIn: parent
                            name: "close"
                            size: 16
                            color: ThemeManager.secondaryTextColor
                        }
                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.close()
                        }
                    }
                }
            }

            // Tab bar
            Rectangle {
                Layout.fillWidth: true
                height: 42
                color: ThemeManager.surfaceColor
                border.color: ThemeManager.borderColor
                border.width: 1

                Flickable {
                    id: tabFlick
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    contentWidth: tabRowStrip.width
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.HorizontalFlick
                    interactive: contentWidth > width

                    function revealItem(item) {
                        if (contentWidth <= width)
                            return;
                        const left = item.x;
                        const right = item.x + item.width;
                        if (left < contentX)
                            contentX = left;
                        else if (right > contentX + width)
                            contentX = Math.min(contentWidth - width, right - width);
                    }

                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(event) {
                            const limit = Math.max(0, tabFlick.contentWidth - tabFlick.width);
                            if (limit <= 0) return;
                            tabFlick.contentX = Math.max(0, Math.min(limit, tabFlick.contentX - event.angleDelta.x - event.angleDelta.y));
                        }
                    }

                    Row {
                        id: tabRowStrip
                        height: tabFlick.height
                        spacing: 12

                        Repeater {
                            model: [
                                { key: "profile", vi: "Hồ sơ kinh doanh", en: "Profile", icon: "biz_tools" },
                                { key: "catalog", vi: "Danh mục sản phẩm", en: "Catalog", icon: "catalog" },
                                { key: "autoreply", vi: "Trả lời tự động", en: "Auto Reply", icon: "autoreply" },
                                { key: "package", vi: "Gói zBusiness", en: "Package", icon: "briefcase" }
                            ]

                            Rectangle {
                                id: tabBtn
                                required property var modelData
                                readonly property string tabKey: tabBtn.modelData.key
                                readonly property bool selected: ZaloClient.businessTab === tabBtn.tabKey

                                anchors.verticalCenter: parent.verticalCenter
                                height: 30
                                width: tabInner.implicitWidth + 20
                                radius: 15
                                onSelectedChanged: if (tabBtn.selected) tabFlick.revealItem(tabBtn)
                                color: tabBtn.selected
                                       ? (ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.4, 0.9, 0.3) : Theme.zaloBlueLight)
                                       : (tabBtnMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)

                                RowLayout {
                                    id: tabInner
                                    anchors.centerIn: parent
                                    spacing: 6
                                    ZIcon {
                                        name: tabBtn.modelData.icon
                                        size: 14
                                        color: tabBtn.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                                    }
                                    Text {
                                        text: I18n.t(tabBtn.modelData.vi, tabBtn.modelData.en)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        font.bold: tabBtn.selected
                                        color: tabBtn.selected ? (ThemeManager.isDarkMode ? "#FFFFFF" : Theme.zaloBlueTonalText)
                                                               : ThemeManager.secondaryTextColor
                                    }
                                }

                                MouseArea {
                                    id: tabBtnMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: ZaloClient.businessTab = tabBtn.tabKey
                                }
                            }
                        }
                    }
                }
            }

            // Tab Content
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: ZaloClient.businessTab === "catalog" ? 1
                              : ZaloClient.businessTab === "autoreply" ? 2
                              : ZaloClient.businessTab === "package" ? 3 : 0

                // 1. Hồ sơ kinh doanh
                Flickable {
                    clip: true
                    contentWidth: width
                    contentHeight: profileCol.implicitHeight + 40
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: profileCol
                        width: parent.width - 40
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 20
                        spacing: 14

                        Text {
                            text: I18n.t("Thông tin cửa hàng / doanh nghiệp", "Store / Business Information")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: I18n.t("Ngành hàng kinh doanh", "Business Category")
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: ThemeManager.secondaryTextColor
                            }
                            ZComboBox {
                                Layout.fillWidth: true
                                model: [
                                    I18n.t("Mua sắm & Bán lẻ", "Shopping & Retail"),
                                    I18n.t("Công nghệ & Thiết bị", "Technology & Devices"),
                                    I18n.t("Nhà hàng & Quán cafe", "Restaurant & Cafe"),
                                    I18n.t("Thời trang & May mặc", "Fashion & Apparel"),
                                    I18n.t("Mỹ phẩm & Làm đẹp", "Cosmetics & Beauty"),
                                    I18n.t("Bất động sản", "Real Estate"),
                                    I18n.t("Dịch vụ khác", "Other Services")
                                ]
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: I18n.t("Địa chỉ kinh doanh", "Business Address")
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: ThemeManager.secondaryTextColor
                            }
                            ZTextField {
                                id: addrField
                                Layout.fillWidth: true
                                placeholderText: I18n.t("Nhập địa chỉ cửa hàng, văn phòng", "Enter address")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    text: I18n.t("Email liên hệ", "Contact Email")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: ThemeManager.secondaryTextColor
                                }
                                ZTextField {
                                    id: emailField
                                    Layout.fillWidth: true
                                    placeholderText: "contact@domain.com"
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    text: I18n.t("Website / Gian hàng", "Website / Store URL")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: ThemeManager.secondaryTextColor
                                }
                                ZTextField {
                                    id: webField
                                    Layout.fillWidth: true
                                    placeholderText: "https://..."
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: I18n.t("Mô tả kinh doanh", "Business Description")
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: ThemeManager.secondaryTextColor
                            }
                            ZTextField {
                                id: descField
                                Layout.fillWidth: true
                                placeholderText: I18n.t("Giới thiệu ngắn về sản phẩm, dịch vụ và chính sách ưu đãi", "Short intro about your products")
                            }
                        }

                        ZButton {
                            Layout.alignment: Qt.AlignRight
                            text: I18n.t("Lưu thông tin hồ sơ", "Save Business Profile")
                            onClicked: {
                                ZaloClient.closeBusinessTools();
                            }
                        }
                    }
                }

                // 2. Danh mục sản phẩm
                Item {
                    ColumnLayout {
                        id: catalogCol
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Sản phẩm & Dịch vụ của bạn", "Your Products & Services")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.bold: true
                                color: ThemeManager.textColor
                                elide: Text.ElideRight
                            }
                            ZButton {
                                variant: "secondary"
                                text: "+ " + I18n.t("Thêm sản phẩm", "Add Product")
                                onClicked: productDialog.visible = !productDialog.visible
                            }
                        }

                        // New Product Inline Form
                        Rectangle {
                            id: productDialog
                            visible: false
                            Layout.fillWidth: true
                            height: formCol.implicitHeight + 20
                            radius: 8
                            color: ThemeManager.hoverColor
                            border.color: ThemeManager.borderColor

                            ColumnLayout {
                                id: formCol
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    ZTextField {
                                        id: prodName
                                        Layout.fillWidth: true
                                        placeholderText: I18n.t("Tên sản phẩm *", "Product Name *")
                                    }
                                    ZTextField {
                                        id: prodPrice
                                        Layout.preferredWidth: 160
                                        placeholderText: I18n.t("Giá (VNĐ) *", "Price (VND) *")
                                    }
                                }
                                ZTextField {
                                    id: prodDesc
                                    Layout.fillWidth: true
                                    placeholderText: I18n.t("Mô tả sản phẩm, khuyến mãi...", "Description, discount...")
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Item { Layout.fillWidth: true }
                                    ZButton {
                                        variant: "secondary"
                                        text: I18n.t("Đóng", "Close")
                                        onClicked: productDialog.visible = false
                                    }
                                    ZButton {
                                        text: I18n.t("Lưu sản phẩm", "Save Product")
                                        enabled: prodName.text.trim().length > 0 && prodPrice.text.trim().length > 0
                                        onClicked: {
                                            productModel.append({
                                                name: prodName.text.trim(),
                                                price: prodPrice.text.trim() + " ₫",
                                                desc: prodDesc.text.trim(),
                                                image: ""
                                            });
                                            prodName.text = "";
                                            prodPrice.text = "";
                                            prodDesc.text = "";
                                            productDialog.visible = false;
                                        }
                                    }
                                }
                            }
                        }

                        ListView {
                            id: prodList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            model: ListModel {
                                id: productModel
                            }

                            Item {
                                anchors.centerIn: parent
                                width: parent.width - 40
                                height: 160
                                visible: productModel.count === 0

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        width: 44
                                        height: 44
                                        radius: 22
                                        color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.06) : ThemeManager.hoverColor

                                        ZIcon {
                                            anchors.centerIn: parent
                                            name: "catalog"
                                            size: 22
                                            color: ThemeManager.secondaryTextColor
                                        }
                                    }

                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: I18n.t("Chưa có sản phẩm nào trong danh mục", "No products in catalog")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: ThemeManager.textColor
                                    }

                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: I18n.t("Bấm nút \"Thêm sản phẩm\" ở góc trên để tạo sản phẩm hoặc dịch vụ.",
                                                     "Click \"Add Product\" above to create a product or service.")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                            }

                            delegate: Rectangle {
                                id: prodRow
                                required property var model
                                required property int index
                                width: prodList.width
                                height: 68
                                radius: 8
                                color: ThemeManager.surfaceColor
                                border.color: ThemeManager.borderColor

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10

                                    Rectangle {
                                        width: 48
                                        height: 48
                                        radius: 6
                                        color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.08) : ThemeManager.hoverColor
                                        ZIcon {
                                            anchors.centerIn: parent
                                            name: "catalog"
                                            size: 22
                                            color: Theme.zaloBlue
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: prodRow.model.name
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 13
                                            font.bold: true
                                            color: ThemeManager.textColor
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: prodRow.model.desc
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 11
                                            color: ThemeManager.secondaryTextColor
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: prodRow.model.price
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 12
                                            font.bold: true
                                            color: Theme.zaloBlue
                                        }
                                    }

                                    ZButton {
                                        text: I18n.t("Gửi vào chat", "Send to chat")
                                        onClicked: {
                                            ZaloClient.sendProductToCurrentChat(prodRow.model.name, prodRow.model.price, prodRow.model.desc, prodRow.model.image);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // 3. Trả lời tự động
                Flickable {
                    clip: true
                    contentWidth: width
                    contentHeight: autoCol.implicitHeight + 40
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: autoCol
                        width: parent.width - 40
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 20
                        spacing: 16

                        Rectangle {
                            Layout.fillWidth: true
                            height: 60
                            radius: 8
                            color: ThemeManager.hoverColor
                            border.color: ThemeManager.borderColor

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: I18n.t("Kích hoạt tin nhắn trả lời tự động", "Enable Auto Reply Message")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        text: I18n.t("Tự động gửi phản hồi khi khách hàng nhắn tin lúc bạn vắng mặt", "Automatically reply to new chats")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                ZSwitch {
                                    id: autoReplySwitch
                                    checked: false
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: I18n.t("Nội dung tin nhắn tự động:", "Auto Reply Content:")
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                font.bold: true
                                color: ThemeManager.textColor
                            }
                            ZTextField {
                                id: replyText
                                Layout.fillWidth: true
                                text: ""
                                placeholderText: I18n.t("Nhập nội dung tin nhắn tự động khi bạn vắng mặt...",
                                                        "Enter auto reply message when away...")
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: I18n.t("Thời gian áp dụng:", "Active Schedule:")
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                font.bold: true
                                color: ThemeManager.textColor
                            }
                            ZComboBox {
                                Layout.fillWidth: true
                                model: [
                                    I18n.t("Mọi lúc (luôn gửi khi có tin nhắn mới)", "Always active"),
                                    I18n.t("Ngoài giờ làm việc (sau 18h và cuối tuần)", "Outside business hours"),
                                    I18n.t("Khi chuyển trạng thái vắng mặt", "When status is away")
                                ]
                            }
                        }

                        ZButton {
                            Layout.alignment: Qt.AlignRight
                            text: I18n.t("Lưu cấu hình", "Save Configuration")
                            onClicked: {
                                root.close();
                            }
                        }
                    }
                }

                // 4. Gói zBusiness
                Flickable {
                    clip: true
                    contentWidth: width
                    contentHeight: pkgCol.implicitHeight + 40
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: pkgCol
                        width: parent.width - 40
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: 20
                        spacing: 16

                        Rectangle {
                            Layout.fillWidth: true
                            height: 100
                            radius: 12
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: ZaloClient.hasBusinessPackage ? "#1A56DB" : "#374151" }
                                GradientStop { position: 1.0; color: ZaloClient.hasBusinessPackage ? "#0084FF" : "#4B5563" }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 14
                                Rectangle {
                                    width: 44
                                    height: 44
                                    radius: 22
                                    color: Qt.rgba(1, 1, 1, 0.2)
                                    ZIcon {
                                        anchors.centerIn: parent
                                        name: "biz_tools"
                                        size: 24
                                        color: "#FFFFFF"
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text {
                                        text: ZaloClient.hasBusinessPackage
                                              ? (ZaloClient.businessPackageName.length > 0 ? ZaloClient.businessPackageName : I18n.t("Tài Khoản zBusiness Chuyên Nghiệp", "zBusiness Pro Account"))
                                              : I18n.t("Tài khoản Zalo Cá nhân (Tiêu chuẩn)", "Standard Zalo Account")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 16
                                        font.bold: true
                                        color: "#FFFFFF"
                                    }
                                    Text {
                                        text: ZaloClient.hasBusinessPackage
                                              ? I18n.t("Đang hoạt động • Bộ công cụ bán hàng & chăm sóc khách hàng tối ưu", "Active • Full sales and CRM toolkit")
                                              : I18n.t("Chưa đăng ký gói zBusiness • Nâng cấp để mở rộng tính năng kinh doanh", "No active zBusiness package • Upgrade for business tools")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: "#E0EFFF"
                                    }
                                }
                            }
                        }

                        Text {
                            text: I18n.t("Quyền lợi gói zBusiness:", "zBusiness Benefits:")
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.bold: true
                            color: ThemeManager.textColor
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            component BenefitRow: RowLayout {
                                property string label: ""
                                spacing: 10
                                Rectangle {
                                    width: 20
                                    height: 20
                                    radius: 10
                                    color: Qt.rgba(0.2, 0.7, 0.4, 0.2)
                                    ZIcon {
                                        anchors.centerIn: parent
                                        name: "check"
                                        size: 12
                                        color: "#2AC5BB"
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: label
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 12
                                    color: ThemeManager.textColor
                                }
                            }

                            BenefitRow { label: I18n.t("Huy hiệu xác thực zBusiness trên trang cá nhân & hội thoại", "Verified zBusiness badge on profile and chat") }
                            BenefitRow { label: I18n.t("Mở rộng danh bạ tối đa lên đến 5.000 bạn bè & khách hàng", "Expand contact list up to 5,000 friends & clients") }
                            BenefitRow { label: I18n.t("Tạo danh mục sản phẩm không giới hạn kèm nút chia sẻ báo giá nhanh", "Unlimited product catalog with quick quotation sharing") }
                            BenefitRow { label: I18n.t("Tin nhắn trả lời tự động và bộ phím tắt tin nhắn nhanh chuyên nghiệp", "Auto reply messaging and quick shortcut templates") }
                            BenefitRow { label: I18n.t("Dung lượng zCloud 100 GB lưu trữ dữ liệu an toàn & đồng bộ xuyên suốt", "100 GB zCloud secure storage and full sync") }
                        }
                    }
                }
            }
        }
    }
}
