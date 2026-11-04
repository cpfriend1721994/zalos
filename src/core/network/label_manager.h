#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace Zalos::Core::Network {

class ZaloSession;

class LabelManager : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList labels READ labels NOTIFY labelsChanged FINAL)
    Q_PROPERTY(QStringList palette READ palette CONSTANT FINAL)
    Q_PROPERTY(int maxLabels READ maxLabels CONSTANT FINAL)
    Q_PROPERTY(int maxNameLength READ maxNameLength CONSTANT FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY labelsChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool manageOpen READ manageOpen WRITE setManageOpen NOTIFY manageOpenChanged FINAL)

public:
    struct Hooks {
        std::function<ZaloSession*()> session;
        std::function<bool(const QString&)> isGroup;
        std::function<void(const QString&)> notice;
        std::function<QString(const QString&, int)> friendlyError;
        std::function<QString(const QString&, const QString&)> tr;
        std::function<void(const QHash<QString, QVariantMap>&)> apply;
    };

    static constexpr int kMaxLabels = 15;
    static constexpr int kMaxNameLength = 30;
    static constexpr int kMaxChatsPerLabel = 5000;
    static const QStringList& defaultPalette();

    explicit LabelManager(Hooks hooks, QObject* parent = nullptr);

    QVariantList labels() const;
    QStringList palette() const { return defaultPalette(); }
    int maxLabels() const { return kMaxLabels; }
    int maxNameLength() const { return kMaxNameLength; }
    bool loaded() const { return m_loaded; }
    bool busy() const { return m_busy; }
    bool manageOpen() const { return m_manageOpen; }
    void setManageOpen(bool open);

    void applyServer(const QJsonObject& data);
    void reset();

    Q_INVOKABLE void reload();
    Q_INVOKABLE int labelOf(const QString& convId) const;
    Q_INVOKABLE void assign(const QString& convId, int labelId);
    Q_INVOKABLE QString create(const QString& text, const QString& color);
    Q_INVOKABLE QString edit(int labelId, const QString& text, const QString& color);
    Q_INVOKABLE void remove(int labelId);
    Q_INVOKABLE void move(int labelId, int delta);

    static QString encodeConvId(const QString& convId, bool isGroup) { return isGroup ? "g" + convId : convId; }
    static QString decodeConvId(const QString& raw);
    static QJsonArray parseLabelData(const QJsonValue& raw);
    QJsonArray dataForTesting() const { return m_data; }
    qint64 versionForTesting() const { return m_version; }
    QJsonArray assignedForTesting(const QString& convId, int labelId) const { return withAssignment(convId, labelId); }
    QJsonArray lastSubmittedForTesting() const { return m_lastSubmitted; }

signals:
    void labelsChanged();
    void busyChanged();
    void manageOpenChanged();

private:
    QString t(const QString& vi, const QString& en) const { return m_hooks.tr ? m_hooks.tr(vi, en) : vi; }
    void notice(const QString& text) const { if (m_hooks.notice) m_hooks.notice(text); }
    ZaloSession* session() const { return m_hooks.session ? m_hooks.session() : nullptr; }
    QString validateName(const QString& text, int ignoreId) const;
    QJsonArray withAssignment(const QString& convId, int labelId) const;
    void submit(const QJsonArray& next, const QString& okNotice);
    void publish();

    Hooks m_hooks;
    QJsonArray m_data;
    qint64 m_version{0};
    bool m_loaded{false};
    bool m_busy{false};
    bool m_manageOpen{false};
    QJsonArray m_lastSubmitted;
};

}
