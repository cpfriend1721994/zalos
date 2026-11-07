#include <QtTest>
#include <QDateTime>
#include "models/conversation_model.h"
#include "models/message_model.h"
#include "models/contact_model.h"
#include "models/text_fold.h"

using namespace Zalos::Core::Models;

class TestModels : public QObject {
    Q_OBJECT

private slots:
    // Vietnamese search ignores case and diacritics, including đ.
    void testAccentInsensitiveSearch() {
        QCOMPARE(foldForSearch(QStringLiteral("Nguyễn Đức Thắng")), QStringLiteral("nguyen duc thang"));

        ConversationModel convs;
        ConversationItem a; a.id = "c1"; a.name = QStringLiteral("Nhóm Đi Học");
        ConversationItem b; b.id = "c2"; b.name = QStringLiteral("Fixture Peer");
        convs.setConversations({a, b});
        convs.setSearchFilter("di hoc");
        QCOMPARE(convs.rowCount(), 1);
        convs.setSearchFilter("");

        MessageModel msgs;
        msgs.loadConversation("c1");
        MessageItem m1; m1.id = "1"; m1.text = QStringLiteral("Hẹn gặp lúc tám giờ"); m1.ts = 1;
        MessageItem m2; m2.id = "2"; m2.text = QStringLiteral("ok"); m2.ts = 2;
        msgs.mergeMessages("c1", {m1, m2});
        QCOMPARE(msgs.searchMessages("HEN GAP").size(), 1);
        QCOMPARE(msgs.searchMessages("khong co").size(), 0);
    }

    // The server copy of a sent message replaces its local echo by cliMsgId, never by text.
    void testLocalEchoBridgedByCliMsgId() {
        MessageModel model;
        model.loadConversation("t");
        MessageItem echo1; echo1.id = "local-1-1"; echo1.cliMsgId = "1700000000001"; echo1.text = "ok"; echo1.ts = 10;
        echo1.isMine = true; echo1.status = "sending";
        MessageItem echo2; echo2.id = "local-1-2"; echo2.cliMsgId = "1700000000002"; echo2.text = "ok"; echo2.ts = 11;
        echo2.isMine = true; echo2.status = "sending";
        model.mergeMessages("t", {echo1, echo2});

        MessageItem server; server.id = "5002"; server.cliMsgId = "1700000000002"; server.text = "ok"; server.ts = 11;
        server.isMine = true;
        model.mergeMessages("t", {server});
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.contains("t", "local-1-1"));  // same text, different client id: untouched
        QVERIFY(model.contains("t", "5002"));
        QVERIFY(!model.contains("t", "local-1-2"));
        QCOMPARE(model.data(model.index(1), MessageModel::StatusRole).toString(), QString("sent"));

        // History cursors skip local echoes.
        QCOMPARE(model.oldestMessageId("t"), QString("5002"));
        QCOMPARE(model.latestMessageId("t"), QString("5002"));
    }

    void testResyncDedupesByCliMsgId() {
        MessageModel model;
        model.loadConversation("t");
        MessageItem synced; synced.id = "1700000000009"; synced.cliMsgId = "1700000000009"; synced.text = "x";
        synced.ts = 5; synced.isMine = true;
        model.mergeMessages("t", {synced});
        MessageItem server = synced; server.id = "9001";
        model.mergeMessages("t", {server});
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.contains("t", "9001"));
    }

    void testEmoticonShortcodes() {
        QVERIFY(MessageModel::linkify(QStringLiteral("hay :v")).contains(QStringLiteral("qrc:/Zalos/assets/icons/pacman.png")));
        QVERIFY(MessageModel::linkify(QStringLiteral("hay :V")).contains(QStringLiteral("qrc:/Zalos/assets/icons/pacman.png")));
        QVERIFY(MessageModel::linkify(QStringLiteral("peace :v:")).contains(QString::fromUtf8("✌️")));
        QVERIFY(MessageModel::linkify(QStringLiteral("haha =))")).contains(QString::fromUtf8("🤣")));
        QVERIFY(MessageModel::linkify(QStringLiteral("12:30")).contains(QStringLiteral("12:30")));
        QVERIFY(MessageModel::linkify(QStringLiteral("https://a.b:8080/v")).contains(QStringLiteral("8080/v")));
    }

    void testReadPointsAndConversationActions() {
        MessageModel msgs;
        MessageItem mine; mine.id = "100"; mine.isMine = true; mine.ts = 1;
        MessageItem in1; in1.id = "101"; in1.ts = 2;
        MessageItem in2; in2.id = "102"; in2.ts = 3;
        msgs.mergeMessages("t", {mine, in1, in2});
        QCOMPARE(msgs.countIncomingAfter("t", "100"), 2);
        QCOMPARE(msgs.countIncomingAfter("t", "101"), 1);
        QCOMPARE(msgs.countIncomingAfter("t", "102"), 0);

        ConversationModel convs;
        ConversationItem c; c.id = "t";
        convs.setConversations({c});
        convs.setUnreadCount("t", 2);
        QCOMPARE(convs.totalUnreadCount(), 2);
        convs.removeConversation("t");
        QCOMPARE(convs.rowCount(), 0);
        QCOMPARE(convs.totalUnreadCount(), 0);

        msgs.clearThread("t");
        QCOMPARE(msgs.threadSize("t"), 0);
    }

    void testQuoteRoles() {
        MessageModel model;
        model.loadConversation("t");
        MessageItem m; m.id = "1"; m.text = "reply"; m.quoteMsgId = "0"; m.quoteSender = "Fixture"; m.quoteText = "orig";
        model.mergeMessages("t", {m});
        QCOMPARE(model.data(model.index(0), MessageModel::QuoteTextRole).toString(), QString("orig"));
        QCOMPARE(model.data(model.index(0), MessageModel::QuoteSenderRole).toString(), QString("Fixture"));
    }

    void testForwardedAndExpireRoles() {
        MessageModel model;
        model.loadConversation("t");
        MessageItem m;
        m.id = "f1";
        m.text = "test";
        m.isForwarded = true;
        m.isE2EE = true;
        m.expireOn = 1800000000000LL;
        model.mergeMessages("t", {m});
        QCOMPARE(model.data(model.index(0), MessageModel::IsForwardedRole).toBool(), true);
        QCOMPARE(model.data(model.index(0), MessageModel::IsE2EERole).toBool(), true);
        QCOMPARE(model.data(model.index(0), MessageModel::ExpireOnRole).toLongLong(), 1800000000000LL);
    }

    // Zalo-style presentation roles: runs by sender (30 min), date pills, status only on the newest own message.
    void testPresentationRoles() {
        MessageModel model;
        model.loadConversation("t");
        const qint64 day = QDateTime(QDate(2026, 11, 2), QTime(9, 0)).toMSecsSinceEpoch();
        MessageItem a; a.id = "1"; a.senderId = "u"; a.ts = day;
        MessageItem b; b.id = "2"; b.senderId = "u"; b.ts = day + 60000;                 // same run
        MessageItem c; c.id = "3"; c.senderId = "u"; c.ts = day + 60000 + 31 * 60000;   // > 30 min: new run
        MessageItem d; d.id = "4"; d.isMine = true; d.senderId = "me"; d.ts = day + 86400000; d.status = "sent";
        MessageItem e; e.id = "5"; e.isMine = true; e.senderId = "me"; e.ts = day + 86400000 + 1000; e.status = "seen";
        model.mergeMessages("t", {a, b, c, d, e});
        auto role = [&](int row, int r) { return model.data(model.index(row), r).toBool(); };
        QVERIFY(role(0, MessageModel::FirstInGroupRole));
        QVERIFY(!role(0, MessageModel::LastInGroupRole));
        QVERIFY(!role(1, MessageModel::FirstInGroupRole));
        QVERIFY(role(1, MessageModel::LastInGroupRole));
        QVERIFY(role(2, MessageModel::FirstInGroupRole));
        QVERIFY(role(0, MessageModel::DateSeparatorRole));
        QVERIFY(!role(1, MessageModel::DateSeparatorRole));
        QVERIFY(role(3, MessageModel::DateSeparatorRole));   // next day
        QVERIFY(!role(3, MessageModel::ShowStatusRole));     // an older own message
        QVERIFY(role(4, MessageModel::ShowStatusRole));      // the newest own message
        QVERIFY(!role(0, MessageModel::ShowStatusRole));     // never on incoming
    }

    void testLinkifyEscapesAndLinks() {
        const QString html = MessageModel::linkify("xem https://example.com/a?b=1, và <b>đậm</b>\nwww.example.org.");
        QVERIFY(html.contains("<a href=\"https://example.com/a?b=1\">https://example.com/a?b=1</a>,"));
        QVERIFY(html.contains("&lt;b&gt;đậm&lt;/b&gt;"));     // user text is never interpreted as HTML
        QVERIFY(html.contains("<br>"));
        QVERIFY(html.contains("<a href=\"https://www.example.org\">www.example.org</a>."));
    }

    void testMediaItemsForViewer() {
        MessageModel model;
        model.loadConversation("t");
        MessageItem p1; p1.id = "1"; p1.ts = 1; p1.messageType = "image"; p1.mediaUrl = "file:///thumb1.png"; p1.href = "file:///full1.png";
        MessageItem t1; t1.id = "2"; t1.ts = 2; t1.text = "hi";
        MessageItem p2; p2.id = "3"; p2.ts = 3; p2.messageType = "image"; p2.mediaUrl = "file:///only-thumb.png";
        MessageItem v1; v1.id = "4"; v1.ts = 4; v1.messageType = "video"; v1.mediaUrl = "file:///vthumb.png"; v1.href = "file:///clip.mp4";
        MessageItem gone; gone.id = "5"; gone.ts = 5; gone.messageType = "image"; gone.mediaUrl = "x"; gone.isRecalled = true;
        model.mergeMessages("t", {p1, t1, p2, v1, gone});
        const QVariantList items = model.mediaItems();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).toMap().value("url").toString(), QString("file:///full1.png")); // original preferred
        QCOMPARE(items.at(0).toMap().value("isVideo").toBool(), false);
        QCOMPARE(items.at(1).toMap().value("url").toString(), QString("file:///only-thumb.png"));
        QCOMPARE(items.at(2).toMap().value("url").toString(), QString("file:///clip.mp4"));
        QCOMPARE(items.at(2).toMap().value("isVideo").toBool(), true);
    }

    void testConversationModelInitial() {
        ConversationModel model;
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.totalUnreadCount(), 0);

        model.addConversation({
            "conv-1", "Hội bạn", "Xin chào", "10:00", 2, true, false, "", "priority", ""
        });
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.totalUnreadCount(), 2);
    }

    void testConversationClearUnread() {
        ConversationModel model;
        model.addConversation({
            "conv-1", "Hội bạn", "Xin chào", "10:00", 2, true, false, "", "priority", ""
        });
        QCOMPARE(model.totalUnreadCount(), 2);
        model.clearUnread("conv-1");
        QCOMPARE(model.totalUnreadCount(), 0);
    }

    void testMessageModelAdd() {
        MessageModel model;
        int countBefore = model.rowCount();
        model.addMessage("Xin chào từ bộ kiểm thử tự động", true);
        QCOMPARE(model.rowCount(), countBefore + 1);

        QModelIndex lastIdx = model.index(countBefore);
        QCOMPARE(model.data(lastIdx, MessageModel::TextRole).toString(), "Xin chào từ bộ kiểm thử tự động");
        QCOMPARE(model.data(lastIdx, MessageModel::IsMineRole).toBool(), true);
    }

    void testMessageSearchAndGetById() {
        MessageModel model;
        model.addMessageDirect("conv-1", "m1", "Alice", "Xin chào mọi người", "10:00", false, false);
        model.addMessageDirect("conv-1", "m2", "Bob", "Hôm nay thời tiết đẹp quá", "10:01", false, false);
        model.addMessageDirect("conv-1", "m3", "Tôi", "Hẹn gặp mọi người vào chiều nay nhé", "10:02", true, false);

        model.loadConversation("conv-1");
        QCOMPARE(model.rowCount(), 3);

        // Search messages
        QList<int> search1 = model.searchMessages("mọi người");
        QCOMPARE(search1.size(), 2);
        QCOMPARE(search1.at(0), 0);
        QCOMPARE(search1.at(1), 2);

        QList<int> search2 = model.searchMessages("thời tiết");
        QCOMPARE(search2.size(), 1);
        QCOMPARE(search2.at(0), 1);

        QList<int> searchEmpty = model.searchMessages("không tồn tại");
        QCOMPARE(searchEmpty.size(), 0);

        // Get by ID
        QVariantMap item = model.getMessageById("m2");
        QCOMPARE(item.value("id").toString(), "m2");
        QCOMPARE(item.value("senderName").toString(), "Bob");
        QCOMPARE(item.value("text").toString(), "Hôm nay thời tiết đẹp quá");
    }

    void testContactModelFilteringAndCounts() {
        ContactModel model;
        QCOMPARE(model.rowCount(), 0);

        ContactItem c1{"u1", "Đới Chàn", "https://zadn.vn/ava1.jpg", true, false, 0, "friends"};
        ContactItem c2{"u2", "Nguyễn Văn A", "https://zadn.vn/ava2.jpg", false, false, 0, "friends"};
        ContactItem c3{"g1", "Nhóm Gia Đình", "https://zadn.vn/grp1.jpg", true, true, 5, "groups"};
        ContactItem c4{"u3", "Người Lạ", "", false, false, 0, "invites"};

        model.setContacts({c1, c2, c3, c4});
        QCOMPARE(model.friendCount(), 2);
        QCOMPARE(model.groupCount(), 1);
        QCOMPARE(model.inviteCount(), 1);

        // Default category filter is "friends"
        QCOMPARE(model.rowCount(), 2);

        // Filter by search query
        model.setSearchFilter("Đới");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ContactModel::NameRole).toString(), "Đới Chàn");

        // Clear search
        model.setSearchFilter("");
        QCOMPARE(model.rowCount(), 2);

        // Switch to "groups"
        model.setCategoryFilter("groups");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ContactModel::NameRole).toString(), "Nhóm Gia Đình");

        // Switch to "invites"
        model.setCategoryFilter("invites");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ContactModel::NameRole).toString(), "Người Lạ");
    }

    void testPollMessageAndVoting() {
        MessageModel model;
        MessageItem pollItem;
        pollItem.id = "poll1";
        pollItem.messageType = "poll";
        pollItem.text = "Trưa nay ăn gì?";
        pollItem.ts = 1000;

        QVariantMap pData;
        pData["poll_id"] = 12345LL;
        pData["question"] = "Trưa nay ăn gì?";
        pData["closed"] = false;
        pData["num_vote"] = 0;
        pData["allow_multi_choices"] = false;

        QVariantList optList;
        QVariantMap opt1{{"option_id", 1}, {"content", "Cơm tấm"}, {"votes", 0}, {"voted", false}};
        QVariantMap opt2{{"option_id", 2}, {"content", "Phở bò"}, {"votes", 0}, {"voted", false}};
        optList << opt1 << opt2;
        pData["options"] = optList;
        pollItem.pollData = pData;

        model.mergeMessages("g1", {pollItem});
        model.loadConversation("g1");
        QCOMPARE(model.rowCount(), 1);

        QVariantMap dataBefore = model.data(model.index(0), MessageModel::PollDataRole).toMap();
        QCOMPARE(dataBefore.value("poll_id").toLongLong(), 12345LL);
        QCOMPARE(dataBefore.value("num_vote").toInt(), 0);

        // Vote on option 1
        model.updatePollVote("g1", 12345LL, 1, true);
        QVariantMap dataAfter = model.data(model.index(0), MessageModel::PollDataRole).toMap();
        QCOMPARE(dataAfter.value("num_vote").toInt(), 1);
        QVariantList optsAfter = dataAfter.value("options").toList();
        QCOMPARE(optsAfter.at(0).toMap().value("voted").toBool(), true);
        QCOMPARE(optsAfter.at(0).toMap().value("votes").toInt(), 1);
    }

    // Active conversation lives exclusively in m_items while open (no duplicate QVector in m_store),
    // and swaps cleanly into/out of m_store across conversation switches.
    void testSingleBufferActiveConversation() {
        MessageModel model;
        MessageItem a1; a1.id = "101"; a1.text = "hello c1"; a1.ts = 10;
        MessageItem b1; b1.id = "201"; b1.text = "hello c2"; b1.ts = 20;
        model.mergeMessages("c1", {a1});
        model.mergeMessages("c2", {b1});
        QCOMPARE(model.threadSize("c1"), 1);
        QCOMPARE(model.threadSize("c2"), 1);

        model.loadConversation("c1");
        QVERIFY(!model.isActiveThreadDuplicatedForTesting());
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.threadSize("c1"), 1);

        // Mutating the open thread never re-inserts a duplicate copy into m_store.
        MessageItem a2; a2.id = "102"; a2.text = "second c1"; a2.ts = 11;
        model.mergeMessages("c1", {a2});
        model.setReaction("c1", "101", "❤️ 1");
        model.setStatus("c1", "102", "seen");
        QVERIFY(!model.isActiveThreadDuplicatedForTesting());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.threadSize("c1"), 2);

        // Switching to c2 moves c1 into m_store and takes c2 into m_items.
        model.loadConversation("c2");
        QVERIFY(!model.isActiveThreadDuplicatedForTesting());
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.threadSize("c1"), 2);
        QCOMPARE(model.threadSize("c2"), 1);
        QCOMPARE(model.message("c1", "101").reaction, QString("❤️ 1"));

        // Switching back to c1 restores both messages without loss.
        model.loadConversation("c1");
        QVERIFY(!model.isActiveThreadDuplicatedForTesting());
        QCOMPARE(model.rowCount(), 2);
    }
};

QTEST_MAIN(TestModels)
#include "test_models.moc"

