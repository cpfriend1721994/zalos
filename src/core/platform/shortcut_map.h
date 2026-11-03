#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QObject>
#include <QStringList>

namespace Zalos::Core::Platform {

// Central keyboard shortcut table (tdesktop-style commands). QML binds `Shortcut.sequences` to keys(command);
// users override entries in <config>/Zalos/shortcuts-custom.json:
//   [ {"command": "chat.next", "keys": ["Alt+J"]}, {"command": "app.quit", "keys": []} ]   (empty = disabled)
// shortcuts-default.json next to it lists every command with its default keys, as a template.
class ShortcutMap : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
public:
    explicit ShortcutMap(QObject* parent = nullptr);

    // Key sequences of `command` (portable text, e.g. "Ctrl+Shift+V"); empty when unknown or disabled.
    Q_INVOKABLE QStringList keys(const QString& command) const { return m_keys.value(command); }
    // Every command with its effective keys: [{command, keys}] (for a settings list / tests).
    Q_INVOKABLE QVariantList all() const;
    QStringList commands() const { return m_order; }

    static QHash<QString, QStringList> defaults();
    QString customPath() const { return m_customPath; }
    // Re-reads the custom file (also used by tests with an explicit directory).
    void load(const QString& configDir = QString());

private:
    QHash<QString, QStringList> m_keys;
    QStringList m_order;
    QString m_customPath;
};

} // namespace Zalos::Core::Platform
