import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

// Personal To-Do backed by Zalo's /api/board/personal/* endpoints.
Rectangle {
    id: root
    color: ThemeManager.backgroundColor

    property string tab: "assigned"

    function hoverColor(): color {
        return (ThemeManager.hoverColor);
    }

    function dueLabel(ms: real): string {
        if (!ms || ms <= 0) return "";
        var d = new Date(ms);
        var text = ("0" + d.getDate()).slice(-2) + "/" + ("0" + (d.getMonth() + 1)).slice(-2) + " "
                 + ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2);
        return (ms < Date.now() ? I18n.t("Quá hạn ", "Overdue ") : I18n.t("Hạn ", "Due ")) + text;
    }

    // Created on demand by a Loader in Main.qml, already visible: run the same init.
    Component.onCompleted: visibleChanged()
    onVisibleChanged: if (visible && ZaloClient.isLoggedIn) ZaloClient.loadTodos(root.tab)
    // Opened before the sign-in finished (offline cache start): load once it does.
    Connections {
        target: ZaloClient
        function onLoginStateChanged() {
            if (root.visible && ZaloClient.isLoggedIn) ZaloClient.loadTodos(root.tab);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: ThemeManager.surfaceColor

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: ThemeManager.borderColor
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 16
                spacing: 10

                ZIcon { name: "todo"; size: 22; color: ThemeManager.textColor }
                Text {
                    Layout.fillWidth: true
                    text: "To-Do"
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    color: ThemeManager.textColor
                }
                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 16
                    color: refreshMouse.containsMouse ? root.hoverColor() : ThemeManager.hoverClearColor
                    ZIcon { anchors.centerIn: parent; name: "refresh"; size: 18; color: ThemeManager.textColor }
                    MouseArea {
                        id: refreshMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: ZaloClient.loadTodos(root.tab)
                    }
                }
            }
        }

        // Tabs
        Row {
            Layout.leftMargin: 20
            Layout.topMargin: 8
            spacing: 18
            Repeater {
                model: [ { key: "assigned", vi: "Được giao cho tôi", en: "Assigned to me" },
                         { key: "created", vi: "Tôi đã giao", en: "Assigned by me" },
                         { key: "done", vi: "Đã xong", en: "Done" } ]
                Item {
                    required property var modelData
                    readonly property bool selected: root.tab === modelData.key
                    width: tabText.implicitWidth
                    height: 34
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: I18n.t(modelData.vi, modelData.en)
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        font.bold: parent.selected
                        color: parent.selected ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: parent.selected ? Theme.zaloBlue : "transparent"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.tab = modelData.key;
                            ZaloClient.loadTodos(root.tab);
                        }
                    }
                }
            }
        }

        // Quick add
        RowLayout {
            visible: root.tab !== "done"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 12
            spacing: 8

            TextField {
                id: newTodo
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                placeholderText: I18n.t("Thêm công việc mới cho bản thân...", "Add a new task for yourself...")
                placeholderTextColor: ThemeManager.placeholderColor
                color: ThemeManager.textColor
                font.family: Theme.fontFamily
                font.pixelSize: 13
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: 12
                rightPadding: 12
                background: Rectangle {
                    radius: 6
                    color: ThemeManager.inputFillColor
                    border.color: newTodo.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                    border.width: 1
                }
                onAccepted: addButton.clicked()
            }
            ComboBox {
                id: dueChoice
                Layout.preferredWidth: 150
                Layout.preferredHeight: 38
                model: [I18n.t("Không hạn", "No due date"), I18n.t("Hôm nay", "Today"),
                        I18n.t("Ngày mai", "Tomorrow"), I18n.t("Tuần sau", "Next week")]
                font.family: Theme.fontFamily
                font.pixelSize: 13

                function dueMs() {
                    var d = new Date();
                    d.setHours(23, 59, 0, 0);
                    if (currentIndex === 0) return 0;
                    if (currentIndex === 2) d.setDate(d.getDate() + 1);
                    if (currentIndex === 3) d.setDate(d.getDate() + 7);
                    return d.getTime();
                }

                background: Rectangle {
                    radius: 6
                    color: ThemeManager.inputFillColor
                    border.color: dueChoice.activeFocus ? Theme.zaloBlue : ThemeManager.borderColor
                    border.width: 1
                }

                contentItem: Text {
                    leftPadding: 12
                    rightPadding: 24
                    text: dueChoice.displayText
                    font: dueChoice.font
                    color: ThemeManager.textColor
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                indicator: ZIcon {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: "arrow_down"
                    size: 14
                    color: ThemeManager.secondaryTextColor
                }

                popup: Popup {
                    y: dueChoice.height + 4
                    width: dueChoice.width
                    padding: 4
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: dueChoice.popup.visible ? dueChoice.delegateModel : null
                        currentIndex: dueChoice.highlightedIndex
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
                    width: dueChoice.width - 8
                    height: 34
                    contentItem: Text {
                        text: modelData
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
            Button {
                id: addButton
                Layout.preferredHeight: 38
                Layout.preferredWidth: 80
                enabled: newTodo.text.trim().length > 0
                background: Rectangle {
                    radius: 6
                    color: !addButton.enabled ? ThemeManager.inputFillColor
                           : addButton.pressed ? Theme.zaloBlueHover
                           : addButton.hovered ? Theme.zaloBlueHover : Theme.zaloBlue
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
                contentItem: Text {
                    text: I18n.t("Thêm", "Add")
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.bold: true
                    color: addButton.enabled ? "#FFFFFF" : ThemeManager.secondaryTextColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    ZaloClient.createTodo(newTodo.text, dueChoice.dueMs());
                    newTodo.text = "";
                }
            }
        }

        // List
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8

            ListView {
                id: todoList
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                clip: true
                spacing: 4
                model: ZaloClient.todoItems
                ScrollBar.vertical: ScrollBar {}
                reuseItems: true
                cacheBuffer: 300

                delegate: Rectangle {
                    id: todoRow
                    required property var modelData
                    width: todoList.width
                    height: todoCol.implicitHeight + 20
                    radius: 8
                    color: rowMouse.containsMouse ? root.hoverColor() : ThemeManager.hoverClearColor
                    Behavior on color { ColorAnimation { duration: 140 } }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 12

                        ZCheckBox {
                            round: true
                            checked: todoRow.modelData.status === 1
                            onToggled: function(value) { ZaloClient.setTodoDone(todoRow.modelData.id, value); }
                        }

                        ColumnLayout {
                            id: todoCol
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: todoRow.modelData.content
                                wrapMode: Text.Wrap
                                font.family: Theme.fontFamily
                                font.pixelSize: 14
                                font.strikeout: todoRow.modelData.status === 1
                                color: ThemeManager.textColor
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: [root.dueLabel(todoRow.modelData.dueDate),
                                       todoRow.modelData.assigneeNames ? I18n.t("Giao cho: ", "To: ") + todoRow.modelData.assigneeNames : ""]
                                      .filter(function(x) { return x.length > 0; }).join("  ·  ")
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: todoRow.modelData.dueDate > 0 && todoRow.modelData.dueDate < Date.now() && todoRow.modelData.status !== 1
                                       ? Theme.badgeRed : ThemeManager.secondaryTextColor
                            }
                        }

                        Rectangle {
                            visible: rowMouse.containsMouse || deleteMouse.containsMouse
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            radius: 15
                            color: deleteMouse.containsMouse ? root.hoverColor() : ThemeManager.hoverClearColor
                            ZIcon { anchors.centerIn: parent; name: "trash"; size: 16; color: Theme.badgeRed }
                            MouseArea {
                                id: deleteMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.deleteTodo(todoRow.modelData.id)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 380)
                visible: todoList.count === 0
                spacing: 10
                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    visible: ZaloClient.isLoadingTodos
                    running: visible
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: !ZaloClient.isLoggedIn ? I18n.t("Cần kết nối tới Zalo để xem công việc.", "Connect to Zalo to see your tasks.")
                          : ZaloClient.isLoadingTodos ? I18n.t("Đang tải công việc...", "Loading tasks...")
                          : ZaloClient.todoUnavailable.length > 0 ? ZaloClient.todoUnavailable
                          : I18n.t("Không có công việc nào ở mục này.", "No tasks here.")
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    color: ThemeManager.secondaryTextColor
                }
            }
        }
    }
}
