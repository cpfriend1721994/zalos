#include "pin_board.h"

#include "zalo_session.h"

#include <QJsonDocument>
#include <QPointer>
#include <QSet>

namespace Zalos::Core::Network {

namespace {

QString idString(const QJsonValue& v) {
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(qint64(v.toDouble()));
    return {};
}

QJsonObject asObject(const QJsonValue& v) {
    if (v.isObject()) return v.toObject();
    if (v.isString()) return QJsonDocument::fromJson(v.toString().toUtf8()).object();
    return {};
}

const QSet<QString>& pinActs() {
    static const QSet<QString> acts{"pin_topic", "unpin_topic", "update_pin_topic", "reorder_pin_topic"};
    return acts;
}

}

PinBoard::PinBoard(Hooks hooks, QObject* parent) : QObject(parent), m_hooks(std::move(hooks)) {}

QVariantMap PinBoard::normalizeTopic(const QJsonValue& raw) {
    QJsonObject o = asObject(raw);
    const int boardType = o.value("boardType").toVariant().toInt();
    if (o.contains("data") && !o.contains("params")) o = asObject(o.value("data"));
    const qint64 pollId = o.value("poll_id").toVariant().toLongLong();
    QString id = idString(o.contains("id") ? o.value("id") : o.value("topicId"));
    if (id.isEmpty() && pollId > 0) id = QStringLiteral("poll-%1").arg(pollId);
    if (id.isEmpty()) return {};
    int type = (o.contains("type") ? o.value("type") : o.value("topicType")).toVariant().toInt();
    if (type == 0 && (boardType == 3 || pollId > 0)) type = 3;
    const QJsonObject p = asObject(o.value("params"));
    QString title = p.value("title").toString();
    if (title.isEmpty()) title = p.value("linkCaption").toString();
    if (title.isEmpty()) title = o.value("question").toString();
    return {{"id", id},
            {"pollId", pollId > 0 ? pollId : p.value("pollId").toVariant().toLongLong()},
            {"type", type},
            {"title", title},
            {"senderUid", idString(p.value("senderUid"))},
            {"senderName", p.value("senderName").toString()},
            {"msgId", idString(p.value("global_msg_id"))},
            {"cliMsgId", idString(p.value("client_msg_id"))},
            {"msgType", p.value("msg_type").toVariant().toInt()},
            {"thumb", p.value("thumb").toString()},
            {"href", p.value("href").toString()}};
}

QVariantMap PinBoard::detail(const QString& topicId) const {
    QVariantMap out;
    for (const QVariant& v : m_boards.value(m_current).topics) {
        if (v.toMap().value("id").toString() == topicId) out = v.toMap();
    }
    if (out.isEmpty()) return out;
    QString text = out.value("title").toString();
    QString href = out.value("href").toString();
    const QString msgId = out.value("msgId").toString();
    const QJsonObject raw = (m_hooks.rawMessage && !msgId.isEmpty()) ? m_hooks.rawMessage(msgId) : QJsonObject();
    if (!raw.isEmpty()) {
        const QJsonValue content = raw.value("content");
        if (content.isString()) {
            if (!content.toString().isEmpty()) text = content.toString();
        } else {
            const QJsonObject c = content.toObject();
            if (!c.value("title").toString().isEmpty()) text = c.value("title").toString();
            if (!c.value("href").toString().isEmpty()) href = c.value("href").toString();
        }
    }
    const QStringList images = mediaCandidates(out, raw, true);
    out.insert("text", text);
    out.insert("image", images.value(0));
    out.insert("images", images);
    out.insert("href", href);
    out.insert("local", !raw.isEmpty());
    return out;
}

QStringList PinBoard::previewUrls(const QString& topicId) const {
    if (topicId.isEmpty()) return {};
    for (const QVariant& v : m_boards.value(m_current).topics) {
        const QVariantMap topic = v.toMap();
        if (topic.value("id").toString() != topicId) continue;
        const QString msgId = topic.value("msgId").toString();
        const QJsonObject raw = (m_hooks.rawMessage && !msgId.isEmpty()) ? m_hooks.rawMessage(msgId) : QJsonObject();
        return mediaCandidates(topic, raw, false);
    }
    return {};
}

QStringList PinBoard::mediaCandidates(const QVariantMap& topic, const QJsonObject& rawMessage, bool preferLarge) {
    QStringList out;
    const auto add = [&out](const QString& url) {
        const QString u = url.trimmed();
        if ((u.startsWith(QLatin1String("http")) || u.startsWith(QLatin1String("file:"))) && !out.contains(u)) out.append(u);
    };
    const int type = topic.value("msgType").toInt();
    const bool picture = type == 32 || type == 37 || type == 49;
    const QJsonValue content = rawMessage.value("content");
    const QJsonObject c = content.isObject() ? content.toObject() : QJsonObject();
    const QJsonObject extra = asObject(c.value("params"));
    const auto addLarge = [&]() {
        if (!picture) return;
        for (const char* key : {"hdUrl", "href", "normalUrl", "oriUrl"}) add(c.value(QLatin1String(key)).toString());
        add(extra.value("hd").toString());
        add(topic.value("href").toString());
    };
    const auto addSmall = [&]() {
        for (const char* key : {"thumb", "thumbUrl"}) add(c.value(QLatin1String(key)).toString());
        add(topic.value("thumb").toString());
    };
    if (preferLarge) {
        addLarge();
        addSmall();
    } else {
        addSmall();
        addLarge();
    }
    return out;
}

QVariantList PinBoard::parseList(const QJsonObject& data, bool isGroup, qint64* version, int* limit) {
    QJsonArray items;
    if (isGroup) {
        items = data.value("items").toArray();
        if (items.isEmpty()) items = data.value("topics").toArray();
        if (version) *version = data.value("boardVersion").toVariant().toLongLong();
        if (limit) *limit = data.value("pinLimit").toInt();
    } else {
        items = data.value("data").toArray();
        if (items.isEmpty()) items = data.value("topics").toArray();
        if (version) *version = data.value("version").toVariant().toLongLong();
        if (limit) *limit = data.value("pinLimit").toInt();
    }
    QVariantList out;
    for (const QJsonValue& v : items) {
        const QVariantMap t = normalizeTopic(v);
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}

QJsonObject PinBoard::topicForMessage(const QJsonObject& msg, const QString& myUid, const QString& senderName) {
    QString sender = idString(msg.value("uidFrom"));
    if (sender.isEmpty() || sender == "0") sender = myUid;
    const QString type = msg.value("msgType").toString();
    QJsonObject params{{"client_msg_id", idString(msg.value("cliMsgId"))},
                       {"global_msg_id", idString(msg.value("msgId"))},
                       {"senderUid", sender},
                       {"senderName", senderName},
                       {"msg_type", ZaloSession::clientMessageType(type)}};
    const QJsonValue content = msg.value("content");
    if (content.isString()) {
        params.insert("title", content.toString());
    } else {
        const QJsonObject c = content.toObject();
        if (!c.value("title").toString().isEmpty()) params.insert("title", c.value("title").toString());
        const QString thumb = c.value("thumb").toString();
        if (!thumb.isEmpty()) params.insert("thumb", thumb);
        if (!c.value("href").toString().isEmpty()) params.insert("href", c.value("href").toString());
        if (type == "share.file" && c.value("params").isString()) params.insert("extra", c.value("params").toString());
    }
    return {{"type", kMessageTopic}, {"color", kDefaultColor}, {"emoji", ""}, {"duration", -1}, {"startTime", -1},
            {"repeat", 0}, {"params", params}};
}

void PinBoard::setCurrent(const QString& convId) {
    if (convId == m_current) return;
    m_current = convId;
    emit pinsChanged();
    if (!convId.isEmpty() && !m_boards.value(convId).loaded) load(convId);
}

void PinBoard::reset() {
    m_boards.clear();
    m_current.clear();
    emit pinsChanged();
}

void PinBoard::reload() {
    if (!m_current.isEmpty()) load(m_current);
}

void PinBoard::load(const QString& convId) {
    ZaloSession* s = session();
    if (!s || convId.isEmpty()) return;
    const bool group = isGroup(convId);
    QPointer<PinBoard> self(this);
    auto done = [self, convId, group](bool ok, const QJsonValue& data, const QString&, int) {
        if (self && ok) self->applyList(convId, data.toObject(), group);
    };
    if (group) s->fetchGroupPins(convId, 0, done);
    else s->fetchFriendPins(convId, 0, done);
}

void PinBoard::applyList(const QString& convId, const QJsonObject& data, bool group) {
    Board& b = m_boards[convId];
    qint64 version = 0;
    int limit = 0;
    b.topics = parseList(data, group, &version, &limit);
    b.version = version;
    if (limit > 0) b.limit = limit;
    b.loaded = true;
    changed(convId);
}

bool PinBoard::isPinned(const QString& msgId) const {
    if (msgId.isEmpty()) return false;
    for (const QVariant& v : m_boards.value(m_current).topics) {
        if (v.toMap().value("msgId").toString() == msgId) return true;
    }
    return false;
}

QJsonArray PinBoard::topicRefs(const QStringList& ids, const QString& convId) const {
    QJsonArray refs;
    for (const QString& id : ids) {
        int type = kMessageTopic;
        for (const QVariant& v : m_boards.value(convId).topics) {
            if (v.toMap().value("id").toString() == id) type = v.toMap().value("type").toInt();
        }
        refs.append(QJsonObject{{"topicId", id}, {"topicType", type}});
    }
    return refs;
}

void PinBoard::pinMessage(const QString& msgId) {
    ZaloSession* s = session();
    const QString convId = m_current;
    if (convId.isEmpty() || msgId.isEmpty()) return;
    if (!s) {
        notice(t("Cần kết nối tới Zalo để ghim tin nhắn", "Connect to Zalo to pin messages"));
        return;
    }
    const QJsonObject raw = m_hooks.rawMessage ? m_hooks.rawMessage(msgId) : QJsonObject();
    if (raw.isEmpty()) {
        notice(t("Chỉ ghim được tin nhắn đã gửi tới máy chủ", "Only messages on the server can be pinned"));
        return;
    }
    if (isPinned(msgId)) {
        notice(t("Tin nhắn này đã được ghim", "This message is already pinned"));
        return;
    }
    QString senderUid = idString(raw.value("uidFrom"));
    const QString me = m_hooks.myUid ? m_hooks.myUid() : QString();
    QString senderName = raw.value("dName").toString();
    if (senderUid.isEmpty() || senderUid == "0" || senderUid == me) senderName = m_hooks.myName ? m_hooks.myName() : QString();
    else if (senderName.isEmpty() && m_hooks.displayName) senderName = m_hooks.displayName(senderUid);
    const QJsonObject topic = topicForMessage(raw, me, senderName);
    QPointer<PinBoard> self(this);
    auto done = [self, convId](bool ok, const QJsonValue&, const QString& e, int code) {
        if (!self) return;
        if (!ok) {
            self->notice(self->t("Không ghim được tin nhắn: ", "Could not pin the message: ") + self->err(e, code));
            return;
        }
        self->notice(self->t("Đã ghim tin nhắn", "Message pinned"));
        self->load(convId);
    };
    if (isGroup(convId)) s->createGroupPin(convId, topic, done);
    else s->createFriendPin(convId, topic, m_boards.value(convId).version, done);
}

void PinBoard::unpin(const QString& topicId) {
    ZaloSession* s = session();
    const QString convId = m_current;
    if (!s || convId.isEmpty() || topicId.isEmpty()) return;
    QPointer<PinBoard> self(this);
    auto done = [self, convId](bool ok, const QJsonValue&, const QString& e, int code) {
        if (!self) return;
        if (!ok) {
            self->notice(self->t("Không bỏ ghim được: ", "Could not unpin: ") + self->err(e, code));
            self->load(convId);
            return;
        }
        self->notice(self->t("Đã bỏ ghim", "Unpinned"));
        self->load(convId);
    };
    const QJsonArray refs = topicRefs({topicId}, convId);
    const qint64 version = m_boards.value(convId).version;
    if (isGroup(convId)) s->unpinGroupPins(convId, refs, version, done);
    else s->unpinFriendPins(convId, refs, version, done);
}

void PinBoard::unpinMessage(const QString& msgId) {
    for (const QVariant& v : m_boards.value(m_current).topics) {
        if (v.toMap().value("msgId").toString() == msgId) {
            unpin(v.toMap().value("id").toString());
            return;
        }
    }
}

void PinBoard::moveToTop(const QString& topicId) {
    ZaloSession* s = session();
    const QString convId = m_current;
    if (!s || convId.isEmpty()) return;
    QStringList order{topicId};
    for (const QVariant& v : m_boards.value(convId).topics) {
        const QString id = v.toMap().value("id").toString();
        if (id != topicId) order << id;
    }
    QPointer<PinBoard> self(this);
    auto done = [self, convId](bool ok, const QJsonValue&, const QString& e, int code) {
        if (!self) return;
        if (!ok) self->notice(self->t("Không sắp xếp được tin ghim: ", "Could not reorder pins: ") + self->err(e, code));
        self->load(convId);
    };
    const QJsonArray refs = topicRefs(order, convId);
    const qint64 version = m_boards.value(convId).version;
    if (isGroup(convId)) s->reorderGroupPins(convId, refs, version, done);
    else s->reorderFriendPins(convId, refs, version, done);
}

bool PinBoard::handleControl(const QString& act, const QJsonValue& data) {
    if (!pinActs().contains(act)) return false;
    const QJsonObject d = asObject(data);
    QString convId;
    for (const char* key : {"groupId", "grid", "conversationId", "userId", "toid", "uidTo", "fromUid"}) {
        convId = idString(d.value(QLatin1String(key)));
        if (!convId.isEmpty()) break;
    }
    if (convId.startsWith('g') && convId.size() > 1 && convId.at(1).isDigit()) convId = convId.mid(1);
    if (convId.isEmpty() || !m_boards.contains(convId)) convId = m_current;
    if (!convId.isEmpty()) load(convId);
    return true;
}

}
