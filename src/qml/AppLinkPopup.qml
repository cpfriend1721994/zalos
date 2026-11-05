import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Popup {
    id: root
    objectName: "appLinkPopup"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(360, parent ? parent.width - 32 : 360)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string url: ""
    property string title: ""
    property string thumb: ""
    property int qrSize: 0
    property list<bool> qrBits: []

    function show(link: string, caption: string, image: string): void {
        root.url = link;
        root.title = caption;
        root.thumb = image;
        const qr = ZaloClient.qrModules(link);
        root.qrSize = qr.size;
        root.qrBits = qr.modules;
        root.open();
        qrCanvas.requestPaint();
    }

    background: Rectangle {
        color: ThemeManager.surfaceColor
        radius: 12
        border.color: ThemeManager.borderColor
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: I18n.t("Nội dung chỉ mở được trong ứng dụng Zalo", "This content opens only in the Zalo app")
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily
            font.pixelSize: 15
            font.bold: true
            color: ThemeManager.textColor
        }

        RowLayout {
            visible: root.title.length > 0 || root.thumb.length > 0
            Layout.fillWidth: true
            spacing: 10

            Image {
                visible: root.thumb.length > 0
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                source: root.thumb
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 128
                sourceSize.height: 128
            }

            Text {
                Layout.fillWidth: true
                text: root.title
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: 13
                color: ThemeManager.textColor
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 200
            Layout.preferredHeight: 200
            radius: 8
            color: "#FFFFFF"

            Canvas {
                id: qrCanvas
                anchors.fill: parent
                anchors.margins: 8
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.fillStyle = "#FFFFFF";
                    ctx.fillRect(0, 0, width, height);
                    const size = root.qrSize;
                    if (size <= 0 || root.qrBits.length < size * size) return;
                    const cell = Math.floor(Math.min(width, height) / size);
                    const offset = Math.floor((Math.min(width, height) - cell * size) / 2);
                    ctx.fillStyle = "#000000";
                    for (let y = 0; y < size; ++y)
                        for (let x = 0; x < size; ++x)
                            if (root.qrBits[y * size + x]) ctx.fillRect(offset + x * cell, offset + y * cell, cell, cell);
                }
            }
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: I18n.t("Quét bằng Zalo trên điện thoại để xem", "Scan with Zalo on your phone to view")
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: ThemeManager.secondaryTextColor
        }

        TextEdit {
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            text: root.url
            wrapMode: TextEdit.WrapAnywhere
            font.family: Theme.fontFamily
            font.pixelSize: 11
            color: ThemeManager.secondaryTextColor
            selectionColor: Theme.zaloBlueLight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Item { Layout.fillWidth: true }

            ZButton {
                variant: "secondary"
                text: I18n.t("Mở trong trình duyệt", "Open in browser")
                onClicked: { Qt.openUrlExternally(root.url); root.close(); }
            }

            ZButton {
                text: I18n.t("Đóng", "Close")
                onClicked: root.close()
            }
        }
    }
}
