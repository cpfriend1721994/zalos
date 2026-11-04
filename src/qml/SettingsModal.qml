import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Zalos

Item {
    id: root
    anchors.fill: parent
    visible: ZaloClient.showSettingsModal
    z: 998

    readonly property var systemSounds: ZaloClient.systemSounds()
    readonly property var tabKeys: ["general", "language", "appearance", "notifications", "privacy", "storage", "sync"]
    property string currentTab: normalizeTab(ZaloClient.settingsInitialTab)

    readonly property bool compact: dialogCard.width < 560

    function normalizeTab(key: string): string {
        return tabKeys.indexOf(key) >= 0 ? key : "general";
    }

    Component.onCompleted: visibleChanged()
    onVisibleChanged: {
        if (visible)
            currentTab = normalizeTab(ZaloClient.settingsInitialTab);
    }

    Connections {
        target: ZaloClient
        function onSettingsModalChanged() {
            if (ZaloClient.showSettingsModal)
                root.currentTab = root.normalizeTab(ZaloClient.settingsInitialTab);
        }
    }

    component SectionTitle: Text {
        Layout.fillWidth: true
        font.family: Theme.fontFamily
        font.bold: true
        font.pixelSize: 15
        color: ThemeManager.textColor
        wrapMode: Text.WordWrap
    }

    component SectionHint: Text {
        Layout.fillWidth: true
        font.family: Theme.fontFamily
        font.pixelSize: 12
        color: ThemeManager.secondaryTextColor
        wrapMode: Text.WordWrap
    }

    component ToggleRow: Rectangle {
        id: toggleRow
        property string title: ""
        property string subtitle: ""
        property bool checked: false
        signal toggled(bool value)

        Layout.fillWidth: true
        implicitHeight: Math.max(48, toggleLayout.implicitHeight + 20)
        radius: Theme.radiusMedium
        color: ThemeManager.cardBackgroundColor

        RowLayout {
            id: toggleLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: toggleRow.title
                    font.family: Theme.fontFamily
                    font.bold: toggleRow.subtitle.length > 0
                    font.pixelSize: 13
                    color: ThemeManager.textColor
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: toggleRow.subtitle.length > 0
                    text: toggleRow.subtitle
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    color: ThemeManager.secondaryTextColor
                    wrapMode: Text.WordWrap
                }
            }

            ZSwitch {
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                Layout.leftMargin: 12
                checked: toggleRow.checked
                onToggled: function(value) { toggleRow.toggled(value); }
            }
        }
    }

    component PreviewOption: ColumnLayout {
        id: previewOption
        property bool selected: false
        property string label: ""
        property color previewColor: "transparent"
        default property alias preview: previewHost.data
        signal chosen()

        Layout.fillWidth: true
        Layout.preferredWidth: 140
        Layout.maximumWidth: 180
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            radius: Theme.radiusMedium
            color: previewOption.previewColor
            border.color: previewOption.selected ? Theme.zaloBlue : ThemeManager.borderColor
            border.width: previewOption.selected ? 2 : 1
            clip: true

            Item {
                id: previewHost
                anchors.fill: parent
                anchors.margins: 2
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: previewOption.chosen()
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: previewOption.width
            spacing: 4

            ZRadio {
                checked: previewOption.selected
                onClicked: previewOption.chosen()
            }

            Text {
                Layout.fillWidth: true
                text: previewOption.label
                font.family: Theme.fontFamily
                font.pixelSize: 12
                color: ThemeManager.textColor
                elide: Text.ElideRight
            }
        }
    }

    component LanguageOption: Rectangle {
        id: langOption
        property string code: "vi"
        property string label: ""
        readonly property bool selected: I18n.currentLanguage === code

        Layout.fillWidth: true
        implicitHeight: 48
        radius: Theme.radiusMedium
        color: ThemeManager.cardBackgroundColor
        border.color: selected ? Theme.zaloBlue : "transparent"
        border.width: selected ? 2 : 0

        function choose(): void {
            ZaloClient.setAppLanguage(code);
            I18n.setLanguage(code);
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 16
            spacing: 6

            ZRadio {
                checked: langOption.selected
                onClicked: langOption.choose()
            }

            Text {
                Layout.fillWidth: true
                text: langOption.label
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.bold: langOption.selected
                color: ThemeManager.textColor
                elide: Text.ElideRight
            }

            ZIcon {
                visible: langOption.selected
                name: "check"
                size: 16
                color: Theme.zaloBlue
            }
        }

        MouseArea {
            anchors.fill: parent
            z: -1
            cursorShape: Qt.PointingHandCursor
            onClicked: langOption.choose()
        }
    }

    component SoundPicker: Rectangle {
        id: soundPicker
        property string title: ""
        property string subtitle: ""
        property string soundType: ""
        property var options: []
        property string selectedKey: ""
        signal picked(string key)

        readonly property bool previewing: ZaloClient.isPreviewPlaying
                                           && ZaloClient.currentPreviewSound === selectedKey

        function indexOfKey(key: string): int {
            for (var i = 0; i < options.length; ++i)
                if (options[i].key === key)
                    return i;
            return 0;
        }

        Layout.fillWidth: true
        implicitHeight: soundLayout.implicitHeight + 24
        radius: Theme.radiusMedium
        color: ThemeManager.cardBackgroundColor

        ColumnLayout {
            id: soundLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            anchors.leftMargin: 16
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: soundPicker.title
                font.family: Theme.fontFamily
                font.bold: true
                font.pixelSize: 13
                color: ThemeManager.textColor
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: soundPicker.subtitle.length > 0
                text: soundPicker.subtitle
                font.family: Theme.fontFamily
                font.pixelSize: 11
                color: ThemeManager.secondaryTextColor
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ComboBox {
                    id: soundCombo
                    Layout.fillWidth: true
                    Layout.maximumWidth: 280
                    Layout.preferredHeight: 34
                    model: soundPicker.options
                    textRole: "label"
                    currentIndex: soundPicker.indexOfKey(soundPicker.selectedKey)
                    onActivated: (index) => soundPicker.picked(soundPicker.options[index].key)
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    background: Rectangle {
                        radius: 6
                        color: ThemeManager.inputFillColor
                        border.color: soundCombo.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                        border.width: 1
                    }
                    contentItem: Text {
                        leftPadding: 10
                        rightPadding: 24
                        text: soundCombo.displayText
                        font: soundCombo.font
                        color: ThemeManager.textColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    indicator: ZIcon {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        name: "arrow_down"
                        size: 14
                        color: ThemeManager.secondaryTextColor
                    }
                    popup: Popup {
                        y: soundCombo.height + 4
                        width: soundCombo.width
                        padding: 4
                        contentItem: ListView {
                            clip: true
                            implicitHeight: Math.min(200, contentHeight)
                            model: soundCombo.popup.visible ? soundCombo.delegateModel : null
                            currentIndex: soundCombo.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator { }
                        }
                        background: Rectangle {
                            radius: 6
                            color: ThemeManager.surfaceColor
                            border.color: ThemeManager.borderColor
                            border.width: 1
                        }
                    }
                    delegate: ItemDelegate {
                        width: soundCombo.width - 8
                        height: 32
                        contentItem: Text {
                            text: modelData.label
                            color: ThemeManager.textColor
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: highlighted ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: previewBtn
                    Layout.alignment: Qt.AlignRight
                    Layout.preferredHeight: 34
                    Layout.preferredWidth: 90
                    background: Rectangle {
                        radius: 6
                        color: soundPicker.previewing ? Theme.badgeRed
                               : previewBtn.pressed ? Theme.zaloBlueHover
                               : previewBtn.hovered ? Theme.zaloBlueHover : Theme.zaloBlue
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }
                    contentItem: Text {
                        text: soundPicker.previewing ? I18n.t("Dừng", "Stop") : I18n.t("Nghe thử", "Preview")
                        color: "#FFFFFF"
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (ZaloClient.isPreviewPlaying)
                            ZaloClient.stopSoundPreview();
                        else
                            ZaloClient.playSoundPreview(soundPicker.soundType, soundPicker.selectedKey);
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.visible ? 0.55 : 0.0
        Behavior on opacity { NumberAnimation { duration: 160 } }

        MouseArea {
            anchors.fill: parent
            onClicked: ZaloClient.closeSettings()
        }
    }

    Rectangle {
        id: dialogCard
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 32, 820))
        height: Math.max(0, Math.min(parent.height - 32,
                                     Math.max(520, 53 + contentContainer.implicitHeight + 8)))
        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        scale: root.visible ? 1.0 : 0.94
        opacity: root.visible ? 1.0 : 0.0
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
        radius: Theme.radiusLarge
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1
        clip: true

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: (wheel) => wheel.accepted = true
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 52

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.right: closeButton.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: I18n.t("Cài đặt", "Settings")
                    font.family: Theme.fontFamily
                    font.bold: true
                    font.pixelSize: 17
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Rectangle {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    radius: 16
                    color: closeMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor

                    ZIcon {
                        anchors.centerIn: parent
                        name: "close"
                        size: 14
                        color: ThemeManager.secondaryTextColor
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: ZaloClient.closeSettings()
                    }

                    ToolTip.visible: closeMouse.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: I18n.t("Đóng", "Close")
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: ThemeManager.borderColor
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: root.compact ? 56 : 196
                    color: (ThemeManager.cardBackgroundColor)

                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 1
                        color: ThemeManager.borderColor
                    }

                    Flickable {
                        anchors.fill: parent
                        anchors.topMargin: 12
                        anchors.bottomMargin: 12
                        anchors.leftMargin: 8
                        anchors.rightMargin: 9
                        contentHeight: tabColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        ColumnLayout {
                            id: tabColumn
                            width: parent.width
                            spacing: 2

                            Repeater {
                                model: [
                                    { key: "general", label: I18n.t("Cài đặt chung", "General"), icon: "settings" },
                                    { key: "language", label: I18n.t("Ngôn ngữ", "Language"), icon: "globe" },
                                    { key: "appearance", label: I18n.t("Giao diện", "Appearance"), icon: "palette" },
                                    { key: "notifications", label: I18n.t("Thông báo", "Notifications"), icon: "bell" },
                                    { key: "privacy", label: I18n.t("Quyền riêng tư", "Privacy"), icon: "lock" },
                                    { key: "storage", label: I18n.t("Dữ liệu & bộ nhớ", "Data & storage"), icon: "download" },
                                    { key: "sync", label: I18n.t("Đồng bộ tin nhắn", "Sync messages"), icon: "refresh" }
                                ]

                                Rectangle {
                                    id: tabItem
                                    required property var modelData
                                    readonly property bool active: root.currentTab === modelData.key

                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 40
                                    radius: 8
                                    color: active ? (ThemeManager.selectedColor)
                                                  : (tabMouse.containsMouse ? (ThemeManager.hoverColor) : ThemeManager.hoverClearColor)

                                    ZIcon {
                                        id: tabIcon
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: root.compact ? undefined : parent.left
                                        anchors.leftMargin: 12
                                        anchors.horizontalCenter: root.compact ? parent.horizontalCenter : undefined
                                        name: tabItem.modelData.icon
                                        size: 18
                                        color: tabItem.active ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                                    }

                                    Text {
                                        visible: !root.compact
                                        anchors.left: tabIcon.right
                                        anchors.leftMargin: 10
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: tabItem.modelData.label
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.bold: tabItem.active
                                        color: tabItem.active ? Theme.zaloBlue : ThemeManager.textColor
                                        elide: Text.ElideRight
                                    }

                                    MouseArea {
                                        id: tabMouse
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        hoverEnabled: true
                                        onClicked: root.currentTab = tabItem.modelData.key
                                    }

                                    ToolTip.visible: root.compact && tabMouse.containsMouse
                                    ToolTip.delay: 400
                                    ToolTip.text: tabItem.modelData.label
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    id: settingsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    Item {
                        id: contentContainer
                        width: settingsScroll.availableWidth
                        implicitHeight: activeTabHeight() + 2 * contentPadding

                        readonly property int contentPadding: root.compact ? 14 : 20

                        function activeTabHeight(): real {
                            switch (root.currentTab) {
                            case "language": return languageTab.implicitHeight;
                            case "appearance": return appearanceTab.implicitHeight;
                            case "notifications": return notificationsTab.implicitHeight;
                            case "privacy": return privacyTab.implicitHeight;
                            case "storage": return storageTab.implicitHeight;
                            case "sync": return syncTab.implicitHeight;
                            default: return generalTab.implicitHeight;
                            }
                        }

                        ColumnLayout {
                            id: generalTab
                            visible: root.currentTab === "general"
                            x: contentContainer.contentPadding
                            y: contentContainer.contentPadding
                            width: contentContainer.width - 2 * contentContainer.contentPadding
                            spacing: 14

                            SectionTitle { text: I18n.t("Cài đặt chung", "General settings") }

                            ToggleRow {
                                title: I18n.t("Khởi động cùng hệ thống", "Start with the system")
                                subtitle: I18n.t("Tự mở Zalos khi bạn đăng nhập vào máy tính",
                                                 "Open Zalos automatically when you sign in to the computer")
                                checked: ZaloClient.startWithSystem
                                onToggled: (value) => ZaloClient.startWithSystem = value
                            }

                            ToggleRow {
                                visible: ZaloClient.trayAvailable
                                title: I18n.t("Khởi động thu nhỏ vào khay hệ thống", "Start minimized to the system tray")
                                subtitle: I18n.t("Khi khởi động cùng hệ thống, Zalos chạy nền ở khay hệ thống, không hiện cửa sổ",
                                                 "When started with the system, Zalos runs in the tray without showing its window")
                                checked: ZaloClient.startHiddenInTray
                                onToggled: (value) => ZaloClient.startHiddenInTray = value
                            }

                            ToggleRow {
                                visible: ZaloClient.trayAvailable
                                title: I18n.t("Đóng cửa sổ thì thu nhỏ vào khay", "Closing the window keeps it in the tray")
                                subtitle: I18n.t("Nút đóng chỉ ẩn cửa sổ, Zalos vẫn nhận tin nhắn; thoát hẳn bằng Ctrl+Q hoặc menu khay hệ thống",
                                                 "The close button only hides the window and Zalos keeps receiving messages; quit with Ctrl+Q or the tray menu")
                                checked: ZaloClient.closeToTray
                                onToggled: (value) => ZaloClient.closeToTray = value
                            }

                            Text {
                                visible: !ZaloClient.trayAvailable
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: I18n.t("Môi trường desktop này không có khay hệ thống nên không thể thu nhỏ Zalos vào khay.",
                                             "This desktop has no system tray, so Zalos can't be minimized to it.")
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: ThemeManager.secondaryTextColor
                            }

                            SectionTitle { text: I18n.t("Cuộc gọi", "Calls") }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: callRow.implicitHeight + 20
                                radius: Theme.radiusMedium
                                color: ThemeManager.inputFillColor
                                RowLayout {
                                    id: callRow
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: I18n.t("Bộ gọi zcall-native", "zcall-native call engine")
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 13
                                            color: ThemeManager.textColor
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideMiddle
                                            text: ZaloClient.calls.engineIsBundled ? I18n.t("Bản đi kèm Zalos", "Bundled with Zalos")
                                                  : ZaloClient.calls.engineDirectory.length > 0 ? ZaloClient.calls.engineDirectory
                                                  : I18n.t("Không tìm thấy bộ gọi — hãy cài lại Zalos", "Call engine missing — reinstall Zalos")
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 12
                                            color: ZaloClient.calls.engineDirectory.length > 0 ? ThemeManager.secondaryTextColor : Theme.badgeRed
                                        }
                                    }
                                    ZButton {
                                        visible: !ZaloClient.calls.engineIsBundled && ZaloClient.calls.engineDirectory.length > 0
                                        variant: "secondary"
                                        text: I18n.t("Dùng bản đi kèm", "Use bundled")
                                        onClicked: ZaloClient.calls.setEngineDirectory("")
                                    }
                                    ZButton {
                                        variant: "secondary"
                                        text: I18n.t("Dùng bản khác…", "Use another…")
                                        onClicked: engineFolderDialog.open()
                                    }
                                }
                            }
                            FolderDialog {
                                id: engineFolderDialog
                                title: I18n.t("Chọn thư mục zalo-for-linux hoặc zcall-native", "Choose the zalo-for-linux or zcall-native folder")
                                onAccepted: {
                                    if (!ZaloClient.calls.setEngineDirectory(selectedFolder.toString()))
                                        ZaloClient.showNotice(I18n.t("Thư mục này không có native-engine/zcall-native.js",
                                                                     "This folder has no native-engine/zcall-native.js"));
                                }
                            }
                        }

                        ColumnLayout {
                            id: languageTab
                            visible: root.currentTab === "language"
                            x: contentContainer.contentPadding
                            y: contentContainer.contentPadding
                            width: contentContainer.width - 2 * contentContainer.contentPadding
                            spacing: 14

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                SectionTitle { text: I18n.t("Ngôn ngữ", "Language") }
                                SectionHint {
                                    text: I18n.t("Chọn ngôn ngữ hiển thị cho ứng dụng",
                                                 "Choose the display language for the app")
                                }
                            }

                            LanguageOption { code: "vi"; label: "Tiếng Việt" }
                            LanguageOption { code: "en"; label: "English" }
                        }

                        ColumnLayout {
                            id: appearanceTab
                            visible: root.currentTab === "appearance"
                            x: contentContainer.contentPadding
                            y: contentContainer.contentPadding
                            width: contentContainer.width - 2 * contentContainer.contentPadding
                            spacing: 18

                            SectionTitle { text: I18n.t("Giao diện", "Appearance") }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                PreviewOption {
                                    label: I18n.t("Zalo Sáng", "Zalo Light")
                                    selected: ThemeManager.themeMode === 0
                                    previewColor: "#FFFFFF"
                                    onChosen: ThemeManager.setThemeMode(0)

                                    Rectangle {
                                        x: 4; y: 4
                                        width: 22
                                        height: parent.height - 8
                                        radius: 4
                                        color: "#005AE0"

                                        Rectangle {
                                            width: 10; height: 10; radius: 5
                                            color: "#FFFFFF"
                                            anchors.top: parent.top
                                            anchors.topMargin: 4
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                    }

                                    Rectangle {
                                        x: 30; y: 4
                                        width: parent.width - 34
                                        height: parent.height - 8
                                        radius: 4
                                        color: "#EBECF0"

                                        Rectangle {
                                            width: Math.min(44, parent.width - 16); height: 18; radius: 4
                                            color: "#E5F1FF"
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 8
                                            anchors.right: parent.right
                                            anchors.rightMargin: 8
                                        }
                                    }
                                }

                                PreviewOption {
                                    label: I18n.t("Zalo Tối", "Zalo Dark")
                                    selected: ThemeManager.themeMode === 1
                                    previewColor: "#22262B"
                                    onChosen: ThemeManager.setThemeMode(1)

                                    Rectangle {
                                        x: 4; y: 4
                                        width: 22
                                        height: parent.height - 8
                                        radius: 4
                                        color: "#121416"

                                        Rectangle {
                                            width: 10; height: 10; radius: 5
                                            color: "#64748B"
                                            anchors.top: parent.top
                                            anchors.topMargin: 4
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                    }

                                    Rectangle {
                                        x: 30; y: 4
                                        width: parent.width - 34
                                        height: parent.height - 8
                                        radius: 4
                                        color: "#16191D"

                                        Rectangle {
                                            width: Math.min(44, parent.width - 16); height: 18; radius: 4
                                            color: "#1F344D"
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 8
                                            anchors.right: parent.right
                                            anchors.rightMargin: 8
                                        }
                                    }
                                }

                                PreviewOption {
                                    label: I18n.t("Theo hệ thống", "System Theme")
                                    selected: ThemeManager.themeMode === 2
                                    onChosen: ThemeManager.setThemeMode(2)

                                    Rectangle {
                                        width: parent.width / 2
                                        height: parent.height
                                        color: "#FFFFFF"

                                        Rectangle {
                                            width: 12
                                            height: parent.height
                                            color: "#005AE0"
                                        }

                                        Rectangle {
                                            width: Math.min(26, parent.width - 20); height: 14; radius: 3
                                            color: "#E5F1FF"
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 10
                                            anchors.right: parent.right
                                            anchors.rightMargin: 6
                                        }
                                    }

                                    Rectangle {
                                        x: parent.width / 2
                                        width: parent.width / 2
                                        height: parent.height
                                        color: "#22262B"

                                        Rectangle {
                                            width: Math.min(26, parent.width - 12); height: 14; radius: 3
                                            color: "#1F344D"
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 10
                                            anchors.left: parent.left
                                            anchors.leftMargin: 6
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: ThemeManager.themeMode === 2
                                    ? I18n.t("Đang áp dụng giao diện hệ thống (kế thừa màu sắc cửa sổ, accent color và chế độ sáng/tối từ Linux Desktop).",
                                             "Applying system theme (inheriting window colors, accent color, and dark/light preference from Linux Desktop).")
                                    : (ThemeManager.themeMode === 1
                                       ? I18n.t("Đang áp dụng giao diện Zalo Tối chuẩn mực (độc lập với theme hệ thống).",
                                                "Applying standard Zalo Dark theme (independent of system theme).")
                                       : I18n.t("Đang áp dụng giao diện Zalo Sáng chuẩn mực (độc lập với theme hệ thống).",
                                                "Applying standard Zalo Light theme (independent of system theme)."))
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: ThemeManager.secondaryTextColor
                                wrapMode: Text.WordWrap
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: ThemeManager.borderColor
                            }

                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Hình nền trò chuyện", "Chat background")
                                font.family: Theme.fontFamily
                                font.bold: true
                                font.pixelSize: 14
                                color: ThemeManager.textColor
                            }

                            ToggleRow {
                                title: I18n.t("Dùng ảnh đại diện làm hình nền", "Use the avatar as the chat background")
                                checked: ZaloClient.useAvatarAsChatBackground
                                onToggled: (value) => ZaloClient.useAvatarAsChatBackground = value
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: ThemeManager.borderColor
                            }

                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Thanh điều hướng", "Navigation bar")
                                font.family: Theme.fontFamily
                                font.bold: true
                                font.pixelSize: 14
                                color: ThemeManager.textColor
                            }

                            ToggleRow {
                                title: I18n.t("Hiển thị thẻ phân loại như thư mục", "Show labels as folders")
                                checked: ZaloClient.showLabelFolders
                                onToggled: (value) => ZaloClient.showLabelFolders = value
                            }
                        }

                        ColumnLayout {
                            id: notificationsTab
                            visible: root.currentTab === "notifications"
                            x: contentContainer.contentPadding
                            y: contentContainer.contentPadding
                            width: contentContainer.width - 2 * contentContainer.contentPadding
                            spacing: 18

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                SectionTitle { text: I18n.t("Thông báo", "Notifications") }
                                SectionHint {
                                    text: I18n.t("Nhận thông báo mỗi khi có tin nhắn mới",
                                                 "Get notified whenever a new message arrives")
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12

                                PreviewOption {
                                    label: I18n.t("Bật", "On")
                                    selected: ZaloClient.notifyNewMessage
                                    previewColor: ThemeManager.cardBackgroundColor
                                    onChosen: ZaloClient.notifyNewMessage = true

                                    Item {
                                        anchors.centerIn: parent
                                        width: Math.min(96, parent.width - 8)
                                        height: 60

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width * 0.84
                                            height: 50
                                            radius: 4
                                            color: (ThemeManager.cardBackgroundColor)
                                            border.color: ZaloClient.notifyNewMessage ? Theme.zaloBlue : (ThemeManager.dividerColor)
                                            border.width: 1.5

                                            Rectangle {
                                                anchors.top: parent.top
                                                anchors.topMargin: 4
                                                anchors.right: parent.right
                                                anchors.rightMargin: 4
                                                width: Math.min(38, parent.width - 8)
                                                height: 16
                                                radius: 3
                                                color: "#FFFFFF"
                                                border.color: Theme.zaloBlue
                                                border.width: 1

                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 2
                                                    spacing: 2
                                                    Rectangle { Layout.preferredWidth: 4; Layout.preferredHeight: 4; radius: 2; color: Theme.zaloBlue }
                                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 2; color: "#94A3B8" }
                                                }
                                            }
                                        }

                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width
                                            height: 6
                                            radius: 3
                                            color: ZaloClient.notifyNewMessage ? Theme.zaloBlue : (ThemeManager.dividerColor)
                                        }
                                    }
                                }

                                PreviewOption {
                                    label: I18n.t("Tắt", "Off")
                                    selected: !ZaloClient.notifyNewMessage
                                    previewColor: ThemeManager.cardBackgroundColor
                                    onChosen: ZaloClient.notifyNewMessage = false

                                    Item {
                                        anchors.centerIn: parent
                                        width: Math.min(96, parent.width - 8)
                                        height: 60

                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width * 0.84
                                            height: 50
                                            radius: 4
                                            color: (ThemeManager.cardBackgroundColor)
                                            border.color: !ZaloClient.notifyNewMessage ? Theme.zaloBlue : (ThemeManager.dividerColor)
                                            border.width: 1.5
                                        }

                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width
                                            height: 6
                                            radius: 3
                                            color: !ZaloClient.notifyNewMessage ? Theme.zaloBlue : (ThemeManager.dividerColor)
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: ThemeManager.borderColor
                            }

                            Text {
                                Layout.fillWidth: true
                                text: I18n.t("Âm thanh", "Sounds")
                                font.family: Theme.fontFamily
                                font.bold: true
                                font.pixelSize: 14
                                color: ThemeManager.textColor
                            }

                            ToggleRow {
                                title: I18n.t("Phát âm thanh khi có tin nhắn và thông báo mới",
                                              "Play a sound for new messages and notifications")
                                checked: ZaloClient.notifySound
                                onToggled: (value) => ZaloClient.notifySound = value
                            }

                            SoundPicker {
                                title: I18n.t("Âm báo tin nhắn", "Message sound")
                                subtitle: I18n.t("Âm thanh phát khi có tin nhắn mới",
                                                 "Sound played when a new message arrives")
                                soundType: "noti"
                                selectedKey: ZaloClient.selectedNotiSound
                                onPicked: (key) => ZaloClient.selectedNotiSound = key
                                options: [
                                    { key: "pop", label: I18n.t("Bong bóng pop", "Pop") },
                                    { key: "bubble", label: I18n.t("Bọt nước", "Bubble") },
                                    { key: "chime", label: I18n.t("Chuông thanh", "Chime") },
                                    { key: "bell", label: I18n.t("Chuông ngân", "Bell") },
                                    { key: "digital", label: I18n.t("Kỹ thuật số", "Digital") },
                                    { key: "positive", label: I18n.t("Tích cực", "Positive") },
                                    { key: "confirm", label: I18n.t("Xác nhận", "Confirm") },
                                    { key: "magic", label: I18n.t("Phép thuật", "Magic") }
                                ].concat(root.systemSounds)
                            }

                            SoundPicker {
                                title: I18n.t("Nhạc chuông cuộc gọi đến", "Incoming call ringtone")
                                subtitle: I18n.t("Giai điệu phát khi có người gọi tới",
                                                 "Tune played when someone calls you")
                                soundType: "ringtone"
                                selectedKey: ZaloClient.selectedRingtone
                                onPicked: (key) => ZaloClient.selectedRingtone = key
                                options: [
                                    { key: "classic", label: I18n.t("Điện thoại văn phòng (mặc định)", "Classic (default)") },
                                    { key: "marimba", label: I18n.t("Marimba du dương", "Marimba") },
                                    { key: "digital_ring", label: I18n.t("Chuông kỹ thuật số", "Digital Ring") },
                                    { key: "soft", label: I18n.t("Giai điệu nhẹ nhàng", "Soft") }
                                ].concat(root.systemSounds)
                            }
                        }

                        ColumnLayout {
                            id: privacyTab
                            visible: root.currentTab === "privacy"
                            x: contentContainer.contentPadding
                            y: contentContainer.contentPadding
                            width: contentContainer.width - 2 * contentContainer.contentPadding
                            spacing: 12

                            SectionTitle { text: I18n.t("Quyền riêng tư", "Privacy") }

                            ToggleRow {
                                title: I18n.t("Ẩn trạng thái đang soạn tin", "Hide typing status")
                                subtitle: I18n.t("Người khác sẽ không thấy “Đang soạn tin...” khi bạn gõ phím",
                                                 "Others won't see “typing...” while you type")
                                checked: ZaloClient.blockTyping
                                onToggled: (value) => ZaloClient.blockTyping = value
                            }

                            ToggleRow {
                                title: I18n.t("Ẩn trạng thái đã nhận", "Hide delivered status")
                                subtitle: I18n.t("Người gửi sẽ không biết tin nhắn đã đến máy bạn",
                                                 "Senders won't know their message reached you")
                                checked: ZaloClient.blockDelivered
                                onToggled: (value) => ZaloClient.blockDelivered = value
                            }

                            ToggleRow {
                                title: I18n.t("Ẩn trạng thái đã xem", "Hide seen status")
                                subtitle: I18n.t("Đọc tin nhắn mà không hiện “Đã xem” cho người gửi",
                                                 "Read messages without showing “Seen” to the sender")
                                checked: ZaloClient.blockSeen
                                onToggled: (value) => ZaloClient.blockSeen = value
                            }

                            ToggleRow {
                                title: I18n.t("Chống nhìn lén", "Anti-peeking")
                                subtitle: I18n.t("Làm mờ tin nhắn cho tới khi rê chuột vào, ẩn nội dung trong thông báo. Bật/tắt nhanh: Ctrl+Shift+H hoặc menu biểu tượng ở khay hệ thống",
                                                 "Dims messages until hovered and hides notification content. Quick switch: Ctrl+Shift+H or the tray icon menu")
                                checked: ZaloClient.antiPeeking
                                onToggled: (value) => ZaloClient.setAntiPeeking(value)
                            }

                            SectionTitle { text: I18n.t("Quyền riêng tư (đồng bộ máy chủ)", "Privacy (synced with server)") }
                            SectionHint {
                                text: I18n.t("Các thiết lập này được lưu trên máy chủ Zalo và áp dụng cho mọi thiết bị của bạn.",
                                             "These are stored on Zalo's servers and apply across all your devices.")
                            }

                            ToggleRow {
                                title: I18n.t("Hiển thị trạng thái hoạt động", "Show active status")
                                subtitle: I18n.t("Cho bạn bè thấy khi bạn đang hoạt động", "Let friends see when you're active")
                                checked: ZaloClient.showOnlineStatus
                                onToggled: (value) => ZaloClient.setServerSetting("show_online_status", value ? 1 : 0)
                            }
                            ToggleRow {
                                title: I18n.t("Hiển thị trạng thái đã xem", "Show seen status")
                                subtitle: I18n.t("Người gửi thấy “Đã xem” khi bạn đọc tin nhắn", "Senders see “Seen” when you read their message")
                                checked: ZaloClient.displaySeenStatus
                                onToggled: (value) => ZaloClient.setServerSetting("display_seen_status", value ? 1 : 0)
                            }
                            ToggleRow {
                                title: I18n.t("Nhận tin nhắn từ người lạ", "Receive messages from strangers")
                                subtitle: I18n.t("Tắt để chỉ bạn bè mới nhắn được cho bạn", "Off: only friends can message you")
                                checked: ZaloClient.receiveStrangerMessage
                                onToggled: (value) => ZaloClient.setServerSetting("receive_message", value ? 1 : 2)
                            }
                            ToggleRow {
                                title: I18n.t("Nhận cuộc gọi từ người lạ", "Accept calls from strangers")
                                subtitle: I18n.t("Tắt để chỉ bạn bè mới gọi được cho bạn", "Off: only friends can call you")
                                checked: ZaloClient.acceptStrangerCall
                                onToggled: (value) => ZaloClient.setServerSetting("accept_stranger_call", value ? 3 : 2)
                            }
                            ToggleRow {
                                title: I18n.t("Hiện tôi trong gợi ý kết bạn", "Show me in friend suggestions")
                                checked: ZaloClient.displayOnRecommendFriend
                                onToggled: (value) => ZaloClient.setServerSetting("display_on_recommend_friend", value ? 1 : 0)
                            }
                            ToggleRow {
                                title: I18n.t("Cho phép kết bạn qua số điện thoại", "Allow friend requests via phone number")
                                checked: ZaloClient.addFriendViaPhone
                                onToggled: (value) => ZaloClient.setServerSetting("add_friend_via_phone", value ? 1 : 0)
                            }
                            ToggleRow {
                                title: I18n.t("Cho phép kết bạn qua mã QR", "Allow friend requests via QR code")
                                checked: ZaloClient.addFriendViaQr
                                onToggled: (value) => ZaloClient.setServerSetting("add_friend_via_qr", value ? 1 : 0)
                            }
                            ToggleRow {
                                title: I18n.t("Cho phép kết bạn qua nhóm chung", "Allow friend requests via common groups")
                                checked: ZaloClient.addFriendViaGroup
                                onToggled: (value) => ZaloClient.setServerSetting("add_friend_via_group", value ? 1 : 0)
                            }

                            SectionTitle { text: I18n.t("Khóa ứng dụng bằng mã", "App passcode lock") }
                            SectionHint {
                                text: I18n.t("Khi đã đặt mã, Zalos tự khóa lúc mở, khi bạn nhấn Ctrl+L và sau thời gian không dùng; thông báo sẽ ẩn nội dung khi đang khóa. Mã chỉ khóa giao diện: tin nhắn lưu trên máy không được mã hóa thêm.",
                                             "With a passcode, Zalos locks on start, on Ctrl+L and after a period of inactivity; notifications hide their content while locked. It only locks the window: messages stored on this computer are not additionally encrypted.")
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: lockCol.implicitHeight + 32
                                radius: 10
                                color: ThemeManager.cardBackgroundColor
                                border.color: ThemeManager.borderColor
                                border.width: 1
                                property string feedback: ""
                                property bool feedbackOk: false
                                id: lockCard

                                ColumnLayout {
                                    id: lockCol
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 10

                                    ZTextField {
                                        id: currentPasscode
                                        Layout.fillWidth: true
                                        visible: ZaloClient.appLock.hasPasscode
                                        echoMode: TextInput.Password
                                        placeholderText: I18n.t("Mã hiện tại", "Current passcode")
                                    }
                                    ZTextField {
                                        id: newPasscode
                                        Layout.fillWidth: true
                                        echoMode: TextInput.Password
                                        placeholderText: I18n.t("Mã mới (ít nhất 4 ký tự)", "New passcode (at least 4 characters)")
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        ZButton {
                                            text: ZaloClient.appLock.hasPasscode ? I18n.t("Đổi mã", "Change passcode") : I18n.t("Đặt mã", "Set passcode")
                                            enabled: newPasscode.text.length >= 4
                                            onClicked: {
                                                const ok = ZaloClient.appLock.setPasscode(currentPasscode.text, newPasscode.text);
                                                lockCard.feedbackOk = ok;
                                                lockCard.feedback = ok ? I18n.t("Đã lưu mã khóa.", "Passcode saved.")
                                                                       : I18n.t("Mã hiện tại không đúng.", "The current passcode is wrong.");
                                                currentPasscode.text = "";
                                                newPasscode.text = "";
                                            }
                                        }
                                        ZButton {
                                            visible: ZaloClient.appLock.hasPasscode
                                            variant: "danger"
                                            text: I18n.t("Xóa mã", "Remove passcode")
                                            enabled: currentPasscode.text.length > 0
                                            onClicked: {
                                                const ok = ZaloClient.appLock.removePasscode(currentPasscode.text);
                                                lockCard.feedbackOk = ok;
                                                lockCard.feedback = ok ? I18n.t("Đã xóa mã khóa.", "Passcode removed.")
                                                                       : I18n.t("Mã hiện tại không đúng.", "The current passcode is wrong.");
                                                currentPasscode.text = "";
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Text {
                                        visible: lockCard.feedback.length > 0
                                        text: lockCard.feedback
                                        color: lockCard.feedbackOk ? Theme.onlineGreen : Theme.badgeRed
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: ZaloClient.appLock.hasPasscode
                                        spacing: 8
                                        Text {
                                            Layout.fillWidth: true
                                            text: I18n.t("Tự khóa khi không dùng", "Auto-lock when idle")
                                            color: ThemeManager.textColor
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 13
                                        }
                                        ZComboBox {
                                            id: autoLockBox
                                            readonly property var minutes: [0, 1, 5, 15, 60]
                                            model: [I18n.t("Tắt", "Off"), I18n.t("1 phút", "1 minute"), I18n.t("5 phút", "5 minutes"),
                                                    I18n.t("15 phút", "15 minutes"), I18n.t("1 giờ", "1 hour")]
                                            currentIndex: Math.max(0, minutes.indexOf(ZaloClient.appLock.autoLockMinutes))
                                            onActivated: (index) => ZaloClient.appLock.autoLockMinutes = autoLockBox.minutes[index]
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            id: storageTab
                            visible: root.currentTab === "storage"
                            x: contentContainer.contentPadding
                            y: contentContainer.contentPadding
                            width: contentContainer.width - 2 * contentContainer.contentPadding
                            spacing: 12
                            property var usage: []
                            function refresh(): void { storageTab.usage = ZaloClient.storage.usage(); }
                            function sizeText(bytes: real): string {
                                if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(1) + " GB";
                                if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + " MB";
                                if (bytes >= 1024) return Math.round(bytes / 1024) + " KB";
                                return Math.round(bytes) + " B";
                            }
                            function labelFor(key: string): string {
                                switch (key) {
                                case "avatars": return I18n.t("Ảnh đại diện (bộ nhớ đệm)", "Avatars (cache)");
                                case "voice": return I18n.t("Bản ghi âm tạm", "Temporary voice recordings");
                                case "sounds": return I18n.t("Âm thanh thông báo", "Notification sounds");
                                case "qmlcache": return I18n.t("Bộ nhớ đệm giao diện", "Interface cache");
                                case "downloads": return I18n.t("File đã tải (thư mục Downloads/Zalos)", "Downloaded files (Downloads/Zalos)");
                                }
                                return key;
                            }
                            onVisibleChanged: if (visible) refresh()
                            Component.onCompleted: if (visible) refresh()
                            Connections {
                                target: ZaloClient.storage
                                function onUsageChanged() { if (storageTab.visible) storageTab.refresh(); }
                            }

                            SectionTitle { text: I18n.t("Dữ liệu & bộ nhớ", "Data & storage") }
                            SectionHint {
                                text: I18n.t("Dung lượng Zalos dùng trên máy. Xóa bộ nhớ đệm không xóa tin nhắn; lịch sử ngoại tuyến của tài khoản không nằm ở đây. File đã tải là của bạn, Zalos không tự xóa.",
                                             "Space Zalos uses on this computer. Clearing caches never deletes messages; the account's offline history is not listed here. Downloaded files are yours and are never deleted by Zalos.")
                            }
                            Repeater {
                                model: storageTab.usage
                                delegate: Rectangle {
                                    id: usageRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    radius: 10
                                    color: ThemeManager.cardBackgroundColor
                                    border.color: ThemeManager.borderColor
                                    border.width: 1
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 14
                                        anchors.rightMargin: 10
                                        spacing: 10
                                        ColumnLayout {
                                            Layout.alignment: Qt.AlignVCenter
                                            spacing: 0
                                            Text {
                                                text: storageTab.labelFor(usageRow.modelData.key)
                                                color: ThemeManager.textColor
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                            }
                                            Text {
                                                text: storageTab.sizeText(usageRow.modelData.bytes) + " · " + usageRow.modelData.files
                                                      + I18n.t(" tệp", " files")
                                                color: ThemeManager.secondaryTextColor
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 11
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                        ZButton {
                                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                            variant: "secondary"
                                            text: usageRow.modelData.clearable ? I18n.t("Xóa", "Clear") : I18n.t("Mở thư mục", "Open folder")
                                            enabled: !usageRow.modelData.clearable || usageRow.modelData.bytes > 0
                                            onClicked: {
                                                if (usageRow.modelData.clearable) ZaloClient.storage.clear(usageRow.modelData.key);
                                                else ZaloClient.storage.openDownloadsFolder();
                                            }
                                        }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: I18n.t("Giới hạn bộ nhớ đệm ảnh đại diện", "Avatar cache limit")
                                    color: ThemeManager.textColor
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                }
                                ZComboBox {
                                    id: avatarLimitBox
                                    readonly property var sizes: [100, 300, 500, 1000, 2000]
                                    model: ["100 MB", "300 MB", "500 MB", "1 GB", "2 GB"]
                                    currentIndex: Math.max(0, sizes.indexOf(ZaloClient.storage.avatarLimitMb))
                                    onActivated: (index) => ZaloClient.storage.avatarLimitMb = avatarLimitBox.sizes[index]
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: I18n.t("Xóa ảnh đại diện không dùng sau", "Remove avatars unused for")
                                    color: ThemeManager.textColor
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                }
                                ZComboBox {
                                    id: ageBox
                                    readonly property var days: [7, 31, 90, 365]
                                    model: [I18n.t("7 ngày", "7 days"), I18n.t("1 tháng", "1 month"), I18n.t("3 tháng", "3 months"), I18n.t("1 năm", "1 year")]
                                    currentIndex: Math.max(0, days.indexOf(ZaloClient.storage.maxAgeDays))
                                    onActivated: (index) => ZaloClient.storage.maxAgeDays = ageBox.days[index]
                                }
                            }
                        }

                        ColumnLayout {
                            id: syncTab
                            visible: root.currentTab === "sync"
                            x: contentContainer.contentPadding
                            y: contentContainer.contentPadding
                            width: contentContainer.width - 2 * contentContainer.contentPadding
                            spacing: 14

                            SectionTitle { text: I18n.t("Đồng bộ tin nhắn từ điện thoại", "Sync messages from your phone") }
                            SectionHint {
                                text: I18n.t("Chuyển lịch sử trò chuyện từ Zalo trên điện thoại sang máy tính này. Điện thoại sẽ hỏi bạn xác nhận; tin nhắn được lưu trên máy này và hiển thị cả khi không có mạng.",
                                             "Transfer your chat history from Zalo on your phone to this computer. Your phone asks you to confirm; messages are kept on this computer and shown even offline.")
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: syncContentCol.implicitHeight + 32
                                radius: 10
                                color: ThemeManager.cardBackgroundColor
                                border.color: ThemeManager.borderColor
                                border.width: 1

                                ColumnLayout {
                                    id: syncContentCol
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 12

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 14

                                        Rectangle {
                                            width: 44
                                            height: 44
                                            radius: 22
                                            color: ZaloClient.mobileSyncState === 4 ? Qt.rgba(43/255, 182/255, 115/255, 0.15)
                                                   : (ZaloClient.mobileSyncState === 5 ? Qt.rgba(229/255, 56/255, 59/255, 0.15) : Qt.rgba(0, 104/255, 255/255, 0.12))

                                            ZIcon {
                                                anchors.centerIn: parent
                                                name: ZaloClient.mobileSyncState === 4 ? "check" : (ZaloClient.mobileSyncState === 5 ? "close" : "refresh")
                                                size: 22
                                                color: ZaloClient.mobileSyncState === 4 ? "#2BB673"
                                                       : (ZaloClient.mobileSyncState === 5 ? "#E5383B" : Theme.zaloBlue)
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4

                                            Text {
                                                Layout.fillWidth: true
                                                text: ZaloClient.mobileSyncState === 0
                                                      ? I18n.t("Đồng bộ tin nhắn từ điện thoại", "Sync messages from phone")
                                                      : (ZaloClient.mobileSyncState === 4
                                                         ? I18n.t("Đã đồng bộ thành công ✅", "Synced successfully ✅")
                                                         : (ZaloClient.mobileSyncState === 5
                                                            ? I18n.t("Đồng bộ chưa thành công", "Sync didn't complete")
                                                            : I18n.t("Đang đồng bộ...", "Syncing in progress...")))
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 14
                                                font.bold: true
                                                color: ThemeManager.textColor
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: ZaloClient.mobileSyncStatus.length > 0
                                                      ? ZaloClient.mobileSyncStatus
                                                      : I18n.t("Chuyển lịch sử tin nhắn từ điện thoại sang máy tính này mà không làm mất dữ liệu hiện tại.",
                                                               "Transfer message history from phone without losing current data.")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 12
                                                color: ThemeManager.secondaryTextColor
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                    }

                                    ProgressBar {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 6
                                        visible: ZaloClient.mobileSyncState >= 1 && ZaloClient.mobileSyncState <= 3
                                        value: ZaloClient.mobileSyncProgress / 100.0
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10

                                        Item { Layout.fillWidth: true }

                                        Rectangle {
                                            visible: ZaloClient.mobileSyncState === 1 || ZaloClient.mobileSyncState === 2
                                            width: 80
                                            height: 32
                                            radius: 6
                                            color: cancelHover.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                            border.color: ThemeManager.borderColor
                                            border.width: 1

                                            Text {
                                                anchors.centerIn: parent
                                                text: I18n.t("Hủy", "Cancel")
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                color: ThemeManager.textColor
                                            }
                                            MouseArea {
                                                id: cancelHover
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: ZaloClient.cancelMobileSync()
                                            }
                                        }

                                        Rectangle {
                                            visible: ZaloClient.mobileSyncState === 0 || ZaloClient.mobileSyncState === 4 || ZaloClient.mobileSyncState === 5
                                            width: syncBtnText.implicitWidth + 24
                                            height: 34
                                            radius: 6
                                            color: syncBtnHover.containsMouse ? Theme.zaloBlueHover : Theme.zaloBlue

                                            Text {
                                                id: syncBtnText
                                                anchors.centerIn: parent
                                                text: ZaloClient.mobileSyncState === 4
                                                      ? I18n.t("Đồng bộ lại", "Sync again")
                                                      : (ZaloClient.mobileSyncState === 5
                                                         ? I18n.t("Thử lại", "Retry")
                                                         : I18n.t("Bắt đầu đồng bộ", "Start sync"))
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 13
                                                font.bold: true
                                                color: "#FFFFFF"
                                            }
                                            MouseArea {
                                                id: syncBtnHover
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: ZaloClient.requestMobileSync()
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
}
