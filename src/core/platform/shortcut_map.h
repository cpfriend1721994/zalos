#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QObject>
#include <QStringList>

namespace Zalos::Core::Platform {

class ShortcutMap : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
public:
    explicit ShortcutMap(QObject* parent = nullptr);

    Q_INVOKABLE QStringList keys(const QString& command) const { return m_keys.value(command); }
    Q_INVOKABLE QVariantList keySequences(const QString& command) const {
        QVariantList out;
        const QStringList list = m_keys.value(command);
        out.reserve(list.size());
        for (const QString& k : list) out << k;
        return out;
    }
    Q_INVOKABLE QVariantList all() const;
    Q_INVOKABLE QString primaryText(const QString& cmd) const;
    QStringList commands() const { return m_order; }

    static QHash<QString, QStringList> defaults();
    QString customPath() const { return m_customPath; }
    void load(const QString& configDir = QString());

private:
    QHash<QString, QStringList> m_keys;
    QStringList m_order;
    QString m_customPath;
};

}
