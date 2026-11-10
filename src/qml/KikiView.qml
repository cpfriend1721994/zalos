pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Rectangle {
    id: root
    color: ThemeManager.chatBackgroundColor

    ListModel {
        id: kikiChatModel
    }

    function useTemplate(cardTitle: string, userPrompt: string, templateBody: string): void {
        kikiChatModel.append({
            isUser: true,
            text: userPrompt,
            isNotice: false,
            templateTitle: "",
            time: Qt.formatTime(new Date(), "HH:mm")
        });

        kikiChatModel.append({
            isUser: false,
            text: templateBody,
            isNotice: false,
            templateTitle: cardTitle,
            time: Qt.formatTime(new Date(), "HH:mm")
        });

        Qt.callLater(() => chatListView.positionViewAtEnd());
    }

    function askKiki(query: string): void {
        const q = query.trim();
        if (q.length === 0) return;

        kikiChatModel.append({
            isUser: true,
            text: q,
            isNotice: false,
            templateTitle: "",
            time: Qt.formatTime(new Date(), "HH:mm")
        });

        let reply = "";
        if (ZaloClient.kikiAvailable && ZaloClient.kikiUrl.length > 0) {
            reply = I18n.t("Dịch vụ Trợ lý Kiki trực tuyến của Zalo khả dụng tại:\n%1\n\nBạn có thể bấm vào liên kết trên để mở phiên làm việc trên trình duyệt.",
                           "Zalo Kiki Assistant web service is available at:\n%1\n\nClick the link to open the online assistant.").arg(ZaloClient.kikiUrl);
        } else {
            reply = I18n.t("Máy chủ Zalo hiện chưa kích hoạt API Trợ lý Kiki cho tài khoản này (tính năng Kiki được Zalo triển khai dưới dạng dịch vụ web thử nghiệm theo đợt cho nhóm tài khoản giới hạn).\n\n💡 Bạn có thể chọn 4 thẻ chức năng mẫu ở trên để tạo nhanh văn bản và chèn vào cuộc trò chuyện.",
                           "The Zalo server has not enabled the Kiki Assistant API for this account yet (Kiki is rolled out by Zalo as an experimental web service for select accounts).\n\n💡 You can use the 4 template cards above to draft emails, meeting summaries, and sales notes.");
        }

        kikiChatModel.append({
            isUser: false,
            text: reply,
            isNotice: true,
            templateTitle: "",
            time: Qt.formatTime(new Date(), "HH:mm")
        });

        Qt.callLater(() => chatListView.positionViewAtEnd());
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                Rectangle {
                    width: 36
                    height: 36
                    radius: 18
                    color: ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.4, 0.9, 0.25) : Theme.zaloBlueLight

                    ZIcon {
                        anchors.centerIn: parent
                        name: "kiki"
                        size: 20
                        color: Theme.zaloBlue
                    }
                }

                ColumnLayout {
                    spacing: 2
                    RowLayout {
                        spacing: 8
                        Text {
                            text: I18n.t("Trợ lý Kiki", "Kiki Assistant")
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                            font.bold: true
                            color: ThemeManager.textColor
                        }

                        Rectangle {
                            height: 18
                            radius: 9
                            implicitWidth: statusText.implicitWidth + 12
                            color: ZaloClient.kikiAvailable ? (ThemeManager.isDarkMode ? Qt.rgba(0.15, 0.45, 0.25, 0.35) : Qt.rgba(0.2, 0.7, 0.3, 0.15))
                                                            : (ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.08) : ThemeManager.hoverColor)
                            Text {
                                id: statusText
                                anchors.centerIn: parent
                                text: ZaloClient.kikiAvailable ? I18n.t("Máy chủ: Sẵn sàng", "Server: Ready")
                                                              : I18n.t("Chế độ mẫu tin nhắn", "Template Mode")
                                font.family: Theme.fontFamily
                                font.pixelSize: 10
                                color: ZaloClient.kikiAvailable ? Theme.onlineGreen : ThemeManager.secondaryTextColor
                            }
                        }
                    }

                    Text {
                        text: I18n.t("Soạn thảo văn bản, tóm tắt và hỗ trợ bán hàng", "Text drafting, summaries and sales productivity")
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        color: ThemeManager.secondaryTextColor
                    }
                }

                Item { Layout.fillWidth: true }

                ZButton {
                    variant: "secondary"
                    visible: kikiChatModel.count > 0
                    text: "+ " + I18n.t("Đoạn chat mới", "New Chat")
                    onClicked: kikiChatModel.clear()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                visible: kikiChatModel.count === 0
                anchors.fill: parent
                contentWidth: width
                contentHeight: Math.max(height, centerCol.implicitHeight + 40)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: centerCol
                    width: Math.min(parent.width - 40, 680)
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 32
                    spacing: 24

                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 12

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 72
                            height: 72
                            radius: 36
                            color: ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.5, 1, 0.2) : Theme.zaloBlueLight

                            ZIcon {
                                anchors.centerIn: parent
                                name: "kiki"
                                size: 40
                                color: Theme.zaloBlue
                            }
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: I18n.t("Xin chào, tôi là Trợ lý Kiki!", "Hello, I am Kiki Assistant!")
                            font.family: Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                            color: ThemeManager.textColor
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumWidth: 520
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: I18n.t("Tôi có thể hỗ trợ bạn soạn thảo tin nhắn, tóm tắt cuộc họp, viết email và hỗ trợ kinh doanh với zBusiness.",
                                         "I can assist you with message drafting, meeting summaries, and productivity tools.")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: parent.width > 560 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 12

                        component CapCard: Rectangle {
                            id: cap
                            property string cardIcon: ""
                            property string title: ""
                            property string desc: ""
                            property string prompt: ""
                            property string body: ""

                            Layout.fillWidth: true
                            height: 76
                            radius: 10
                            color: capMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.surfaceColor
                            border.color: ThemeManager.borderColor
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12

                                Rectangle {
                                    width: 40
                                    height: 40
                                    radius: 20
                                    color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.08) : ThemeManager.hoverColor

                                    ZIcon {
                                        anchors.centerIn: parent
                                        name: cap.cardIcon
                                        size: 20
                                        color: Theme.zaloBlue
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        text: cap.title
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: ThemeManager.textColor
                                    }
                                    Text {
                                        text: cap.desc
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        color: ThemeManager.secondaryTextColor
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                id: capMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.useTemplate(cap.title, cap.prompt, cap.body)
                            }
                        }

                        CapCard {
                            cardIcon: "chat"
                            title: I18n.t("Soạn thảo văn bản & Email", "Draft Text & Email")
                            desc: I18n.t("Viết email xin nghỉ phép trang trọng", "Draft a formal leave request")
                            prompt: I18n.t("Soạn email xin nghỉ phép 1 ngày", "Draft a leave request email")
                            body: I18n.t("✉️ **Mẫu email xin nghỉ phép trang trọng**:\n\nKính gửi [Tên Quản lý / Bộ phận Nhân sự],\n\nTôi viết email này để xin phép được nghỉ phép vào ngày [Ngày nghỉ] vì lý do cá nhân.\n\nTrong thời gian vắng mặt, tôi đã bàn giao các công việc khẩn cấp cho đồng nghiệp [Tên người hỗ trợ]. Tôi vẫn sẽ theo dõi tin nhắn Zalo và email trong trường hợp cần hỗ trợ đột xuất.\n\nRất mong nhận được sự chấp thuận từ anh/chị.\n\nTrân trọng cảm ơn,\n[Họ và tên của bạn]",
                                         "✉️ **Formal Leave Request Email Template**:\n\nDear [Manager / HR],\n\nI am writing to request a day of leave on [Date] due to personal reasons.\n\nI have handed over urgent tasks to [Colleague name] and will remain reachable via Zalo/email for urgent matters.\n\nThank you for your understanding.\n\nBest regards,\n[Your Name]")
                        }

                        CapCard {
                            cardIcon: "file"
                            title: I18n.t("Tóm tắt cuộc họp", "Summarize Meetings")
                            desc: I18n.t("Trích xuất các ý chính và hành động cần làm", "Extract key actions & takeaways")
                            prompt: I18n.t("Mẫu biên bản tóm tắt cuộc họp", "Meeting summary template")
                            body: I18n.t("📋 **Mẫu biên bản tóm tắt cuộc họp**:\n\n1. **Mục tiêu**: Đánh giá tiến độ dự án tuần vừa qua.\n2. **Các điểm đã thống nhất**:\n   - Hoàn tất kiểm thử các tính năng mới trên nền tảng Linux.\n   - Đóng gói và phát hành bản thử nghiệm AppImage.\n3. **Hành động cần làm (Action Items)**:\n   - @Thành viên A: Rà soát giao diện và phản hồi từ người dùng.\n   - @Thành viên B: Tối ưu bộ nhớ và cập nhật tài liệu kỹ thuật.\n4. **Thời hạn hoàn thành**: Trước 17:00 Thứ Sáu.",
                                         "📋 **Meeting Summary Template**:\n\n1. **Objective**: Review weekly milestones.\n2. **Decisions**:\n   - Finalize testing on Linux platforms.\n   - Deliver packaged AppImage builds.\n3. **Action Items**:\n   - @Member A: Review UI Polish.\n   - @Member B: Optimize memory footprint.\n4. **Deadline**: Friday 17:00.")
                        }

                        CapCard {
                            cardIcon: "biz_tools"
                            title: I18n.t("Tư vấn bán hàng zBusiness", "zBusiness Sales Tips")
                            desc: I18n.t("Ý tưởng tăng tương tác và chốt đơn nhanh", "Tips to boost engagement")
                            prompt: I18n.t("5 ý tưởng tăng tương tác khách hàng", "5 customer engagement ideas")
                            body: I18n.t("💡 **5 ý tưởng tăng tương tác khách hàng trên Zalo**:\n\n1. **Tin nhắn nhanh chuyên nghiệp**: Cài đặt phím tắt `/` để gửi thông tin sản phẩm và chính sách bảo hành trong 1 giây.\n2. **Danh mục sản phẩm zBusiness**: Chia sẻ danh mục sản phẩm trực quan kèm hình ảnh và giá niêm yết rõ ràng.\n3. **Trả lời tự động ngoài giờ**: Duy trì phản hồi tức thì ngay cả khi bạn vắng mặt.\n4. **Ưu đãi định kỳ cho khách hàng thân thiết**: Gửi mã giảm giá hoặc thông báo khuyến mãi qua Zalo.\n5. **Hỏi thăm sau bán hàng**: Đặt lịch hẹn nhắc nhở chăm sóc khách sau 3 ngày mua hàng.",
                                         "💡 **5 Customer Engagement Ideas for Zalo**:\n\n1. Use quick message templates for 1-second replies.\n2. Share visual zBusiness product catalog cards.\n3. Enable auto-reply outside working hours.\n4. Reward loyal clients with exclusive promotions.\n5. Follow up with care reminders after purchases.")
                        }

                        CapCard {
                            cardIcon: "clock"
                            title: I18n.t("Lời cảm ơn đối tác & Khách hàng", "Partner Appreciation")
                            desc: I18n.t("Mẫu tin nhắn tri ân đối tác trang trọng", "Formal appreciation message")
                            prompt: I18n.t("Viết lời cảm ơn đối tác trang trọng", "Formal partner thank-you note")
                            body: I18n.t("🤝 **Mẫu lời cảm ơn đối tác trang trọng**:\n\nKính gửi Quý đối tác,\n\nChúng tôi xin gửi lời cảm ơn chân thành nhất đến Quý đơn vị vì sự tin tưởng và đồng hành quý báu trong suốt thời gian qua. Sự hợp tác của Quý vị là động lực lớn để chúng tôi không ngừng hoàn thiện dịch vụ và sản phẩm.\n\nKính chúc Quý đối tác luôn phát triển thịnh vượng và gặt hái thêm nhiều thành công mới!\n\nTrân trọng,\n[Tên của bạn / Doanh nghiệp]",
                                         "🤝 **Formal Partner Appreciation Message**:\n\nDear Partner,\n\nWe sincerely thank you for your continued trust and fruitful collaboration. We look forward to our ongoing shared success.\n\nBest regards,\n[Your Name / Business]")
                        }
                    }
                }
            }

            ListView {
                id: chatListView
                visible: kikiChatModel.count > 0
                anchors.fill: parent
                anchors.margins: 16
                clip: true
                spacing: 16
                model: kikiChatModel

                delegate: ColumnLayout {
                    id: msgDelegate
                    required property var model
                    required property int index
                    width: chatListView.width
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: msgDelegate.model.isUser ? Qt.AlignRight : Qt.AlignLeft
                        spacing: 10

                        Rectangle {
                            visible: !msgDelegate.model.isUser
                            Layout.alignment: Qt.AlignTop
                            implicitWidth: 32
                            implicitHeight: 32
                            radius: 16
                            color: ThemeManager.isDarkMode ? Qt.rgba(0.2, 0.4, 0.9, 0.3) : Theme.zaloBlueLight

                            ZIcon {
                                anchors.centerIn: parent
                                name: "kiki"
                                size: 18
                                color: Theme.zaloBlue
                            }
                        }

                        Rectangle {
                            id: bubbleBox
                            implicitWidth: Math.min(Math.max(80, bubbleContent.implicitWidth + 28), Math.min(chatListView.width * 0.78, 560))
                            implicitHeight: bubbleContent.implicitHeight + 20
                            radius: 12
                            color: msgDelegate.model.isUser ? Theme.zaloBlue : ThemeManager.surfaceColor
                            border.color: msgDelegate.model.isUser ? "transparent" : ThemeManager.borderColor
                            border.width: 1

                            ColumnLayout {
                                id: bubbleContent
                                anchors.centerIn: parent
                                width: parent.width - 24
                                spacing: 4

                                Text {
                                    id: bubbleText
                                    Layout.fillWidth: true
                                    text: msgDelegate.model.text
                                    wrapMode: Text.WordWrap
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    lineHeight: 1.35
                                    color: msgDelegate.model.isUser ? "#FFFFFF" : ThemeManager.textColor
                                }
                            }
                        }
                    }

                    RowLayout {
                        visible: !msgDelegate.model.isUser && !msgDelegate.model.isNotice
                        Layout.leftMargin: 42
                        spacing: 8

                        Rectangle {
                            implicitHeight: 26
                            implicitWidth: copyRow.implicitWidth + 18
                            radius: 13
                            color: copyMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.surfaceColor
                            border.color: ThemeManager.borderColor
                            border.width: 1

                            RowLayout {
                                id: copyRow
                                anchors.centerIn: parent
                                spacing: 5
                                ZIcon { name: "edit"; size: 13; color: ThemeManager.secondaryTextColor }
                                Text {
                                    text: I18n.t("Sao chép", "Copy")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: ThemeManager.secondaryTextColor
                                }
                            }
                            MouseArea {
                                id: copyMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: ZaloClient.copyText(msgDelegate.model.text)
                            }
                        }

                        Rectangle {
                            implicitHeight: 26
                            implicitWidth: sendChatRow.implicitWidth + 18
                            radius: 13
                            color: sendChatMouse.containsMouse ? (ThemeManager.isDarkMode ? Qt.rgba(0.18, 0.45, 0.75, 0.35) : Theme.zaloBlueLight)
                                                               : ThemeManager.surfaceColor
                            border.color: Theme.zaloBlue
                            border.width: 1

                            RowLayout {
                                id: sendChatRow
                                anchors.centerIn: parent
                                spacing: 5
                                ZIcon { name: "send"; size: 13; color: Theme.zaloBlue }
                                Text {
                                    text: I18n.t("Chèn vào chat", "Send to chat")
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    color: Theme.zaloBlue
                                    font.bold: true
                                }
                            }
                            MouseArea {
                                id: sendChatMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    ZaloClient.sendMessage(msgDelegate.model.text);
                                    ZaloClient.setCurrentNavigationTab("messages");
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 62
            color: ThemeManager.surfaceColor
            border.color: ThemeManager.borderColor
            border.width: 1

            RowLayout {
                width: Math.min(parent.width - 32, 680)
                anchors.centerIn: parent
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    radius: 20
                    color: ThemeManager.isDarkMode ? Qt.rgba(1, 1, 1, 0.06) : ThemeManager.backgroundColor
                    border.color: ThemeManager.borderColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 10
                        spacing: 8

                        TextField {
                            id: inputField
                            Layout.fillWidth: true
                            background: null
                            padding: 0
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            color: ThemeManager.textColor
                            placeholderText: I18n.t("Nhập yêu cầu hoặc câu hỏi... (Enter để gửi)", "Type a prompt or query... (Enter to send)")
                            placeholderTextColor: ThemeManager.secondaryTextColor
                            onAccepted: sendBtn.send()
                        }

                        Rectangle {
                            id: sendBtn
                            width: 28
                            height: 28
                            radius: 14
                            color: inputField.text.trim().length > 0 ? Theme.zaloBlue : ThemeManager.hoverClearColor
                            visible: inputField.text.trim().length > 0

                            function send(): void {
                                if (inputField.text.trim().length === 0) return;
                                const t = inputField.text.trim();
                                inputField.text = "";
                                root.askKiki(t);
                            }

                            ZIcon {
                                anchors.centerIn: parent
                                name: "arrow_up"
                                size: 14
                                color: "#FFFFFF"
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: sendBtn.send()
                            }
                        }
                    }
                }
            }
        }
    }
}
