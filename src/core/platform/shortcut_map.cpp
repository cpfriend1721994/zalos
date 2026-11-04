#include "shortcut_map.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QStandardPaths>
#include <QVariantMap>

namespace Zalos::Core::Platform {

namespace {

const QStringList kOrder = {
    "chat.next", "chat.previous", "chat.pinned1", "chat.pinned2", "chat.pinned3", "chat.pinned4", "chat.pinned5",
    "chat.search", "chat.find", "composer.voice", "privacy.antiPeeking", "app.lock", "app.hide", "app.quit",
};

}

QHash<QString, QStringList> ShortcutMap::defaults() {
    return {
        {"chat.next", {"Alt+Down", "Ctrl+PgDown"}},
        {"chat.previous", {"Alt+Up", "Ctrl+PgUp"}},
        {"chat.pinned1", {"Ctrl+1"}},
        {"chat.pinned2", {"Ctrl+2"}},
        {"chat.pinned3", {"Ctrl+3"}},
        {"chat.pinned4", {"Ctrl+4"}},
        {"chat.pinned5", {"Ctrl+5"}},
        {"chat.search", {"Ctrl+K"}},
        {"chat.find", {"Ctrl+F"}},
        {"composer.voice", {"Ctrl+Shift+V"}},
        {"privacy.antiPeeking", {"Ctrl+Shift+H"}},
        {"app.lock", {"Ctrl+L"}},
        {"app.hide", {"Ctrl+W"}},
        {"app.quit", {"Ctrl+Q"}},
    };
}

ShortcutMap::ShortcutMap(QObject* parent) : QObject(parent) { load(); }

void ShortcutMap::load(const QString& configDir) {
    m_keys = defaults();
    m_order = kOrder;
    QString dir = configDir;
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/Zalos";
    QDir().mkpath(dir);
    m_customPath = dir + "/shortcuts-custom.json";

    QJsonArray defaultsJson;
    for (const QString& command : std::as_const(m_order)) {
        defaultsJson.append(QJsonObject{{"command", command}, {"keys", QJsonArray::fromStringList(m_keys.value(command))}});
    }
    QFile templateFile(dir + "/shortcuts-default.json");
    if (templateFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        templateFile.write(QJsonDocument(defaultsJson).toJson(QJsonDocument::Indented));
    }

    QFile custom(m_customPath);
    if (!custom.open(QIODevice::ReadOnly)) return;
    const QJsonArray entries = QJsonDocument::fromJson(custom.readAll()).array();
    for (const QJsonValue& v : entries) {
        const QJsonObject o = v.toObject();
        const QString command = o.value("command").toString();
        if (!m_keys.contains(command)) continue;
        QStringList keys;
        for (const QJsonValue& k : o.value("keys").toArray()) {
            const QString text = k.toString().trimmed();
            if (!text.isEmpty() && !QKeySequence(text, QKeySequence::PortableText).isEmpty()) keys << text;
        }
        m_keys.insert(command, keys);
    }
}

QVariantList ShortcutMap::all() const {
    QVariantList out;
    for (const QString& command : m_order) {
        out.append(QVariantMap{{"command", command}, {"keys", m_keys.value(command)}});
    }
    return out;
}

}
