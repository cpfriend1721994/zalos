#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

namespace Zalos::Core::Network {

class ZaloSession;

class PinBoard : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList pins READ pins NOTIFY pinsChanged FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY pinsChanged FINAL)
    Q_PROPERTY(int pinLimit READ pinLimit NOTIFY pinsChanged FINAL)

public:
    static constexpr int kMessageTopic = 2;
    static constexpr int kDefaultColor = -16777216;

    struct Hooks {
        std::function<ZaloSession*()> session;
        std::function<bool(const QString&)> isGroup;
        std::function<QJsonObject(const QString&)> rawMessage;
        std::function<QString()> myUid;
        std::function<QString()> myName;
        std::function<QString(const QString&)> displayName;
        std::function<void(const QString&)> notice;
        std::function<QString(const QString&, int)> friendlyError;
        std::function<QString(const QString&, const QString&)> tr;
    };

    explicit PinBoard(Hooks hooks, QObject* parent = nullptr);

    QVariantList pins() const { return m_boards.value(m_current).topics; }
    bool loaded() const { return m_boards.value(m_current).loaded; }
    int pinLimit() const { return m_boards.value(m_current).limit; }

    void setCurrent(const QString& convId);
    void reset();
    bool handleControl(const QString& act, const QJsonValue& data);

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool isPinned(const QString& msgId) const;
    Q_INVOKABLE void pinMessage(const QString& msgId);
    Q_INVOKABLE void unpin(const QString& topicId);
    Q_INVOKABLE void unpinMessage(const QString& msgId);
    Q_INVOKABLE void moveToTop(const QString& topicId);
    Q_INVOKABLE QVariantMap detail(const QString& topicId) const;

    static QVariantMap normalizeTopic(const QJsonValue& raw);
    static QVariantList parseList(const QJsonObject& data, bool isGroup, qint64* version, int* limit);
    static QJsonObject topicForMessage(const QJsonObject& msg, const QString& myUid, const QString& senderName);
    void applyListForTesting(const QString& convId, const QJsonObject& data, bool isGroup) { applyList(convId, data, isGroup); }
    qint64 versionForTesting(const QString& convId) const { return m_boards.value(convId).version; }

signals:
    void pinsChanged();

private:
    struct Board {
        QVariantList topics;
        qint64 version{0};
        int limit{0};
        bool loaded{false};
    };
    QString t(const QString& vi, const QString& en) const { return m_hooks.tr ? m_hooks.tr(vi, en) : vi; }
    void notice(const QString& text) const { if (m_hooks.notice) m_hooks.notice(text); }
    QString err(const QString& e, int code) const { return m_hooks.friendlyError ? m_hooks.friendlyError(e, code) : e; }
    ZaloSession* session() const { return m_hooks.session ? m_hooks.session() : nullptr; }
    bool isGroup(const QString& convId) const { return m_hooks.isGroup && m_hooks.isGroup(convId); }
    void load(const QString& convId);
    void applyList(const QString& convId, const QJsonObject& data, bool isGroup);
    QJsonArray topicRefs(const QStringList& ids, const QString& convId) const;
    void changed(const QString& convId) { if (convId == m_current) emit pinsChanged(); }

    Hooks m_hooks;
    QHash<QString, Board> m_boards;
    QString m_current;
};

}
