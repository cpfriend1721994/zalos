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

// Conversation labels ("Phân loại", zca-js getLabels/updateLabels, Zalo PC label manager). The server keeps one
// list per account: [{id, text, textKey, conversations[], color, offset, emoji, createTime}] plus a version; every
// change sends the whole list with the last version (error 230 = changed elsewhere first → reload). A conversation
// carries at most one label (assigning another moves it). Conversation ids: user id, or "g" + group id.
// Limits and palette are Zalo PC's chat_label defaults. Exposed to QML as ZaloClient.labels.
class LabelManager : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // [{id, text, color, emoji, count}] in display order (offset)
    Q_PROPERTY(QVariantList labels READ labels NOTIFY labelsChanged FINAL)
    Q_PROPERTY(QStringList palette READ palette CONSTANT FINAL)
    Q_PROPERTY(int maxLabels READ maxLabels CONSTANT FINAL)
    Q_PROPERTY(int maxNameLength READ maxNameLength CONSTANT FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY labelsChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool manageOpen READ manageOpen WRITE setManageOpen NOTIFY manageOpenChanged FINAL)

public:
    struct Hooks {
        std::function<ZaloSession*()> session;                    // nullptr when signed out
        std::function<bool(const QString&)> isGroup;              // plain conversation id
        std::function<void(const QString&)> notice;
        std::function<QString(const QString&, int)> friendlyError;
        std::function<QString(const QString&, const QString&)> tr; // (vi, en)
        // convId (no prefix) -> {id, text, color}; ids missing from the map have no label.
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

    // Server reply of convlabel/get or convlabel/update ({labelData: string|array, version}).
    void applyServer(const QJsonObject& data);
    void reset();

    Q_INVOKABLE void reload();
    Q_INVOKABLE int labelOf(const QString& convId) const;
    // Zalo PC semantics: the conversation leaves every label; it joins `labelId` unless it already had it
    // (toggle). labelId 0 only removes.
    Q_INVOKABLE void assign(const QString& convId, int labelId);
    // Validation failures return a Vietnamese/English reason, "" when the request went out.
    Q_INVOKABLE QString create(const QString& text, const QString& color);
    Q_INVOKABLE QString edit(int labelId, const QString& text, const QString& color);
    Q_INVOKABLE void remove(int labelId);
    Q_INVOKABLE void move(int labelId, int delta);

    // Pure helpers (tests).
    static QString encodeConvId(const QString& convId, bool isGroup) { return isGroup ? "g" + convId : convId; }
    static QString decodeConvId(const QString& raw);
    static QJsonArray parseLabelData(const QJsonValue& raw);
    QJsonArray dataForTesting() const { return m_data; }
    qint64 versionForTesting() const { return m_version; }
    // The list that assign() would send (no network).
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
    QJsonArray m_data;     // server objects in display order, unknown fields preserved
    qint64 m_version{0};
    bool m_loaded{false};
    bool m_busy{false};
    bool m_manageOpen{false};
    QJsonArray m_lastSubmitted; // tests
};

} // namespace Zalos::Core::Network
