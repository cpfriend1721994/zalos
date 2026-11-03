#include "label_manager.h"

#include "zalo_session.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <algorithm>

namespace Zalos::Core::Network {

namespace {

constexpr int kVersionConflict = 230; // Zalo PC: reload and try again

int idOf(const QJsonObject& label) {
    return label.value("id").toVariant().toInt();
}

QString colorOf(const QJsonObject& label) {
    QString c = label.value("color").toString();
    if (!c.isEmpty() && !c.startsWith('#')) c.prepend('#');
    return c;
}

} // namespace

const QStringList& LabelManager::defaultPalette() {
    // Zalo PC chat_label color_palette preset.
    static const QStringList palette{"#D91B1B", "#F31BC8", "#FF6905", "#FAC000",
                                     "#4BC377", "#2AC5BB", "#0068FF", "#6F3FCF"};
    return palette;
}

LabelManager::LabelManager(Hooks hooks, QObject* parent) : QObject(parent), m_hooks(std::move(hooks)) {}

QString LabelManager::decodeConvId(const QString& raw) {
    // Groups are "g<id>"; some lists also prefix users with "u". Zalo ids are numeric, so only a letter followed by
    // digits is a prefix.
    if (raw.size() > 1 && (raw.startsWith('g') || raw.startsWith('u'))) {
        bool digits = true;
        for (int i = 1; i < raw.size() && digits; ++i) digits = raw.at(i).isDigit();
        if (digits) return raw.mid(1);
    }
    return raw;
}

QJsonArray LabelManager::parseLabelData(const QJsonValue& raw) {
    QJsonArray list;
    if (raw.isString()) list = QJsonDocument::fromJson(raw.toString().toUtf8()).array();
    else if (raw.isArray()) list = raw.toArray();
    else if (raw.isObject()) { // older shape: {id: label}
        const QJsonObject o = raw.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) list.append(it.value());
    }
    QJsonArray clean;
    for (const QJsonValue& v : list) {
        if (v.isObject() && idOf(v.toObject()) > 0) clean.append(v);
    }
    QList<QJsonValue> sorted(clean.begin(), clean.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonValue& a, const QJsonValue& b) {
        return a.toObject().value("offset").toInt() < b.toObject().value("offset").toInt();
    });
    QJsonArray out;
    for (const QJsonValue& v : sorted) out.append(v);
    return out;
}

QVariantList LabelManager::labels() const {
    QVariantList out;
    for (const QJsonValue& v : m_data) {
        const QJsonObject l = v.toObject();
        QString text = l.value("text").toString();
        if (text.isEmpty()) text = l.value("textKey").toString();
        out.append(QVariantMap{{"id", idOf(l)},
                               {"text", text.left(kMaxNameLength)},
                               {"color", colorOf(l)},
                               {"emoji", l.value("emoji").toString()},
                               {"count", l.value("conversations").toArray().size()}});
    }
    return out;
}

void LabelManager::setManageOpen(bool open) {
    if (open == m_manageOpen) return;
    m_manageOpen = open;
    emit manageOpenChanged();
}

void LabelManager::applyServer(const QJsonObject& data) {
    m_data = parseLabelData(data.value("labelData"));
    m_version = data.value("version").toVariant().toLongLong();
    m_loaded = true;
    publish();
}

void LabelManager::reset() {
    m_data = {};
    m_version = 0;
    m_loaded = false;
    m_busy = false;
    setManageOpen(false);
    publish();
    emit busyChanged();
}

void LabelManager::publish() {
    QHash<QString, QVariantMap> map;
    for (const QJsonValue& v : std::as_const(m_data)) {
        const QJsonObject l = v.toObject();
        QString text = l.value("text").toString();
        if (text.isEmpty()) text = l.value("textKey").toString();
        const QVariantMap info{{"id", idOf(l)}, {"text", text}, {"color", colorOf(l)}};
        for (const QJsonValue& c : l.value("conversations").toArray()) {
            const QString id = decodeConvId(c.toVariant().toString());
            if (!id.isEmpty() && !map.contains(id)) map.insert(id, info); // first label wins, like Zalo PC
        }
    }
    if (m_hooks.apply) m_hooks.apply(map);
    emit labelsChanged();
}

void LabelManager::reload() {
    ZaloSession* s = session();
    if (!s) return;
    QPointer<LabelManager> self(this);
    s->fetchLabels([self](bool ok, const QJsonValue& data, const QString&, int) {
        if (self && ok) self->applyServer(data.toObject());
    });
}

int LabelManager::labelOf(const QString& convId) const {
    for (const QJsonValue& v : m_data) {
        for (const QJsonValue& c : v.toObject().value("conversations").toArray()) {
            if (decodeConvId(c.toVariant().toString()) == convId) return idOf(v.toObject());
        }
    }
    return 0;
}

QJsonArray LabelManager::withAssignment(const QString& convId, int labelId) const {
    const bool group = m_hooks.isGroup && m_hooks.isGroup(convId);
    const QString encoded = encodeConvId(convId, group);
    const bool hadIt = labelOf(convId) == labelId;
    QJsonArray next;
    for (const QJsonValue& v : m_data) {
        QJsonObject l = v.toObject();
        QJsonArray convs;
        for (const QJsonValue& c : l.value("conversations").toArray()) {
            if (decodeConvId(c.toVariant().toString()) != convId) convs.append(c);
        }
        if (idOf(l) == labelId && labelId > 0 && !hadIt) convs.append(encoded);
        l.insert("conversations", convs);
        next.append(l);
    }
    return next;
}

void LabelManager::assign(const QString& convId, int labelId) {
    if (convId.isEmpty() || !m_loaded) return;
    if (labelId > 0 && labelOf(convId) != labelId) {
        for (const QJsonValue& v : std::as_const(m_data)) {
            const QJsonObject l = v.toObject();
            if (idOf(l) == labelId && l.value("conversations").toArray().size() >= kMaxChatsPerLabel) {
                notice(t("Thẻ phân loại này đã đủ %1 hội thoại", "This label already has %1 conversations").arg(kMaxChatsPerLabel));
                return;
            }
        }
    }
    submit(withAssignment(convId, labelId), QString());
}

QString LabelManager::validateName(const QString& text, int ignoreId) const {
    const QString name = text.trimmed();
    if (name.isEmpty()) return t("Tên thẻ không được để trống", "The label needs a name");
    if (name.size() > kMaxNameLength)
        return t("Tên thẻ tối đa %1 ký tự", "Label names are at most %1 characters").arg(kMaxNameLength);
    for (const QJsonValue& v : m_data) {
        const QJsonObject l = v.toObject();
        if (idOf(l) != ignoreId && l.value("text").toString().trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return t("Đã có thẻ phân loại cùng tên", "A label with this name already exists");
    }
    return {};
}

QString LabelManager::create(const QString& text, const QString& color) {
    if (!m_loaded) return t("Chưa tải được thẻ phân loại", "Labels are not loaded yet");
    if (m_data.size() >= kMaxLabels) return t("Tối đa %1 thẻ phân loại", "At most %1 labels").arg(kMaxLabels);
    const QString why = validateName(text, -1);
    if (!why.isEmpty()) return why;
    int nextId = 1;
    for (const QJsonValue& v : std::as_const(m_data)) nextId = qMax(nextId, idOf(v.toObject()) + 1); // Zalo PC genId
    QJsonArray next = m_data;
    next.append(QJsonObject{{"id", nextId},
                            {"text", text.trimmed()},
                            {"conversations", QJsonArray{}},
                            {"color", color.isEmpty() ? defaultPalette().first() : color},
                            {"offset", int(m_data.size())},
                            {"emoji", ""},
                            {"createTime", QDateTime::currentMSecsSinceEpoch()}});
    submit(next, t("Đã thêm thẻ phân loại", "Label added"));
    return {};
}

QString LabelManager::edit(int labelId, const QString& text, const QString& color) {
    if (!m_loaded) return t("Chưa tải được thẻ phân loại", "Labels are not loaded yet");
    const QString why = validateName(text, labelId);
    if (!why.isEmpty()) return why;
    QJsonArray next;
    bool found = false;
    for (const QJsonValue& v : std::as_const(m_data)) {
        QJsonObject l = v.toObject();
        if (idOf(l) == labelId) {
            found = true;
            if (l.value("text").toString() != text.trimmed()) l.remove("textKey"); // a renamed default label
            l.insert("text", text.trimmed());
            if (!color.isEmpty()) l.insert("color", color);
        }
        next.append(l);
    }
    if (!found) return t("Không tìm thấy thẻ phân loại", "Label not found");
    submit(next, t("Đã cập nhật thẻ phân loại", "Label updated"));
    return {};
}

void LabelManager::remove(int labelId) {
    if (!m_loaded) return;
    QJsonArray next;
    int offset = 0;
    for (const QJsonValue& v : std::as_const(m_data)) {
        QJsonObject l = v.toObject();
        if (idOf(l) == labelId) continue;
        l.insert("offset", offset++);
        next.append(l);
    }
    if (next.size() == m_data.size()) return;
    submit(next, t("Đã xóa thẻ phân loại", "Label deleted"));
}

void LabelManager::move(int labelId, int delta) {
    if (!m_loaded || delta == 0) return;
    QList<QJsonValue> list(m_data.begin(), m_data.end());
    int from = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (idOf(list.at(i).toObject()) == labelId) from = i;
    }
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= list.size()) return;
    list.move(from, to);
    QJsonArray next;
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject l = list.at(i).toObject();
        l.insert("offset", i);
        next.append(l);
    }
    submit(next, QString());
}

void LabelManager::submit(const QJsonArray& next, const QString& okNotice) {
    m_lastSubmitted = next;
    ZaloSession* s = session();
    if (!s) {
        notice(t("Cần kết nối tới Zalo để đổi thẻ phân loại", "Connect to Zalo to change labels"));
        return;
    }
    m_busy = true;
    emit busyChanged();
    QPointer<LabelManager> self(this);
    s->updateLabels(next, m_version, [self, okNotice, next](bool ok, const QJsonValue& data, const QString& e, int code) {
        if (!self) return;
        self->m_busy = false;
        emit self->busyChanged();
        if (!ok) {
            if (code == kVersionConflict) {
                self->notice(self->t("Thẻ phân loại vừa được đổi trên thiết bị khác. Đã tải lại, hãy thử lại.",
                                     "Labels were just changed on another device. Reloaded — please try again."));
                self->reload();
                return;
            }
            const QString why = self->m_hooks.friendlyError ? self->m_hooks.friendlyError(e, code) : e;
            self->notice(self->t("Không cập nhật được thẻ phân loại: ", "Could not update labels: ") + why);
            return;
        }
        QJsonObject reply = data.toObject();
        if (!reply.contains("labelData")) reply.insert("labelData", next); // keep what we sent
        if (!reply.contains("version")) reply.insert("version", self->m_version + 1);
        self->applyServer(reply);
        if (!okNotice.isEmpty()) self->notice(okNotice);
    });
}

} // namespace Zalos::Core::Network
