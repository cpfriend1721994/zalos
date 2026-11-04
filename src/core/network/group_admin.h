#pragma once

#include <QtQml/qqmlregistration.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace Zalos::Core::Network {

class ZaloSession;

class GroupAdmin : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString groupId READ groupId NOTIFY manageChanged FINAL)
    Q_PROPERTY(bool manageOpen READ manageOpen NOTIFY manageChanged FINAL)
    Q_PROPERTY(bool canAdminister READ canAdminister NOTIFY manageChanged FINAL)
    Q_PROPERTY(QVariantMap link READ link NOTIFY linkChanged FINAL)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QVariantList pending READ pending NOTIFY pendingChanged FINAL)
    Q_PROPERTY(bool pendingLoaded READ pendingLoaded NOTIFY pendingChanged FINAL)
    Q_PROPERTY(QVariantList blocked READ blocked NOTIFY blockedChanged FINAL)
    Q_PROPERTY(bool blockedLoaded READ blockedLoaded NOTIFY blockedChanged FINAL)
    Q_PROPERTY(bool joinOpen READ joinOpen NOTIFY joinChanged FINAL)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY joinChanged FINAL)

public:
    struct Hooks {
        std::function<ZaloSession*()> session;
        std::function<QString(const QString&)> myRole;
        std::function<bool(const QString&)> isMember;
        std::function<void(const QString&)> notice;
        std::function<QString(const QString&, int)> friendlyError;
        std::function<QString(const QString&, const QString&)> tr;
        std::function<void(const QString&)> groupChanged;
        std::function<void(const QString&, const QString&, const QString&)> openGroup;
    };

    static const QStringList& settingKeys();
    Q_INVOKABLE static bool isGroupLink(const QString& url);
    static QString normalizeGroupLink(const QString& url);

    explicit GroupAdmin(Hooks hooks, QObject* parent = nullptr);

    QString groupId() const { return m_groupId; }
    bool manageOpen() const { return m_manageOpen; }
    bool canAdminister() const;
    QVariantMap link() const { return m_link; }
    QVariantMap settings() const;
    QVariantList pending() const { return m_pending; }
    bool pendingLoaded() const { return m_pendingLoaded; }
    QVariantList blocked() const { return m_blocked; }
    bool blockedLoaded() const { return m_blockedLoaded; }
    bool joinOpen() const { return m_joinOpen; }
    QVariantMap preview() const { return m_preview; }

    void rememberSettings(const QString& groupId, const QJsonObject& setting);
    QJsonObject settingsOf(const QString& groupId) const { return m_settings.value(groupId); }
    void reset();

    Q_INVOKABLE void openManage(const QString& groupId);
    Q_INVOKABLE void closeManage();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void createLink();
    Q_INVOKABLE void disableLink();
    Q_INVOKABLE void copyLink();
    Q_INVOKABLE void setSetting(const QString& key, bool value);
    Q_INVOKABLE void reviewRequest(const QString& memberId, bool approve);
    Q_INVOKABLE void reviewAll(bool approve);
    Q_INVOKABLE void setBlocked(const QString& groupId, const QString& memberId, bool blocked);

    Q_INVOKABLE void openJoin(const QString& link = QString());
    Q_INVOKABLE void closeJoin();
    Q_INVOKABLE void previewLink(const QString& link);
    Q_INVOKABLE void join();

    Q_INVOKABLE void inviteToGroups(const QString& userId, const QStringList& groupIds);

    void applyLinkDetailForTesting(const QJsonObject& data) { applyLink(data); }
    void applyPendingForTesting(const QJsonObject& data) { applyPending(data); }
    void applyBlockedForTesting(const QJsonObject& data) { applyBlocked(data); }
    void applyPreviewForTesting(const QString& link, const QJsonObject& data) { applyPreview(link, data); }
    static QStringList invitedGroupsForTesting(const QJsonObject& data, QStringList* failed) {
        return invitedGroups(data, failed);
    }

signals:
    void manageChanged();
    void linkChanged();
    void settingsChanged();
    void pendingChanged();
    void blockedChanged();
    void joinChanged();

private:
    QString t(const QString& vi, const QString& en) const { return m_hooks.tr ? m_hooks.tr(vi, en) : vi; }
    QString err(const QString& e, int code) const { return m_hooks.friendlyError ? m_hooks.friendlyError(e, code) : e; }
    void notice(const QString& text) const { if (m_hooks.notice) m_hooks.notice(text); }
    ZaloSession* session() const { return m_hooks.session ? m_hooks.session() : nullptr; }

    void loadLink();
    void loadPending();
    void loadBlocked();
    void applyLink(const QJsonObject& data);
    void applyPending(const QJsonObject& data);
    void applyBlocked(const QJsonObject& data);
    void applyPreview(const QString& link, const QJsonObject& data);
    static QStringList invitedGroups(const QJsonObject& data, QStringList* failed);

    Hooks m_hooks;
    QHash<QString, QJsonObject> m_settings;

    QString m_groupId;
    bool m_manageOpen{false};
    QVariantMap m_link;
    QVariantList m_pending;
    bool m_pendingLoaded{false};
    QVariantList m_blocked;
    bool m_blockedLoaded{false};
    quint64 m_generation{0};

    bool m_joinOpen{false};
    QVariantMap m_preview;
};

}
