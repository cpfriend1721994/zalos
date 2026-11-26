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

    function strField(obj: var, key: string): string {
        return obj ? String(obj[key] || "") : "";
    }

    function numField(obj: var, key: string): real {
        return obj ? Number(obj[key] || 0) : 0;
    }

    function boolField(obj: var, key: string): bool {
        return !!(obj && obj[key]);
    }

    function categoryName(code: int): string {
        switch (code) {
        case 1: return I18n.t("Bất động sản", "Real Estate");
        case 2: return I18n.t("Công nghệ & Thiết bị", "Technology & Devices");
        case 3: return I18n.t("Du lịch & Lưu trú", "Travel & Hospitality");
        case 4: return I18n.t("Giáo dục & Đào tạo", "Education & Training");
        case 5: return I18n.t("Mua sắm & Bán lẻ", "Shopping & Retail");
        case 6: return I18n.t("Mỹ phẩm & Làm đẹp", "Cosmetics & Beauty");
        case 7: return I18n.t("Nhà hàng & Quán", "Restaurant & Cafe");
        case 8: return I18n.t("Ô tô & Xe máy", "Auto & Motorbike");
        case 9: return I18n.t("Thời trang & May mặc", "Fashion & Apparel");
        case 10: return I18n.t("Thực phẩm & Đồ uống", "Food & Beverage");
        case 11: return I18n.t("Truyền thông & Giải trí", "Media & Entertainment");
        case 12: return I18n.t("Truyền thông nội bộ", "Internal Communications");
        case 13: return I18n.t("Vận tải", "Transportation");
        case 14: return I18n.t("Viễn thông", "Telecommunications");
        default: return I18n.t("Dịch vụ khác", "Other Services");
        }
    }

    function autoReplyScopeLabel(scope: int): string {
        switch (scope) {
        case 1: return I18n.t("Người lạ", "Strangers");
        case 2: return I18n.t("Bạn bè cụ thể", "Specific friends");
        case 3: return I18n.t("Bạn bè (trừ một số người)", "Friends except");
        default: return I18n.t("Mọi người", "Everyone");
        }
    }

    ZModalScrim {
        onDismissed: root.close()
    }

    Rectangle {
        id: card
        width: Math.min(parent.width - 32, 660)
        height: Math.min(parent.height - 40, 620)
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
                        color: ThemeManager.accentTonalColor
                        ZIcon {
                            anchors.centerIn: parent
                            name: "biz_tools"
                            size: 18
                            color: ThemeManager.accentTonalTextColor
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: I18n.t("Công cụ zBusiness", "zBusiness Tools")
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: ZaloClient.businessLoading
                                  ? I18n.t("Đang đồng bộ dữ liệu zBusiness...", "Syncing zBusiness data...")
                                  : I18n.t("Hồ sơ kinh doanh, danh mục sản phẩm và tin nhắn tự động",
                                           "Business profile, product catalog and auto reply")
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

                    function revealItem(item: Item): void {
                        if (contentWidth <= width) return;
                        const left = item.x;
                        const right = item.x + item.width;
                        if (left < contentX) contentX = left;
                        else if (right > contentX + width) contentX = Math.min(contentWidth - width, right - width);
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
                                readonly property string tabKey: root.strField(tabBtn.modelData, "key")
                                readonly property string tabVi: root.strField(tabBtn.modelData, "vi")
                                readonly property string tabEn: root.strField(tabBtn.modelData, "en")
                                readonly property string tabIcon: root.strField(tabBtn.modelData, "icon")
                                readonly property bool selected: ZaloClient.businessTab === tabBtn.tabKey

                                anchors.verticalCenter: parent.verticalCenter
                                height: 30
                                width: tabInner.implicitWidth + 20
                                radius: 15
                                onSelectedChanged: if (tabBtn.selected) tabFlick.revealItem(tabBtn)
                                color: tabBtn.selected
                                       ? ThemeManager.accentTonalColor
                                       : (tabBtnMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor)

                                RowLayout {
                                    id: tabInner
                                    anchors.centerIn: parent
                                    spacing: 6
                                    ZIcon {
                                        name: tabBtn.tabIcon
                                        size: 14
                                        color: tabBtn.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                                    }
                                    Text {
                                        text: I18n.t(tabBtn.tabVi, tabBtn.tabEn)
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        font.weight: tabBtn.selected ? Font.DemiBold : Font.Normal
                                        color: tabBtn.selected ? ThemeManager.accentTonalTextColor
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

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: ZaloClient.businessTab === "catalog" ? 1
                              : ZaloClient.businessTab === "autoreply" ? 2
                              : ZaloClient.businessTab === "package" ? 3 : 0

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
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: I18n.t("Thông tin cửa hàng / doanh nghiệp", "Store / Business Information")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: ThemeManager.textColor
                            }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Làm mới", "Refresh")
                                enabled: !ZaloClient.businessLoading
                                onClicked: ZaloClient.loadBusinessProfile()
                            }
                        }

                        component InfoBox: Rectangle {
                            id: infoBox
                            property string label: ""
                            property string value: ""
                            property string linkUrl: ""
                            Layout.fillWidth: true
                            implicitHeight: infoInner.implicitHeight + 16
                            radius: 8
                            color: ThemeManager.cardBackgroundColor
                            border.color: ThemeManager.borderColor
                            ColumnLayout {
                                id: infoInner
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 3
                                Text {
                                    Layout.fillWidth: true
                                    text: infoBox.label
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: ThemeManager.secondaryTextColor
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: infoBox.value.length > 0 ? infoBox.value : I18n.t("Chưa thiết lập", "Not set")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: infoBox.value.length > 0 ? Font.Medium : Font.Normal
                                    color: infoBox.linkUrl.length > 0 && infoBox.value.length > 0
                                           ? Theme.linkColor
                                           : (infoBox.value.length > 0 ? ThemeManager.textColor : ThemeManager.secondaryTextColor)
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: infoBox.linkUrl.length > 0 && infoBox.value.length > 0
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: ZaloClient.openExternalUrl(infoBox.linkUrl)
                                    }
                                }
                            }
                        }

                        InfoBox {
                            label: I18n.t("Ngành hàng kinh doanh", "Business Category")
                            value: ZaloClient.businessProfile
                                   ? root.categoryName(Math.round(root.numField(ZaloClient.businessProfile, "cate")))
                                   : ""
                        }
                        InfoBox {
                            label: I18n.t("Địa chỉ kinh doanh", "Business Address")
                            value: root.strField(ZaloClient.businessProfile, "addr")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            InfoBox {
                                label: I18n.t("Email liên hệ", "Contact Email")
                                value: root.strField(ZaloClient.businessProfile, "email")
                            }
                            InfoBox {
                                readonly property string webUrl: root.strField(ZaloClient.businessProfile, "website")
                                label: I18n.t("Website / Gian hàng", "Website / Store URL")
                                value: webUrl
                                linkUrl: webUrl
                            }
                        }
                        InfoBox {
                            label: I18n.t("Mô tả kinh doanh", "Business Description")
                            value: root.strField(ZaloClient.businessProfile, "desc")
                        }
                        InfoBox {
                            readonly property string slug: root.strField(ZaloClient.businessProfile, "ctaCatalog")
                            visible: slug.length > 0
                            label: I18n.t("Đường dẫn danh mục công khai", "Public Catalog URL")
                            value: slug.length > 0 ? ("https://catalog.zalo.me/" + slug) : ""
                            linkUrl: slug.length > 0 ? ("https://catalog.zalo.me/" + slug) : ""
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: I18n.t("Chỉnh sửa thông tin hồ sơ kinh doanh hiện chỉ thực hiện trên Zalo điện thoại/web (Zalo chưa cho phép cập nhật hồ sơ từ máy tính).",
                                         "Editing business profile fields is only supported on Zalo mobile/web (Zalo doesn't allow updating it from a computer yet).")
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            color: ThemeManager.secondaryTextColor
                        }
                    }
                }

                Item {
                    id: catalogTab
                    property string editingProductId: ""
                    property real editingCreateTime: 0
                    property bool catalogFormOpen: false
                    property bool productFormOpen: false

                    function startCreateProduct(): void {
                        catalogTab.editingProductId = "";
                        catalogTab.editingCreateTime = 0;
                        prodName.text = "";
                        prodPrice.text = "";
                        prodDesc.text = "";
                        catalogTab.productFormOpen = true;
                    }

                    function startEditProduct(p: var): void {
                        catalogTab.editingProductId = root.strField(p, "productId");
                        catalogTab.editingCreateTime = root.numField(p, "createTime");
                        prodName.text = root.strField(p, "name");
                        prodPrice.text = root.strField(p, "price");
                        prodDesc.text = root.strField(p, "description");
                        catalogTab.productFormOpen = true;
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Danh mục & Sản phẩm", "Catalogs & Products")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: ThemeManager.textColor
                                elide: Text.ElideRight
                            }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Làm mới", "Refresh")
                                enabled: !ZaloClient.businessLoading
                                onClicked: ZaloClient.loadBusinessCatalogs()
                            }
                            ZButton {
                                variant: "secondary"
                                text: "+ " + I18n.t("Danh mục", "Catalog")
                                onClicked: {
                                    catalogNameField.text = "";
                                    catalogTab.catalogFormOpen = !catalogTab.catalogFormOpen;
                                }
                            }
                            ZButton {
                                text: "+ " + I18n.t("Sản phẩm", "Product")
                                enabled: ZaloClient.selectedCatalogId.length > 0
                                onClicked: {
                                    if (catalogTab.productFormOpen && catalogTab.editingProductId === "") {
                                        catalogTab.productFormOpen = false;
                                    } else {
                                        catalogTab.startCreateProduct();
                                    }
                                }
                            }
                        }

                        Flickable {
                            visible: ZaloClient.businessCatalogs.length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            contentWidth: catStrip.width
                            contentHeight: height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            flickableDirection: Flickable.HorizontalFlick

                            Row {
                                id: catStrip
                                height: parent.height
                                spacing: 6
                                Repeater {
                                    model: ZaloClient.businessCatalogs
                                    Rectangle {
                                        id: catChip
                                        required property var modelData
                                        readonly property string cid: root.strField(catChip.modelData, "id")
                                        readonly property string cname: root.strField(catChip.modelData, "name")
                                        readonly property bool active: cid === ZaloClient.selectedCatalogId
                                        readonly property bool isDefault: root.boolField(catChip.modelData, "isDefault")
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: 28
                                        width: catChipRow.implicitWidth + 16
                                        radius: 14
                                        color: active ? ThemeManager.accentTonalColor : ThemeManager.cardBackgroundColor
                                        border.color: active ? Theme.zaloBlue : ThemeManager.borderColor
                                        RowLayout {
                                            id: catChipRow
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Text {
                                                text: catChip.cname
                                                textFormat: Text.PlainText
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                font.weight: catChip.active ? Font.DemiBold : Font.Normal
                                                color: catChip.active ? ThemeManager.accentTonalTextColor : ThemeManager.textColor
                                            }
                                            Rectangle {
                                                visible: !catChip.isDefault
                                                width: 14
                                                height: 14
                                                radius: 7
                                                color: catDelMouse.containsMouse ? Theme.badgeRed : ThemeManager.hoverClearColor
                                                ZIcon {
                                                    anchors.centerIn: parent
                                                    name: "close"
                                                    size: 9
                                                    color: catDelMouse.containsMouse ? "#FFFFFF" : ThemeManager.secondaryTextColor
                                                }
                                                MouseArea {
                                                    id: catDelMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: ZaloClient.deleteBusinessCatalog(catChip.cid)
                                                }
                                            }
                                        }
                                        MouseArea {
                                            z: -1
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: ZaloClient.selectBusinessCatalog(catChip.cid)
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: catalogTab.catalogFormOpen
                            Layout.fillWidth: true
                            height: catFormRow.implicitHeight + 16
                            radius: 8
                            color: ThemeManager.cardBackgroundColor
                            border.color: ThemeManager.borderColor
                            RowLayout {
                                id: catFormRow
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8
                                ZTextField {
                                    id: catalogNameField
                                    Layout.fillWidth: true
                                    placeholderText: I18n.t("Tên danh mục mới...", "New catalog name...")
                                }
                                ZButton {
                                    text: I18n.t("Tạo", "Create")
                                    enabled: catalogNameField.text.trim().length > 0
                                    onClicked: {
                                        ZaloClient.createBusinessCatalog(catalogNameField.text);
                                        catalogNameField.text = "";
                                        catalogTab.catalogFormOpen = false;
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: catalogTab.productFormOpen
                            Layout.fillWidth: true
                            height: formCol.implicitHeight + 20
                            radius: 8
                            color: ThemeManager.cardBackgroundColor
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
                                        Layout.preferredWidth: 150
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
                                        onClicked: catalogTab.productFormOpen = false
                                    }
                                    ZButton {
                                        text: catalogTab.editingProductId.length > 0
                                              ? I18n.t("Cập nhật", "Update")
                                              : I18n.t("Lưu sản phẩm", "Save Product")
                                        enabled: prodName.text.trim().length > 0 && prodPrice.text.trim().length > 0
                                        onClicked: {
                                            if (catalogTab.editingProductId.length > 0) {
                                                ZaloClient.updateBusinessProduct(
                                                    ZaloClient.selectedCatalogId,
                                                    catalogTab.editingProductId,
                                                    prodName.text,
                                                    prodPrice.text,
                                                    prodDesc.text,
                                                    catalogTab.editingCreateTime);
                                            } else {
                                                ZaloClient.createBusinessProduct(
                                                    ZaloClient.selectedCatalogId,
                                                    prodName.text,
                                                    prodPrice.text,
                                                    prodDesc.text);
                                            }
                                            catalogTab.productFormOpen = false;
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: prodList
                                anchors.fill: parent
                                clip: true
                                spacing: 8
                                model: ZaloClient.businessProducts

                                delegate: Rectangle {
                                    id: prodRow
                                    required property var modelData
                                    readonly property string pImg: root.strField(prodRow.modelData, "image")
                                    readonly property string pName: root.strField(prodRow.modelData, "name")
                                    readonly property string pDesc: root.strField(prodRow.modelData, "description")
                                    readonly property string pPrice: root.strField(prodRow.modelData, "price")
                                    readonly property string pCur: {
                                        const c = root.strField(prodRow.modelData, "currency");
                                        return c.length > 0 ? c : "₫";
                                    }
                                    readonly property string pCatId: {
                                        const c = root.strField(prodRow.modelData, "catalogId");
                                        return c.length > 0 ? c : ZaloClient.selectedCatalogId;
                                    }
                                    readonly property string pId: root.strField(prodRow.modelData, "productId")
                                    width: prodList.width
                                    height: 74
                                    radius: 8
                                    color: ThemeManager.surfaceColor
                                    border.color: ThemeManager.borderColor

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 10

                                        Rectangle {
                                            width: 52
                                            height: 52
                                            radius: 6
                                            color: ThemeManager.cardBackgroundColor
                                            clip: true
                                            ZRoundedImage {
                                                visible: prodRow.pImg.length > 0
                                                anchors.fill: parent
                                                radius: 6
                                                source: prodRow.pImg
                                            }
                                            ZIcon {
                                                visible: prodRow.pImg.length === 0
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
                                                Layout.fillWidth: true
                                                text: prodRow.pName
                                                textFormat: Text.PlainText
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                font.weight: Font.DemiBold
                                                color: ThemeManager.textColor
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                id: prodDescText
                                                Layout.fillWidth: true
                                                text: ZaloClient.linkifyText(prodRow.pDesc)
                                                textFormat: Text.StyledText
                                                linkColor: Theme.linkColor
                                                onLinkActivated: function(link) { ZaloClient.openLink(link); }
                                                HoverHandler { cursorShape: prodDescText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 11
                                                color: ThemeManager.secondaryTextColor
                                                elide: Text.ElideRight
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: prodRow.pPrice + (prodRow.pCur.length > 0 ? " " + prodRow.pCur : "")
                                                textFormat: Text.PlainText
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                                color: Theme.zaloBlue
                                                elide: Text.ElideRight
                                            }
                                        }

                                        ZButton {
                                            text: I18n.t("Gửi vào chat", "Send")
                                            onClicked: {
                                                const priceLabel = prodRow.pPrice + (prodRow.pCur.length > 0 ? " " + prodRow.pCur : "");
                                                ZaloClient.sendProductToCurrentChat(
                                                    prodRow.pName,
                                                    priceLabel,
                                                    prodRow.pDesc,
                                                    prodRow.pImg);
                                            }
                                        }
                                        ZButton {
                                            variant: "secondary"
                                            text: I18n.t("Sửa", "Edit")
                                            onClicked: catalogTab.startEditProduct(prodRow.modelData)
                                        }
                                        ZButton {
                                            variant: "danger"
                                            text: I18n.t("Xóa", "Delete")
                                            onClicked: ZaloClient.deleteBusinessProduct(prodRow.pCatId, prodRow.pId)
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 40, 380)
                                spacing: 8
                                visible: !ZaloClient.businessLoading && ZaloClient.businessProducts.length === 0

                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    width: 44
                                    height: 44
                                    radius: 22
                                    color: ThemeManager.cardBackgroundColor
                                    ZIcon {
                                        anchors.centerIn: parent
                                        name: "catalog"
                                        size: 22
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    text: ZaloClient.businessCatalogs.length === 0
                                          ? I18n.t("Chưa có danh mục sản phẩm", "No product catalogs yet")
                                          : I18n.t("Chưa có sản phẩm nào trong danh mục này", "No products in this catalog")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: ThemeManager.textColor
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    text: ZaloClient.businessCatalogs.length === 0
                                          ? I18n.t("Bấm \"+ Danh mục\" phía trên để tạo danh mục đầu tiên.",
                                                   "Click \"+ Catalog\" above to create your first catalog.")
                                          : I18n.t("Bấm \"+ Sản phẩm\" phía trên để thêm sản phẩm và đồng bộ lên Zalo.",
                                                   "Click \"+ Product\" above to add products synced to Zalo.")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                        }
                    }
                }

                Item {
                    id: autoReplyTab
                    property real editingId: 0
                    property real editingStartTime: 0
                    property real editingEndTime: 0
                    property bool formOpen: false

                    function startCreate(): void {
                        autoReplyTab.editingId = 0;
                        autoReplyTab.editingStartTime = 0;
                        autoReplyTab.editingEndTime = 0;
                        ruleSwitch.checked = true;
                        ruleContent.text = "";
                        ruleScopeCombo.currentIndex = 0;
                        autoReplyTab.formOpen = true;
                    }

                    function startEdit(r: var): void {
                        autoReplyTab.editingId = root.numField(r, "id");
                        autoReplyTab.editingStartTime = root.numField(r, "startTime");
                        autoReplyTab.editingEndTime = root.numField(r, "endTime");
                        ruleSwitch.checked = root.boolField(r, "enable");
                        ruleContent.text = root.strField(r, "content");
                        const sc = Math.round(root.numField(r, "scope"));
                        ruleScopeCombo.currentIndex = (sc >= 0 && sc <= 3) ? sc : 0;
                        autoReplyTab.formOpen = true;
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Tin nhắn trả lời tự động", "Auto Reply Rules")
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: ThemeManager.textColor
                                elide: Text.ElideRight
                            }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Làm mới", "Refresh")
                                enabled: !ZaloClient.businessLoading
                                onClicked: ZaloClient.loadBusinessAutoReplies()
                            }
                            ZButton {
                                text: "+ " + I18n.t("Thêm quy tắc", "Add Rule")
                                onClicked: {
                                    if (autoReplyTab.formOpen && autoReplyTab.editingId === 0) {
                                        autoReplyTab.formOpen = false;
                                    } else {
                                        autoReplyTab.startCreate();
                                    }
                                }
                            }
                        }

                        Rectangle {
                            visible: autoReplyTab.formOpen
                            Layout.fillWidth: true
                            height: autoFormCol.implicitHeight + 20
                            radius: 8
                            color: ThemeManager.cardBackgroundColor
                            border.color: ThemeManager.borderColor

                            ColumnLayout {
                                id: autoFormCol
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: I18n.t("Kích hoạt quy tắc", "Enable rule")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: ThemeManager.textColor
                                    }
                                    ZSwitch {
                                        id: ruleSwitch
                                        checked: true
                                    }
                                }
                                ZTextField {
                                    id: ruleContent
                                    Layout.fillWidth: true
                                    placeholderText: I18n.t("Nhập nội dung trả lời tự động *", "Enter auto reply content *")
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: I18n.t("Đối tượng:", "Scope:")
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                    ZComboBox {
                                        id: ruleScopeCombo
                                        Layout.fillWidth: true
                                        model: [
                                            I18n.t("Mọi người", "Everyone"),
                                            I18n.t("Người lạ", "Strangers"),
                                            I18n.t("Bạn bè cụ thể", "Specific friends"),
                                            I18n.t("Bạn bè (trừ một số người)", "Friends except")
                                        ]
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Item { Layout.fillWidth: true }
                                    ZButton {
                                        variant: "secondary"
                                        text: I18n.t("Đóng", "Close")
                                        onClicked: autoReplyTab.formOpen = false
                                    }
                                    ZButton {
                                        text: autoReplyTab.editingId > 0
                                              ? I18n.t("Cập nhật", "Update")
                                              : I18n.t("Lưu quy tắc", "Save Rule")
                                        enabled: ruleContent.text.trim().length > 0
                                        onClicked: {
                                            if (autoReplyTab.editingId > 0) {
                                                ZaloClient.updateBusinessAutoReply(
                                                    autoReplyTab.editingId,
                                                    ruleContent.text,
                                                    ruleSwitch.checked,
                                                    ruleScopeCombo.currentIndex,
                                                    autoReplyTab.editingStartTime,
                                                    autoReplyTab.editingEndTime);
                                            } else {
                                                ZaloClient.createBusinessAutoReply(
                                                    ruleContent.text,
                                                    ruleSwitch.checked,
                                                    ruleScopeCombo.currentIndex,
                                                    0, 0);
                                            }
                                            autoReplyTab.formOpen = false;
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ListView {
                                id: autoList
                                anchors.fill: parent
                                clip: true
                                spacing: 8
                                model: ZaloClient.businessAutoReplies

                                delegate: Rectangle {
                                    id: ruleRow
                                    required property var modelData
                                    readonly property real rId: root.numField(ruleRow.modelData, "id")
                                    readonly property bool rEnable: root.boolField(ruleRow.modelData, "enable")
                                    readonly property string rContent: root.strField(ruleRow.modelData, "content")
                                    readonly property int rScope: Math.round(root.numField(ruleRow.modelData, "scope"))
                                    readonly property real rStart: root.numField(ruleRow.modelData, "startTime")
                                    readonly property real rEnd: root.numField(ruleRow.modelData, "endTime")
                                    width: autoList.width
                                    height: ruleInner.implicitHeight + 20
                                    radius: 8
                                    color: ThemeManager.surfaceColor
                                    border.color: ThemeManager.borderColor

                                    RowLayout {
                                        id: ruleInner
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 10

                                        ZSwitch {
                                            checked: ruleRow.rEnable
                                            onToggled: function(val) {
                                                ZaloClient.updateBusinessAutoReply(
                                                    ruleRow.rId,
                                                    ruleRow.rContent,
                                                    val,
                                                    ruleRow.rScope,
                                                    ruleRow.rStart,
                                                    ruleRow.rEnd);
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 3
                                            Text {
                                                id: ruleContentText
                                                Layout.fillWidth: true
                                                text: ZaloClient.linkifyText(ruleRow.rContent)
                                                textFormat: Text.StyledText
                                                linkColor: Theme.linkColor
                                                onLinkActivated: function(link) { ZaloClient.openLink(link); }
                                                HoverHandler { cursorShape: ruleContentText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 3
                                                elide: Text.ElideRight
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                                color: ThemeManager.textColor
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: root.autoReplyScopeLabel(ruleRow.rScope)
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 11
                                                color: ThemeManager.secondaryTextColor
                                            }
                                        }

                                        ZButton {
                                            variant: "secondary"
                                            text: I18n.t("Sửa", "Edit")
                                            onClicked: autoReplyTab.startEdit(ruleRow.modelData)
                                        }
                                        ZButton {
                                            variant: "danger"
                                            text: I18n.t("Xóa", "Delete")
                                            onClicked: ZaloClient.deleteBusinessAutoReply(ruleRow.rId)
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 40, 380)
                                spacing: 8
                                visible: !ZaloClient.businessLoading && ZaloClient.businessAutoReplies.length === 0

                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    width: 44
                                    height: 44
                                    radius: 22
                                    color: ThemeManager.cardBackgroundColor
                                    ZIcon {
                                        anchors.centerIn: parent
                                        name: "autoreply"
                                        size: 22
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    text: I18n.t("Chưa có cấu hình trả lời tự động", "No auto-reply rules yet")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: ThemeManager.textColor
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    text: I18n.t("Bấm \"+ Thêm quy tắc\" phía trên để tạo câu trả lời tự động trên tài khoản zBusiness.",
                                                 "Click \"+ Add Rule\" above to configure server-synced auto replies.")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                        }
                    }
                }

                Flickable {
                    id: pkgTab
                    clip: true
                    contentWidth: width
                    contentHeight: pkgCol.implicitHeight + 40
                    boundsBehavior: Flickable.StopAtBounds

                    readonly property bool pkgActive: ZaloClient.hasBusinessPackage
                                                   || Math.round(root.numField(ZaloClient.businessProfile, "pkgId")) > 0

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
                                GradientStop { position: 0.0; color: pkgTab.pkgActive ? "#1A56DB" : "#374151" }
                                GradientStop { position: 1.0; color: pkgTab.pkgActive ? "#0084FF" : "#4B5563" }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 18
                                spacing: 14
                                Rectangle {
                                    width: 44
                                    height: 44
                                    radius: 22
                                    color: "#33FFFFFF"
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
                                        text: pkgTab.pkgActive
                                              ? (ZaloClient.businessPackageName.length > 0 ? ZaloClient.businessPackageName : I18n.t("Tài Khoản zBusiness Chuyên Nghiệp", "zBusiness Pro Account"))
                                              : I18n.t("Tài khoản Zalo Cá nhân (Tiêu chuẩn)", "Standard Zalo Account")
                                        textFormat: Text.PlainText
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        color: "#FFFFFF"
                                    }
                                    Text {
                                        text: pkgTab.pkgActive
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
                            font.weight: Font.DemiBold
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
                                    color: "#3333B266"
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
