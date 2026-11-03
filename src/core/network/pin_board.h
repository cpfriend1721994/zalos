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

// Pinned messages of a conversation (Zalo PC "PinTopic"): a pinned message is a board topic of type 2 (MSG_TOPIC)
// whose params name the message (client_msg_id, global_msg_id, senderUid, senderName, msg_type, title, thumb).
// Groups use the group board (pin/list, topic/createv2 pinAct 1, unpinv2 / multi_unpin, pin/update), 1-1 chats the
// friend board (friendboard/list|create|multi_unpin|reorder). Every change carries the board version; the server
// answers with the new one. Other devices' changes come as controls pin_topic / unpin_topic / update_pin_topic /
// reorder_pin_topic → reload. Exposed to QML as ZaloClient.pins (the open conversation's pins, newest first).
class PinBoard : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    // [{id, type, title, senderUid, senderName, msgId, cliMsgId, msgType, thumb}] of the open conversation
    Q_PROPERTY(QVariantList pins READ pins NOTIFY pinsChanged FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY pinsChanged FINAL)
    Q_PROPERTY(int pinLimit READ pinLimit NOTIFY pinsChanged FINAL)

public:
    static constexpr int kMessageTopic = 2;  // Zalo PC MSG_TOPIC
    static constexpr int kDefaultColor = -16777216;

    struct Hooks {
        std::function<ZaloSession*()> session;
        std::function<bool(const QString&)> isGroup;
        std::function<QJsonObject(const QString&)> rawMessage; // TMessage by global id ({} when unknown)
        std::function<QString()> myUid;
        std::function<QString()> myName;
        std::function<QString(const QString&)> displayName; // uid → name
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
    // A cmd 601/603 control: reloads the board when it is a pin act. Returns true when it was one.
    bool handleControl(const QString& act, const QJsonValue& data);

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool isPinned(const QString& msgId) const;
    Q_INVOKABLE void pinMessage(const QString& msgId);
    Q_INVOKABLE void unpin(const QString& topicId);
    Q_INVOKABLE void unpinMessage(const QString& msgId);
    Q_INVOKABLE void moveToTop(const QString& topicId);

    // Pure helpers (tests).
    static QVariantMap normalizeTopic(const QJsonValue& raw);
    static QVariantList parseList(const QJsonObject& data, bool isGroup, qint64* version, int* limit);
    // Topic for a TMessage (Zalo PC createPinTopicFromMessage).
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

} // namespace Zalos::Core::Network
