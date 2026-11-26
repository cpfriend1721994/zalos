pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Zalos

Item {
    id: root
    anchors.fill: parent
    z: 230
    focus: true

    readonly property real pollId: ZaloClient.pollDialogId
    readonly property var poll: ZaloClient.pollRevision >= 0 ? ZaloClient.pollView(root.pollId) : ({})
    readonly property bool loaded: root.poll.loaded === true
    readonly property bool multi: root.poll.allow_multi_choices === true
    readonly property bool canVote: root.poll.can_vote === true
    readonly property bool hideResults: root.poll.hide_results === true
    readonly property var options: root.loaded ? root.poll.options : []
    readonly property bool canViewVoters: root.poll.can_view_voters === true
    readonly property bool canAddOption: root.poll.can_add_option === true
    readonly property bool canManage: root.poll.can_manage === true
    readonly property bool closed: root.poll.closed === true
    readonly property bool joined: root.poll.joined === true
    readonly property bool anonymous: root.poll.is_anonymous === true
    readonly property bool hidePreview: root.poll.is_hide_vote_preview === true
    readonly property string settingsVi: root.loaded && root.poll.settings_full_vi ? String(root.poll.settings_full_vi) : ""
    readonly property string settingsEn: root.loaded && root.poll.settings_full_en ? String(root.poll.settings_full_en) : ""
    readonly property bool creatorIsMe: root.poll.creator_is_me === true
    readonly property int voterCount: root.loaded ? Number(root.poll.voter_count) : 0
    readonly property real createdTime: root.loaded ? Number(root.poll.created_time) : 0
    readonly property string question: root.loaded ? String(root.poll.question) : ""
    readonly property string creatorName: root.loaded && root.poll.creator_name ? String(root.poll.creator_name) : ""
    readonly property string lockReason: root.loaded && root.poll.lock_reason ? String(root.poll.lock_reason) : ""
    readonly property int addedCount: root.addedTexts.length
    property list<int> selected: []
    property list<string> addedTexts: []
    property list<bool> addedVotes: []
    property bool dirty: false
    readonly property bool hasChange: root.addedTexts.length > 0 || !root.sameAsSaved()

    function sameAsSaved(): bool {
        const opts = root.options;
        let savedCount = 0;
        for (let i = 0; i < opts.length; ++i) {
            if (opts[i].voted !== true) continue;
            savedCount++;
            if (!root.isSelected(Number(opts[i].option_id))) return false;
        }
        return savedCount === root.selected.length;
    }
    property bool votersPage: ZaloClient.pollDialogVoters

    function syncFromPoll(): void {
        if (root.dirty || !root.loaded) return;
        const ids = [];
        const opts = root.options;
        for (let i = 0; i < opts.length; ++i) {
            if (opts[i].voted === true) ids.push(Number(opts[i].option_id));
        }
        root.selected = ids;
    }
    function isSelected(id: int): bool {
        for (let i = 0; i < root.selected.length; ++i) {
            if (root.selected[i] === id) return true;
        }
        return false;
    }
    function clearAddedVotes(): void {
        const next = [];
        for (let i = 0; i < root.addedVotes.length; ++i) next.push(false);
        root.addedVotes = next;
    }
    function toggle(id: int): void {
        if (!root.canVote) return;
        if (root.multi) {
            const next = [];
            let found = false;
            for (let i = 0; i < root.selected.length; ++i) {
                if (root.selected[i] === id) found = true;
                else next.push(root.selected[i]);
            }
            if (!found) next.push(id);
            root.selected = next;
        } else {
            root.selected = root.isSelected(id) ? [] : [id];
            root.clearAddedVotes();
        }
        root.dirty = true;
    }
    function toggleAdded(index: int): void {
        const next = [];
        for (let i = 0; i < root.addedVotes.length; ++i) {
            next.push(i === index ? !root.addedVotes[i] : (root.multi ? root.addedVotes[i] : false));
        }
        if (!root.multi && next[index]) root.selected = [];
        root.addedVotes = next;
        root.dirty = true;
    }
    function addOption(text: string): void {
        const value = text.trim();
        if (value.length === 0) return;
        const key = value.toLowerCase();
        const opts = root.options;
        for (let i = 0; i < opts.length; ++i) {
            if (String(opts[i].content).trim().toLowerCase() === key) {
                ZaloClient.notice(I18n.t("Phương án được thêm đã tồn tại", "That option already exists"));
                return;
            }
        }
        for (let j = 0; j < root.addedTexts.length; ++j) {
            if (root.addedTexts[j].toLowerCase() === key) {
                ZaloClient.notice(I18n.t("Phương án được thêm đã tồn tại", "That option already exists"));
                return;
            }
        }
        const texts = [];
        const votes = [];
        for (let k = 0; k < root.addedTexts.length; ++k) {
            texts.push(root.addedTexts[k]);
            votes.push(root.addedVotes[k]);
        }
        texts.push(value);
        votes.push(false);
        root.addedTexts = texts;
        root.addedVotes = votes;
        root.toggleAdded(texts.length - 1);
    }
    function removeAdded(index: int): void {
        const texts = [];
        const votes = [];
        for (let i = 0; i < root.addedTexts.length; ++i) {
            if (i === index) continue;
            texts.push(root.addedTexts[i]);
            votes.push(root.addedVotes[i]);
        }
        root.addedTexts = texts;
        root.addedVotes = votes;
        root.dirty = true;
    }
    function submit(): void {
        if (root.selected.length === 0 && root.addedTexts.length === 0 && !root.joined) return;
        ZaloClient.submitPollChoices(root.pollId, root.selected, root.addedTexts, root.addedVotes);
        root.dirty = false;
        root.addedTexts = [];
        root.addedVotes = [];
        ZaloClient.closePollDialog();
    }
    function requestClose(): void {
        if (root.hasChange && root.canVote) discardConfirm.open();
        else ZaloClient.closePollDialog();
    }
    function reasonText(): string {
        if (root.lockReason === "closed") return I18n.t("Bình chọn đã đóng", "This poll is closed");
        if (root.lockReason === "expired") return I18n.t("Bình chọn đã hết hạn", "This poll has ended");
        if (root.lockReason === "after_vote") return I18n.t("Không thể thay đổi bình chọn", "You can't change your vote");
        return "";
    }

    onPollChanged: root.syncFromPoll()
    Component.onCompleted: {
        root.syncFromPoll();
        root.forceActiveFocus();
    }
    Keys.onEscapePressed: root.requestClose()

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.5

        MouseArea {
            anchors.fill: parent
            onClicked: root.requestClose()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        height: Math.min(root.height - 40, 640)
        radius: 12
        color: ThemeManager.surfaceColor
        border.color: ThemeManager.borderColor
        border.width: 1

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    visible: root.votersPage
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 14
                    color: backMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                    ZIcon {
                        anchors.centerIn: parent
                        name: "arrow_left"
                        size: 16
                        color: ThemeManager.secondaryTextColor
                    }
                    MouseArea {
                        id: backMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.votersPage = false
                    }
                }

                ZIcon {
                    visible: !root.votersPage
                    name: "poll"
                    size: 20
                    color: Theme.zaloBlue
                }

                Text {
                    Layout.fillWidth: true
                    text: root.votersPage
                          ? I18n.t(root.voterCount + " người bình chọn", root.voterCount + " voters")
                          : I18n.t("Bình chọn", "Poll")
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: ThemeManager.textColor
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
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
                        onClicked: root.requestClose()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: ThemeManager.dividerColor
            }

            Text {
                visible: !root.loaded
                Layout.fillWidth: true
                Layout.fillHeight: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: I18n.t("Đang tải bình chọn…", "Loading poll…")
                font.family: Theme.fontFamily
                font.pixelSize: 13
                color: ThemeManager.secondaryTextColor
            }

            StackLayout {
                visible: root.loaded
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.votersPage ? 1 : 0

                ScrollView {
                    id: voteScroll
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: voteScroll.availableWidth
                        spacing: 10

                        Text {
                            id: pollQuestionText
                            Layout.fillWidth: true
                            text: ZaloClient.linkifyText(root.question)
                            textFormat: Text.StyledText
                            linkColor: Theme.linkColor
                            onLinkActivated: function(link) { ZaloClient.openLink(link); }
                            HoverHandler { cursorShape: pollQuestionText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                            wrapMode: Text.Wrap
                            font.family: Theme.fontFamily
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: ThemeManager.textColor
                        }

                        Text {
                            visible: root.creatorName.length > 0
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: I18n.t("Tạo bởi ", "Created by ")
                                  + (root.creatorIsMe ? I18n.t("Bạn", "you") : root.creatorName)
                                  + (root.createdTime > 0
                                     ? " · " + Qt.formatDateTime(new Date(root.createdTime), "dd/MM/yyyy") : "")
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }

                        Text {
                            Layout.fillWidth: true
                            text: I18n.t(root.settingsVi, root.settingsEn)
                            elide: Text.ElideRight
                            wrapMode: Text.Wrap
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            color: ThemeManager.secondaryTextColor
                        }

                        Text {
                            visible: root.voterCount > 0
                            text: I18n.t(root.voterCount + " người bình chọn", root.voterCount + " voters")
                                  + (root.canViewVoters ? "  ›" : "")
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.underline: dlgVotersMouse.containsMouse && root.canViewVoters
                            color: root.canViewVoters ? Theme.zaloBlue : ThemeManager.secondaryTextColor
                            MouseArea {
                                id: dlgVotersMouse
                                anchors.fill: parent
                                enabled: root.canViewVoters
                                hoverEnabled: true
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.votersPage = true
                            }
                        }

                        Rectangle {
                            id: reasonBox
                            readonly property string message: root.reasonText().length > 0 ? root.reasonText()
                                : (root.hidePreview && !root.joined
                                   ? I18n.t("Bạn chỉ thấy kết quả sau khi bình chọn và không thể thay đổi lựa chọn.",
                                            "You'll see the results after voting, and you can't change your vote.")
                                   : "")
                            visible: message.length > 0
                            Layout.fillWidth: true
                            implicitHeight: reasonLbl.implicitHeight + 16
                            radius: 8
                            color: ThemeManager.inputFillColor
                            Text {
                                id: reasonLbl
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: 10
                                text: reasonBox.message
                                wrapMode: Text.Wrap
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: ThemeManager.textColor
                            }
                        }

                        Repeater {
                            model: root.options
                            delegate: Rectangle {
                                id: optRow
                                required property var modelData
                                readonly property int optionId: Number(optRow.modelData.option_id)
                                readonly property bool voted: optRow.modelData.voted === true
                                readonly property real weight: Number(optRow.modelData.weight)
                                readonly property bool chosen: root.isSelected(optRow.optionId)
                                Layout.fillWidth: true
                                Layout.preferredHeight: 42
                                radius: 8
                                color: ThemeManager.inputFillColor
                                border.color: optRow.chosen ? Theme.zaloBlue : ThemeManager.inputFillColor
                                border.width: 1
                                clip: true

                                Rectangle {
                                    visible: !root.hideResults
                                    width: parent.width * optRow.weight
                                    height: parent.height
                                    radius: 8
                                    color: Theme.zaloBlueLight
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8

                                    ZCheckBox {
                                        visible: root.canVote && root.multi
                                        checked: optRow.chosen
                                        onToggled: root.toggle(optRow.optionId)
                                    }
                                    ZRadio {
                                        visible: root.canVote && !root.multi
                                        checked: optRow.chosen
                                        onClicked: root.toggle(optRow.optionId)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: optRow.modelData.content
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        color: ThemeManager.textColor
                                    }
                                    ZIcon {
                                        visible: !root.canVote && optRow.voted
                                        name: "check"
                                        size: 14
                                        color: Theme.zaloBlue
                                    }
                                    Row {
                                        visible: !root.hideResults && !root.anonymous
                                        spacing: -6
                                        Repeater {
                                            model: optRow.modelData.preview || []
                                            delegate: ZAvatar {
                                                required property var modelData
                                                size: 20
                                                avatarUrl: modelData.avatar || ""
                                                name: modelData.name || ""
                                            }
                                        }
                                    }
                                    Text {
                                        visible: !root.hideResults
                                        text: optRow.modelData.votes + " · " + optRow.modelData.percent + "%"
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        color: ThemeManager.secondaryTextColor
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    z: -1
                                    enabled: root.canVote
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: root.toggle(optRow.optionId)
                                }
                            }
                        }

                        Repeater {
                            model: root.addedTexts
                            delegate: Rectangle {
                                id: addedRow
                                required property string modelData
                                required property int index
                                readonly property bool voted: root.addedVotes[addedRow.index] === true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 42
                                radius: 8
                                color: ThemeManager.inputFillColor
                                border.color: addedRow.voted ? Theme.zaloBlue : ThemeManager.borderColor
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 6
                                    spacing: 8
                                    ZCheckBox {
                                        visible: root.multi
                                        checked: addedRow.voted
                                        onToggled: root.toggleAdded(addedRow.index)
                                    }
                                    ZRadio {
                                        visible: !root.multi
                                        checked: addedRow.voted
                                        onClicked: root.toggleAdded(addedRow.index)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: addedRow.modelData + I18n.t("  (mới)", "  (new)")
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 13
                                        color: ThemeManager.textColor
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 26
                                        Layout.preferredHeight: 26
                                        radius: 13
                                        color: removeAddedMouse.containsMouse ? ThemeManager.hoverColor : ThemeManager.hoverClearColor
                                        ZIcon {
                                            anchors.centerIn: parent
                                            name: "close"
                                            size: 14
                                            color: ThemeManager.secondaryTextColor
                                        }
                                        MouseArea {
                                            id: removeAddedMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.removeAdded(addedRow.index)
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            id: addOptRow
                            visible: root.canAddOption
                            Layout.fillWidth: true
                            spacing: 8
                            function submitNew(): void {
                                Qt.inputMethod.commit();
                                root.addOption(newOptionInput.text);
                                newOptionInput.text = "";
                            }
                            ZTextField {
                                id: newOptionInput
                                Layout.fillWidth: true
                                maximumLength: 120
                                placeholderText: I18n.t("Thêm phương án", "Add an option")
                                onAccepted: addOptRow.submitNew()
                            }
                            ZButton {
                                variant: "secondary"
                                text: I18n.t("Thêm", "Add")
                                enabled: newOptionInput.length > 0
                                onClicked: addOptRow.submitNew()
                            }
                        }
                    }
                }

                Item {
                    id: votersPagePane

                    Text {
                        visible: !root.canViewVoters
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        wrapMode: Text.Wrap
                        text: root.anonymous
                              ? I18n.t("Người tham gia không thể thấy người khác bình chọn.", "Participants can't see who voted.")
                              : I18n.t("Bạn chỉ thấy người bình chọn sau khi đã bình chọn.", "You'll see the voters after you vote.")
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        color: ThemeManager.secondaryTextColor
                    }

                    ListView {
                        id: votersList
                        visible: root.canViewVoters
                        anchors.fill: parent
                        clip: true
                        cacheBuffer: 160
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        model: {
                            if (!root.canViewVoters || !root.options) return null;
                            const rows = [];
                            for (let i = 0; i < root.options.length; ++i) {
                                const opt = root.options[i];
                                rows.push({ kind: "header", label: String(opt.content || "") + " · " + Number(opt.votes || 0), avatar: "", isMe: false });
                                const voters = opt.voters || [];
                                if (voters.length === 0) {
                                    rows.push({ kind: "empty", label: I18n.t("Chưa có ai chọn", "No votes yet"), avatar: "", isMe: false });
                                } else {
                                    for (let j = 0; j < voters.length; ++j) {
                                        const v = voters[j];
                                        rows.push({ kind: "voter", label: String(v.name || ""), avatar: String(v.avatar || ""), isMe: v.isMe === true });
                                    }
                                }
                            }
                            return rows;
                        }
                        delegate: Item {
                            id: voterEntry
                            required property var modelData
                            readonly property string kind: String(voterEntry.modelData.kind || "voter")
                            width: votersList.width
                            height: kind === "header" ? 30 : (kind === "empty" ? 24 : 36)

                            Text {
                                visible: voterEntry.kind === "header"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 4
                                text: voterEntry.modelData.label
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: ThemeManager.textColor
                            }
                            Text {
                                visible: voterEntry.kind === "empty"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: voterEntry.modelData.label
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                color: ThemeManager.secondaryTextColor
                            }
                            RowLayout {
                                visible: voterEntry.kind === "voter"
                                anchors.fill: parent
                                spacing: 10
                                ZAvatar {
                                    size: 28
                                    avatarUrl: voterEntry.modelData.avatar
                                    name: voterEntry.modelData.label
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: voterEntry.modelData.label + (voterEntry.modelData.isMe ? I18n.t(" (Bạn)", " (You)") : "")
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 13
                                    color: ThemeManager.textColor
                                }
                            }
                        }
                    }
                }
            }

            Flow {
                visible: root.loaded && !root.votersPage
                Layout.fillWidth: true
                spacing: 8

                ZButton {
                    visible: root.canManage && !root.closed
                    variant: "secondary"
                    text: I18n.t("Khóa bình chọn", "Close poll")
                    onClicked: lockConfirm.open()
                }
                ZButton {
                    variant: "secondary"
                    text: I18n.t("Gửi vào nhóm", "Share to group")
                    onClicked: ZaloClient.sharePoll(root.pollId)
                }
                ZButton {
                    variant: "secondary"
                    text: root.canVote ? I18n.t("Hủy", "Cancel") : I18n.t("Đóng", "Close")
                    onClicked: root.requestClose()
                }
                ZButton {
                    visible: root.canVote
                    variant: "primary"
                    text: I18n.t("Bình chọn", "Vote")
                    enabled: root.hasChange
                    onClicked: root.submit()
                }
            }
        }
    }

    ZConfirmDialog {
        id: lockConfirm
        title: I18n.t("Khóa bình chọn?", "Close this poll?")
        okText: I18n.t("Khóa bình chọn", "Close poll")
        danger: true
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: I18n.t("Sau khi khóa, mọi người sẽ không thể bình chọn nữa.", "Once closed, nobody can vote anymore.")
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: ThemeManager.textColor
        }
        onAccepted: ZaloClient.lockPoll(root.pollId)
    }

    ZConfirmDialog {
        id: discardConfirm
        title: I18n.t("Thoát bình chọn?", "Leave the poll?")
        okText: I18n.t("Thoát", "Leave")
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: I18n.t("Bạn chưa lưu bình chọn của mình. Thoát bình chọn?", "Your vote isn't saved yet. Leave anyway?")
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: ThemeManager.textColor
        }
        onAccepted: {
            root.dirty = false;
            ZaloClient.closePollDialog();
        }
    }
}
