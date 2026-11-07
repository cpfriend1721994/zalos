#include "group_admin.h"

#include "zalo_session.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPointer>
#include <QRegularExpression>

namespace Zalos::Core::Network {

namespace {

const QRegularExpression& groupLinkPattern() {
    static const QRegularExpression re(QStringLiteral(R"(^(?:https?://)?(?:www\.)?zalo\.me/g/([A-Za-z0-9_-]{4,64})/?(?:[?#].*)?$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString idString(const QJsonValue& v) {
    return v.isString() ? v.toString() : (v.isDouble() ? QString::number(qint64(v.toDouble())) : QString());
}

qint64 toMs(const QJsonValue& v) {
    const qint64 t = qint64(v.toDouble());
    return t > 0 && t < 100000000000LL ? t * 1000 : t;
}

}

const QStringList& GroupAdmin::settingKeys() {
    static const QStringList keys{"blockName",      "signAdminMsg",   "setTopicOnly", "enableMsgHistory",
                                  "joinAppr",       "lockCreatePost", "lockCreatePoll", "lockSendMsg",
                                  "lockViewMember", "bannFeature",    "dirtyMedia",   "banDuration"};
    return keys;
}

bool GroupAdmin::isGroupLink(const QString& url) {
    return groupLinkPattern().match(url.trimmed()).hasMatch();
}

QString GroupAdmin::normalizeGroupLink(const QString& url) {
    const QRegularExpressionMatch m = groupLinkPattern().match(url.trimmed());
    return m.hasMatch() ? QStringLiteral("https://zalo.me/g/") + m.captured(1) : QString();
}

GroupAdmin::GroupAdmin(Hooks hooks, QObject* parent) : QObject(parent), m_hooks(std::move(hooks)) {}

bool GroupAdmin::canAdminister() const {
    if (m_groupId.isEmpty() || !m_hooks.myRole) return false;
    const QString role = m_hooks.myRole(m_groupId);
    return role == QLatin1String("owner") || role == QLatin1String("admin");
}

QVariantMap GroupAdmin::settings() const {
    QVariantMap out;
    const auto it = m_settings.constFind(m_groupId);
    if (it == m_settings.cend()) return out;
    for (const QString& key : settingKeys()) out.insert(key, it->value(key).toInt() != 0);
    out.insert("addMemberOnly", it->value("addMemberOnly").toInt() != 0);
    return out;
}

void GroupAdmin::rememberSettings(const QString& groupId, const QJsonObject& setting) {
    if (groupId.isEmpty() || setting.isEmpty()) return;
    if (m_settings.value(groupId) == setting) return;
    m_settings.insert(groupId, setting);
    if (groupId == m_groupId) emit settingsChanged();
}

void GroupAdmin::reset() {
    m_settings.clear();
    closeManage();
    closeJoin();
}

void GroupAdmin::openManage(const QString& groupId) {
    if (groupId.isEmpty()) return;
    ++m_generation;
    m_groupId = groupId;
    m_manageOpen = true;
    m_link = {};
    m_pending = {};
    m_pendingLoaded = false;
    m_blocked = {};
    m_blockedLoaded = false;
    emit manageChanged();
    emit linkChanged();
    emit settingsChanged();
    emit pendingChanged();
    emit blockedChanged();
    reload();
}

void GroupAdmin::closeManage() {
    if (!m_manageOpen && m_groupId.isEmpty()) return;
    ++m_generation;
    m_manageOpen = false;
    m_groupId.clear();
    emit manageChanged();
}

void GroupAdmin::reload() {
    if (m_groupId.isEmpty()) return;
    if (m_hooks.groupChanged) m_hooks.groupChanged(m_groupId);
    loadLink();
    if (canAdminister()) {
        loadPending();
        loadBlocked();
    }
}

void GroupAdmin::loadLink() {
    ZaloSession* s = session();
    if (!s) return;
    const quint64 gen = m_generation;
    QPointer<GroupAdmin> self(this);
    s->fetchGroupLink(m_groupId, [self, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self || gen != self->m_generation) return;
        self->applyLink(ok ? data.toObject() : QJsonObject{{"enabled", 0}});
    });
}

void GroupAdmin::applyLink(const QJsonObject& data) {
    const QString link = data.value("link").toString();
    const bool enabled = data.value("enabled").toInt() == 1 && !link.isEmpty();
    m_link = {{"loaded", true},
              {"enabled", enabled},
              {"link", enabled ? link : QString()},
              {"expiration", enabled ? toMs(data.value("expiration_date")) : 0}};
    emit linkChanged();
}

void GroupAdmin::createLink() {
    ZaloSession* s = session();
    if (!s || !canAdminister()) return;
    const quint64 gen = m_generation;
    QPointer<GroupAdmin> self(this);
    s->newGroupLink(m_groupId, [self, gen](bool ok, const QJsonValue& data, const QString& e, int code) {
        if (!self) return;
        if (!ok) {
            self->notice(self->t("Không tạo được link nhóm: ", "Could not create the group link: ") + self->err(e, code));
            return;
        }
        if (gen != self->m_generation) return;
        QJsonObject o = data.toObject();
        if (!o.contains("enabled")) o.insert("enabled", 1);
        self->applyLink(o);
        self->notice(self->t("Đã tạo link tham gia nhóm mới", "New group invite link created"));
    });
}

void GroupAdmin::disableLink() {
    ZaloSession* s = session();
    if (!s || !canAdminister()) return;
    const quint64 gen = m_generation;
    QPointer<GroupAdmin> self(this);
    s->disableGroupLink(m_groupId, [self, gen](bool ok, const QJsonValue&, const QString& e, int code) {
        if (!self) return;
        if (!ok) {
            self->notice(self->t("Không tắt được link nhóm: ", "Could not turn off the group link: ") + self->err(e, code));
            return;
        }
        if (gen != self->m_generation) return;
        self->applyLink(QJsonObject{{"enabled", 0}});
        self->notice(self->t("Đã tắt link tham gia nhóm", "Group invite link turned off"));
    });
}

void GroupAdmin::copyLink() {
    const QString link = m_link.value("link").toString();
    if (link.isEmpty()) return;
    if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(link);
    notice(t("Đã sao chép link nhóm", "Group link copied"));
}

void GroupAdmin::setSetting(const QString& key, bool value) {
    ZaloSession* s = session();
    if (!s || !canAdminister() || !settingKeys().contains(key)) return;
    const auto it = m_settings.constFind(m_groupId);
    if (it == m_settings.cend()) return;
    QJsonObject next = *it;
    next.insert(key, value ? 1 : 0);
    const QString groupId = m_groupId;
    QPointer<GroupAdmin> self(this);
    s->updateGroupSettings(groupId, next, [self, groupId, next](bool ok, const QJsonValue&, const QString& e, int code) {
        if (!self) return;
        if (!ok) {
            self->notice(self->t("Không đổi được cài đặt nhóm: ", "Could not change the group setting: ") + self->err(e, code));
            if (groupId == self->m_groupId) emit self->settingsChanged();
            return;
        }
        self->m_settings.insert(groupId, next);
        if (groupId == self->m_groupId) emit self->settingsChanged();
    });
}

void GroupAdmin::loadPending() {
    ZaloSession* s = session();
    if (!s) return;
    const quint64 gen = m_generation;
    QPointer<GroupAdmin> self(this);
    s->fetchPendingMembers(m_groupId, [self, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self || gen != self->m_generation) return;
        self->applyPending(ok ? data.toObject() : QJsonObject{});
    });
}

void GroupAdmin::applyPending(const QJsonObject& data) {
    QVariantList list;
    for (const QJsonValue& v : data.value("users").toArray()) {
        const QJsonObject u = v.toObject();
        const QString id = idString(u.value("uid"));
        if (id.isEmpty()) continue;
        list.append(QVariantMap{{"id", id}, {"name", u.value("dpn").toString()}, {"avatar", u.value("avatar").toString()}});
    }
    m_pending = list;
    m_pendingLoaded = true;
    emit pendingChanged();
}

void GroupAdmin::reviewRequest(const QString& memberId, bool approve) {
    ZaloSession* s = session();
    if (!s || memberId.isEmpty() || !canAdminister()) return;
    const QString groupId = m_groupId;
    QPointer<GroupAdmin> self(this);
    s->reviewPendingMembers(groupId, {memberId}, approve,
        [self, groupId, memberId, approve](bool ok, const QJsonValue& data, const QString& e, int code) {
            if (!self) return;
            const int status = ok ? data.toObject().value(memberId).toInt(0) : -1;
            if (!ok || (status != 0 && status != 170 && status != 178)) {
                const QString why = !ok ? self->err(e, code)
                                        : status == 166 ? self->t("không đủ quyền", "insufficient permission")
                                                        : QString::number(status);
                self->notice(self->t("Không xử lý được yêu cầu tham gia: ", "Could not handle the join request: ") + why);
                return;
            }
            if (groupId != self->m_groupId) return;
            QVariantList rest;
            for (const QVariant& v : std::as_const(self->m_pending)) {
                if (v.toMap().value("id").toString() != memberId) rest.append(v);
            }
            self->m_pending = rest;
            emit self->pendingChanged();
            if (approve && self->m_hooks.groupChanged) self->m_hooks.groupChanged(groupId);
        });
}

void GroupAdmin::reviewAll(bool approve) {
    ZaloSession* s = session();
    if (!s || m_pending.isEmpty() || !canAdminister()) return;
    QStringList ids;
    for (const QVariant& v : std::as_const(m_pending)) ids << v.toMap().value("id").toString();
    const QString groupId = m_groupId;
    QPointer<GroupAdmin> self(this);
    s->reviewPendingMembers(groupId, ids, approve, [self, groupId, approve](bool ok, const QJsonValue&, const QString& e, int code) {
        if (!self) return;
        if (!ok) {
            self->notice(self->t("Không xử lý được yêu cầu tham gia: ", "Could not handle the join requests: ") + self->err(e, code));
            return;
        }
        if (approve && self->m_hooks.groupChanged) self->m_hooks.groupChanged(groupId);
        if (groupId == self->m_groupId) self->loadPending();
    });
}

void GroupAdmin::loadBlocked() {
    ZaloSession* s = session();
    if (!s) return;
    const quint64 gen = m_generation;
    QPointer<GroupAdmin> self(this);
    s->fetchBlockedMembers(m_groupId, 1, 200, [self, gen](bool ok, const QJsonValue& data, const QString&, int) {
        if (!self || gen != self->m_generation) return;
        self->applyBlocked(ok ? data.toObject() : QJsonObject{});
    });
}

void GroupAdmin::applyBlocked(const QJsonObject& data) {
    QVariantList list;
    for (const QJsonValue& v : data.value("blocked_members").toArray()) {
        const QJsonObject u = v.toObject();
        const QString id = idString(u.value("id"));
        if (id.isEmpty()) continue;
        QString name = u.value("dName").toString();
        if (name.isEmpty()) name = u.value("zaloName").toString();
        list.append(QVariantMap{{"id", id}, {"name", name}, {"avatar", u.value("avatar").toString()}});
    }
    m_blocked = list;
    m_blockedLoaded = true;
    emit blockedChanged();
}

void GroupAdmin::setBlocked(const QString& groupId, const QString& memberId, bool blocked) {
    ZaloSession* s = session();
    if (!s || groupId.isEmpty() || memberId.isEmpty()) return;
    QPointer<GroupAdmin> self(this);
    s->setMembersBlocked(groupId, {memberId}, blocked,
        [self, groupId, memberId, blocked](bool ok, const QJsonValue&, const QString& e, int code) {
            if (!self) return;
            if (!ok) {
                self->notice((blocked ? self->t("Không chặn được thành viên: ", "Could not block the member: ")
                                      : self->t("Không bỏ chặn được: ", "Could not unblock: ")) + self->err(e, code));
                return;
            }
            self->notice(blocked ? self->t("Đã chặn khỏi nhóm", "Blocked from the group")
                                 : self->t("Đã bỏ chặn", "Unblocked"));
            if (self->m_hooks.groupChanged) self->m_hooks.groupChanged(groupId);
            if (groupId == self->m_groupId) self->loadBlocked();
        });
}

void GroupAdmin::openJoin(const QString& link) {
    m_joinOpen = true;
    m_preview = {};
    emit joinChanged();
    if (!link.isEmpty()) previewLink(link);
}

void GroupAdmin::closeJoin() {
    if (!m_joinOpen && m_preview.isEmpty()) return;
    m_joinOpen = false;
    m_preview = {};
    emit joinChanged();
}

void GroupAdmin::previewLink(const QString& raw) {
    const QString link = normalizeGroupLink(raw);
    if (link.isEmpty()) {
        m_preview = {{"link", raw.trimmed()}, {"error", t("Link không phải link tham gia nhóm Zalo (zalo.me/g/…)",
                                                          "Not a Zalo group invite link (zalo.me/g/…)")}};
        emit joinChanged();
        return;
    }
    ZaloSession* s = session();
    if (!s) {
        m_preview = {{"link", link}, {"error", t("Cần kết nối tới Zalo để xem nhóm", "Connect to Zalo to see the group")}};
        emit joinChanged();
        return;
    }
    m_preview = {{"link", link}, {"loading", true}};
    emit joinChanged();
    QPointer<GroupAdmin> self(this);
    s->fetchGroupLinkInfo(link, 1, [self, link](bool ok, const QJsonValue& data, const QString& e, int code) {
        if (!self || self->m_preview.value("link").toString() != link) return;
        if (!ok) {
            self->m_preview = {{"link", link},
                               {"error", self->t("Không mở được link nhóm: ", "Could not open the group link: ") + self->err(e, code)}};
            emit self->joinChanged();
            return;
        }
        self->applyPreview(link, data.toObject());
    });
}

void GroupAdmin::applyPreview(const QString& link, const QJsonObject& g) {
    QVariantList members;
    for (const QJsonValue& v : g.value("currentMems").toArray()) {
        const QJsonObject m = v.toObject();
        QString name = m.value("dName").toString();
        if (name.isEmpty()) name = m.value("zaloName").toString();
        members.append(QVariantMap{{"id", idString(m.value("id"))}, {"name", name}, {"avatar", m.value("avatar").toString()}});
        if (members.size() >= 12) break;
    }
    const QString groupId = idString(g.value("groupId"));
    QString avatar = g.value("avt").toString();
    if (avatar.isEmpty()) avatar = g.value("fullAvt").toString();
    m_preview = {{"link", link},
                 {"groupId", groupId},
                 {"name", g.value("name").toString()},
                 {"desc", g.value("desc").toString()},
                 {"avatar", avatar},
                 {"totalMember", g.value("totalMember").toInt()},
                 {"members", members},
                 {"isMember", !groupId.isEmpty() && m_hooks.isMember && m_hooks.isMember(groupId)},
                 {"needsApproval", g.value("setting").toObject().value("joinAppr").toInt() == 1}};
    emit joinChanged();
}

void GroupAdmin::join() {
    ZaloSession* s = session();
    const QString link = m_preview.value("link").toString();
    if (!s || link.isEmpty() || m_preview.value("joining").toBool()) return;
    if (m_preview.value("isMember").toBool()) {
        if (m_hooks.openGroup) m_hooks.openGroup(m_preview.value("groupId").toString(), m_preview.value("name").toString(),
                                                 m_preview.value("avatar").toString());
        closeJoin();
        return;
    }
    m_preview.insert("joining", true);
    emit joinChanged();
    QPointer<GroupAdmin> self(this);
    s->joinGroupLink(link, [self, link](bool ok, const QJsonValue&, const QString& e, int code) {
        if (!self) return;
        const QVariantMap p = self->m_preview;
        const bool current = p.value("link").toString() == link;
        if (!ok && code != 178 && code != 240) {
            if (current) {
                self->m_preview.remove("joining");
                self->m_preview.insert("error", self->t("Không tham gia được nhóm: ", "Could not join the group: ") + self->err(e, code));
                emit self->joinChanged();
            }
            return;
        }
        const QString groupId = p.value("groupId").toString();
        if ((p.value("needsApproval").toBool() || code == 240) && code != 178) {
            self->notice(self->t("Đã gửi yêu cầu tham gia. Chờ trưởng hoặc phó nhóm duyệt.",
                                 "Join request sent. Waiting for the group's owner or deputies."));
        } else {
            self->notice(self->t("Đã tham gia nhóm", "Joined the group"));
            if (self->m_hooks.groupChanged && !groupId.isEmpty()) self->m_hooks.groupChanged(groupId);
            if (self->m_hooks.openGroup && !groupId.isEmpty())
                self->m_hooks.openGroup(groupId, p.value("name").toString(), p.value("avatar").toString());
        }
        if (current) self->closeJoin();
    });
}

QStringList GroupAdmin::invitedGroups(const QJsonObject& data, QStringList* failed) {
    QStringList done;
    const QJsonObject map = data.value("grid_message_map").toObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it.value().toObject().value("error_code").toInt() == 0) done << it.key();
        else if (failed) failed->append(it.key());
    }
    return done;
}

void GroupAdmin::inviteToGroups(const QString& userId, const QStringList& groupIds) {
    ZaloSession* s = session();
    if (!s || userId.isEmpty() || groupIds.isEmpty()) return;
    QPointer<GroupAdmin> self(this);
    s->inviteToGroups(userId, groupIds, [self, groupIds](bool ok, const QJsonValue& data, const QString& e, int code) {
        if (!self) return;
        if (!ok) {
            self->notice(self->t("Không thêm được vào nhóm: ", "Could not add to the groups: ") + self->err(e, code));
            return;
        }
        QStringList failed;
        const QStringList done = invitedGroups(data.toObject(), &failed);
        for (const QString& id : done) {
            if (self->m_hooks.groupChanged) self->m_hooks.groupChanged(id);
        }
        if (failed.isEmpty())
            self->notice(self->t("Đã thêm vào %1 nhóm", "Added to %1 group(s)").arg(done.isEmpty() ? groupIds.size() : done.size()));
        else
            self->notice(self->t("Đã thêm vào %1 nhóm, %2 nhóm không thêm được", "Added to %1 group(s), %2 failed")
                             .arg(done.size()).arg(failed.size()));
    });
}

}
