#include <QtTest>
#include <QDateTime>
#include <QStandardPaths>
#include "models/conversation_model.h"
#include "models/message_model.h"
#include "models/contact_model.h"
#include "models/text_fold.h"
#include "models/emoji_catalog.h"

using namespace Zalos::Core::Models;

class TestModels : public QObject {
    Q_OBJECT

private slots:
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
        QVERIFY(model.contains("t", "local-1-1"));
        QVERIFY(model.contains("t", "5002"));
        QVERIFY(!model.contains("t", "local-1-2"));
        QCOMPARE(model.data(model.index(1), MessageModel::StatusRole).toString(), QString("sent"));

        QCOMPARE(model.oldestMessageId("t"), QString("5002"));
        QCOMPARE(model.latestMessageId("t"), QString("5002"));
    }

    void testSameBatchCliMsgIdDuplicatesInOpenThread() {
        MessageModel model;
        model.loadConversation("t");
        MessageItem first; first.id = "100"; first.cliMsgId = "1700000000100"; first.senderId = "u1"; first.ts = 1;
        model.mergeMessages("t", {first});
        QCOMPARE(model.rowCount(), 1);

        MessageItem phoneCopy; phoneCopy.id = "1700000000200"; phoneCopy.cliMsgId = "1700000000200";
        phoneCopy.senderId = "u1"; phoneCopy.text = "fixture"; phoneCopy.ts = 2;
        MessageItem serverCopy = phoneCopy; serverCopy.id = "9200";
        model.mergeMessages("t", {phoneCopy, serverCopy});
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.contains("t", "9200"));
        QVERIFY(!model.contains("t", "1700000000200"));
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

    void testLinkDetectionCoversBareDomainsAndStyles() {
        const auto links = MessageModel::findLinks(QStringLiteral("xem vt.example.com/abc, fb.watch/x1 va https://a.example.vn/p?q=1."));
        QCOMPARE(links.size(), 3);
        QCOMPARE(links.at(0).href, QStringLiteral("https://vt.example.com/abc"));
        QCOMPARE(links.at(1).href, QStringLiteral("https://fb.watch/x1"));
        QCOMPARE(links.at(2).href, QStringLiteral("https://a.example.vn/p?q=1"));
        QVERIFY(MessageModel::findLinks(QStringLiteral("fixture@example.com")).isEmpty());
        QVERIFY(MessageModel::findLinks(QStringLiteral("ver 1.2.3 and file.txt")).isEmpty());
        QVERIFY(MessageModel::findLinks(QStringLiteral("example.community")).isEmpty());
        QCOMPARE(MessageModel::linkifyPlain(QStringLiteral("a <b> example.com")),
                 QStringLiteral("a &lt;b&gt; <a href=\"https://example.com\">example.com</a>"));

        const QString text = QStringLiteral("go https://example.com/long/path now");
        const QVariantList styles{QVariantMap{{"start", 12}, {"len", 6}, {"st", "b"}}};
        const QString html = MessageModel::styledHtml(text, {}, styles);
        QCOMPARE(html.count(QStringLiteral("href=\"https://example.com/long/path\"")), 3);
        QVERIFY(!html.contains(QStringLiteral("href=\"https://example.com/lo\"")));
    }

    void testEmoticonShortcodes() {
        QVERIFY(MessageModel::linkify(QStringLiteral("hay :v")).contains(QStringLiteral("width=\"18\" height=\"18\"")));
        QVERIFY(MessageModel::linkify(QStringLiteral("hay :V")).contains(QStringLiteral("qrc:/Zalos/assets/icons/pacman.png")));
        QVERIFY(MessageModel::linkify(QStringLiteral("peace :v:")).contains(QString::fromUtf8("✌️")));
        QVERIFY(MessageModel::linkify(QStringLiteral("haha =))")).contains(QString::fromUtf8("🤣")));
        QVERIFY(MessageModel::linkify(QStringLiteral("gui /-loveu")).contains(QString::fromUtf8("🫶")));
        QVERIFY(MessageModel::linkify(QStringLiteral("vo tay :handclap")).contains(QString::fromUtf8("👏")));
        QVERIFY(MessageModel::linkify(QStringLiteral("tang /-rose va /-coffee")).contains(QString::fromUtf8("🌹")));
        QVERIFY(MessageModel::linkify(QStringLiteral("tang /-rose va /-coffee")).contains(QString::fromUtf8("☕")));
        QVERIFY(MessageModel::linkify(QStringLiteral("ok /-strong")).contains(QString::fromUtf8("👍")));
        QVERIFY(MessageModel::linkify(QStringLiteral("cam on /-thanks /-showlove")).contains(QString::fromUtf8("🙏")));
        QVERIFY(MessageModel::linkify(QStringLiteral("12:30")).contains(QStringLiteral("12:30")));
        QVERIFY(MessageModel::linkify(QStringLiteral("https://a.b:8080/v")).contains(QStringLiteral("8080/v")));
        QVERIFY(!MessageModel::linkify(QStringLiteral(":))))")).contains(QString::fromUtf8("😆")));
        QVERIFY(!MessageModel::linkify(QStringLiteral("=))))")).contains(QString::fromUtf8("🤣")));
        QVERIFY(!MessageModel::linkify(QStringLiteral("(:))")).contains(QString::fromUtf8("🤣")));
        QVERIFY(!MessageModel::linkify(QStringLiteral(":))!")).contains(QString::fromUtf8("🤣")));
        QVERIFY(MessageModel::linkify(QStringLiteral("hi :))) ok")).contains(QString::fromUtf8("😆")));
        QVERIFY(MessageModel::linkify(QStringLiteral("hi =)) ok")).contains(QString::fromUtf8("🤣")));
    }

    void testGroupSeenRecording() {
        MessageModel model;
        model.loadConversation("g-1");
        MessageItem m;
        m.id = "msg-101";
        m.text = "Hello group";
        m.ts = 1000;
        m.isMine = true;
        m.status = "sent";
        model.mergeMessages("g-1", {m});

        QCOMPARE(model.data(model.index(0), MessageModel::SeenUidsRole).toStringList().size(), 0);
        QCOMPARE(model.data(model.index(0), MessageModel::StatusRole).toString(), QString("sent"));

        model.recordGroupSeen("g-1", "msg-101", "user-2");
        model.recordGroupSeen("g-1", "msg-101", "user-3");
        model.recordGroupSeen("g-1", "msg-101", "user-2");

        const QStringList seen = model.data(model.index(0), MessageModel::SeenUidsRole).toStringList();
        QCOMPARE(seen.size(), 2);
        QCOMPARE(seen.at(0), QString("user-2"));
        QCOMPARE(seen.at(1), QString("user-3"));
        QCOMPARE(model.data(model.index(0), MessageModel::StatusRole).toString(), QString("seen"));
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

    void testPresentationRoles() {
        MessageModel model;
        model.loadConversation("t");
        const qint64 day = QDateTime(QDate(2026, 11, 2), QTime(9, 0)).toMSecsSinceEpoch();
        MessageItem a; a.id = "1"; a.senderId = "u"; a.ts = day;
        MessageItem b; b.id = "2"; b.senderId = "u"; b.ts = day + 60000;
        MessageItem c; c.id = "3"; c.senderId = "u"; c.ts = day + 60000 + 31 * 60000;
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
        QVERIFY(role(3, MessageModel::DateSeparatorRole));
        QVERIFY(!role(3, MessageModel::ShowStatusRole));
        QVERIFY(role(4, MessageModel::ShowStatusRole));
        QVERIFY(!role(0, MessageModel::ShowStatusRole));
    }

    void testLinkifyEscapesAndLinks() {
        const QString html = MessageModel::linkify("xem https://example.com/a?b=1, và <b>đậm</b>\nwww.example.org.");
        QVERIFY(html.contains("<a href=\"https://example.com/a?b=1\">https://example.com/a?b=1</a>,"));
        QVERIFY(html.contains("&lt;b&gt;đậm&lt;/b&gt;"));
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
        QCOMPARE(items.at(0).toMap().value("url").toString(), QString("file:///full1.png"));
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
        QCOMPARE(model.conversations().size(), 1);
        QCOMPARE(model.conversations().first().id, QStringLiteral("conv-1"));
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

    void testConversationModelViewModeDirectAndArchived() {
        ConversationModel model;
        ConversationItem cDirect;
        cDirect.id = "c-direct";
        cDirect.name = "Alice";
        cDirect.isGroup = false;

        ConversationItem cGroup;
        cGroup.id = "c-group";
        cGroup.name = "Work Group";
        cGroup.isGroup = true;
        cGroup.hasUnreadMention = false;

        ConversationItem cMention;
        cMention.id = "c-mention";
        cMention.name = "Class Group";
        cMention.isGroup = true;
        cMention.hasUnreadMention = true;
        cMention.unreadCount = 3;

        ConversationItem cArchived;
        cArchived.id = "c-archived";
        cArchived.name = "Old Chat";
        cArchived.isGroup = false;

        model.setConversations({cDirect, cGroup, cMention, cArchived});
        model.setArchivedThreads({"c-archived"});

        model.setViewMode("normal");
        QCOMPARE(model.rowCount(), 3);

        model.setViewMode("archived");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ConversationModel::IdRole).toString(), "c-archived");

        model.setViewMode("direct");
        QCOMPARE(model.rowCount(), 2);
        QSet<QString> directIds;
        directIds.insert(model.data(model.index(0), ConversationModel::IdRole).toString());
        directIds.insert(model.data(model.index(1), ConversationModel::IdRole).toString());
        QVERIFY(directIds.contains("c-direct"));
        QVERIFY(directIds.contains("c-mention"));
        QVERIFY(!directIds.contains("c-group"));

        model.setViewMode("normal");
        QSignalSpy convResetSpy(&model, &QAbstractItemModel::modelReset);
        QSignalSpy convDataSpy(&model, &QAbstractItemModel::dataChanged);
        cDirect.lastMessage = QStringLiteral("Updated message");
        cDirect.unreadCount = 4;
        model.upsertConversation(cDirect);
        QCOMPARE(convResetSpy.count(), 0);
        QVERIFY(convDataSpy.count() >= 1);
        const QList<int> changedRoles = convDataSpy.last().at(2).value<QList<int>>();
        QVERIFY(changedRoles.contains(ConversationModel::LastMessageRole));
        QVERIFY(changedRoles.contains(ConversationModel::UnreadCountRole));
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

        QList<int> search1 = model.searchMessages("mọi người");
        QCOMPARE(search1.size(), 2);
        QCOMPARE(search1.at(0), 0);
        QCOMPARE(search1.at(1), 2);

        QList<int> search2 = model.searchMessages("thời tiết");
        QCOMPARE(search2.size(), 1);
        QCOMPARE(search2.at(0), 1);

        QList<int> searchEmpty = model.searchMessages("không tồn tại");
        QCOMPARE(searchEmpty.size(), 0);

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

        QCOMPARE(model.rowCount(), 2);

        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

        model.setSearchFilter("Đới");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ContactModel::NameRole).toString(), "Đới Chàn");
        QCOMPARE(resetSpy.count(), 0);

        model.setSearchFilter("");
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(resetSpy.count(), 0);

        c2.isOnline = true;
        model.setContacts({c1, c2, c3, c4});
        QCOMPARE(resetSpy.count(), 0);
        QVERIFY(dataSpy.count() >= 1);
        const QList<int> contactRoles = dataSpy.last().at(2).value<QList<int>>();
        QVERIFY(contactRoles.contains(ContactModel::IsOnlineRole));

        model.setCategoryFilter("groups");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ContactModel::NameRole).toString(), "Nhóm Gia Đình");

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

        model.updatePollVote("g1", 12345LL, 1, true);
        QVariantMap dataAfter = model.data(model.index(0), MessageModel::PollDataRole).toMap();
        QCOMPARE(dataAfter.value("num_vote").toInt(), 1);
        QVariantList optsAfter = dataAfter.value("options").toList();
        QCOMPARE(optsAfter.at(0).toMap().value("voted").toBool(), true);
        QCOMPARE(optsAfter.at(0).toMap().value("votes").toInt(), 1);
    }

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

        MessageItem a2; a2.id = "102"; a2.text = "second c1"; a2.ts = 11;
        model.mergeMessages("c1", {a2});
        model.setReaction("c1", "101", "❤️ 1");
        model.setStatus("c1", "102", "seen");
        QVERIFY(!model.isActiveThreadDuplicatedForTesting());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.threadSize("c1"), 2);

        model.loadConversation("c2");
        QVERIFY(!model.isActiveThreadDuplicatedForTesting());
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.threadSize("c1"), 2);
        QCOMPARE(model.threadSize("c2"), 1);
        QCOMPARE(model.message("c1", "101").reaction, QString("❤️ 1"));

        model.loadConversation("c1");
        QVERIFY(!model.isActiveThreadDuplicatedForTesting());
        QCOMPARE(model.rowCount(), 2);
    }

    void testEmojiCatalog() {
        QStandardPaths::setTestModeEnabled(true);
        EmojiCatalog catalog;
        QCOMPARE(EmojiCatalog::categoryCount(), 11);
        QVERIFY(EmojiCatalog::totalCount() >= 600);
        QCOMPARE(catalog.categories().size(), 11);
        QCOMPARE(catalog.currentCategory(), QStringLiteral("zalo"));
        QVERIFY(catalog.items().size() >= 80);

        catalog.selectCategory(QStringLiteral("smileys"));
        QVERIFY(catalog.items().size() >= 100);
        catalog.selectCategory(QStringLiteral("flags"));
        QVERIFY(catalog.items().size() >= 30);

        catalog.search(QStringLiteral("/-heart"));
        QVERIFY(!catalog.items().isEmpty());
        QCOMPARE(catalog.items().first().toMap().value(QStringLiteral("glyph")).toString(), QStringLiteral("❤️"));

        catalog.search(QStringLiteral("cà phê"));
        QVERIFY(!catalog.items().isEmpty());

        catalog.search(QString());
        catalog.recordRecent(QStringLiteral("🚀"));
        catalog.recordRecent(QStringLiteral("🎉"));
        catalog.recordRecent(QStringLiteral("🚀"));
        catalog.selectCategory(QStringLiteral("recent"));
        const QVariantList rec = catalog.items();
        QVERIFY(rec.size() >= 2);
        QCOMPARE(rec.at(0).toMap().value(QStringLiteral("glyph")).toString(), QStringLiteral("🚀"));
        QCOMPARE(rec.at(1).toMap().value(QStringLiteral("glyph")).toString(), QStringLiteral("🎉"));

        const QVariantList allSections = catalog.sections();
        QCOMPARE(allSections.size(), 11);
        QCOMPARE(allSections.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("recent"));
        QCOMPARE(allSections.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("zalo"));

        catalog.search(QStringLiteral("cà phê"));
        const QVariantList searchSections = catalog.sections();
        QCOMPARE(searchSections.size(), 1);
        QCOMPARE(searchSections.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("search"));

        EmojiCatalog reloaded;
        const QVariantList reloadedSections = reloaded.sections();
        QCOMPARE(reloadedSections.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("recent"));
        const QVariantList reloadedRecent = reloadedSections.first().toMap().value(QStringLiteral("items")).toList();
        QCOMPARE(reloadedRecent.at(0).toMap().value(QStringLiteral("glyph")).toString(), QStringLiteral("🚀"));

        const QVariantList rows = reloaded.flatRows(8);
        QVERIFY(rows.size() > reloadedSections.size());
        QVERIFY(rows.first().toMap().value(QStringLiteral("isHeader")).toBool());
        QVERIFY(!rows.at(1).toMap().value(QStringLiteral("isHeader")).toBool());
        QVERIFY(rows.at(1).toMap().value(QStringLiteral("items")).toList().size() <= 8);
    }

    void testConversationTypeFilter() {
        ConversationModel model;
        ConversationItem direct;
        direct.id = QStringLiteral("u-1");
        direct.name = QStringLiteral("Fixture Direct");
        direct.isGroup = false;
        ConversationItem stranger;
        stranger.id = QStringLiteral("u-2");
        stranger.name = QStringLiteral("Fixture Stranger");
        stranger.isGroup = false;
        ConversationItem myDocs;
        myDocs.id = QStringLiteral("u-docs");
        myDocs.name = QStringLiteral("My Documents");
        myDocs.isGroup = false;
        ConversationItem group;
        group.id = QStringLiteral("g-1");
        group.name = QStringLiteral("Fixture Group");
        group.isGroup = true;
        model.setFriendContext({QStringLiteral("u-1")}, {}, QStringLiteral("u-me"), true);
        model.setConversations({direct, stranger, myDocs, group});

        QCOMPARE(model.typeFilter(), QStringLiteral("all"));
        QCOMPARE(model.rowCount(), 4);

        model.setTypeFilter(QStringLiteral("group"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), ConversationModel::IdRole).toString(), QStringLiteral("g-1"));

        model.setTypeFilter(QStringLiteral("direct"));
        QCOMPARE(model.rowCount(), 3);

        model.setTypeFilter(QStringLiteral("friend"));
        QCOMPARE(model.typeFilter(), QStringLiteral("friend"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), ConversationModel::IdRole).toString(), QStringLiteral("u-1"));

        model.setTypeFilter(QStringLiteral("stranger"));
        QCOMPARE(model.typeFilter(), QStringLiteral("stranger"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), ConversationModel::IdRole).toString(), QStringLiteral("u-2"));

        model.setTypeFilter(QStringLiteral("all"));
        QCOMPARE(model.rowCount(), 4);
    }

    void testShowStatusAndEmoticonsAndStyledHtml() {
        MessageModel model;
        model.loadConversation("c1");
        MessageItem m1; m1.id = "m1"; m1.ts = 1; m1.isMine = true; m1.text = "first";
        MessageItem m2; m2.id = "m2"; m2.ts = 2; m2.isMine = false; m2.text = "peer";
        MessageItem m3; m3.id = "m3"; m3.ts = 3; m3.isMine = true; m3.text = "second";
        model.mergeMessages("c1", {m1, m2, m3});
        QVERIFY(!model.data(model.index(0), MessageModel::ShowStatusRole).toBool());
        QVERIFY(!model.data(model.index(1), MessageModel::ShowStatusRole).toBool());
        QVERIFY(model.data(model.index(2), MessageModel::ShowStatusRole).toBool());
        model.removeMessage("c1", "m3");
        QVERIFY(model.data(model.index(0), MessageModel::ShowStatusRole).toBool());
        model.clearAll();
        QCOMPARE(model.rowCount(), 0);

        QCOMPARE(MessageModel::linkify(QStringLiteral("plain text")), QStringLiteral("plain text"));
        QVERIFY(MessageModel::linkify(QStringLiteral("hi :-) :v")).contains(QStringLiteral("🙂")));
        QVERIFY(MessageModel::linkify(QStringLiteral("hi :v")).contains(QStringLiteral("pacman.png")));
        QCOMPARE(MessageModel::linkify(QStringLiteral("https://example.com/path?q=:)&x=1")),
                 QStringLiteral("<a href=\"https://example.com/path?q=:)&amp;x=1\">https://example.com/path?q=:)&amp;x=1</a>"));

        const QString xss = MessageModel::styledHtml(
            QStringLiteral("<script>alert(1)</script>"), {},
            QVariantList{QVariantMap{{"start", 0}, {"len", 5}, {"st", "c_ff\"'><"}}});
        QVERIFY(!xss.contains(QStringLiteral("<script>")));
        QVERIFY(!xss.contains(QStringLiteral("\"'><")));
    }

    void testConversationModelMultiRowMoveNoResetAndOfficialCache() {
        ConversationModel model;
        ConversationItem c1; c1.id = "c1"; c1.name = QStringLiteral("Nguyễn Văn A"); c1.lastTs = 100;
        ConversationItem c2; c2.id = "c2"; c2.name = QStringLiteral("Trần B"); c2.lastTs = 90;
        ConversationItem c3; c3.id = "c3"; c3.name = QStringLiteral("ZaloPay"); c3.lastTs = 80;
        ConversationItem c4; c4.id = "c4"; c4.name = QStringLiteral("Cửa hàng"); c4.lastTs = 70;
        model.setConversations({c1, c2, c3, c4});

        QCOMPARE(model.data(model.index(2), ConversationModel::IsOfficialRole).toBool(), true);
        QCOMPARE(model.data(model.index(3), ConversationModel::IsOfficialRole).toBool(), false);

        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

        model.beginBatch();
        model.recordMessage("c4", "hi", "", 200, true);
        model.recordMessage("c3", "yo", "", 150, false);
        model.endBatch();

        QCOMPARE(resetSpy.count(), 0);
        QVERIFY(moveSpy.count() >= 1);
        QCOMPARE(dataSpy.isEmpty(), false);
        QCOMPARE(model.data(model.index(0), ConversationModel::IdRole).toString(), QStringLiteral("c4"));
        QCOMPARE(model.data(model.index(1), ConversationModel::IdRole).toString(), QStringLiteral("c3"));
        QCOMPARE(model.data(model.index(2), ConversationModel::IdRole).toString(), QStringLiteral("c1"));
        QCOMPARE(model.data(model.index(3), ConversationModel::IdRole).toString(), QStringLiteral("c2"));

        model.updateProfile("c4", QStringLiteral("ZBusiness"), QString());
        QCOMPARE(model.data(model.index(0), ConversationModel::IsOfficialRole).toBool(), true);
    }

    void testMessageModelActiveIndexAndTargetedPollData() {
        MessageModel model;
        MessageItem p1; p1.id = "10"; p1.cliMsgId = "c10"; p1.messageType = "poll"; p1.text = QStringLiteral("Ăn gì?"); p1.ts = 100;
        p1.pollData = {{"poll_id", 77LL}, {"question", QStringLiteral("Ăn gì?")}};
        MessageItem m1; m1.id = "20"; m1.cliMsgId = "c20"; m1.text = "unread1"; m1.ts = 200;
        MessageItem m2; m2.id = "30"; m2.cliMsgId = "c30"; m2.text = "unread2"; m2.ts = 300;
        model.mergeMessages("g1", {p1, m1, m2});

        MessageItem p2; p2.id = "99"; p2.messageType = "poll"; p2.text = QStringLiteral("Uống gì?"); p2.ts = 100;
        p2.pollData = {{"poll_id", 77LL}, {"question", QStringLiteral("Uống gì?")}};
        model.mergeMessages("g2", {p2});

        model.loadConversation("g1");
        model.setPollData(77LL, {{"poll_id", 77LL}, {"question", QStringLiteral("Trưa nay ăn gì?")}, {"loaded", true}},
                          QStringLiteral("g1"));
        QCOMPARE(model.message("g1", "10").text, QStringLiteral("Trưa nay ăn gì?"));
        QCOMPARE(model.message("g2", "99").text, QStringLiteral("Uống gì?"));

        QCOMPARE(model.rowOfMessage("10"), 0);
        QCOMPARE(model.rowOfMessage("c20"), 1);
        QCOMPARE(model.getMessageById("30").value("text").toString(), QStringLiteral("unread2"));
        QVERIFY(model.contains("g1", "20"));

        model.setUnreadMarker("g1", 2);
        QCOMPARE(model.unreadMarkerRow(), 1);
        model.clearUnreadMarker();
        QCOMPARE(model.unreadMarkerRow(), -1);

        model.addMessage("temp", true);
        const QString localId = model.data(model.index(3), MessageModel::IdRole).toString();
        QCOMPARE(model.rowOfMessage(localId), 3);
        model.confirmMessage("g1", localId, "40");
        QCOMPARE(model.rowOfMessage(localId), -1);
        QCOMPARE(model.rowOfMessage("40"), 3);

        model.removeMessage("g1", "20");
        QCOMPARE(model.rowOfMessage("20"), -1);
        QCOMPARE(model.rowOfMessage("30"), 1);
        QCOMPARE(model.rowOfMessage("40"), 2);

        model.removeMessagesUpTo("g1", "10");
        QCOMPARE(model.rowOfMessage("10"), -1);
        QCOMPARE(model.rowOfMessage("30"), 0);

        model.loadConversation("g2");
        QCOMPARE(model.rowOfMessage("99"), 0);
        QCOMPARE(model.rowOfMessage("30"), -1);
        model.loadConversation("g1");
        QCOMPARE(model.rowOfMessage("30"), 0);
    }

    void testMessageItemPackingAndStringInterning() {
        const size_t expectedSize = 15 * sizeof(QString) + 4 * sizeof(qint64) + 2 * sizeof(QVariantMap)
                                  + sizeof(QVector<MessageMention>) + sizeof(QStringList) + 8;
        QCOMPARE(sizeof(MessageItem), expectedSize);
        const QString t1 = MessageModel::internMessageType(QStringLiteral("image"));
        const QString t2 = MessageModel::internMessageType(QStringLiteral("image"));
        QCOMPARE(t1.constData(), t2.constData());
        QCOMPARE(MessageModel::internMessageType(QString()), QStringLiteral("text"));
        QCOMPARE(MessageModel::internMessageType(QStringLiteral("custom.x")), QStringLiteral("custom.x"));

        const QString s1 = MessageModel::internStatus(QStringLiteral("seen"));
        const QString s2 = MessageModel::internStatus(QStringLiteral("seen"));
        QCOMPARE(s1.constData(), s2.constData());
        QVERIFY(MessageModel::internStatus(QString()).isEmpty());
        QCOMPARE(MessageModel::internStatus(QStringLiteral("queued")), QStringLiteral("queued"));
    }

    void testMessageModelMediaRunCap16() {
        MessageModel model;
        QVector<MessageItem> batch;
        batch.reserve(20);
        for (int i = 0; i < 20; ++i) {
            MessageItem m;
            m.id = QStringLiteral("img-%1").arg(i);
            m.senderId = QStringLiteral("u-1");
            m.messageType = QStringLiteral("image");
            m.mediaUrl = QStringLiteral("https://cdn.example/%1.jpg").arg(i);
            m.ts = 1700000000000LL + i * 1000LL;
            batch.append(m);
        }
        model.mergeMessages(QStringLiteral("c-run"), batch);
        model.loadConversation(QStringLiteral("c-run"));
        QCOMPARE(model.rowCount(), 20);
        QCOMPARE(model.data(model.index(0), MessageModel::InMediaRunRole).toBool(), false);
        QCOMPARE(model.data(model.index(0), MessageModel::MediaRunRole).toList().size(), 16);
        QCOMPARE(model.data(model.index(15), MessageModel::InMediaRunRole).toBool(), true);
        QCOMPARE(model.data(model.index(16), MessageModel::InMediaRunRole).toBool(), false);
        QCOMPARE(model.data(model.index(16), MessageModel::MediaRunRole).toList().size(), 4);
    }

    void testMediaRunPrependRefreshesWholeRun() {
        MessageModel model;
        auto image = [](int i) {
            MessageItem m;
            m.id = QStringLiteral("run-%1").arg(i);
            m.senderId = QStringLiteral("u-1");
            m.messageType = QStringLiteral("image");
            m.mediaUrl = QStringLiteral("https://cdn.example/run-%1.jpg").arg(i);
            m.ts = 1700000000000LL + i * 1000LL;
            return m;
        };
        QVector<MessageItem> newer;
        for (int i = 10; i < 40; ++i) newer.append(image(i));
        model.mergeMessages(QStringLiteral("c-run2"), newer);
        model.loadConversation(QStringLiteral("c-run2"));
        QCOMPARE(model.rowCount(), 30);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QVector<MessageItem> older;
        for (int i = 0; i < 10; ++i) older.append(image(i));
        model.mergeMessages(QStringLiteral("c-run2"), older);
        QCOMPARE(model.rowCount(), 40);
        int maxRow = -1;
        for (const QList<QVariant>& args : std::as_const(changed)) {
            const QVector<int> roles = args.at(2).value<QVector<int>>();
            if (roles.contains(MessageModel::InMediaRunRole)) maxRow = qMax(maxRow, args.at(1).toModelIndex().row());
        }
        QCOMPARE(maxRow, 39);
        QCOMPARE(model.data(model.index(32), MessageModel::InMediaRunRole).toBool(), false);
        QCOMPARE(model.data(model.index(32), MessageModel::MediaRunRole).toList().size(), 8);
    }

    void testMediaItemsIncludesPlayableVideoFiles() {
        MessageModel model;
        MessageItem vidFile;
        vidFile.id = QStringLiteral("f-mp4");
        vidFile.senderId = QStringLiteral("u-1");
        vidFile.messageType = QStringLiteral("file");
        vidFile.text = QStringLiteral("clip.mp4");
        vidFile.href = QStringLiteral("https://cdn.example/clip.mp4");
        vidFile.ts = 1700000001000LL;

        MessageItem movFile;
        movFile.id = QStringLiteral("f-mov");
        movFile.senderId = QStringLiteral("u-1");
        movFile.messageType = QStringLiteral("file");
        movFile.text = QStringLiteral("attachment-1");
        movFile.href = QStringLiteral("https://cdn.example/video/sample.MOV?token=123");
        movFile.ts = 1700000002000LL;

        MessageItem docFile;
        docFile.id = QStringLiteral("f-pdf");
        docFile.senderId = QStringLiteral("u-1");
        docFile.messageType = QStringLiteral("file");
        docFile.text = QStringLiteral("notes.pdf");
        docFile.href = QStringLiteral("https://cdn.example/notes.pdf");
        docFile.ts = 1700000003000LL;

        model.mergeMessages(QStringLiteral("c-vid"), {vidFile, movFile, docFile});
        model.loadConversation(QStringLiteral("c-vid"));
        const QVariantList items = model.mediaItems();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("msgId")).toString(), QStringLiteral("f-mp4"));
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("isVideo")).toBool(), true);
        QCOMPARE(items.at(1).toMap().value(QStringLiteral("msgId")).toString(), QStringLiteral("f-mov"));
        QCOMPARE(items.at(1).toMap().value(QStringLiteral("isVideo")).toBool(), true);
    }

    void serverCallLogReplacesNearbyLocalPlaceholder() {
        MessageModel model;
        const qint64 t0 = 1795515000000LL;
        MessageItem oldLocal;
        oldLocal.id = QStringLiteral("local-call-%1").arg(t0 - 300000);
        oldLocal.senderId = QStringLiteral("me");
        oldLocal.isMine = true;
        oldLocal.messageType = QStringLiteral("call");
        oldLocal.text = QStringLiteral("Cuộc gọi thoại đi");
        oldLocal.ts = t0 - 300000;

        MessageItem recentLocal;
        recentLocal.id = QStringLiteral("local-call-%1").arg(t0);
        recentLocal.senderId = QStringLiteral("me");
        recentLocal.isMine = true;
        recentLocal.messageType = QStringLiteral("call");
        recentLocal.text = QStringLiteral("Cuộc gọi video đi đã hủy");
        recentLocal.ts = t0;

        model.mergeMessages(QStringLiteral("c-call"), {oldLocal, recentLocal});
        model.loadConversation(QStringLiteral("c-call"));
        QCOMPARE(model.rowCount(), 2);

        MessageItem serverCall;
        serverCall.id = QStringLiteral("8524449999001");
        serverCall.cliMsgId = QStringLiteral("1795515002000");
        serverCall.senderId = QStringLiteral("me");
        serverCall.isMine = true;
        serverCall.messageType = QStringLiteral("call");
        serverCall.text = QStringLiteral("Cuộc gọi video đi");
        serverCall.ts = t0 + 2000;
        serverCall.extra = {{QStringLiteral("video"), true}, {QStringLiteral("outgoing"), true}, {QStringLiteral("seconds"), 5}};

        model.mergeMessages(QStringLiteral("c-call"), {serverCall});
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), MessageModel::IdRole).toString(), oldLocal.id);
        QCOMPARE(model.data(model.index(1), MessageModel::IdRole).toString(), serverCall.id);
        QCOMPARE(model.data(model.index(1), MessageModel::TextRole).toString(), serverCall.text);
        QVERIFY(model.contains(QStringLiteral("c-call"), serverCall.id));
        QVERIFY(!model.contains(QStringLiteral("c-call"), recentLocal.id));
    }
};

QTEST_MAIN(TestModels)
#include "test_models.moc"

