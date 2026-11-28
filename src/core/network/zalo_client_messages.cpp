#include "zalo_client_internal.h"

namespace Zalos::Core::Network {

using namespace Detail;

namespace {

QJsonObject parseNestedJsonObject(const QJsonValue& raw) {
    if (raw.isObject()) return raw.toObject();
    if (!raw.isString()) return {};
    const QString s = raw.toString().trimmed();
    if (!s.startsWith(QLatin1Char('{'))) return {};
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

qint64 groupTopicPollId(const QJsonObject& data) {
    return parseNestedJsonObject(data.value(QLatin1String("groupTopic")).toObject().value(QLatin1String("params")))
        .value(QLatin1String("pollId")).toVariant().toLongLong();
}

constexpr qint64 kReadPointMaxWaitMs = 15000;

}

void ZaloClient::callLogItem(const QJsonObject& obj, bool missed, Models::MessageItem* item, QString* label) const {
    const QJsonObject params = parseNestedJsonObject(obj.value(QLatin1String("params")));
    const int ct = params.contains("calltype") ? params.value("calltype").toInt() : params.value("callType").toInt();
    const bool video = (ct == 1 || ct == 3 || ct == 6 || params.value("video").toBool() || params.value("isVideo").toBool());
    const bool hasCallerFlag = params.contains("isCaller") || params.contains("is_caller");
    const bool outgoing = hasCallerFlag
        ? (params.value("isCaller").toInt() == 1 || params.value("is_caller").toBool())
        : (obj.value("action").toString() == QLatin1String("recommened.calling"));
    const int seconds = params.contains("duration") ? params.value("duration").toInt()
                                                    : obj.value("duration").toInt();
    item->messageType = QStringLiteral("call");
    if (missed) {
        if (outgoing) {
            item->text = seconds == 0
                ? (video ? L("Cuộc gọi video đi đã hủy", "Canceled outgoing video call")
                         : L("Cuộc gọi thoại đi đã hủy", "Canceled outgoing voice call"))
                : (video ? L("Cuộc gọi video đi không được trả lời", "Unanswered outgoing video call")
                         : L("Cuộc gọi thoại đi không được trả lời", "Unanswered outgoing voice call"));
        } else {
            item->text = video ? L("Cuộc gọi video nhỡ", "Missed video call")
                               : L("Cuộc gọi thoại nhỡ", "Missed voice call");
        }
    } else {
        item->text = outgoing ? (video ? L("Cuộc gọi video đi", "Outgoing video call") : L("Cuộc gọi thoại đi", "Outgoing voice call"))
                              : (video ? L("Cuộc gọi video đến", "Incoming video call") : L("Cuộc gọi thoại đến", "Incoming voice call"));
    }
    QString duration;
    if (!missed && seconds > 0) {
        duration = seconds >= 60 ? L("%1 phút %2 giây", "%1 min %2 s").arg(seconds / 60).arg(seconds % 60)
                                 : L("%1 giây", "%1 s").arg(seconds);
    }
    item->extra = {{"video", video}, {"missed", missed}, {"outgoing", outgoing}, {"seconds", seconds},
                   {"duration", duration}};
    *label = "[" + item->text + "]";
}

Models::MessageItem ZaloClient::toMessageItem(const QJsonObject& msg, bool isGroup, QString* threadId, QString* preview) const {
    Models::MessageItem item;
    const QString uidFrom = idOf(msg.value("uidFrom"));
    const QString idTo = idOf(msg.value("idTo"));
    const bool isSelf = uidFrom == "0" || (!m_myUid.isEmpty() && uidFrom == m_myUid);
    *threadId = isGroup ? idTo : (isSelf ? idTo : uidFrom);

    item.id = idOf(msg.value("msgId"));
    item.cliMsgId = idOf(msg.value("cliMsgId"));
    if (item.id.isEmpty()) item.id = item.cliMsgId;
    const QJsonObject quote = msg.value("quote").toObject();
    if (!quote.isEmpty()) {
        item.quoteMsgId = idOf(quote.value("globalMsgId"));
        if (item.quoteMsgId.isEmpty() || item.quoteMsgId == QLatin1String("0")) {
            item.quoteMsgId = idOf(quote.value("msgId"));
        }
        if (item.quoteMsgId.isEmpty() || item.quoteMsgId == QLatin1String("0")) {
            item.quoteMsgId = idOf(quote.value("cliMsgId"));
        }
        const QString owner = idOf(quote.value("ownerId"));
        QString qSender;
        if (!m_myUid.isEmpty() && owner == m_myUid) {
            qSender = L("Bạn", "You");
        } else {
            qSender = displayNameOf(owner);
            if (qSender.isEmpty()) qSender = quote.value("fromD").toString();
        }
        item.quoteSender = qSender;
        item.quoteText = quote.value("msg").toString();
        if (item.quoteText.isEmpty()) {
            item.quoteText = quote.value("attach").toString().isEmpty() ? L("[Tin nhắn]", "[Message]")
                                                                         : L("[Tệp đính kèm]", "[Attachment]");
        }
    }
    item.ts = msg.value("ts").toVariant().toLongLong();
    item.timeStr = item.ts >= kMinPlausibleEpochMs ? QDateTime::fromMSecsSinceEpoch(item.ts).toString("HH:mm") : QString();
    item.isMine = isSelf;
    item.status = isSelf ? QStringLiteral("sent") : QString();
    item.senderId = isSelf ? m_myUid : uidFrom;
    const Profile sender = m_users.value(item.senderId);
    if (!isSelf) {
        const QString alias = m_aliases.value(item.senderId);
        const QString dName = msg.value("dName").toString();
        if (!alias.isEmpty()) item.senderName = alias;
        else if (!m_users.contains(item.senderId) || (!dName.isEmpty() && dName != sender.name)) item.senderName = dName.isEmpty() ? sender.name : dName;
        if (!m_users.contains(item.senderId)) item.senderAvatar = sender.avatar;
    }

    const QString type = msg.value("msgType").toString();
    QJsonValue content = msg.value("content");
    if (content.isString() && !type.isEmpty() && type != QLatin1String("webchat")) {
        const QString trimmed = content.toString().trimmed();
        if (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['))) {
            const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
            if (doc.isObject()) content = doc.object();
            else if (doc.isArray()) content = doc.array();
        }
    }
    const QJsonObject obj = content.toObject();
    const QJsonObject objParams = obj.isEmpty() ? QJsonObject() : parseNestedJsonObject(obj.value(QLatin1String("params")));
    QString label;

    if (content.isString()) {
        item.text = content.toString();
        label = item.text;
        for (const char* key : {"textProperties", "params", "propertyExt"}) {
            const QJsonObject props = parseNestedJsonObject(msg.value(QLatin1String(key)));
            QVariantList styles;
            for (const QJsonValue& v : props.value("styles").toArray()) {
                const QJsonObject o = v.toObject();
                const int start = o.value("start").toInt(-1);
                const int len = o.value("len").toInt();
                if (start < 0 || len <= 0 || start + len > item.text.size()) continue;
                styles.append(QVariantMap{{"start", start}, {"len", len}, {"st", o.value("st").toString()}});
            }
            if (!styles.isEmpty()) {
                item.extra.insert("styles", styles);
                break;
            }
        }
    } else if (type == "chat.photo" || type == "chat.gif" || type == "chat.doodle") {
        item.messageType = QStringLiteral("image");
        item.mediaUrl = pickString(obj, {"thumb", "thumbUrl", "hd"});
        item.href = pickString(obj, {"href", "hdUrl", "oriUrl", "normalUrl", "url"});
        if (item.mediaUrl.isEmpty()) item.mediaUrl = item.href;
        if (item.href.isEmpty()) item.href = item.mediaUrl;
        item.text = captionOf(obj);
        if (Offline::ZaloOfflineStore::isAnimatedImageHref(type, item.href))
            item.extra.insert("animatedUrl", item.href);
        label = L("[Hình ảnh]", "[Photo]");
    } else if (type == "share.file") {
        item.messageType = QStringLiteral("file");
        item.mediaUrl = obj.value("href").toString();
        item.href = item.mediaUrl;
        const QJsonObject& params = objParams;
        QString title = obj.value("title").toString().trimmed();
        QString ext = params.value("fileExt").toString().trimmed();
        while (ext.startsWith('.')) ext.remove(0, 1);
        if (title.isEmpty() || title.compare(QLatin1String("renewMedia"), Qt::CaseInsensitive) == 0) {
            const QString urlFile = QUrl(item.href).fileName();
            if (!urlFile.isEmpty() && urlFile.compare(QLatin1String("renewMedia"), Qt::CaseInsensitive) != 0) {
                title = urlFile;
            } else {
                title = QStringLiteral("attachment-%1").arg(item.id.isEmpty() ? QString::number(item.ts) : item.id);
            }
        }
        if (!ext.isEmpty() && !title.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) {
            title += QLatin1Char('.') + ext;
        }
        item.text = title;
        item.fileSize = params.value("fileSize").toVariant().toLongLong();
        label = L("[Tệp] ", "[File] ") + item.text;
    } else if (type == "chat.sticker") {
        item.messageType = QStringLiteral("sticker");
        const QVariantMap info = m_stickerStore->info(idOf(obj.value("id")));
        if (!info.isEmpty()) {
            item.mediaUrl = info.value("url").toString();
            item.extra.insert("sticker", info);
        }
        label = L("[Sticker]", "[Sticker]");
    } else if (type == "chat.voice") {
        const QJsonObject& params = objParams;
        item.messageType = QStringLiteral("voice");
        item.href = obj.value("href").toString();
        if (item.href.isEmpty()) item.href = params.value("m4a").toString();
        item.fileSize = params.value("fileSize").toVariant().toLongLong();
        item.extra = {{"durationMs", params.value("duration").toVariant().toLongLong()}};
        item.text = L("Tin nhắn thoại", "Voice message");
        label = L("[Tin nhắn thoại]", "[Voice message]");
    } else if (type == "chat.video.msg") {
        item.messageType = QStringLiteral("video");
        item.mediaUrl = obj.value("thumb").toString();
        item.href = obj.value("href").toString();
        item.text = captionOf(obj);
        item.fileSize = objParams.value("fileSize").toVariant().toLongLong();
        label = L("[Video]", "[Video]");
    } else if (type == "chat.location.new") {
        const QJsonObject& params = objParams;
        auto field = [&](const char* key) {
            const QString v = params.value(QLatin1String(key)).toVariant().toString();
            return v.isEmpty() ? obj.value(QLatin1String(key)).toVariant().toString() : v;
        };
        bool latOk = false, lngOk = false;
        const double lat = field("latitude").toDouble(&latOk);
        const double lng = field("longitude").toDouble(&lngOk);
        const bool hasCoords = latOk && lngOk && qAbs(lat) <= 90 && qAbs(lng) <= 180 && !(lat == 0 && lng == 0);
        const QString coords = hasCoords ? QStringLiteral("%1, %2").arg(lat, 0, 'f', 6).arg(lng, 0, 'f', 6) : QString();
        const QString title = field("title");
        const QString thumb = field("thumb");
        item.messageType = QStringLiteral("location");
        item.text = !title.isEmpty() ? title : L("Vị trí được chia sẻ", "Shared location");
        if (!thumb.isEmpty()) item.mediaUrl = thumb;
        if (hasCoords) {
            item.href = QStringLiteral("https://www.google.com/maps?q=%1,%2").arg(lat, 0, 'f', 6).arg(lng, 0, 'f', 6);
        } else {
            const QString rawHref = field("href");
            if (!rawHref.isEmpty()) item.href = rawHref;
        }
        item.extra = {{"desc", field("description")},
                      {"coords", coords},
                      {"lat", hasCoords ? lat : 0.0},
                      {"lng", hasCoords ? lng : 0.0},
                      {"hasCoords", hasCoords}};
        label = L("[Vị trí] ", "[Location] ") + item.text;
    } else if (type == "chat.ecard") {
        item.messageType = QStringLiteral("ecard");
        const QJsonObject& params = objParams;
        const QString ecardType = obj.value("type").toVariant().toString();
        QVariantList stickers;
        const QJsonArray actions = params.value("actions").toArray();
        if (!actions.isEmpty()) {
            const QJsonObject data = parseNestedJsonObject(actions.first().toObject().value(QLatin1String("data")));
            for (const QJsonValue& v : data.value("stickerList").toArray()) {
                const QJsonObject s = v.toObject();
                const QString sid = idOf(s.value("id"));
                if (sid.isEmpty()) continue;
                stickers.append(QVariantMap{{"id", sid},
                                            {"cateId", s.value("catId").toVariant().toLongLong()},
                                            {"type", s.value("type").toInt(7)}});
            }
        }
        const QString title = obj.value("title").toString();
        const QString who = resolvedSenderName(item);
        if (ecardType == QLatin1String("1") && !who.isEmpty()) {
            item.text = L("Hôm nay là sinh nhật của %1", "Today is %1's birthday").arg(who);
        } else {
            item.text = title.isEmpty() ? L("Thiệp", "Card") : title;
        }
        const QString desc = obj.value("description").toString();
        item.extra = {{"ecardType", ecardType},
                      {"desc", ecardType == QLatin1String("1") && !stickers.isEmpty()
                                   ? L("Gửi ngay sticker dưới đây thay lời chúc mừng",
                                       "Send one of these stickers as your wishes")
                                   : (desc == title ? QString() : desc)},
                      {"thumb", obj.value("thumb").toString()},
                      {"stickers", stickers}};
        label = L("[Thiệp] ", "[Card] ") + item.text;
    } else if (type == "chat.list.action" && (content.isArray() ? content.toArray().at(0).toObject() : obj)
                                                    .value("href").toString().isEmpty()) {
        const QJsonObject first = content.isArray() ? content.toArray().at(0).toObject() : obj;
        const QString title = first.value("title").toString();
        const QString desc = first.value("description").toString();
        if (isGroup) {
            item.messageType = QStringLiteral("system");
            item.text = !title.isEmpty() ? title : desc;
        } else {
            item.messageType = QStringLiteral("link");
            item.text = !title.isEmpty() ? title : desc;
            item.mediaUrl = first.value("thumb").toString();
            item.extra = {{"desc", desc == item.text ? QString() : desc}, {"domain", QString()}};
        }
        if (item.text.isEmpty())
            item.text = L("Tin nhắn chưa hỗ trợ hiển thị trên phiên bản hiện tại", "This message isn't supported in the current version");
        label = item.text;
    } else if (type == "chat.list.action") {
        const QJsonArray cards = content.toArray();
        const QJsonObject first = cards.isEmpty() ? obj : cards.first().toObject();
        const QString title = first.value("title").toString();
        const QString href = first.value("href").toString();
        item.messageType = QStringLiteral("link");
        item.href = href;
        item.mediaUrl = first.value("thumb").toString();
        item.text = title.isEmpty() ? href : title;
        const QString desc = first.value("description").toString();
        item.extra = {{"desc", desc == title ? QString() : desc}, {"domain", QUrl(href).host()}};
        label = L("[Liên kết] ", "[Link] ") + item.text;
    } else if (type == "chat.webcontent" || type == "chat.webcontent.v2") {
        const QString title = obj.value("title").toString();
        const QString href = obj.value("href").toString();
        const QJsonObject& params = objParams;
        const QJsonObject custom = params.value("customMsg").toObject().value("msg").toObject();
        QString localized = custom.value(m_appLanguage == QLatin1String("en") ? "en" : "vi").toString();
        if (localized.isEmpty()) localized = custom.value("en").toString();
        if (localized.isEmpty()) localized = custom.value("vi").toString();
        localized = fillHighlights(localized, params.value("customMsg").toObject().value("highLightsV2").toArray());
        const QString templateId = params.value("item").toObject().value("zinstantdata_id").toString();
        const int webType = params.value("type").toVariant().toInt();
        const bool birthday = webType == 11 || templateId == QLatin1String("templateId=14030");
        const bool bankCard = templateId == QLatin1String("templateId=11845")
                              || obj.value("action").toString() == QLatin1String("zinstant.bankcard")
                              || params.value("action").toString() == QLatin1String("zinstant.bankcard");
        if (bankCard) {
            item.messageType = QStringLiteral("bankcard");
            item.text = !localized.isEmpty() ? localized : (!title.isEmpty() ? title : L("Thông tin chuyển khoản", "Bank transfer details"));
            const QJsonObject rootObj{{"obj", obj}, {"params", params}};
            const BankCardFields card = extractBankCardFields(rootObj, item.text);
            const QString dataUrl = params.value("item").toObject().value("data_url").toString(obj.value("data_url").toString());
            QString desc = L("Mở Zalo trên điện thoại để xem chi tiết và chuyển khoản",
                             "Open Zalo on your phone to see the details and transfer");
            if (!card.accountNumber.isEmpty() && !card.accountHolder.isEmpty()) {
                desc = card.accountHolder;
            }
            item.extra = {{"desc", desc},
                          {"binBank", card.binBank},
                          {"bankName", card.bankName},
                          {"accountNumber", card.accountNumber},
                          {"accountHolder", card.accountHolder},
                          {"dataUrl", dataUrl}};
            label = L("[Chuyển khoản] ", "[Bank transfer] ") + item.text;
        } else if (birthday) {
            item.messageType = QStringLiteral("ecard");
            const QString who = resolvedSenderName(item);
            item.text = !localized.isEmpty() ? localized
                      : (!who.isEmpty() ? L("Hôm nay là sinh nhật của %1", "Today is %1's birthday").arg(who)
                                        : title);
            item.extra = {{"ecardType", QStringLiteral("1")},
                          {"desc", L("Gửi ngay sticker dưới đây thay lời chúc mừng", "Send one of these stickers as your wishes")},
                          {"thumb", QString()},
                          {"stickers", QVariantList{}},
                          {"suggestStickers", true}};
            label = L("[Sinh nhật] ", "[Birthday] ") + item.text;
        } else if (!localized.isEmpty() && !isGroup && webType != 9
                   && params.value("customMsg").toObject().value("highLightsV2").toArray().isEmpty()
                   && templateId != QLatin1String("templateId=11366") && templateId != QLatin1String("templateId=10709")
                   && templateId != QLatin1String("templateId=10667")) {
            const QString body = localized.trimmed();
            const qsizetype cut = body.indexOf(QLatin1Char('\n'));
            item.messageType = QStringLiteral("link");
            item.text = cut > 0 ? body.left(cut).trimmed() : body;
            item.mediaUrl = obj.value("thumb").toString();
            QString buttonLabel = params.value("actionTitle").toString();
            QString buttonUrl = href;
            for (const QJsonValue& a : params.value("actions").toArray()) {
                const QJsonObject act = a.toObject();
                const QString at = act.value("actionTitle").toString(act.value("title").toString());
                const QString au = act.value("actionUrl").toString(act.value("href").toString());
                if (!at.isEmpty()) { buttonLabel = at; if (!au.isEmpty()) buttonUrl = au; break; }
            }
            if (buttonUrl.isEmpty() && !href.isEmpty()) buttonUrl = href;
            item.href = buttonUrl;
            item.extra = {{"desc", cut > 0 ? body.mid(cut + 1).trimmed() : QString()},
                          {"domain", buttonUrl.isEmpty() ? QString() : QUrl(buttonUrl).host()},
                          {"actionLabel", buttonLabel}};
            label = item.text;
        } else if (!localized.isEmpty()) {
            item.messageType = QStringLiteral("system");
            item.text = localized;
            for (const QJsonValue& a : params.value("actions").toArray()) {
                const QJsonObject act = a.toObject();
                if (act.value("actionType").toString() != QLatin1String("action.open.calendar.event")) continue;
                const QJsonObject data = parseNestedJsonObject(act.value(QLatin1String("actionData")));
                const QString eventId = idOf(data.value("eventId"));
                if (!eventId.isEmpty()) item.extra.insert("reminderEventId", eventId);
            }
            label = item.text;
        } else if (!href.isEmpty()) {
            item.messageType = QStringLiteral("link");
            item.href = href;
            item.mediaUrl = obj.value("thumb").toString();
            item.text = title.isEmpty() ? L("Nội dung web của Zalo", "Zalo web content") : title;
            const QString desc = obj.value("description").toString();
            item.extra = {{"desc", desc == title ? QString() : desc}, {"domain", QUrl(href).host()}};
            label = L("[Liên kết] ", "[Link] ") + item.text;
        } else if (!isGroup && webType != 6 && webType != 16 && webType != 19) {
            item.messageType = QStringLiteral("link");
            item.mediaUrl = obj.value("thumb").toString();
            const QString desc = webType == 34 && !title.isEmpty() ? title : obj.value("description").toString();
            item.text = webType == 34 ? L("Zalo gửi thông báo về an toàn tài khoản của bạn", "Zalo sent an account safety notice")
                      : !title.isEmpty() ? title
                      : (!desc.isEmpty() ? desc
                                         : L("Tin nhắn chưa hỗ trợ hiển thị trên phiên bản hiện tại",
                                             "This message isn't supported in the current version"));
            item.extra = {{"desc", desc == item.text ? QString() : desc}, {"domain", QString()}};
            label = item.text;
        } else {
            item.messageType = QStringLiteral("system");
            if (webType == 6) {
                item.text = title.contains(QLatin1String("created"), Qt::CaseInsensitive)
                                ? L("Bạn vừa tạo nhóm", "You just created the group")
                                : L("Bạn đã trở thành thành viên của nhóm", "You became a member of the group");
            } else if (webType == 19) {
                item.text = L("Bạn được bổ nhiệm làm trưởng nhóm", "You were appointed as the group owner");
            } else if (webType == 34) {
                item.text = L("Zalo gửi thông báo về an toàn tài khoản của bạn", "Zalo sent an account safety notice");
            } else if (!title.isEmpty()) {
                item.text = title;
            } else {
                item.text = L("Tin nhắn chưa hỗ trợ hiển thị trên phiên bản hiện tại",
                              "This message isn't supported in the current version");
            }
            label = item.text;
        }
    } else if (type == QLatin1String("chat.todo")) {
        const QJsonValue rawMessage = obj.value("message");
        QString msg = rawMessage.isString() ? rawMessage.toString() : rawMessage.toObject().value("message").toString();
        if (msg.isEmpty()) msg = obj.value("title").toString();
        if (msg.isEmpty()) msg = obj.value("description").toString();
        item.messageType = QStringLiteral("system");
        item.text = msg.isEmpty() ? L("Nhắc nhở", "Reminder") : msg;
        const QJsonObject& params = objParams;
        const QString topicId = idOf(params.value("topicId").isUndefined() ? params.value("id") : params.value("topicId"));
        if (!topicId.isEmpty()) item.extra.insert("reminderEventId", topicId);
        label = item.text;
    } else if ((type == "chat.recommended" || type == "chat.link")
               && obj.value("action").toString() == QLatin1String("recommened.misscall")) {
        callLogItem(obj, true, &item, &label);
    } else if ((type == "chat.recommended" || type == "chat.link")
               && (obj.value("action").toString() == QLatin1String("recommened.calltime")
                   || obj.value("action").toString() == QLatin1String("recommened.calling")
                   || obj.value("action").toString() == QLatin1String("recommened.receivecall"))) {
        callLogItem(obj, false, &item, &label);
    } else if ((type == "chat.recommended" || type == "chat.link")
               && (obj.value("action").toString() == QLatin1String("recommened.user")
                   || obj.value("action").toString() == QLatin1String("recommened.vip"))) {
        item.messageType = QStringLiteral("card");
        item.text = obj.value("title").toString();
        item.mediaUrl = obj.value("thumb").toString();
        const QJsonObject desc = parseNestedJsonObject(obj.value(QLatin1String("description")));
        const QJsonValue params = obj.value("params");
        QString uid = params.isString() ? params.toString() : idOf(params);
        if (uid.startsWith('{')) uid = idOf(objParams.value("uid"));
        item.href = obj.value("href").toString();
        item.extra = {{"uid", uid}, {"phone", desc.value("phone").toVariant().toString()},
                      {"qrCodeUrl", desc.value("qrCodeUrl").toString()},
                      {"official", obj.value("action").toString() == QLatin1String("recommened.vip")}};
        if (item.text.isEmpty()) item.text = L("Danh thiếp", "Contact card");
        label = L("[Danh thiếp] ", "[Contact card] ") + item.text;
    } else if (type == "chat.recommended" || type == "chat.link") {
        const QString title = obj.value("title").toString();
        const QString href = linkHrefOf(obj, objParams);
        item.messageType = QStringLiteral("link");
        item.href = href;
        item.mediaUrl = obj.value("thumb").toString();
        item.text = title.isEmpty() ? href : title;
        const QString desc = obj.value("description").toString();
        item.extra = {{"desc", desc == title ? QString() : desc}, {"domain", QUrl(href).host()}};
        label = L("[Liên kết] ", "[Link] ") + item.text;
    } else if (type == "group.poll" || type == "chat.poll" || obj.contains("poll_id")) {
        const QJsonObject& params = objParams;
        const QString action = obj.value("action").toString();
        item.messageType = QStringLiteral("poll");
        const QString q = obj.value("question").toString(params.value("question").toString());
        item.text = q.isEmpty() ? L("Cuộc bình chọn", "Poll") : q;
        label = L("[Bình chọn] ", "[Poll] ") + item.text;
        qint64 pollId = obj.value("poll_id").toVariant().toLongLong();
        if (pollId <= 0) pollId = params.value("pollId").toVariant().toLongLong();
        if (pollId <= 0) pollId = params.value("poll_id").toVariant().toLongLong();
        item.pollData = QVariantMap{{"poll_id", pollId}, {"question", item.text}, {"loaded", false}};
        if (pollId > 0 && m_pollDetails.contains(pollId)) item.pollData = pollView(pollId);
        const bool created = action.isEmpty() || action == QLatin1String("create");
        const QJsonObject templates = params.value("msg").toObject();
        QString vi = templates.value("vi").toString();
        QString en = templates.value("en").toString();
        if (vi.isEmpty()) vi = en;
        if (en.isEmpty()) en = vi;
        const bool anonymousActor = params.value("isAnonymous").toBool() && !vi.contains(QStringLiteral("%1$s"))
                                    && !en.contains(QStringLiteral("%1$s"));
        QString viText = pollTemplateText(vi);
        QString enText = pollTemplateText(en);
        viText.remove(QStringLiteral("Có thành viên"));
        enText.remove(QStringLiteral("A member"));
        if (viText.trimmed().isEmpty()) viText = created ? QStringLiteral("tạo cuộc bình chọn mới") : QStringLiteral("cập nhật cuộc bình chọn");
        if (enText.trimmed().isEmpty()) enText = created ? QStringLiteral("created a new poll") : QStringLiteral("updated the poll");
        item.extra.insert(QStringLiteral("pollTextVi"), viText.simplified());
        item.extra.insert(QStringLiteral("pollTextEn"), enText.simplified());
        item.extra.insert(QStringLiteral("pollAnonActor"), anonymousActor);
        if (!created) item.extra.insert(QStringLiteral("pollEvent"), action);
    } else if (type == "chat.contact" || msg.value("msgType").toInt() == 6 || obj.contains("contactUid")) {
        item.messageType = QStringLiteral("card");
        const QString uid = idOf(obj.value("contactUid"));
        QString cName = obj.value("contactName").toString();
        if (cName.isEmpty()) cName = displayNameOf(uid);
        item.text = cName.isEmpty() ? L("Danh thiếp", "Contact card") : cName;
        item.mediaUrl = m_users.value(uid).avatar;
        item.extra = {{"uid", uid}, {"phone", obj.value("phone").toVariant().toString()},
                      {"qrCodeUrl", obj.value("qrCodeUrl").toString()}};
        label = L("[Danh thiếp] ", "[Contact Card] ") + item.text;
    } else {
        item.text = obj.value("title").toString();
        if (item.text.isEmpty()) item.text = obj.value("description").toString();
        if (item.text.isEmpty())
            item.text = L("[Tin nhắn chưa hỗ trợ ở phiên bản hiện tại]", "[Message not supported in this version]");
        label = item.text;
    }
    if (type == QLatin1String("chat.undo") || type == QLatin1String("chat.delete.everyone")) {
        item.isRecalled = true;
        item.messageType = QStringLiteral("text");
        item.mediaUrl.clear();
        item.href.clear();
        item.extra.clear();
        item.text = type == QLatin1String("chat.undo") ? L("Tin nhắn đã thu hồi", "Message recalled")
                                                       : L("Tin nhắn đã bị xóa", "Message deleted");
        label = item.text;
    }

    for (const QJsonValue& v : msg.value("mentions").toArray()) {
        const QJsonObject m = v.toObject();
        Models::MessageMention mention;
        mention.uid = idOf(m.value("uid"));
        mention.pos = m.value("pos").toInt(-1);
        mention.len = m.value("len").toInt();
        if (mention.pos >= 0 && mention.len > 0 && mention.pos + mention.len <= item.text.size()) {
            item.mentions.append(mention);
        }
    }

    if (preview) {
        *preview = label.simplified();
    }
    if (const qint64 expireOn = Offline::ZaloOfflineStore::expiryOf(msg, item.ts); expireOn > 0) {
        item.extra.insert(QStringLiteral("expireOn"), expireOn);
        item.expireOn = expireOn;
    } else if (item.extra.contains(QStringLiteral("expireOn"))) {
        item.expireOn = item.extra.value(QStringLiteral("expireOn")).toLongLong();
    }

    const QJsonObject decorLog = parseNestedJsonObject(msg.value(QLatin1String("decorLog")));
    const QJsonObject ref = parseNestedJsonObject(msg.value(QLatin1String("reference")));
    item.isForwarded = decorLog.contains(QLatin1String("fw")) || ref.contains(QLatin1String("fw"))
        || msg.contains(QLatin1String("forwardRefLog")) || msg.value(QLatin1String("isForward")).toBool();

    item.isE2EE = msg.value(QLatin1String("e2eeStatus")).toInt() > 0 || msg.value(QLatin1String("e2ee")).toBool()
        || msg.contains(QLatin1String("e2eeExt")) || msg.value(QLatin1String("msgType")).toString() == QLatin1String("chat.e2ee.replace")
        || (msg.contains(QLatin1String("params")) && msg.value(QLatin1String("params")).toString().contains(QLatin1String("e2ee")));

    const QString snapshotReaction = parseReactionSnapshot(msg);
    if (!snapshotReaction.isEmpty()) item.reaction = snapshotReaction;

    const QJsonArray seenUids = msg.value(QLatin1String("seenUids")).toArray();
    for (const QJsonValue& v : seenUids) {
        const QString u = idOf(v);
        if (!u.isEmpty() && u != QLatin1String("0") && !item.seenUids.contains(u)) item.seenUids.append(u);
    }
    if (!item.seenUids.isEmpty() && item.isMine && item.status.isEmpty()) item.status = QStringLiteral("seen");

    return item;
}

void ZaloClient::handleIncomingMessages(const QJsonArray& messages, bool isGroup, bool isOld) {
    if (isOld) setSyncing(false);
    if (!isOld && m_readPointSyncTimer && m_readPointSyncTimer->isActive()) {
        const qint64 untilDeadline = kReadPointMaxWaitMs - m_readPointSyncFirstAsk.elapsed();
        if (untilDeadline > 0)
            m_readPointSyncTimer->start(static_cast<int>(qMin<qint64>(m_readPointSyncTimer->interval(), untilDeadline)));
    }
    m_conversationModel->beginBatch();
    struct BatchGuard {
        Models::ConversationModel* model;
        ~BatchGuard() { model->endBatch(); }
    } batchGuard{m_conversationModel};
    QHash<QString, QVector<Models::MessageItem>> byThread;
    QHash<QString, QJsonObject> batchRaws;
    QHash<QString, QVector<QPair<Models::MessageItem, QJsonObject>>> coldThreads;
    QJsonArray receiptCandidates;
    QHash<QString, QPair<QString, QString>> recalls;
    if (m_offlineStore) m_offlineStore->beginBatch();
    for (const QJsonValue& v : messages) {
        const QJsonObject msg = v.toObject();
        const QString target = undoTargetOf(msg);
        if (target.isEmpty()) continue;
        const QString uidFrom = idOf(msg.value("uidFrom"));
        const QString thread = isGroup ? idOf(msg.value("idTo"))
                                       : ((uidFrom == "0" || uidFrom == m_myUid) ? idOf(msg.value("idTo")) : uidFrom);
        recalls.insert(target, qMakePair(thread, msg.value("msgType").toString() == QLatin1String("chat.delete.everyone")
                                                     ? L("Tin nhắn đã bị xóa", "Message deleted")
                                                     : L("Tin nhắn đã được thu hồi", "Message recalled")));
    }

    for (const QJsonValue& v : messages) {
        const QJsonObject msg = v.toObject();
        const QJsonValue content = msg.value("content");

        if (msg.value("msgType").toString() == QLatin1String("chat.delete")) {
            handleRemoteDeletes(content.isArray() ? content.toArray() : QJsonArray{content}, isGroup);
            ++m_ingestDrops[QStringLiteral("remote_delete")];
            continue;
        }

        if (!undoTargetOf(msg).isEmpty()) {
            ++m_ingestDrops[QStringLiteral("undo_target")];
            continue;
        }

        QString threadId;
        QString preview;
        Models::MessageItem item = toMessageItem(msg, isGroup, &threadId, &preview);
        if (threadId.isEmpty() || item.id.isEmpty()) {
            ++m_ingestDrops[QStringLiteral("no_thread_or_id")];
            continue;
        }
        if (const auto recall = recalls.constFind(item.id); recall != recalls.cend()) {
            item.isRecalled = true;
            item.text = recall->second;
            item.messageType = "text";
            item.mediaUrl.clear();
            item.href.clear();
            preview = recall->second;
            recalls.erase(recall);
        }
        if (const qint64 expireOn = item.extra.value(QStringLiteral("expireOn")).toLongLong();
            expireOn > 0 && expireOn <= QDateTime::currentMSecsSinceEpoch()) {
            if (m_offlineStore) m_offlineStore->deleteMessage(item.id);
            ++m_ingestDrops[QStringLiteral("expired_ttl")];
            continue;
        }
        if (item.messageType == QLatin1String("poll")) {
            const qint64 pollId = item.pollData.value(QStringLiteral("poll_id")).toLongLong();
            if (pollId > 0) {
                m_pollGroups.insert(pollId, threadId);
                if (m_pollDetails.contains(pollId)) item.pollData = pollView(pollId);
                if (item.extra.contains(QStringLiteral("pollEvent")) && !isOld) refreshPoll(pollId, true);
            }
        }
        const bool alreadySeen = m_messageThread.contains(item.id) || m_recentIds.contains(item.id);
        rememberMessageId(item.id);
        if (storeOnlyIngest(isThreadResident(threadId), cacheUsable(), m_phoneImporting)) {
            coldThreads[threadId].append(qMakePair(item, msg));
        } else {
            byThread[threadId].append(item);
            batchRaws.insert(item.id, msg);
            if (!cacheUsable()) m_rawMessages.insert(item.id, msg);
            m_messageThread.insert(item.id, threadId);
            if (item.messageType == "sticker" && !item.extra.contains("sticker")) {
                resolveSticker(threadId, item.id, content.toObject().value("id").toVariant().toLongLong());
            } else if (item.messageType == "bankcard" && item.extra.value("accountNumber").toString().isEmpty()) {
                const QString dataUrl = item.extra.value("dataUrl").toString();
                if (!dataUrl.isEmpty()) resolveZInstantBankCard(threadId, item.id, dataUrl);
            }
        }
        if (!isOld && !item.isMine) {
            const bool wasTyping = m_typingUntil.remove(threadId) > 0;
            m_typingUser.remove(threadId);
            m_conversationModel->setTyping(threadId, false);
            if (wasTyping && threadId == m_currentConvId) emit conversationStatusChanged();
        }

        ensureConversation(threadId, isGroup);
        if (!item.isMine && !item.senderId.isEmpty() && !m_users.contains(item.senderId)) {
            m_pendingUserLookups.insert(item.senderId);
            m_userLookupTimer->start();
        }

        const QString senderDisplay = resolvedSenderName(item);
        const QString prefix = previewPrefixFor(item, isGroup);
        const qint64 prevLastTs = m_conversationModel->conversation(threadId).lastTs;
        if (isGroup && item.ts >= prevLastTs) {
            if (!item.isMine && item.messageType != QLatin1String("system") && !item.senderId.isEmpty()) {
                m_lastGroupSender.insert(threadId, {item.senderId, item.ts});
            } else {
                m_lastGroupSender.remove(threadId);
            }
        }
        const bool fresh = !isOld && !item.isMine && !alreadySeen;
        if (fresh) receiptCandidates.append(msg);
        const bool quiet = isQuietMessageType(msg.value("msgType").toString())
                           || item.extra.contains(QStringLiteral("pollEvent"));
        bool numericId = false;
        const qulonglong numericMsgId = item.id.toULongLong(&numericId);
        const bool alreadyRead = numericId && numericMsgId > 0 && numericMsgId <= readPointOf(threadId);
        const bool unread = fresh && !quiet && (threadId != m_currentConvId || !m_windowVisible) && !alreadyRead;
        bool hasMention = false;
        if (unread && !m_myUid.isEmpty()) {
            for (const auto& men : item.mentions) {
                if (men.uid == m_myUid || men.uid == QLatin1String("-1")) {
                    hasMention = true;
                    break;
                }
            }
        }
        m_conversationModel->recordMessage(threadId, preview, prefix, item.ts, unread, hasMention);
        if (fresh && quiet) {
        } else if (fresh && m_hiddenThreads.contains(threadId)) {
            if (threadId != m_currentConvId && !isThreadMuted(threadId)) markHiddenUnread(item.ts);
        } else if (fresh && !isThreadMuted(threadId)) {
            const auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
            const bool focused = app && app->focusWindow();
            const bool callRinging = m_calls
                && (m_calls->phase() == QLatin1String("incoming") || m_calls->phase() == QLatin1String("outgoing"));
            if (threadId != m_currentConvId || !focused) {
                if (m_notifySound && !callRinging) m_sound->play("notification", m_selectedNotiSound);
                if (m_notifyNewMessage && !focused) {
                    const QString title = isGroup ? m_groups.value(threadId).name : senderDisplay;
                    const QString body = (isGroup && item.messageType != QLatin1String("system") && !senderDisplay.isEmpty())
                                             ? (senderDisplay + QStringLiteral(": ") + preview)
                                             : preview;
                    showMessageNotification(threadId, title, body);
                }
            }
        }
    }

    for (auto it = coldThreads.cbegin(); it != coldThreads.cend(); ++it) {
        if (m_phoneImporting && isThreadResident(it.key())) m_phoneImportedResident.insert(it.key());
        for (const auto& [item, raw] : it.value()) m_offlineStore->saveMessage(it.key(), item, raw, isGroup);
        m_offlineStore->saveConversation(m_conversationModel->conversation(it.key()));
    }
    for (auto it = byThread.begin(); it != byThread.end(); ++it) {
        mergeCachedThread(it.key());
        touchResidentThread(it.key());
        m_messageModel->mergeMessages(it.key(), it.value());
        for (const auto& item : it.value()) {
            if (m_offlineStore) m_offlineStore->saveMessage(it.key(), item, batchRaws.value(item.id), isGroup);
            if (hasReactions(item.id)) refreshReactionSummary(it.key(), item.id);
        }
        if (m_offlineStore) {
            m_offlineStore->saveConversation(m_conversationModel->conversation(it.key()));
        }
    }
    for (auto it = recalls.cbegin(); it != recalls.cend(); ++it) {
        const QString& thread = it.value().first;
        m_messageModel->markRecalled(thread, it.key(), it.value().second);
        if (isThreadResident(thread) && !m_messageModel->message(thread, it.key()).id.isEmpty()) persistMessage(thread, it.key());
        else if (m_offlineStore) m_offlineStore->markRecalled(it.key(), it.value().second);
        if (m_offlineStore) m_offlineStore->removeUnreadReactions(it.key());
    }
    if (m_offlineStore) m_offlineStore->endBatch();
    refreshUnreadReactions();
    scheduleExpiry();
    if (!byThread.isEmpty()) evictColdThreads();
    trimMessageCaches();
    sendDeliveredReceipts(receiptCandidates, isGroup);
    if (m_windowVisible && byThread.contains(m_currentConvId)) {
        sendSeenForCurrent();
        emit conversationStatusChanged();
    }
}

void ZaloClient::openWelcomeScreen() {
    m_currentConvId.clear();
    m_currentConvName.clear();
    m_currentConvAvatar.clear();
    m_messageModel->loadConversation(QString());
    emit currentConversationChanged();
    emit pinnedMessageChanged();
}

void ZaloClient::selectConversation(const QString& id, const QString& name, const QString& avatar) {
    m_currentConvId = id;
    if (!id.isEmpty() && id != myCloudId()) m_lastChatId = id;
    const Models::ConversationItem existing = m_conversationModel->conversation(id);
    const Profile known = isGroupThread(id) ? m_groups.value(id) : m_users.value(id);
    m_currentConvName = !name.isEmpty() ? name : (!existing.name.isEmpty() ? existing.name : known.name);
    m_currentConvAvatar = !avatar.isEmpty() ? avatar : (!existing.avatarUrl.isEmpty() ? existing.avatarUrl : known.avatar);
    mergeCachedThread(id);
    touchResidentThread(id);
    evictColdThreads();
    trimMessageCaches();
    m_messageModel->loadConversation(id);
    m_messageModel->setUnreadMarker(id, m_conversationModel->conversation(id).unreadCount);
    m_conversationModel->clearUnread(id);
    if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
    m_notifications->close(QStringLiteral("thread:") + id);
    if (m_markedUnread.contains(id)) toggleUnreadMark(id);
    m_currentLastOnline = 0;
    emit currentConversationChanged();
    emit conversationStatusChanged();
    emit pinnedMessageChanged();
    emit composerStateChanged();

    if (m_isLoggedIn && !id.isEmpty()) {
        sendSeenForCurrent();
        refreshStrangerRank(id);
        if (!currentConversationIsGroup() && id != myCloudId()) {
            m_apiClient->session()->fetchLastOnline(id, [this, id](bool ok, const QJsonValue& data, const QString&, int) {
                if (!ok || id != m_currentConvId) return;
                const QJsonObject obj = data.toObject();
                if (!obj.value("settings").toObject().value("show_online_status").toBool(true)) return;
                qint64 ts = obj.value("lastOnline").toVariant().toLongLong();
                if (ts > 0 && ts < 10000000000LL) ts *= 1000;
                m_currentLastOnline = ts > 0 && ts < kMinPlausibleEpochMs ? -1 : ts;
                emit conversationStatusChanged();
            });
        }
    }

    fetchInitialConversationHistory(id);
}

void ZaloClient::fetchInitialConversationHistory(const QString& id) {
    if (id.isEmpty() || !m_isLoggedIn || m_historyLoaded.contains(id)) return;
    m_historyLoaded.insert(id);
    const bool isGroup = isGroupThread(id) || m_conversationModel->conversation(id).isGroup;
    const bool isMyCloud = (id == myCloudId());
    if (isGroup || isMyCloud) {
        m_loadingHistory = true;
        emit historyLoadingChanged();
        fetchCloudHistoryPage(id, isGroup, isMyCloud, QString(), false, true, true);
    } else {
        if (m_messageModel->threadSize(id) < 20) loadOlderMessages();
        if (m_apiClient->session()->isRealtimeConnected()) m_apiClient->session()->requestOldMessages(false);
    }
}

void ZaloClient::fetchCloudHistoryPage(const QString& convId, bool isGroup, bool isMyCloud, const QString& cursor,
                                       bool useOld, bool initialOpen, bool allowOldFollowup) {
    ZaloSession* session = m_apiClient->session();
    const quint64 gen = m_accountLoadGen;
    QPointer<ZaloClient> guard(this);
    session->fetchCloudHistory(convId, isMyCloud, cursor, 50,
        [guard, this, convId, isGroup, isMyCloud, session, cursor, useOld, initialOpen, allowOldFollowup, gen](
            bool ok, const QJsonValue& data, const QString&, int errorCode) {
            if (!guard) return;
            if (gen != guard->m_accountLoadGen) {
                guard->m_loadingHistory = false;
                emit guard->historyLoadingChanged();
                return;
            }
            if (!ok) {
                if (errorCode == -69) {
                    m_historyExhausted.insert(convId);
                    m_loadingHistory = false;
                    emit historyLoadingChanged();
                    return;
                }
                if (initialOpen && isGroup) {
                    session->fetchGroupHistory(convId, 50,
                        [guard, this, isGroup, gen](bool gOk, const QJsonValue& gData, const QString&, int) {
                            if (!guard || gen != guard->m_accountLoadGen) return;
                            if (gOk) {
                                QJsonArray gMsgs;
                                unwrapCloudHistoryPayload(gData, &gMsgs);
                                handleIncomingMessages(gMsgs, isGroup, true);
                            }
                            m_loadingHistory = false;
                            emit historyLoadingChanged();
                        });
                    return;
                }
                m_loadingHistory = false;
                emit historyLoadingChanged();
                if (!initialOpen && session->isRealtimeConnected()) {
                    session->requestOldMessages(isGroup, m_messageModel->oldestMessageId(convId));
                }
                return;
            }

            QJsonArray msgs;
            const QJsonObject obj = unwrapCloudHistoryPayload(data, &msgs);
            const bool hasMorePresent = obj.contains("hasMore");
            const bool hasMore = obj.value("hasMore").toVariant().toInt() != 0;
            const bool isFiltered = obj.value("isFiltered").toVariant().toInt() != 0;
            const bool isOldPresent = obj.contains("isOld");
            const bool isOld = isOldPresent ? (obj.value("isOld").toVariant().toInt() != 0) : useOld;
            const QString nextCursor = idOf(obj.value("lastMsgId"));
            if (hasMorePresent || isOldPresent || !nextCursor.isEmpty()) {
                CloudPageState& st = m_cloudPageState[convId];
                st.initialized = true;
                st.useOld = isOld;
                if (!nextCursor.isEmpty() && nextCursor != QLatin1String("0")) st.lastMsgId = nextCursor;
            }
            const int before = m_messageModel->threadSize(convId);
            handleIncomingMessages(msgs, isGroup, true);
            const int after = m_messageModel->threadSize(convId);

            const bool exhaustedByMore = hasMorePresent && !hasMore && (!initialOpen || !msgs.isEmpty() || !isOld);
            const bool emptyStop = !initialOpen && msgs.isEmpty()
                && (isFiltered || !hasMorePresent || nextCursor.isEmpty() || nextCursor == cursor);
            const bool stalledStop = !initialOpen && !hasMorePresent && after == before;
            if (exhaustedByMore || emptyStop || stalledStop) {
                m_historyExhausted.insert(convId);
            }

            const CloudPageState stAfter = m_cloudPageState.value(convId);
            const bool needInitialOld = initialOpen && msgs.size() < 20 && (stAfter.useOld || msgs.size() <= 1);
            const bool needScrollOld = !initialOpen && !useOld && stAfter.useOld && hasMore && after == before;
            if (allowOldFollowup && !m_historyExhausted.contains(convId) && (needInitialOld || needScrollOld)) {
                const QString nextOldCursor = (!stAfter.lastMsgId.isEmpty() && stAfter.lastMsgId != QLatin1String("0"))
                    ? stAfter.lastMsgId
                    : m_messageModel->oldestMessageId(convId);
                fetchCloudHistoryPage(convId, isGroup, isMyCloud, nextOldCursor, true, false, false);
                return;
            }

            m_loadingHistory = false;
            emit historyLoadingChanged();
        },
        useOld);
}

void ZaloClient::loadOlderMessages() {
    if (m_currentConvId.isEmpty() || m_loadingHistory) return;
    const QString convId = m_currentConvId;
    const bool isGroup = currentConversationIsGroup();
    const bool isMyCloud = convId == myCloudId();
    const CloudPageState st = m_cloudPageState.value(convId);
    if (!m_isLoggedIn || (!isGroup && !isMyCloud) || st.initialized) {
        if (loadOlderFromCache(convId)) return;
    }
    if (!m_isLoggedIn) return;
    ZaloSession* session = m_apiClient->session();
    if (!isGroup && !isMyCloud) {
        if (session->isRealtimeConnected()) session->requestOldMessages(false, m_messageModel->oldestMessageId(convId));
        return;
    }
    if (m_historyExhausted.contains(convId)) return;
    const QString cursor = (!st.lastMsgId.isEmpty() && st.lastMsgId != QLatin1String("0"))
        ? st.lastMsgId
        : m_messageModel->oldestMessageId(convId);
    const bool useOld = st.initialized ? st.useOld : true;
    m_loadingHistory = true;
    emit historyLoadingChanged();
    fetchCloudHistoryPage(convId, isGroup, isMyCloud, cursor, useOld, false, !useOld);
}

void ZaloClient::sendMessage(const QString& text) {
    sendMessageWithMentions(text, {});
}

void ZaloClient::sendMessageWithMentions(const QString& text, const QVariantList& mentionList) {
    sendComposedMessage(text, mentionList, {});
}

void ZaloClient::sendComposedMessage(const QString& text, const QVariantList& mentionList, const QVariantList& styleList) {
    const QString cleanText = text.trimmed();
    if (cleanText.isEmpty() || m_currentConvId.isEmpty()) return;
    int lead = 0;
    while (lead < text.size() && text.at(lead).isSpace()) ++lead;
    QJsonArray mentions;
    QVector<Models::MessageMention> localMentions;
    for (const QVariant& v : mentionList) {
        const QVariantMap m = v.toMap();
        Models::MessageMention mention;
        mention.uid = m.value("uid").toString();
        mention.pos = m.value("pos").toInt() - lead;
        mention.len = m.value("len").toInt();
        if (mention.uid.isEmpty() || mention.pos < 0 || mention.len <= 0 || mention.pos + mention.len > cleanText.size()) continue;
        localMentions.append(mention);
        mentions.append(QJsonObject{{"uid", mention.uid}, {"pos", mention.pos}, {"len", mention.len}});
    }

    QJsonArray styles;
    for (const QVariant& v : styleList) {
        const QVariantMap m = v.toMap();
        const QString st = m.value("st").toString();
        int start = m.value("start").toInt() - lead;
        int end = start + m.value("len").toInt();
        start = qMax(0, start);
        end = qMin(end, int(cleanText.size()));
        if (!Models::RichComposer::isKnownStyle(st) || end <= start) continue;
        styles.append(QJsonObject{{"start", start}, {"len", end - start}, {"st", st}});
    }
    const QJsonObject quoted = m_replyTarget.isEmpty() ? QJsonObject()
                                                       : rawMessage(m_replyTarget.value("id").toString());
    sendTextTo(m_currentConvId, currentConversationIsGroup(), cleanText, localMentions, mentions, quoted, styles);
}

void ZaloClient::sendTextTo(const QString& convId, bool isGroup, const QString& cleanText,
                            const QVector<Models::MessageMention>& localMentions, const QJsonArray& mentions,
                            const QJsonObject& quoted, const QJsonArray& styles) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    m_lastClientId = qMax(now, m_lastClientId + 1);
    local.cliMsgId = QString::number(m_lastClientId);
    local.text = cleanText;
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sending");
    if (isGroup) local.mentions = localMentions;
    if (!styles.isEmpty()) local.extra.insert("styles", styles.toVariantList());
    if (!quoted.isEmpty()) {
        local.quoteMsgId = m_replyTarget.value("id").toString();
        local.quoteSender = m_replyTarget.value("senderName").toString();
        local.quoteText = m_replyTarget.value("text").toString();
    }
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, cleanText, L("Bạn: ", "You: "), now, false);
    if (m_offlineStore && m_conversationModel->contains(convId)) m_offlineStore->saveConversation(m_conversationModel->conversation(convId));
    setDraft(convId, QString());
    if (convId == m_currentConvId) m_messageModel->clearUnreadMarker();
    if (!quoted.isEmpty()) clearReplyTarget();
    emit messageSent(cleanText);

    if (!m_isLoggedIn) {
        m_messageModel->setStatus(convId, local.id, QStringLiteral("failed"));
        if (m_isOfflineMode) {
            emit notice(L("Đang ngoại tuyến nên chưa gửi được tin nhắn. Hãy gửi lại khi đã kết nối.",
                          "You're offline, so the message wasn't sent. Send it again once connected."));
        }
        return;
    }
    sendTextNow(convId, isGroup, cleanText, isGroup ? mentions : QJsonArray(), quoted, local.id, local.cliMsgId, styles);
}

void ZaloClient::sendTextNow(const QString& convId, bool isGroup, const QString& text, const QJsonArray& mentions,
                             const QJsonObject& quoted, const QString& localId, const QString& cliMsgId,
                             const QJsonArray& styles) {
    auto done = [this, convId, localId, text](bool ok, const QJsonValue& data, const QString& error, int code) {
        if (ok) {
            const QString serverId = idOf(data.toObject().value("msgId"));
            m_messageModel->setStatus(convId, localId, QStringLiteral("sent"));
            if (!serverId.isEmpty()) m_messageModel->confirmMessage(convId, localId, serverId);
        } else {
            m_messageModel->setStatus(convId, localId, QStringLiteral("failed"));
            if (code == 164 && isGroupThread(convId)) {
                markThreadReadOnly(convId, QStringLiteral("removed"), QDateTime::currentMSecsSinceEpoch());
                return;
            }
            emit notice(L("Không gửi được tin nhắn (%1): ", "Message not sent (%1): ").arg(friendlyError(error, code))
                        + text.left(40));
        }
    };
    ZaloSession* session = m_apiClient->session();
    const qint64 ttl = m_autoDeleteTtl.value(convId, 0);
    if (!quoted.isEmpty()) {
        session->sendQuote(convId, isGroup, text, quoted, done, cliMsgId, styles, mentions, ttl);
        return;
    }
    if (!mentions.isEmpty()) {
        session->sendMention(convId, text, mentions, done, cliMsgId, styles, ttl);
        return;
    }
    static const QRegularExpression urlRe(QStringLiteral(R"(https?://[^\s<>"']+)"));
    const QRegularExpressionMatch url = urlRe.match(text);
    if (!url.hasMatch() || !styles.isEmpty()) {
        session->sendText(convId, isGroup, text, done, cliMsgId, styles, ttl);
        return;
    }
    QString link = url.captured(0);
    while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
    QPointer<ZaloSession> guard(session);
    session->parseLink(link, [guard, convId, isGroup, text, done, cliMsgId, ttl](bool ok, const QJsonValue& data,
                                                                           const QString&, int) {
        if (!guard) return;
        const QJsonObject preview = data.toObject().value("data").toObject();
        if (!ok || preview.value("href").toString().isEmpty()) {
            guard->sendText(convId, isGroup, text, done, cliMsgId, QJsonArray(), ttl);
            return;
        }
        guard->sendLink(convId, isGroup, text, preview,
            [guard, convId, isGroup, text, done, cliMsgId, ttl](bool linkOk, const QJsonValue& linkData,
                                                                 const QString& linkErr, int linkCode) {
                if (linkOk || !guard) {
                    done(linkOk, linkData, linkErr, linkCode);
                    return;
                }
                guard->sendText(convId, isGroup, text, done, cliMsgId, QJsonArray(), ttl);
            },
            cliMsgId, ttl);
    });
}

void ZaloClient::setReplyTarget(const QString& msgId) {
    const QVariantMap msg = m_messageModel->getMessageById(msgId);
    if (msg.isEmpty() || rawMessage(msgId).isEmpty()) {
        clearReplyTarget();
        return;
    }
    QString text = msg.value("text").toString();
    if (text.isEmpty()) {
        const QString type = msg.value("messageType").toString();
        text = type == "image" ? L("[Hình ảnh]", "[Photo]") : type == "sticker" ? L("[Sticker]", "[Sticker]")
             : type == "file" ? L("[Tệp]", "[File]") : L("[Tin nhắn]", "[Message]");
    }
    QString sender = msg.value("isMine").toBool() ? L("Bạn", "You") : msg.value("senderName").toString();
    if (sender.isEmpty()) sender = m_users.value(msg.value("senderId").toString()).name;
    if (sender.isEmpty() && !currentConversationIsGroup()) sender = m_currentConvName;
    m_replyTarget = {{"id", msgId}, {"senderName", sender}, {"text", text}};
    emit replyTargetChanged();
}

void ZaloClient::clearReplyTarget() {
    if (m_replyTarget.isEmpty()) return;
    m_replyTarget.clear();
    emit replyTargetChanged();
}

void ZaloClient::sendAttachment(const QString& type, const QString& name, const QString& url) {
    Q_UNUSED(type);
    Q_UNUSED(name);
    if (url.isEmpty()) {
        emit notice(L("Hãy chọn một tệp để gửi.", "Choose a file to send."));
        return;
    }
    sendFile(url);
}

bool ZaloClient::currentConversationIsGroup() const {
    return !m_currentConvId.isEmpty() &&
           (m_groupIds.contains(m_currentConvId) || m_conversationModel->conversation(m_currentConvId).isGroup);
}

int ZaloClient::currentConversationMemberCount() const {
    return m_groups.value(m_currentConvId).memberCount;
}

bool ZaloClient::currentConversationPinned() const {
    return m_conversationModel->conversation(m_currentConvId).isPinned;
}

bool ZaloClient::currentConversationMuted() const {
    return m_mutedIds.contains(m_currentConvId);
}

QString ZaloClient::myCloudId() const {
    return m_apiClient->session()->send2meId();
}

void ZaloClient::openMyCloud() {
    const QString id = myCloudId();
    if (id.isEmpty()) {
        emit notice(L("My Documents chưa sẵn sàng.", "My Documents isn't available yet."));
        return;
    }
    openConversationWithContact(id, L("My Documents", "My Documents"), m_users.value(id).avatar);
}

void ZaloClient::openMessagesTab() {
    setCurrentNavigationTab(QStringLiteral("messages"));
    const QString cloud = myCloudId();
    if (m_currentConvId.isEmpty() || cloud.isEmpty() || m_currentConvId != cloud) return;
    if (!m_lastChatId.isEmpty() && m_lastChatId != cloud && m_conversationModel->contains(m_lastChatId)) {
        selectConversation(m_lastChatId, QString(), QString());
    } else {
        openWelcomeScreen();
    }
}

QVariantList ZaloClient::cloudSharedItems(const QString& kind) const {
    const QString cloudId = myCloudId();
    if (cloudId.isEmpty() || !m_isLoggedIn) {
        return {};
    }

    auto tagList = [](QVariantList list, const QString& type) {
        for (auto& item : list) {
            QVariantMap m = item.toMap();
            m[QStringLiteral("itemType")] = type;
            if (m.value(QStringLiteral("title")).toString().isEmpty()) {
                m[QStringLiteral("title")] = (type == QLatin1String("media"))
                    ? (m.value(QStringLiteral("isVideo")).toBool() ? QStringLiteral("Video") : QStringLiteral("Hình ảnh"))
                    : (type == QLatin1String("file") ? QStringLiteral("Tập tin") : QStringLiteral("Liên kết"));
            }
            item = m;
        }
        return list;
    };

    if (m_currentConvId == cloudId && m_messageModel) {
        if (kind == QLatin1String("media")) return tagList(m_messageModel->sharedItems(QStringLiteral("media"), 100), QStringLiteral("media"));
        if (kind == QLatin1String("file")) return tagList(m_messageModel->sharedItems(QStringLiteral("file"), 100), QStringLiteral("file"));
        if (kind == QLatin1String("link")) return tagList(m_messageModel->sharedItems(QStringLiteral("link"), 100), QStringLiteral("link"));
        QVariantList all;
        all.append(tagList(m_messageModel->sharedItems(QStringLiteral("media"), 50), QStringLiteral("media")));
        all.append(tagList(m_messageModel->sharedItems(QStringLiteral("file"), 50), QStringLiteral("file")));
        all.append(tagList(m_messageModel->sharedItems(QStringLiteral("link"), 50), QStringLiteral("link")));
        std::sort(all.begin(), all.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap().value("ts").toLongLong() > b.toMap().value("ts").toLongLong();
        });
        return all;
    }

    if (!m_offlineStore) return {};
    const auto msgs = m_offlineStore->loadMessages(cloudId, nullptr, 300);
    static const QRegularExpression urlRegex(QStringLiteral(R"(https?://[^\s<>"']+)"));

    QVariantList mediaList;
    QVariantList fileList;
    QVariantList linkList;

    for (int i = msgs.size() - 1; i >= 0; --i) {
        const auto& m = msgs.at(i);
        if (m.isRecalled) continue;
        QVariantMap e{
            {QStringLiteral("msgId"), m.id},
            {QStringLiteral("senderName"), resolvedSenderName(m)},
            {QStringLiteral("ts"), m.ts},
            {QStringLiteral("timeStr"), m.timeStr}
        };

        if (m.messageType == QLatin1String("image") || m.messageType == QLatin1String("video")) {
            if (m.mediaUrl.isEmpty()) continue;
            e[QStringLiteral("itemType")] = QStringLiteral("media");
            e[QStringLiteral("thumb")] = m.mediaUrl;
            e[QStringLiteral("url")] = m.href.isEmpty() ? m.mediaUrl : m.href;
            const bool isVid = (m.messageType == QLatin1String("video"));
            e[QStringLiteral("isVideo")] = isVid;
            e[QStringLiteral("title")] = m.text.isEmpty() ? (isVid ? QStringLiteral("Video") : QStringLiteral("Hình ảnh")) : m.text;
            mediaList.append(e);
        } else if (m.messageType == QLatin1String("file")) {
            e[QStringLiteral("itemType")] = QStringLiteral("file");
            e[QStringLiteral("title")] = m.text.isEmpty() ? QStringLiteral("Tập tin") : m.text;
            e[QStringLiteral("url")] = m.href.isEmpty() ? m.mediaUrl : m.href;
            e[QStringLiteral("fileSize")] = m.fileSize;
            fileList.append(e);
        } else if (m.messageType == QLatin1String("link")) {
            e[QStringLiteral("itemType")] = QStringLiteral("link");
            e[QStringLiteral("url")] = m.href;
            e[QStringLiteral("title")] = m.text.isEmpty() ? m.href : m.text;
            e[QStringLiteral("thumb")] = m.mediaUrl;
            linkList.append(e);
        } else {
            const auto match = urlRegex.match(m.text);
            if (match.hasMatch()) {
                QString link = match.captured(0);
                while (!link.isEmpty() && QStringLiteral(".,;:!?)]}").contains(link.back())) link.chop(1);
                e[QStringLiteral("itemType")] = QStringLiteral("link");
                e[QStringLiteral("url")] = link;
                e[QStringLiteral("title")] = link;
                linkList.append(e);
            }
        }
    }

    if (kind == QLatin1String("media")) return mediaList;
    if (kind == QLatin1String("file")) return fileList;
    if (kind == QLatin1String("link")) return linkList;

    QVariantList all = mediaList;
    all.append(fileList);
    all.append(linkList);
    std::sort(all.begin(), all.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("ts").toLongLong() > b.toMap().value("ts").toLongLong();
    });
    return all;
}

void ZaloClient::openConversationWithContact(const QString& id, const QString& name, const QString& avatarUrl) {
    const bool isGroup = isGroupThread(id);
    if (!m_conversationModel->contains(id)) {
        ensureConversation(id, isGroup);
        m_conversationModel->updateProfile(id, name, avatarUrl);
        if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
    }
    setCurrentNavigationTab("messages");
    selectConversation(id, name, avatarUrl);
}

void ZaloClient::findUserByPhone(const QString& phone) {
    if (!m_isLoggedIn) {
        emit userSearchFinished(false, {}, L("Bạn cần đăng nhập trước.", "Please sign in first."));
        return;
    }
    m_apiClient->session()->findUserByPhone(phone, [this](bool ok, const QJsonValue& data, const QString& error, int code) {
        const QJsonObject obj = data.toObject();
        const QString uid = idOf(obj.value("uid"));
        if (!ok || uid.isEmpty()) {
            emit userSearchFinished(false, {}, code == 216 || ok
                ? L("Số điện thoại chưa đăng ký Zalo hoặc không cho phép tìm kiếm.",
                    "This number isn't on Zalo or doesn't allow search.")
                : L("Không tìm được: ", "Search failed: ") + friendlyError(error));
            return;
        }
        QVariantMap profile;
        profile["uid"] = uid;
        profile["name"] = pickName(obj);
        profile["avatar"] = obj.value("avatar").toString();
        m_users[uid] = {profile["name"].toString(), profile["avatar"].toString(), 0};
        noteAccountFlags(uid, obj);
        emit userSearchFinished(true, profile, QString());
    });
}

void ZaloClient::sendFriendRequest(const QString& uid, const QString& message) {
    m_apiClient->session()->sendFriendRequest(uid, message, [this](bool ok, const QJsonValue&, const QString& error, int) {
        emit friendRequestFinished(ok, ok ? L("Đã gửi lời mời kết bạn.", "Friend request sent.")
                                          : L("Không gửi được lời mời: ", "Couldn't send the request: ") + friendlyError(error));
    });
}

void ZaloClient::createGroup(const QString& name, const QStringList& members) {
    if (members.size() < 2) {
        emit groupCreateFinished(false, L("Nhóm cần ít nhất 3 thành viên (gồm cả bạn).",
                                          "A group needs at least 3 members including you."));
        return;
    }
    m_apiClient->session()->createGroup(name, members,
        [this, name](bool ok, const QJsonValue& data, const QString& error, int) {
            if (!ok) {
                emit groupCreateFinished(false, L("Không tạo được nhóm: ", "Couldn't create the group: ") + friendlyError(error));
                return;
            }
            const QString groupId = idOf(data.toObject().value("groupId"));
            emit groupCreateFinished(true, L("Đã tạo nhóm.", "Group created."));
            if (!groupId.isEmpty()) {
                m_groupIds.insert(groupId);
                m_groups[groupId] = {name, QString(), 0};
                loadGroupInfo({groupId});
                openConversationWithContact(groupId, name, QString());
            }
        });
}

void ZaloClient::pinConversation(const QString& id) {
    if (id.isEmpty()) return;
    const bool pinned = !m_conversationModel->conversation(id).isPinned;
    const bool isGroup = isGroupThread(id);
    m_apiClient->session()->apiPost("conversation", "/api/pinconvers/updatev2",
        {{"actionType", pinned ? 1 : 2}, {"conversations", QJsonArray{(isGroup ? "g" : "u") + id}}},
        [this, id, pinned](bool ok, const QJsonValue&, const QString&, int) {
            if (!ok) {
                emit notice(L("Không thể cập nhật ghim hội thoại.", "Couldn't update the pinned state."));
                return;
            }
            m_conversationModel->setPinned(id, pinned);
            if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
            emit currentConversationChanged();
        });
}

void ZaloClient::muteConversation(const QString& id) {
    if (id.isEmpty()) return;
    muteConversationFor(id, isThreadMuted(id) ? 0 : -1);
}

void ZaloClient::muteConversationFor(const QString& id, int seconds) {
    if (id.isEmpty()) return;
    qint64 duration = seconds;
    if (seconds == -2) {
        const QDateTime now = QDateTime::currentDateTime();
        QDateTime next(now.date(), QTime(8, 0));
        if (now.time().hour() >= 8) next = next.addDays(1);
        duration = qMax<qint64>(60, now.secsTo(next));
    }
    const bool mute = duration != 0;
    m_apiClient->session()->setMute(id, isGroupThread(id), duration,
        [this, id, mute, duration](bool ok, const QJsonValue&, const QString& error, int code) {
            if (!ok) {
                emit notice(L("Không thể thay đổi thông báo của hội thoại: ", "Couldn't change notifications for this chat: ")
                            + friendlyError(error, code));
                return;
            }
            if (mute) {
                m_mutedIds.insert(id);
                m_muteUntil.insert(id, duration > 0 ? QDateTime::currentMSecsSinceEpoch() + duration * 1000 : 0);
            } else {
                m_mutedIds.remove(id);
                m_muteUntil.remove(id);
            }
            m_conversationModel->setMuted(id, mute);
            if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
            emit currentConversationChanged();
        });
}

bool ZaloClient::isThreadMuted(const QString& threadId) {
    if (!m_mutedIds.contains(threadId)) return false;
    const qint64 until = m_muteUntil.value(threadId, 0);
    if (until > 0 && until <= QDateTime::currentMSecsSinceEpoch()) {
        m_mutedIds.remove(threadId);
        m_muteUntil.remove(threadId);
        m_conversationModel->setMuted(threadId, false);
        if (m_offlineStore && m_conversationModel->contains(threadId)) m_offlineStore->saveConversation(m_conversationModel->conversation(threadId));
        return false;
    }
    return true;
}

void ZaloClient::toggleUnreadMark(const QString& id) {
    if (id.isEmpty() || !m_isLoggedIn) return;
    const bool unread = !m_markedUnread.contains(id);
    m_apiClient->session()->setUnreadMark(id, isGroupThread(id), unread,
        [this, id, unread](bool ok, const QJsonValue&, const QString& error, int code) {
            if (!ok) {
                emit notice(L("Không thể đánh dấu hội thoại: ", "Couldn't mark the conversation: ")
                            + friendlyError(error, code));
                return;
            }
            if (unread) {
                m_markedUnread.insert(id);
                if (m_conversationModel->conversation(id).unreadCount == 0) m_conversationModel->setUnreadCount(id, 1);
            } else {
                m_markedUnread.remove(id);
                m_conversationModel->clearUnread(id);
            }
            if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
        });
}

void ZaloClient::deleteConversation(const QString& id) {
    if (id.isEmpty()) return;
    if (m_readOnlyThreads.contains(id)) {
        if (id == m_currentConvId) openWelcomeScreen();
        m_messageModel->clearThread(id);
        m_conversationModel->removeConversation(id);
        m_historyLoaded.remove(id);
        m_historyExhausted.remove(id);
        m_cloudPageState.remove(id);
        m_markedUnread.remove(id);
        m_groupIds.remove(id);
        m_groups.remove(id);
        m_groupVersions.remove(id);
        m_groupRosters.remove(id);
        m_groupMembersCache.remove(id);
        if (m_offlineStore) m_offlineStore->deleteConversation(id);
        m_readOnlyThreads.remove(id);
        saveReadOnlyThreads();
        rebuildContacts();
        emit notice(L("Đã xóa hội thoại.", "Conversation deleted."));
        return;
    }
    if (!m_isLoggedIn) return;
    const QJsonObject last = rawMessage(m_messageModel->latestMessageId(id));
    if (last.isEmpty()) {
        emit notice(L("Chưa tải được tin nhắn mới nhất của hội thoại nên chưa thể xóa. Hãy mở hội thoại rồi thử lại.",
                      "The latest message of this chat isn't loaded yet. Open the chat and try again."));
        return;
    }
    m_apiClient->session()->deleteConversation(id, isGroupThread(id), last,
        [this, id](bool ok, const QJsonValue&, const QString& error, int code) {
            if (!ok) {
                emit notice(L("Không thể xóa hội thoại: ", "Couldn't delete the conversation: ") + friendlyError(error, code));
                return;
            }
            if (id == m_currentConvId) openWelcomeScreen();
            m_messageModel->clearThread(id);
            m_conversationModel->removeConversation(id);
            m_historyLoaded.remove(id);
        m_historyExhausted.remove(id);
        m_cloudPageState.remove(id);
            m_markedUnread.remove(id);
            if (m_offlineStore) m_offlineStore->deleteConversation(id);
            emit notice(L("Đã xóa hội thoại.", "Conversation deleted."));
        });
}

bool ZaloClient::loadOfflineCache() {
    if (!m_offlineStore || m_cachedUid.isEmpty() || m_offlineStore->ownerUid() != m_cachedUid) return false;
    if (m_myUid.isEmpty()) m_myUid = m_cachedUid;
    loadDrafts();
    for (const auto& p : m_offlineStore->loadProfiles()) {
        if (p.isGroup) {
            m_groups[p.id] = {p.name, p.avatar, p.memberCount};
            m_groupIds.insert(p.id);
        } else {
            if (!m_users.contains(p.id)) m_users[p.id] = {p.name, p.avatar, 0};
            if (p.isFriend) {
                m_friendIds.insert(p.id);
                m_friendsKnown = true;
            }
            if (p.isOfficial && !p.isFriend) m_officialIds.insert(p.id);
        }
    }
    const auto convs = m_offlineStore->loadConversations();
    m_conversationModel->beginBatch();
    struct BatchGuard {
        Models::ConversationModel* model;
        ~BatchGuard() { model->endBatch(); }
    } batchGuard{m_conversationModel};
    m_offlineStore->beginBatch();
    for (const auto& c : convs) {
        Models::ConversationItem item = c;
        if (item.name.compare(QLatin1String("Cloud của tôi"), Qt::CaseInsensitive) == 0
            || item.name.compare(QLatin1String("My Cloud"), Qt::CaseInsensitive) == 0
            || item.name.compare(QLatin1String("Truyền File"), Qt::CaseInsensitive) == 0) {
            item.name = L("My Documents", "My Documents");
        }
        item.isOnline = false;
        if (item.isMuted) m_mutedIds.insert(item.id);
        if (item.isGroup) m_groupIds.insert(item.id);
        if (item.isGroup && item.senderPrefix.isEmpty() && !item.lastMessage.isEmpty()) {
            const auto latest = m_offlineStore->loadMessages(item.id, nullptr, 1, 0);
            if (!latest.isEmpty()) {
                const auto& last = latest.first();
                item.senderPrefix = previewPrefixFor(last, true);
                if (!last.isMine && last.messageType != QLatin1String("system") && !last.senderId.isEmpty()) {
                    m_lastGroupSender.insert(item.id, {last.senderId, item.lastTs});
                    if (item.senderPrefix.isEmpty() && !m_users.contains(last.senderId)) {
                        m_pendingUserLookups.insert(last.senderId);
                        if (m_userLookupTimer) m_userLookupTimer->start();
                    }
                }
                if (!item.senderPrefix.isEmpty()) m_offlineStore->saveConversation(item);
            }
        }
        m_conversationModel->upsertConversation(item);
    }
    m_offlineStore->endBatch();
    if (m_labelManager && m_offlineStore) {
        const QString cachedLabels = m_offlineStore->metaValue(QStringLiteral("labels_cache"));
        if (!cachedLabels.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(cachedLabels.toUtf8());
            if (doc.isObject()) m_labelManager->applyCached(doc.object());
        }
    }
    rebuildContacts();
    purgeExpiredMessages();
    migrateCachedParses();
    return !convs.isEmpty();
}

void ZaloClient::migrateCachedParses() {
    if (!m_offlineStore) return;
    const QString version = QString::number(kCacheParseVersion);
    if (m_offlineStore->metaValue(QStringLiteral("parse_version")) == version) return;
    const QString migrationName = QStringLiteral("cachedParseMigration");
    if (findChild<QTimer*>(migrationName)) return;
    const QString owner = m_offlineStore->ownerUid();
    const Offline::ZaloOfflineStore::StoredReparseFn parse =
        [this](const QJsonObject& raw, bool isGroup, Models::MessageItem* out) {
            if (!isReparsedOnLoad(raw.value("msgType").toString()) && !raw.contains("quote")
                && !raw.contains("mentions")) return false;
            QString thread, preview;
            *out = toMessageItem(raw, isGroup, &thread, &preview);
            return out->messageType != QLatin1String("poll_update");
        };
    struct Cursor {
        qint64 afterRowId{0};
        QSet<QString> touched;
    };
    auto cursor = std::make_shared<Cursor>();
    QTimer* timer = new QTimer(this);
    timer->setObjectName(migrationName);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer, owner, parse, cursor, version]() {
        if (!m_offlineStore || m_offlineStore->ownerUid() != owner) {
            timer->deleteLater();
            return;
        }
        if (m_offlineStore->reparseStoredChunk(parse, cursor->afterRowId, Offline::ZaloOfflineStore::kReparseChunkRows, cursor->touched)) {
            timer->start();
            return;
        }
        timer->deleteLater();
        for (const QString& convId : std::as_const(cursor->touched)) {
            const auto latest = m_offlineStore->loadMessages(convId, nullptr, 1, 0);
            if (!latest.isEmpty()) {
                const QString prefix = previewPrefixFor(latest.first(), isGroupThread(convId));
                m_conversationModel->updateLastMessage(convId, latest.first().text, latest.first().timeStr, prefix, true);
                m_offlineStore->saveConversation(m_conversationModel->conversation(convId));
            }
        }
        m_offlineStore->setMetaValue(QStringLiteral("parse_version"), version);
    });
    timer->start(0);
}

void ZaloClient::purgeExpiredMessages() {
    if (!m_offlineStore) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> touched;
    for (const auto& [convId, msgId] : m_offlineStore->purgeExpired(now)) {
        m_messageModel->removeMessage(convId, msgId);
        touched.insert(convId);
    }
    if (touched.contains(m_currentConvId)) refreshUnreadReactions();
    for (const QString& convId : std::as_const(touched)) {
        const auto latest = m_offlineStore->loadMessages(convId, nullptr, 1, 0);
        const QString prefix = latest.isEmpty() ? QString() : previewPrefixFor(latest.first(), isGroupThread(convId));
        m_conversationModel->updateLastMessage(convId, latest.isEmpty() ? QString() : latest.first().text,
                                               latest.isEmpty() ? QString() : latest.first().timeStr, prefix, true);
        m_offlineStore->saveConversation(m_conversationModel->conversation(convId));
    }
    scheduleExpiry();
}

void ZaloClient::scheduleExpiry() {
    if (!m_offlineStore) return;
    if (!m_expiryTimer) {
        m_expiryTimer = new QTimer(this);
        m_expiryTimer->setSingleShot(true);
        connect(m_expiryTimer, &QTimer::timeout, this, &ZaloClient::purgeExpiredMessages);
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 next = m_offlineStore->nextExpiry(now);
    if (next <= 0) {
        m_expiryTimer->stop();
        return;
    }
    m_expiryTimer->start(int(qBound<qint64>(1000, next - now + 500, 6LL * 60 * 60 * 1000)));
}

int ZaloClient::applyCorrectedSenders(const QHash<QString, QString>& senders) {
    if (senders.isEmpty() || !m_offlineStore) return 0;
    const int changed = m_offlineStore->correctMessageSenders(senders);
    for (auto it = senders.cbegin(); it != senders.cend(); ++it) {
        const QString thread = m_messageThread.value(it.key());
        if (thread.isEmpty()) continue;
        const Models::MessageItem shown = m_messageModel->message(thread, it.key());
        if (shown.id.isEmpty() || shown.isMine || shown.senderId == it.value()) continue;
        m_messageModel->setSender(thread, it.key(), it.value());
        auto raw = m_rawMessages.find(it.key());
        if (raw != m_rawMessages.end()) raw->insert(QStringLiteral("uidFrom"), it.value());
    }
    return changed;
}

void ZaloClient::refreshPhoneImportedThreads() {
    const QSet<QString> threads = std::exchange(m_phoneImportedResident, {});
    for (const QString& thread : threads) {
        m_cacheMerged.remove(thread);
        if (thread != m_currentConvId) continue;
        mergeCachedThread(thread);
        m_messageModel->loadConversation(thread);
    }
}

void ZaloClient::mergeCachedThread(const QString& convId) {
    if (!m_offlineStore || convId.isEmpty() || m_cacheMerged.contains(convId)) return;
    if (m_offlineStore->ownerUid().isEmpty() || m_offlineStore->ownerUid() != (m_myUid.isEmpty() ? m_cachedUid : m_myUid)) {
        return;
    }
    m_cacheMerged.insert(convId);
    QHash<QString, QJsonObject> raws;
    const auto cached = m_offlineStore->loadMessages(convId, &raws, kCachePageRows);
    if (cached.isEmpty()) {
        m_cacheFloor.remove(convId);
    } else {
        m_cacheFloor.insert(convId, cached.first().ts);
    }
    mergeCacheRows(convId, cached, raws);
}

bool ZaloClient::loadOlderFromCache(const QString& convId) {
    if (!cacheUsable() || !m_cacheMerged.contains(convId)) return false;
    qint64 floor = qMax(m_cacheFloor.value(convId), m_messageModel->oldestTimestamp(convId));
    if (floor <= 0) return false;
    const int before = m_messageModel->threadSize(convId);
    while (true) {
        QHash<QString, QJsonObject> raws;
        const auto rows = m_offlineStore->loadMessages(convId, &raws, kCachePageRows + 1, floor);
        if (rows.isEmpty()) break;
        const qint64 next = rows.first().ts;
        mergeCacheRows(convId, rows, raws);
        const bool stuck = next >= floor || rows.size() <= kCachePageRows;
        floor = qMin(floor, next);
        if (stuck || m_messageModel->threadSize(convId) > before) break;
    }
    m_cacheFloor.insert(convId, floor);
    return m_messageModel->threadSize(convId) > before;
}

QJsonObject ZaloClient::rawMessage(const QString& msgId) const {
    if (msgId.isEmpty()) return {};
    const auto it = m_rawMessages.constFind(msgId);
    if (it != m_rawMessages.constEnd()) return it.value();
    if (!m_messageThread.contains(msgId) || !cacheUsable()) return {};
    return m_offlineStore->rawMessage(msgId);
}

void ZaloClient::mergeCacheRows(const QString& convId, QVector<Models::MessageItem> rows,
                                const QHash<QString, QJsonObject>& raws) {
    for (const auto& row : rows) m_messageThread.insert(row.id, convId);
    if (rows.isEmpty()) return;
    const bool isGroup = isGroupThread(convId);
    for (Models::MessageItem& row : rows) {
        const QJsonObject raw = raws.value(row.id);
        if (!raw.isEmpty() && !row.isRecalled) {
            const QJsonArray seenUids = raw.value(QLatin1String("seenUids")).toArray();
            for (const QJsonValue& v : seenUids) {
                const QString u = idOf(v);
                if (!u.isEmpty() && u != QLatin1String("0") && !row.seenUids.contains(u)) row.seenUids.append(u);
            }
            const QString type = raw.value("msgType").toString();
            if (raw.contains("mentions") || raw.contains("quote") || raw.contains("textProperties")
                || raw.contains("propertyExt") || isReparsedOnLoad(type)) {
                QString thread, preview;
                Models::MessageItem rebuilt = toMessageItem(raw, isGroup, &thread, &preview);
                if (rebuilt.messageType == QLatin1String("poll_update")) continue;
                rebuilt.id = row.id;
                rebuilt.status = row.status;
                rebuilt.reaction = row.reaction;
                for (const QString& u : row.seenUids) {
                    if (!rebuilt.seenUids.contains(u)) rebuilt.seenUids.append(u);
                }
                row = rebuilt;
            }
        }
        if (!row.seenUids.isEmpty() && row.isMine && row.status.isEmpty()) row.status = QStringLiteral("seen");
    }
    m_messageModel->mergeMessages(convId, rows);
    for (const auto& item : rows) {
        if (hasReactions(item.id)) refreshReactionSummary(convId, item.id);
        if (item.messageType == "sticker" && !item.extra.contains("sticker")) {
            const qint64 stickerId = raws.value(item.id).value("content").toObject().value("id").toVariant().toLongLong();
            if (stickerId > 0) resolveSticker(convId, item.id, stickerId);
        } else if (item.messageType == "bankcard" && item.extra.value("accountNumber").toString().isEmpty()) {
            const QString dataUrl = item.extra.value("dataUrl").toString();
            if (!dataUrl.isEmpty()) resolveZInstantBankCard(convId, item.id, dataUrl);
        }
    }
}

bool ZaloClient::storeOnlyIngest(bool resident, bool cacheUsable, bool phoneImport) {
    return cacheUsable && (!resident || phoneImport);
}

bool ZaloClient::cacheUsable() const {
    if (!m_offlineStore) return false;
    const QString owner = m_offlineStore->ownerUid();
    return !owner.isEmpty() && owner == (m_myUid.isEmpty() ? m_cachedUid : m_myUid);
}

bool ZaloClient::isThreadResident(const QString& convId) const {
    return convId == m_currentConvId || m_cacheMerged.contains(convId) || m_messageModel->threadSize(convId) > 0;
}

void ZaloClient::touchResidentThread(const QString& convId) {
    if (convId.isEmpty()) return;
    m_residentThreads.removeAll(convId);
    m_residentThreads.append(convId);
}

void ZaloClient::evictColdThreads(int maxResident) {
    if (!cacheUsable()) return;
    constexpr int kResidentMessageBudget = 400;
    constexpr int kInactiveThreadWindow = 100;
    constexpr int kMinResidentThreads = 1;
    for (const QString& t : m_residentThreads) {
        if (t != m_currentConvId && !m_messageModel->hasLocalEcho(t)) {
            m_messageModel->trimThreadWindow(t, kInactiveThreadWindow);
        }
    }
    int residentMessages = 0;
    const QStringList& residentThreads = m_residentThreads;
    for (const QString& t : residentThreads) residentMessages += m_messageModel->threadSize(t);
    if (m_residentThreads.size() <= maxResident && residentMessages <= kResidentMessageBudget) return;
    QSet<QString> evicted;
    const auto stillOver = [&]() {
        const int remaining = m_residentThreads.size() - int(evicted.size());
        return remaining > maxResident || (residentMessages > kResidentMessageBudget && remaining > kMinResidentThreads);
    };
    for (int i = 0; i < m_residentThreads.size() && stillOver(); ++i) {
        const QString t = m_residentThreads.at(i);
        if (t == m_currentConvId || m_messageModel->hasLocalEcho(t)) continue;
        evicted.insert(t);
        residentMessages -= m_messageModel->threadSize(t);
    }
    if (evicted.isEmpty()) return;
    for (auto it = m_messageThread.begin(); it != m_messageThread.end();) {
        if (evicted.contains(it.value())) {
            m_rawMessages.remove(it.key());
            m_reactions.remove(it.key());
            m_seenSent.remove(it.key());
            it = m_messageThread.erase(it);
        } else {
            ++it;
        }
    }
    for (const QString& t : evicted) {
        m_messageModel->clearThread(t);
        m_cacheMerged.remove(t);
        m_residentThreads.removeAll(t);
    }
    releaseFreedMemory();
}

void ZaloClient::requestMemoryTrim() {
    if (m_memoryTrimTimer) m_memoryTrimTimer->start();
}

void ZaloClient::trimMemoryForBackground() {
    if (cacheUsable()) {
        m_rawMessages.clear();
        m_rawMessages.squeeze();
        evictColdThreads(m_currentConvId.isEmpty() ? 0 : 1);
    }
    m_groupMembersCache.clear();
    m_serverSharedMedia.clear();
    m_bankCardFetching.clear();
    if (m_storage) {
        m_storage->cleanAvatarMemory();
        Platform::StorageManager::cleanTemporaryFiles();
    }
    m_typingUser.clear();
    m_typingUntil.clear();
    if (m_seenSent.size() > 5000) {
        m_seenSent.clear();
        m_seenSent.squeeze();
    }
    releaseFreedMemory();
}

void ZaloClient::setWindowVisible(bool visible) {
    if (m_windowVisible == visible) return;
    m_windowVisible = visible;
    if (!m_presenceTimer) return;
    if (visible && m_isLoggedIn) {
        if (!m_currentConvId.isEmpty()) sendSeenForCurrent();
        scheduleReadPointSync(1000);
        refreshPresence();
        if (!m_presenceTimer->isActive()) m_presenceTimer->start();
    } else {
        m_presenceTimer->stop();
    }
}

void ZaloClient::rememberMessageId(const QString& msgId) {
    constexpr int kMaxRecentIds = 5000;
    if (msgId.isEmpty() || m_recentIds.contains(msgId)) return;
    m_recentIds.insert(msgId);
    m_recentIdOrder.append(msgId);
    while (m_recentIdOrder.size() > kMaxRecentIds) m_recentIds.remove(m_recentIdOrder.takeFirst());
}

namespace {

void returnFreedHeap() {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

}

void ZaloClient::releaseFreedMemory() {
    if (!m_trimTimer) {
        m_trimTimer = new QTimer(this);
        m_trimTimer->setSingleShot(true);
        m_trimTimer->setInterval(2000);
        connect(m_trimTimer, &QTimer::timeout, this, &returnFreedHeap);
    }
    m_trimTimer->start();
}

void ZaloClient::startIdleMemoryTrim() {
    if (m_idleTrimTimer) return;
    m_idleTrimTimer = new QTimer(this);
    m_idleTrimTimer->setInterval(5 * 60 * 1000);
    connect(m_idleTrimTimer, &QTimer::timeout, this, &returnFreedHeap);
    m_idleTrimTimer->start();
}

void ZaloClient::persistMessage(const QString& convId, const QString& msgId) {
    if (!m_offlineStore || convId.isEmpty() || msgId.isEmpty()) return;
    const Models::MessageItem item = m_messageModel->message(convId, msgId);
    if (item.id.isEmpty()) return;
    m_offlineStore->saveMessage(convId, item, m_rawMessages.value(msgId), isGroupThread(convId));
}

void ZaloClient::persistProfile(const QString& id, bool isGroup) {
    if (!m_offlineStore || id.isEmpty() || m_myUid.isEmpty()) return;
    const Profile p = isGroup ? m_groups.value(id) : m_users.value(id);
    m_offlineStore->saveProfile({id, p.name, p.avatar, isGroup, p.memberCount, !isGroup && m_friendIds.contains(id),
                                 !isGroup && m_officialIds.contains(id)});
}

void ZaloClient::fetchBytes(const QString& url, std::function<void(const QByteArray&, const QString&)> done) {
    constexpr qint64 kMaxFetchBytes = 64LL * 1024 * 1024;
    const QUrl u(url);
    if (u.isLocalFile()) {
        QFile f(u.toLocalFile());
        if (!f.open(QIODevice::ReadOnly)) {
            done({}, QStringLiteral("local_file_error"));
            return;
        }
        if (f.size() > kMaxFetchBytes) {
            done({}, QStringLiteral("file_too_large"));
            return;
        }
        done(f.readAll(), QString());
        return;
    }
    QNetworkReply* reply = m_apiClient->session()->getWithCookies(u);
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, tooLarge, kMaxFetchBytes](qint64 received, qint64 total) {
        if (*tooLarge) return;
        if (received > kMaxFetchBytes || (total > 0 && total > kMaxFetchBytes)) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, tooLarge, kMaxFetchBytes, done]() {
        reply->deleteLater();
        if (*tooLarge || reply->bytesAvailable() > kMaxFetchBytes) {
            done({}, QStringLiteral("response_too_large"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            done({}, http > 0 ? QStringLiteral("HTTP %1").arg(http) : reply->errorString());
            return;
        }
        done(reply->readAll(), QString());
    });
}

QString ZaloClient::suggestedMediaFileName(const QString& url) const {
    const QUrl u(url);
    QString name = u.fileName();
    if (name.isEmpty() || !name.contains('.') || name.compare(QLatin1String("renewMedia"), Qt::CaseInsensitive) == 0) {
        const QString lower = url.toLower();
        const bool isVid = lower.contains(QLatin1String("video")) || lower.contains(QLatin1String(".mp4"))
                           || lower.contains(QLatin1String(".mov")) || lower.contains(QLatin1String(".webm"));
        name = QStringLiteral("zalo-%1.%2")
                   .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
                        isVid ? QStringLiteral("mp4") : QStringLiteral("jpg"));
    }
    return name;
}

QString ZaloClient::suggestedMediaSaveUrl(const QString& url) const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (dir.isEmpty() || !QDir(dir).exists()) dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    return QUrl::fromLocalFile(QDir(dir).filePath(suggestedMediaFileName(url))).toString();
}

QString ZaloClient::annotatedImageTempPath() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("annotated-copy.png"));
}

void ZaloClient::saveMediaTo(const QString& url, const QString& destinationFileUrl) {
    const QString dest = QUrl(destinationFileUrl).isLocalFile() ? QUrl(destinationFileUrl).toLocalFile() : destinationFileUrl;
    if (url.isEmpty() || dest.isEmpty()) {
        emit notice(L("Không lưu được tệp đa phương tiện", "Couldn't save the media file"));
        return;
    }
    downloadToFile(url, dest, [this, dest](const QString& error) {
        if (!error.isEmpty()) {
            emit notice(L("Không lưu được tệp đa phương tiện", "Couldn't save the media file")
                        + (error.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(friendlyError(error))));
            return;
        }
        emit notice(L("Đã lưu vào ", "Saved to ") + dest);
    });
}

void ZaloClient::copyImage(const QString& url) {
    fetchBytes(url, [this](const QByteArray& data, const QString& error) {
        QImage image;
        if (error.isEmpty()) image.loadFromData(data);
        if (image.isNull()) {
            emit notice(L("Không sao chép được ảnh.", "Couldn't copy the picture."));
            return;
        }
        QGuiApplication::clipboard()->setImage(image);
        emit notice(L("Đã sao chép ảnh.", "Picture copied."));
    });
}

void ZaloClient::openFileMessage(const QString& url, const QString& fileName) {
    openRemoteFile(url, fileName, L("File không còn tồn tại trên hệ thống", "The file no longer exists on the server"), true);
}

void ZaloClient::downloadFileMessage(const QString& url, const QString& fileName) {
    openRemoteFile(url, fileName, L("File không còn tồn tại trên hệ thống", "The file no longer exists on the server"), false);
}

bool ZaloClient::isVideoFileName(const QString& nameOrUrl) {
    auto hasExt = [](const QString& s) {
        static const QStringList kExts{
            QStringLiteral(".mp4"), QStringLiteral(".mov"), QStringLiteral(".webm"),
            QStringLiteral(".mkv"), QStringLiteral(".m4v")
        };
        const QString trimmed = s.trimmed();
        for (const QString& ext : kExts) {
            if (trimmed.endsWith(ext, Qt::CaseInsensitive)) return true;
        }
        return false;
    };
    return hasExt(nameOrUrl) || hasExt(QUrl(nameOrUrl).path());
}

void ZaloClient::prepareVideoPlayback(const QString& url) {
    if (url.isEmpty()) {
        emit videoPlaybackReady(url, QString(), QStringLiteral("empty_url"));
        return;
    }
    const QUrl parsed(url);
    if (parsed.isLocalFile() || (!parsed.scheme().startsWith(QLatin1String("http"), Qt::CaseInsensitive) && QFileInfo::exists(url))) {
        const QString localPath = parsed.isLocalFile() ? parsed.toLocalFile() : url;
        if (QFileInfo::exists(localPath)) {
            emit videoPlaybackReady(url, QUrl::fromLocalFile(localPath).toString(), QString());
        } else {
            emit videoPlaybackReady(url, QString(), QStringLiteral("local_file_missing"));
        }
        return;
    }
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/videos");
    QDir().mkpath(cacheDir);
    QString suffix = QFileInfo(parsed.path()).suffix().toLower();
    static const QStringList kVideoExts{QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("webm"),
                                        QStringLiteral("mkv"), QStringLiteral("m4v")};
    if (!kVideoExts.contains(suffix)) suffix = QStringLiteral("mp4");
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString cachePath = cacheDir + QLatin1Char('/') + hash + QLatin1Char('.') + suffix;
    const QFileInfo existing(cachePath);
    if (existing.exists() && existing.size() > 0) {
        emit videoPlaybackReady(url, QUrl::fromLocalFile(cachePath).toString(), QString());
        return;
    }
    const QString key = QStringLiteral("video:") + hash;
    if (m_downloadsInFlight.contains(key)) return;
    m_downloadsInFlight.insert(key);
    downloadToFile(url, cachePath, [this, url, cachePath, key](const QString& error) {
        m_downloadsInFlight.remove(key);
        if (!error.isEmpty()) {
            emit videoPlaybackReady(url, QString(), error);
            return;
        }
        emit videoPlaybackReady(url, QUrl::fromLocalFile(cachePath).toString(), QString());
    });
}

bool ZaloClient::isGoneError(const QString& error) {
    return error == QLatin1String("HTTP 403") || error == QLatin1String("HTTP 404") || error == QLatin1String("HTTP 410");
}

void ZaloClient::openRemoteFile(const QString& url, const QString& fileName, const QString& goneText, bool openAfter) {
    if (url.isEmpty()) {
        emit notice(L("Tệp này không còn đường dẫn tải.", "This file has no download link."));
        return;
    }
    const QString key = QStringLiteral("downloadIndex/")
                        + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
    const QString known = QSettings("Zalos", "ZalosApp").value(key).toString();
    if (!known.isEmpty() && QFileInfo::exists(known)) {
        if (openAfter) {
            openLocalFile(known);
        } else {
            emit notice(L("Tệp đã có tại ", "File is already saved at ") + known);
        }
        return;
    }
    if (m_downloadsInFlight.contains(key)) {
        emit notice(L("Tệp đang được tải, vui lòng chờ...", "The file is still downloading..."));
        return;
    }
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath("Zalos");
    QDir().mkpath(dir);
    QString safe = QFileInfo(fileName).fileName();
    if (safe.isEmpty()) safe = suggestedMediaFileName(url);
    const QString path = uniqueFilePath(dir, safe);
    m_downloadsInFlight.insert(key);
    emit notice(L("Đang tải ", "Downloading ") + QFileInfo(path).fileName() + "...");
    downloadToFile(url, path, [this, path, key, goneText, openAfter](const QString& error) {
        m_downloadsInFlight.remove(key);
        if (isGoneError(error)) {
            emit notice(goneText);
            return;
        }
        if (!error.isEmpty()) {
            emit notice(L("Tải tệp thất bại", "Download failed") + " (" + friendlyError(error) + ")");
            return;
        }
        QSettings("Zalos", "ZalosApp").setValue(key, path);
        emit notice(L("Đã tải về ", "Downloaded to ") + path);
        if (openAfter) openLocalFile(path);
    });
}

QString ZaloClient::uniqueFilePath(const QString& dir, const QString& fileName) {
    const QString name = fileName.isEmpty() ? QStringLiteral("file") : fileName;
    QString candidate = QDir(dir).filePath(name);
    if (!QFileInfo::exists(candidate)) return candidate;
    const QFileInfo info(name);
    const QString base = info.completeBaseName().isEmpty() ? name : info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() || info.completeBaseName().isEmpty() ? QString() : "." + info.suffix();
    for (int n = 1; n < 10000; ++n) {
        candidate = QDir(dir).filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix));
        if (!QFileInfo::exists(candidate)) return candidate;
    }
    return QDir(dir).filePath(QStringLiteral("%1 (%2)%3").arg(base, QString::number(QDateTime::currentMSecsSinceEpoch()), suffix));
}

void ZaloClient::downloadToFile(const QString& url, const QString& path, std::function<void(const QString& error)> done) {
    const QUrl u(url);
    if (u.isLocalFile()) {
        QFile::remove(path);
        done(QFile::copy(u.toLocalFile(), path) ? QString() : QStringLiteral("local_file_error"));
        return;
    }
    auto out = std::make_shared<QSaveFile>(path);
    if (!out->open(QIODevice::WriteOnly)) {
        done(QStringLiteral("local_file_error"));
        return;
    }
    auto writeFailed = std::make_shared<bool>(false);
    QNetworkReply* reply = m_apiClient->session()->getWithCookies(u);
    connect(reply, &QNetworkReply::readyRead, this, [reply, out, writeFailed]() {
        const QByteArray chunk = reply->readAll();
        if (*writeFailed || chunk.isEmpty()) return;
        if (out->write(chunk) != chunk.size()) {
            *writeFailed = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, out, writeFailed, done]() {
        reply->deleteLater();
        const QByteArray tail = reply->readAll();
        if (!*writeFailed && !tail.isEmpty() && out->write(tail) != tail.size()) *writeFailed = true;
        if (*writeFailed) {
            out->cancelWriting();
            done(QStringLiteral("local_file_error"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            out->cancelWriting();
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            done(http > 0 ? QStringLiteral("HTTP %1").arg(http) : reply->errorString());
            return;
        }
        done(out->commit() ? QString() : QStringLiteral("local_file_error"));
    });
}

QString ZaloClient::undoTargetOf(const QJsonObject& msg) {
    const QString type = msg.value("msgType").toString();
    const QJsonValue raw = msg.value("content");
    const QJsonObject content = raw.isString() ? QJsonDocument::fromJson(raw.toString().toUtf8()).object() : raw.toObject();
    const bool undo = type == QLatin1String("chat.undo") || type == QLatin1String("chat.delete.everyone")
                      || content.contains(QLatin1String("deleteMsg"));
    if (!undo) return {};
    for (const char* key : {"globalMsgId", "msgId", "cliMsgId"}) {
        const QString id = idOf(content.value(QLatin1String(key)));
        if (!id.isEmpty() && id != "0" && id != idOf(msg.value("msgId"))) return id;
    }
    return {};
}

QString ZaloClient::linkHrefOf(const QJsonObject& obj, const QJsonObject& params) {
    static const QRegularExpression url(QStringLiteral(R"((?:https?://|www\.)[^\s<>"']+)"),
                                        QRegularExpression::CaseInsensitiveOption);
    QString href = pickString(obj, {"href", "url", "link"});
    if (href.isEmpty()) href = pickString(params, {"href", "url", "link", "src", "mediaUrl"});
    if (href.isEmpty()) {
        for (const QString& text : {obj.value(QLatin1String("title")).toString(),
                                    obj.value(QLatin1String("description")).toString(),
                                    params.value(QLatin1String("mediaTitle")).toString()}) {
            const QRegularExpressionMatch m = url.match(text);
            if (m.hasMatch()) {
                href = m.captured(0);
                break;
            }
        }
    }
    while (!href.isEmpty() && QStringLiteral(".,;:!?)]}").contains(href.back())) href.chop(1);
    if (href.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) href.prepend(QStringLiteral("https://"));
    return href;
}

bool ZaloClient::isReparsedOnLoad(const QString& msgType) {
    static const QSet<QString> types{
        QStringLiteral("chat.recommended"), QStringLiteral("chat.voice"), QStringLiteral("group.poll"),
        QStringLiteral("chat.link"), QStringLiteral("chat.webcontent"), QStringLiteral("chat.webcontent.v2"),
        QStringLiteral("chat.ecard"), QStringLiteral("chat.list.action"), QStringLiteral("chat.location.new"),
        QStringLiteral("chat.photo"), QStringLiteral("chat.doodle"), QStringLiteral("chat.gif"),
        QStringLiteral("chat.video.msg"), QStringLiteral("share.file"),
        QStringLiteral("chat.undo"), QStringLiteral("chat.delete.everyone")};
    return types.contains(msgType);
}

bool ZaloClient::isQuietMessageType(const QString& msgType) {
    static const QSet<QString> quiet{QStringLiteral("chat.webcontent"), QStringLiteral("chat.webcontent.v2"),
                                     QStringLiteral("chat.livelocation"), QStringLiteral("chat.video.live.msg"),
                                     QStringLiteral("chat.list.action")};
    return quiet.contains(msgType);
}

QString ZaloClient::fillHighlights(const QString& text, const QJsonArray& highlights) const {
    static const QRegularExpression placeholder(QStringLiteral("%(\\d+)\\$s"));
    if (!text.contains(QLatin1Char('%'))) return text;
    QString out;
    qsizetype last = 0;
    auto it = placeholder.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const int index = m.captured(1).toInt() - 1;
        if (index < 0 || index >= highlights.size()) continue;
        const QJsonObject h = highlights.at(index).toObject();
        const QString uid = idOf(h.value("uid"));
        QString name;
        if (!uid.isEmpty() && uid == m_myUid) name = L("Bạn", "You");
        if (name.isEmpty() && !uid.isEmpty()) name = displayNameOf(uid);
        if (name.isEmpty()) name = h.value("dpn").toString();
        out += name;
    }
    out += text.mid(last);
    return out.simplified();
}

QString ZaloClient::displayNameOf(const QString& uid) const {
    if (!uid.isEmpty() && uid == myCloudId()) return L("My Documents", "My Documents");
    const QString alias = m_aliases.value(uid);
    return alias.isEmpty() ? m_users.value(uid).name : alias;
}

QString ZaloClient::avatarOf(const QString& uid) const {
    if (!m_myUid.isEmpty() && uid == m_myUid) return m_userAvatar;
    return m_users.value(uid).avatar;
}

void ZaloClient::loadAliases(int page, QHash<QString, QString> collected) {
    if (!m_isLoggedIn) return;
    constexpr int kPage = 100;
    m_apiClient->session()->fetchAliasList(page, kPage, [this, page, collected](bool ok, const QJsonValue& data,
                                                                               const QString&, int) mutable {
        if (!ok) return;
        const QJsonArray items = data.toObject().value("items").toArray();
        for (const QJsonValue& v : items) {
            const QJsonObject o = v.toObject();
            const QString uid = idOf(o.value("userId"));
            const QString alias = o.value("alias").toString().trimmed();
            if (!uid.isEmpty() && !alias.isEmpty()) collected.insert(uid, alias);
        }
        if (items.size() >= kPage && page < 50) {
            loadAliases(page + 1, collected);
            return;
        }
        applyAliases(collected);
    });
}

void ZaloClient::applyAliases(const QHash<QString, QString>& complete) {
    QSet<QString> touched;
    for (auto it = complete.cbegin(); it != complete.cend(); ++it) {
        if (m_aliases.value(it.key()) != it.value()) touched.insert(it.key());
    }
    for (auto it = m_aliases.cbegin(); it != m_aliases.cend(); ++it) {
        if (!complete.contains(it.key())) touched.insert(it.key());
    }
    m_aliases = complete;
    if (touched.isEmpty()) return;
    m_conversationModel->beginBatch();
    for (const QString& uid : touched) applyProfileToConversation(uid);
    refreshGroupSenderPrefixes(touched);
    m_conversationModel->endBatch();
    m_messageModel->refreshSenders();
    rebuildContacts();
}

void ZaloClient::loadUnreadMarks() {
    if (!m_isLoggedIn) return;
    m_apiClient->session()->fetchUnreadMarks([this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        QJsonObject obj = data.toObject();
        if (obj.value("data").isObject()) obj = obj.value("data").toObject();
        QSet<QString> marked;
        for (const char* key : {"convsUser", "convsGroup"}) {
            for (const QJsonValue& v : obj.value(key).toArray()) {
                const QString id = idOf(v.isObject() ? v.toObject().value("id") : v);
                if (!id.isEmpty()) marked.insert(id);
            }
        }
        for (const QString& id : std::as_const(m_markedUnread)) {
            if (marked.contains(id)) continue;
            m_conversationModel->clearUnread(id);
            if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
        }
        m_markedUnread = marked;
        for (const QString& id : marked) {
            if (m_conversationModel->contains(id) && m_conversationModel->conversation(id).unreadCount == 0) {
                m_conversationModel->setUnreadCount(id, 1);
                if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
            }
        }
    });
}

void ZaloClient::resyncAfterReconnect() {
    if (!m_isLoggedIn) return;
    loadAccountData();
    loadAliases();
    loadLabels();
    loadUnreadMarks();
    preloadRecentThreads();
    if (!m_currentConvId.isEmpty()) {
        m_historyLoaded.remove(m_currentConvId);
        if ((currentConversationIsGroup() || m_currentConvId == myCloudId()) && !m_loadingHistory) {
            fetchInitialConversationHistory(m_currentConvId);
        }
    }
}

void ZaloClient::addSystemNotice(const QString& threadId, bool isGroup, const QString& text, qint64 ts) {
    if (threadId.isEmpty() || text.isEmpty()) return;
    if (ts < kMinPlausibleEpochMs) ts = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem notice;
    notice.id = QStringLiteral("sys-%1-%2-%3").arg(threadId).arg(ts).arg(qHash(text), 0, 16);
    notice.messageType = QStringLiteral("system");
    notice.senderId = QStringLiteral("system");
    notice.text = text;
    notice.ts = ts;
    notice.timeStr = QDateTime::fromMSecsSinceEpoch(ts).toString("HH:mm");
    if (isThreadResident(threadId) || !cacheUsable()) m_messageModel->mergeMessages(threadId, {notice});
    if (m_offlineStore) m_offlineStore->saveMessage(threadId, notice, {}, isGroup);
    m_conversationModel->recordMessage(threadId, text, QString(), ts, false);
    if (m_offlineStore && m_conversationModel->contains(threadId)) m_offlineStore->saveConversation(m_conversationModel->conversation(threadId));
}

void ZaloClient::handleRemoteDeletes(const QJsonArray& entries, bool isGroup) {
    for (const QJsonValue& v : entries) {
        const QJsonObject e = v.toObject();
        const QString from = idOf(e.value("uidFrom"));
        const QString to = idOf(e.value("uidTo"));
        const QString target = idOf(e.value("globalDelMsgId"));
        const QString thread = isGroup ? to : ((to == m_myUid || to == "0") ? from : to);
        if (thread.isEmpty() || target.isEmpty()) continue;
        const int type = e.value("type").toInt();
        if (type == 2 || type == 4) {
            m_messageModel->removeMessagesUpTo(thread, target);
            if (m_offlineStore) m_offlineStore->deleteMessagesUpTo(thread, target);
            refreshUnreadReactions();
            if (m_messageModel->threadSize(thread) == 0) {
                if (thread == m_currentConvId) openWelcomeScreen();
                m_conversationModel->removeConversation(thread);
                m_cacheMerged.remove(thread);
                if (m_offlineStore) m_offlineStore->deleteConversation(thread);
            }
        } else {
            m_messageModel->removeMessage(thread, target);
            m_rawMessages.remove(target);
            if (m_offlineStore) m_offlineStore->deleteMessage(target);
            refreshUnreadReactions();
        }
    }
}

void ZaloClient::handleGroupControl(const QString& act, const QJsonObject& data) {
    const QString gid = idOf(data.value("groupId").isUndefined() ? data.value("group_id") : data.value("groupId"));
    if (gid.isEmpty()) return;
    const QString actor = idOf(data.value("sourceId"));
    const QString actorName = actor == m_myUid ? L("Bạn", "You")
                              : (displayNameOf(actor).isEmpty() ? L("Một thành viên", "A member") : displayNameOf(actor));
    QStringList names;
    bool meAffected = false;
    for (const QJsonValue& v : data.value("updateMembers").toArray()) {
        const QJsonObject m = v.toObject();
        const QString id = idOf(m.value("id"));
        if (id == m_myUid) meAffected = true;
        const QString n = id == m_myUid ? L("bạn", "you") : m.value("dName").toString(displayNameOf(id));
        if (!n.isEmpty()) names << n;
    }
    const qint64 ts = data.value("time").toVariant().toLongLong() > 0 ? data.value("time").toVariant().toLongLong()
                                                                        : QDateTime::currentMSecsSinceEpoch();
    const QString who = names.join(", ");

    if (act == "update") {
        const QString name = data.value("groupName").toString();
        if (!name.isEmpty()) {
            m_groups[gid].name = name;
            applyProfileToConversation(gid);
            addSystemNotice(gid, true, L("%1 đã đổi tên nhóm thành \"%2\"", "%1 renamed the group to \"%2\"").arg(actorName, name), ts);
        }
    } else if (act == "update_avatar") {
        QString avatar = data.value("avt").toString();
        if (avatar.isEmpty()) avatar = data.value("fullAvt").toString();
        if (!avatar.isEmpty()) {
            m_groups[gid].avatar = avatar;
            applyProfileToConversation(gid);
        }
        addSystemNotice(gid, true, L("%1 đã đổi ảnh đại diện nhóm", "%1 changed the group photo").arg(actorName), ts);
    } else if (act == "join") {
        if (meAffected) {
            ensureConversation(gid, true);
            if (m_readOnlyThreads.remove(gid) > 0) {
                saveReadOnlyThreads();
                if (gid == m_currentConvId) emit composerStateChanged();
            }
        }
        loadGroupInfo({gid});
        addSystemNotice(gid, true, actor.isEmpty() || names.contains(actorName)
                                       ? L("%1 đã tham gia nhóm", "%1 joined the group").arg(who)
                                       : L("%1 đã thêm %2 vào nhóm", "%1 added %2").arg(actorName, who), ts);
    } else if (act == "leave" || act == "remove_member" || act == "block_member") {
        if (meAffected && act != "leave") {
            markThreadReadOnly(gid, QStringLiteral("removed"), ts);
            return;
        }
        if (meAffected) {
            if (gid == m_currentConvId) openWelcomeScreen();
            m_conversationModel->removeConversation(gid);
            m_groupIds.remove(gid);
            m_groups.remove(gid);
            m_groupVersions.remove(gid);
            m_groupRosters.remove(gid);
            m_groupMembersCache.remove(gid);
            if (m_offlineStore) m_offlineStore->deleteConversation(gid);
            rebuildContacts();
            return;
        }
        loadGroupInfo({gid});
        addSystemNotice(gid, true, act == "leave" ? L("%1 đã rời nhóm", "%1 left the group").arg(who)
                                                  : L("%1 đã xóa %2 khỏi nhóm", "%1 removed %2").arg(actorName, who), ts);
    } else if (act == "add_admin" || act == "remove_admin") {
        if (m_groupRosters.contains(gid)) {
            for (const QJsonValue& v : data.value("updateMembers").toArray()) {
                const QString id = idOf(v.toObject().value("id"));
                if (act == "add_admin") m_groupRosters[gid].admins.insert(id);
                else m_groupRosters[gid].admins.remove(id);
            }
            m_groupMembersCache.remove(gid);
            emit groupMembersChanged(gid);
            if (gid == m_currentConvId) emit composerStateChanged();
        }
        addSystemNotice(gid, true, act == "add_admin"
                                       ? L("%1 đã thêm %2 làm phó nhóm", "%1 made %2 a deputy").arg(actorName, who)
                                       : L("%1 đã xóa quyền phó nhóm của %2", "%1 removed %2 as deputy").arg(actorName, who), ts);
    } else if (act == "update_board" || act == "new_pin_topic") {
        if (const qint64 pollId = groupTopicPollId(data); pollId > 0) refreshPoll(pollId, true);
    } else if (act == "disperse" || act == "disband") {
        if (actor != m_myUid) markThreadReadOnly(gid, QStringLiteral("disbanded"), ts);
    } else if (act == "update_ttl") {
        QJsonValue raw = data.value("ttl");
        if (raw.isUndefined()) raw = data.value("data").toObject().value("ttl");
        qint64 ttl = raw.toVariant().toLongLong();
        if (ttl > 0 && ttl < 10000000) ttl *= 1000;
        if (ttl > 0) m_autoDeleteTtl.insert(gid, ttl);
        else m_autoDeleteTtl.remove(gid);
        emit conversationFlagsChanged();
        addSystemNotice(gid, true, ttl > 0
                                       ? L("%1 đã đặt thời gian tự xóa cho tin nhắn là %2", "%1 set disappearing messages to %2")
                                             .arg(actorName, ttlText(ttl))
                                       : L("%1 đã tắt tin nhắn tự xóa", "%1 turned off disappearing messages").arg(actorName), ts);
    } else if (act == "change_owner") {
        loadGroupInfo({gid});
        if (!who.isEmpty())
            addSystemNotice(gid, true, L("%1 đã chuyển quyền trưởng nhóm cho %2", "%1 made %2 the group owner")
                                           .arg(actorName, who), ts);
        if (gid == m_currentConvId) emit composerStateChanged();
    } else if (act == "update_setting" || act == "force_sync" || act == "group_type" || act == "new_link"
               || act == "invite_multi" || act == "remove_multi") {
        loadGroupInfo({gid});
    }
}

void ZaloClient::markThreadReadOnly(const QString& id, const QString& reason, qint64 ts) {
    if (id.isEmpty()) return;
    const bool fresh = m_readOnlyThreads.value(id) != reason;
    m_readOnlyThreads.insert(id, reason);
    saveReadOnlyThreads();
    if (fresh) {
        addSystemNotice(id, true, reason == QLatin1String("disbanded")
                                      ? L("Trưởng nhóm đã giải tán nhóm", "The owner disbanded the group")
                                      : L("Bạn đã được xóa khỏi nhóm", "You were removed from the group"), ts);
    }
    if (id == m_currentConvId) emit composerStateChanged();
}

void ZaloClient::loadReadOnlyThreads() {
    m_readOnlyThreads.clear();
    if (m_myUid.isEmpty()) return;
    QSettings settings("Zalos", "ZalosApp");
    const QVariantMap map = settings.value(QStringLiteral("readonlyThreads/") + m_myUid).toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) m_readOnlyThreads.insert(it.key(), it.value().toString());
    m_strangerLocks.clear();
    const QVariantMap locks = settings.value(QStringLiteral("strangerLocks/") + m_myUid).toMap();
    for (auto it = locks.cbegin(); it != locks.cend(); ++it) m_strangerLocks.insert(it.key(), it.value().toLongLong());
    scheduleStrangerUnlock();
    emit composerStateChanged();
}

void ZaloClient::saveReadOnlyThreads() const {
    if (m_myUid.isEmpty()) return;
    QVariantMap map;
    for (auto it = m_readOnlyThreads.cbegin(); it != m_readOnlyThreads.cend(); ++it) map.insert(it.key(), it.value());
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue(QStringLiteral("readonlyThreads/") + m_myUid, map);
}

bool ZaloClient::isStrangerLocked(const QString& id) const {
    const auto it = m_strangerLocks.constFind(id);
    return it != m_strangerLocks.cend() && *it > QDateTime::currentMSecsSinceEpoch() && !m_friendIds.contains(id);
}

bool ZaloClient::noteSendFailure(const QString& convId, int code, const QJsonValue& data) {
    if (code != 804 || convId.isEmpty() || isGroupThread(convId)) return false;
    const QJsonObject info = data.isString() ? QJsonDocument::fromJson(data.toString().toUtf8()).object() : data.toObject();
    const qint64 until = info.value("bannedExpireTimestamp").toVariant().toLongLong();
    if (until <= QDateTime::currentMSecsSinceEpoch()) return false;
    m_strangerLocks.insert(convId, until);
    saveStrangerLocks();
    scheduleStrangerUnlock();
    emit composerStateChanged();
    return true;
}

void ZaloClient::saveStrangerLocks() const {
    if (m_myUid.isEmpty()) return;
    QVariantMap map;
    for (auto it = m_strangerLocks.cbegin(); it != m_strangerLocks.cend(); ++it) map.insert(it.key(), it.value());
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue(QStringLiteral("strangerLocks/") + m_myUid, map);
}

void ZaloClient::scheduleStrangerUnlock() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 next = 0;
    for (auto it = m_strangerLocks.begin(); it != m_strangerLocks.end();) {
        if (it.value() <= now) {
            it = m_strangerLocks.erase(it);
            continue;
        }
        if (next == 0 || it.value() < next) next = it.value();
        ++it;
    }
    if (!m_strangerUnlockTimer) {
        m_strangerUnlockTimer = new QTimer(this);
        m_strangerUnlockTimer->setSingleShot(true);
        connect(m_strangerUnlockTimer, &QTimer::timeout, this, [this]() {
            scheduleStrangerUnlock();
            saveStrangerLocks();
            emit composerStateChanged();
        });
    }
    if (next == 0) {
        m_strangerUnlockTimer->stop();
        return;
    }
    m_strangerUnlockTimer->start(int(qBound<qint64>(1000, next - now + 500, 6LL * 60 * 60 * 1000)));
}

bool ZaloClient::currentConversationIsStranger() const {
    return m_conversationModel->isStranger(m_currentConvId) && !m_blockedIds.contains(m_currentConvId);
}

QString ZaloClient::strangerBannerTitle() const {
    const StrangerRank r = m_strangerRanks.value(m_currentConvId);
    if (!r.title.isEmpty()) return r.title;
    if (r.rank == 2) return L("Cảnh báo lừa đảo", "Scam warning");
    if (r.rank == 1) return L("Cảnh giác lừa đảo", "Beware of scams");
    return L("Người này chưa là bạn", "This person isn't your friend yet");
}

QString ZaloClient::strangerBannerText() const {
    const StrangerRank r = m_strangerRanks.value(m_currentConvId);
    if (!r.text.isEmpty()) return r.text;
    if (r.rank == 2) return L("Cẩn trọng khi chia sẻ thông tin với người này.", "Be careful sharing information with this person.");
    if (r.rank == 1) return L("Cẩn thận khi chia sẻ thông tin với người lạ", "Be mindful when sharing information with strangers");
    return L("Lưu ý rủi ro lừa đảo từ người lạ.", "Watch out for scams from strangers.");
}

void ZaloClient::applyStrangerRank(const QString& id, const QJsonObject& data) {
    StrangerRank r;
    const QString rank = data.value("userRank").toString(data.value("rank_type").toString(data.value("rank").toString()));
    r.rank = rank == QLatin1String("BLACK") ? 2 : (rank == QLatin1String("GRAY") ? 1 : 0);
    const QJsonObject render = data.value("data_render").toObject();
    const QString lang = m_appLanguage == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("vi");
    r.title = render.value("title").toObject().value(lang).toString();
    r.text = render.value("desc").toObject().value(lang).toString();
    if (!m_strangerRanks.contains(id)) dropOldestIfFull(m_strangerRanks);
    m_strangerRanks.insert(id, r);
    if (id == m_currentConvId) emit composerStateChanged();
}

#ifdef ZALOS_TESTING
void ZaloClient::setStrangerRankForTesting(const QString& id, const QJsonObject& data) {
    applyStrangerRank(id, data);
}
#endif

void ZaloClient::refreshStrangerRank(const QString& convId) {
    if (!m_isLoggedIn || m_strangerRanks.contains(convId) || !m_conversationModel->isStranger(convId)) return;
    if (!m_strangerRanks.contains(convId)) dropOldestIfFull(m_strangerRanks);
    m_strangerRanks.insert(convId, StrangerRank{});
    QPointer<ZaloClient> guard(this);
    m_apiClient->session()->fetchProfileRank(convId, [guard, convId](bool ok, const QJsonValue& data, const QString&, int) {
        if (!guard) return;
        if (!ok) {
            guard->m_strangerRanks.remove(convId);
            return;
        }
        guard->applyStrangerRank(convId, data.toObject());
    });
}

void ZaloClient::syncFriendContext() {
    m_conversationModel->setFriendContext(m_friendIds, m_officialIds, m_myUid, m_friendsKnown);
    emit composerStateChanged();
}

QString ZaloClient::composerLockReason() const {
    if (m_currentConvId.isEmpty()) return {};
    if (m_readOnlyThreads.contains(m_currentConvId))
        return L("Bạn không thể gửi tin nhắn vào nhóm được nữa", "You can no longer send messages to this group");
    if (isStrangerLocked(m_currentConvId))
        return friendlyError(QString(), 804) + QLatin1Char(' ')
               + L("Bạn có thể trả lời lại lúc %1.", "You can reply again at %1.")
                     .arg(QDateTime::fromMSecsSinceEpoch(m_strangerLocks.value(m_currentConvId)).toString("HH:mm dd/MM/yyyy"));
    if (!isGroupThread(m_currentConvId)) return {};
    if (m_groupAdmin->settingsOf(m_currentConvId).value("lockSendMsg").toVariant().toInt() != 1) return {};
    const auto roster = m_groupRosters.constFind(m_currentConvId);
    if (roster != m_groupRosters.cend() && (roster->creator == m_myUid || roster->admins.contains(m_myUid))) return {};
    return L("Nhóm không cho phép thành viên gửi tin nhắn", "Only admins can send messages in this group");
}

QString ZaloClient::ttlText(qint64 ttlMs) const {
    const qint64 hours = ttlMs / 3600000;
    if (hours >= 24 && hours % 24 == 0) return L("%1 ngày", "%1 day(s)").arg(hours / 24);
    if (hours >= 1) return L("%1 giờ", "%1 hour(s)").arg(hours);
    return L("%1 phút", "%1 minute(s)").arg(qMax<qint64>(1, ttlMs / 60000));
}

void ZaloClient::handleSyncControl(const QString& actType, const QString& act, const QJsonValue& data) {
    const QJsonObject obj = data.toObject();
    if (actType == "hidden_chat") {
        auto threadOf = [](const QJsonValue& v) {
            QString id = idOf(v.toObject().value("thread_id"));
            if (id.size() > 1 && (id.startsWith('g') || id.startsWith('u')) && id.at(1).isDigit()) id.remove(0, 1);
            return id;
        };
        if (act == "add_remove") {
            QSet<QString> next = m_hiddenThreads;
            for (const QJsonValue& v : obj.value("add_items").toArray()) {
                const QString id = threadOf(v);
                if (!id.isEmpty()) next.insert(id);
            }
            for (const QJsonValue& v : obj.value("del_items").toArray()) next.remove(threadOf(v));
            applyHiddenSet(next);
        } else if (act == "reset") {
            const QSet<QString> dropped = m_hiddenThreads;
            m_hiddenPinHash.clear();
            emit hiddenPinChanged();
            dropHiddenConversationsLocally(dropped);
            applyHiddenSet({});
        } else if (act == "change_pin") {
            m_hiddenPinHash = obj.value("pin").toString().toLower();
            emit hiddenPinChanged();
        }
        return;
    }
    if (actType == "group" && act == "update_pin_topic") {
        if (const qint64 pollId = groupTopicPollId(obj); pollId > 0) refreshPoll(pollId, true);
    }
    if (m_pinBoard->handleControl(act, data)) return;
    if (actType == "group") {
        handleGroupControl(act, obj);
    } else if (actType == "alias") {
        loadAliases();
    } else if (actType == "profile") {
        if (act == "update") loadAccountData();
        else if (act == "online_status") refreshPresence();
    } else if (actType == "mute") {
        QString id = idOf(obj.value("toId"));
        if (id.isEmpty()) id = idOf(obj.value("id"));
        if (id.isEmpty()) return;
        const int muteMode = obj.value("muteMode").toInt(0);
        const qint64 duration = obj.value("duration").toVariant().toLongLong();
        const bool mute = (act == "set") && (duration != 0 || muteMode == 2 || muteMode == 3);
        if (mute) {
            m_mutedIds.insert(id);
            m_muteUntil.insert(id, duration > 0 ? QDateTime::currentMSecsSinceEpoch() + duration * 1000 : 0);
        } else {
            m_mutedIds.remove(id);
            m_muteUntil.remove(id);
        }
        m_conversationModel->setMuted(id, mute);
        if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
        if (id == m_currentConvId) emit currentConversationChanged();
    } else if (actType == "pinned_convers") {
        const bool pin = obj.value("action").toInt() == 1;
        for (const QJsonValue& v : obj.value("pinIds").toArray()) {
            QString raw = v.toString();
            const bool isGroup = raw.startsWith('g');
            if (raw.startsWith('g') || raw.startsWith('u')) raw.remove(0, 1);
            if (pin) ensureConversation(raw, isGroup);
            m_conversationModel->setPinned(raw, pin);
            if (m_offlineStore && m_conversationModel->contains(raw)) m_offlineStore->saveConversation(m_conversationModel->conversation(raw));
        }
        emit currentConversationChanged();
    } else if (actType == "label_convers") {
        m_labelDebounce->start();
    } else if (actType == "mark_unread") {
        for (const char* key : {"convsUser", "convsGroup"}) {
            for (const QJsonValue& v : obj.value(key).toArray()) {
                const QString id = idOf(v.isObject() ? v.toObject().value("id") : v);
                if (id.isEmpty()) continue;
                if (act == "add") {
                    m_markedUnread.insert(id);
                    if (id != m_currentConvId && m_conversationModel->conversation(id).unreadCount == 0) {
                        m_conversationModel->setUnreadCount(id, 1);
                        if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
                    }
                } else if (m_markedUnread.remove(id)) {
                    m_conversationModel->clearUnread(id);
                    if (m_offlineStore && m_conversationModel->contains(id)) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
                }
            }
        }
    } else if (actType == "archived_chat") {
        loadArchivedChats();
    } else if (actType == "conv_settings") {
        loadAutoDeleteSettings();
    } else if (actType == "my_settings") {
        loadServerSettings();
    } else if (actType == "quick_message") {
        loadQuickMessages();
    }
}

void ZaloClient::preloadRecentThreads() {
    m_apiClient->session()->fetchLastMessages({}, {}, [this](bool ok, const QJsonValue& data, const QString&, int) {
        if (!ok) return;
        QJsonObject obj = data.toObject();
        if (obj.value("data").isObject()) obj = obj.value("data").toObject();
        QJsonArray users = obj.value("msgs").toArray();
        for (const QJsonValue& v : obj.value("pageMsgs").toArray()) users.append(v);
        handleIncomingMessages(users, false, true);
        handleIncomingMessages(obj.value("groupMsgs").toArray(), true, true);
        applyReadPoints(obj.value("clearUnreads").toArray());
    });
}

void ZaloClient::applyReadPoints(const QJsonArray& clearUnreads) {
    for (const QJsonValue& v : clearUnreads) {
        const QJsonObject e = v.toObject();
        if (e.value("type").toInt() == 2 && e.contains("sct")) continue;
        const QString id = idOf(e.value("idTo"));
        const QString point = idOf(e.value("lastMsgId"));
        if (id.isEmpty() || id == "0" || point.isEmpty() || point == "0") continue;
        const QJsonValue g = e.value("isGroup");
        const bool isGroup = g.toBool() || g.toInt() == 1 || isGroupThread(id);
        advanceReadPoint(id, point);
        if (!m_conversationModel->contains(id)) {
            if (isGroup) continue;
            ensureConversation(id, false);
        }
        if (id == m_currentConvId || m_markedUnread.contains(id)) continue;

        const auto conv = m_conversationModel->conversation(id);
        const int serverIncoming = m_messageModel->countIncomingAfter(id, point);
        if (conv.unreadCount == 0 && serverIncoming > 0) {
            sendSeenFor(id, isGroup);
            continue;
        }

        if (m_messageModel->threadSize(id) == 0) continue;
        const int unread = m_messageModel->countIncomingAfter(id, QString::number(readPointOf(id)));
        if (unread == conv.unreadCount) continue;
        m_conversationModel->setUnreadCount(id, unread);
        if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(id));
    }
}

qulonglong ZaloClient::readPointOf(const QString& convId) {
    if (!m_readPointsLoaded) {
        m_readPointsLoaded = true;
        if (m_offlineStore) {
            const auto stored = m_offlineStore->loadReadPoints();
            for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
                bool ok = false;
                const qulonglong point = it.value().toULongLong(&ok);
                if (ok && point > 0) m_readPoints.insert(it.key(), qMax(point, m_readPoints.value(it.key())));
            }
        }
    }
    return m_readPoints.value(convId, 0);
}

bool ZaloClient::advanceReadPoint(const QString& convId, const QString& msgId) {
    bool ok = false;
    const qulonglong point = msgId.toULongLong(&ok);
    if (convId.isEmpty() || !ok || point == 0 || point <= readPointOf(convId)) return false;
    m_readPoints.insert(convId, point);
    if (m_offlineStore) m_offlineStore->saveReadPoint(convId, QString::number(point));
    return true;
}

void ZaloClient::resetReadPoints() {
    m_readPoints.clear();
    m_readPointsLoaded = false;
    m_readPointSyncRetries = 0;
    m_readPointSyncInFlight = 0;
    m_readPointSyncPending = false;
    if (m_readPointSyncTimer) m_readPointSyncTimer->stop();
}

void ZaloClient::scheduleReadPointSync(int delayMs) {
    if (!m_readPointSyncTimer || !m_isLoggedIn) return;
    if (m_readPointSyncTimer->isActive() && m_readPointSyncTimer->remainingTime() <= delayMs) return;
    if (!m_readPointSyncTimer->isActive()) m_readPointSyncFirstAsk.start();
    m_readPointSyncTimer->start(delayMs);
}

void ZaloClient::syncReadPoints() {
    if (!m_isLoggedIn) return;
    if (m_readPointSyncInFlight > 0) {
        m_readPointSyncPending = true;
        return;
    }
    QStringList groups;
    QStringList users;
    const QString cloudId = myCloudId();
    for (const auto& c : m_conversationModel->conversations()) {
        if (c.unreadCount <= 0 || c.id.isEmpty() || c.id == QLatin1String("0") || m_markedUnread.contains(c.id)) continue;
        if (c.id == cloudId) continue;
        (c.isGroup || isGroupThread(c.id) ? groups : users).append(c.id);
    }
    if (groups.isEmpty() && users.isEmpty()) return;
    constexpr int kBatch = 50;
    const QString owner = m_myUid;
    const quint64 gen = m_accountLoadGen;
    for (int i = 0; i * kBatch < qMax(groups.size(), users.size()); ++i) {
        ++m_readPointSyncInFlight;
        m_apiClient->session()->fetchLastSeen(groups.mid(i * kBatch, kBatch), users.mid(i * kBatch, kBatch),
            [this, owner, gen](bool ok, const QJsonValue& data, const QString&, int) {
                if (owner != m_myUid || gen != m_accountLoadGen || !m_isLoggedIn) return;
                if (m_readPointSyncInFlight > 0) --m_readPointSyncInFlight;
                if (m_readPointSyncInFlight == 0 && m_readPointSyncPending) {
                    m_readPointSyncPending = false;
                    scheduleReadPointSync(0);
                }
                if (!ok) {
                    if (m_readPointSyncRetries < 3) {
                        ++m_readPointSyncRetries;
                        scheduleReadPointSync(30000);
                    }
                    return;
                }
                m_readPointSyncRetries = 0;
                QJsonObject obj = data.toObject();
                if (data.isString()) obj = QJsonDocument::fromJson(data.toString().toUtf8()).object();
                applyReadPoints(ZaloSession::readPointsOf(obj));
            });
    }
}

void ZaloClient::showMessageNotification(const QString& threadId, const QString& title, const QString& body) {
    Platform::DesktopNotifications::Request request;
    const bool hide = m_antiPeeking || m_appLock->locked();
    request.title = hide || title.isEmpty() ? QStringLiteral("Zalos") : title;
    request.body = hide ? L("Bạn có tin nhắn mới", "You have a new message") : body.left(200);
    request.category = QStringLiteral("im.received");
    request.actions = {{QStringLiteral("default"), L("Mở", "Open")},
                       {QStringLiteral("mark-read"), L("Đánh dấu đã đọc", "Mark as read")}};
    request.inlineReply = !m_appLock->locked();
    request.replyPlaceholder = L("Trả lời…", "Reply…");
    m_notifications->show(QStringLiteral("thread:") + threadId, request);
}

void ZaloClient::onNotificationAction(const QString& key, const QString& action, const QString& activationToken) {
    if (key == QLatin1String("call")) {
        if (action == QLatin1String("answer")) m_calls->accept();
        else if (action == QLatin1String("decline")) m_calls->reject();
        if (action != QLatin1String("decline")) emit windowActivationRequested(activationToken);
        return;
    }
    if (key.startsWith(QLatin1String("reaction:"))) {
        const QString rest = key.mid(9);
        const int split = rest.lastIndexOf(QLatin1Char(':'));
        if (split <= 0) return;
        const QString convId = rest.left(split);
        const QString msgId = rest.mid(split + 1);
        if (m_currentNavigationTab != QLatin1String("messages")) setCurrentNavigationTab(QStringLiteral("messages"));
        openConversationById(convId);
        emit windowActivationRequested(activationToken);
        QPointer<ZaloClient> self(this);
        QTimer::singleShot(300, this, [self, convId, msgId]() {
            if (self && self->m_currentConvId == convId) emit self->reactionJumpRequested(msgId);
        });
        return;
    }
    if (!key.startsWith(QLatin1String("thread:"))) return;
    const QString convId = key.mid(7);
    if (action == QLatin1String("mark-read")) {
        markThreadRead(convId);
        m_notifications->close(key);
        return;
    }
    if (m_currentNavigationTab != QLatin1String("messages")) setCurrentNavigationTab(QStringLiteral("messages"));
    openConversationById(convId);
    emit windowActivationRequested(activationToken);
}

void ZaloClient::sendVoiceMessage(const QString& convId, const QString& filePath, int durationMs) {
    if (convId.isEmpty() || filePath.isEmpty()) return;
    const bool isGroup = isGroupThread(convId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Models::MessageItem local;
    local.id = QStringLiteral("local-%1-%2").arg(now).arg(++m_localSeq);
    m_lastClientId = qMax(now, m_lastClientId + 1);
    local.cliMsgId = QString::number(m_lastClientId);
    local.messageType = QStringLiteral("voice");
    local.text = L("Tin nhắn thoại", "Voice message");
    local.href = QUrl::fromLocalFile(filePath).toString();
    local.extra = QVariantMap{{"durationMs", durationMs}};
    local.ts = now;
    local.timeStr = QDateTime::fromMSecsSinceEpoch(now).toString("HH:mm");
    local.isMine = true;
    local.senderId = m_myUid;
    local.senderName = m_userName;
    local.senderAvatar = m_userAvatar;
    local.status = QStringLiteral("sending");
    m_messageModel->mergeMessages(convId, {local});
    m_conversationModel->recordMessage(convId, L("[Tin nhắn thoại]", "[Voice message]"), L("Bạn: ", "You: "), now, false);
    if (m_offlineStore && m_conversationModel->contains(convId)) m_offlineStore->saveConversation(m_conversationModel->conversation(convId));
    if (!m_isLoggedIn) {
        m_messageModel->setStatus(convId, local.id, QStringLiteral("failed"));
        emit notice(L("Đang ngoại tuyến nên chưa gửi được tin nhắn thoại.", "You're offline, so the voice message wasn't sent."));
        return;
    }
    const QString localId = local.id;
    const qint64 ttl = m_autoDeleteTtl.value(convId, 0);
    m_apiClient->session()->sendVoice(convId, isGroup, filePath, local.cliMsgId,
        [this, convId, localId](bool ok, const QJsonValue&, const QString& error, int code) {
            m_messageModel->setStatus(convId, localId, ok ? QStringLiteral("sent") : QStringLiteral("failed"));
            if (!ok) emit notice(L("Chưa gửi được tin nhắn thoại: ", "Voice message not sent: ") + friendlyError(error, code));
        }, ttl);
}

void ZaloClient::setDraft(const QString& convId, const QString& text) {
    if (convId.isEmpty()) return;
    const QString clean = text.trimmed().isEmpty() ? QString() : text;
    if (m_conversationModel->draft(convId) == clean) return;
    m_conversationModel->setDraft(convId, clean);
    if (m_myUid.isEmpty()) return;
    QSettings settings;
    settings.beginGroup(QStringLiteral("drafts/") + m_myUid);
    if (clean.isEmpty()) settings.remove(convId);
    else settings.setValue(convId, clean);
}

QString ZaloClient::draftFor(const QString& convId) const {
    return m_conversationModel->draft(convId);
}

void ZaloClient::loadDrafts() {
    m_conversationModel->clearDrafts();
    if (m_myUid.isEmpty()) return;
    QSettings settings;
    settings.beginGroup(QStringLiteral("drafts/") + m_myUid);
    for (const QString& convId : settings.childKeys()) {
        m_conversationModel->setDraft(convId, settings.value(convId).toString());
    }
}

void ZaloClient::selectAdjacentConversation(int delta) {
    const QString id = m_conversationModel->adjacentId(m_currentConvId, delta);
    if (!id.isEmpty() && id != m_currentConvId) openConversationById(id);
}

void ZaloClient::openPinnedConversation(int n) {
    const QStringList pinned = m_conversationModel->pinnedIds();
    if (n >= 1 && n <= pinned.size()) openConversationById(pinned.at(n - 1));
}

void ZaloClient::openConversationById(const QString& id) {
    if (id.isEmpty()) return;
    if (needsHiddenPin(id)) {
        requestHiddenFlow(QStringLiteral("open"), id, false);
        return;
    }
    const auto conv = m_conversationModel->conversation(id);
    const Profile p = m_users.value(id);
    const QString name = !conv.name.isEmpty() ? conv.name : p.name;
    const QString avatar = !conv.avatarUrl.isEmpty() ? conv.avatarUrl : p.avatar;
    selectConversation(id, name, avatar);
}

void ZaloClient::markThreadRead(const QString& convId) {
    if (convId.isEmpty()) return;
    qulonglong newest = 0;
    for (const auto& m : m_messageModel->threadOf(convId)) {
        bool ok = false;
        const qulonglong id = m.id.toULongLong(&ok);
        if (ok && id > newest) newest = id;
    }
    if (newest > 0) advanceReadPoint(convId, QString::number(newest));
    m_conversationModel->clearUnread(convId);
    if (m_offlineStore) m_offlineStore->saveConversation(m_conversationModel->conversation(convId));
    if (m_markedUnread.contains(convId)) toggleUnreadMark(convId);
    sendSeenFor(convId, isGroupThread(convId));
}

QVariantList ZaloClient::scheduledMessages() const {
    return m_scheduler->entriesFor(m_currentConvId);
}

bool ZaloClient::scheduleMessage(const QString& text, qint64 sendAtMs) {
    QString error;
    if (!m_scheduler->schedule(m_currentConvId, currentConversationIsGroup(), text, sendAtMs, &error)) {
        if (error == QLatin1String("past")) emit notice(L("Hãy chọn thời điểm ở tương lai.", "Pick a time in the future."));
        return false;
    }
    emit notice(L("Đã hẹn giờ gửi lúc %1. Zalos cần đang mở để gửi.", "Scheduled for %1. Zalos must be running to send it.")
                    .arg(QDateTime::fromMSecsSinceEpoch(sendAtMs).toString("HH:mm dd/MM")));
    return true;
}

void ZaloClient::cancelScheduledMessage(const QString& id) {
    m_scheduler->cancel(id);
}

bool ZaloClient::testE2EEDecryption() {
    bool ok = Crypto::DbCross::verifyCryptoSelfTest();
    if (ok) {
        emit e2eeVerified(true, L("Mô-đun mã hóa hoạt động bình thường.", "The encryption module works correctly."));
    } else {
        emit e2eeVerified(false, L("Lỗi kiểm tra mô-đun mã hóa.", "The encryption module self-test failed."));
    }
    return ok;
}

}
