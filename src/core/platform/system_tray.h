#pragma once

#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

#include <QAction>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QImage>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QDBusServiceWatcher;

namespace Zalos::Core::Platform {

struct SniPixmap {
    int width{0};
    int height{0};
    QByteArray data;
};
using SniPixmapList = QList<SniPixmap>;

struct SniToolTip {
    QString iconName;
    SniPixmapList iconPixmap;
    QString title;
    QString subTitle;
};

struct DbusMenuLayoutItem {
    int id{0};
    QVariantMap properties;
    QVariantList children;
};

struct DbusMenuItemProps {
    int id{0};
    QVariantMap properties;
};
using DbusMenuItemPropsList = QList<DbusMenuItemProps>;

struct DbusMenuRemovedProps {
    int id{0};
    QStringList properties;
};
using DbusMenuRemovedPropsList = QList<DbusMenuRemovedProps>;

struct DbusMenuEvent {
    int id{0};
    QString eventId;
    QDBusVariant data;
    uint timestamp{0};
};
using DbusMenuEventList = QList<DbusMenuEvent>;

QDBusArgument& operator<<(QDBusArgument& arg, const SniPixmap& p);
const QDBusArgument& operator>>(const QDBusArgument& arg, SniPixmap& p);
QDBusArgument& operator<<(QDBusArgument& arg, const SniToolTip& t);
const QDBusArgument& operator>>(const QDBusArgument& arg, SniToolTip& t);
QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuLayoutItem& item);
const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuLayoutItem& item);
QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuItemProps& props);
const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuItemProps& props);
QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuRemovedProps& props);
const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuRemovedProps& props);
QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuEvent& ev);
const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuEvent& ev);

class SystemTrayController;

namespace Detail {

class StatusNotifierAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category FINAL)
    Q_PROPERTY(QString Id READ id FINAL)
    Q_PROPERTY(QString Title READ title FINAL)
    Q_PROPERTY(QString Status READ status FINAL)
    Q_PROPERTY(quint32 WindowId READ windowId FINAL)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu FINAL)
    Q_PROPERTY(QDBusObjectPath Menu READ menu FINAL)
    Q_PROPERTY(QString IconName READ iconName FINAL)
    Q_PROPERTY(Zalos::Core::Platform::SniPixmapList IconPixmap READ iconPixmap FINAL)
    Q_PROPERTY(QString IconThemePath READ iconThemePath FINAL)
    Q_PROPERTY(QString OverlayIconName READ overlayIconName FINAL)
    Q_PROPERTY(Zalos::Core::Platform::SniPixmapList OverlayIconPixmap READ overlayIconPixmap FINAL)
    Q_PROPERTY(QString AttentionIconName READ attentionIconName FINAL)
    Q_PROPERTY(Zalos::Core::Platform::SniPixmapList AttentionIconPixmap READ attentionIconPixmap FINAL)
    Q_PROPERTY(QString AttentionMovieName READ attentionMovieName FINAL)
    Q_PROPERTY(Zalos::Core::Platform::SniToolTip ToolTip READ toolTip FINAL)

public:
    explicit StatusNotifierAdaptor(SystemTrayController* owner);
    QString category() const { return QStringLiteral("Communications"); }
    QString id() const { return QStringLiteral("zalos"); }
    QString title() const { return QStringLiteral("Zalos"); }
    QString status() const { return QStringLiteral("Active"); }
    quint32 windowId() const { return 0; }
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menu() const { return QDBusObjectPath(QStringLiteral("/MenuBar")); }
    QString iconName() const { return {}; }
    Zalos::Core::Platform::SniPixmapList iconPixmap() const;
    QString iconThemePath() const { return {}; }
    QString overlayIconName() const { return {}; }
    Zalos::Core::Platform::SniPixmapList overlayIconPixmap() const { return {}; }
    QString attentionIconName() const { return {}; }
    Zalos::Core::Platform::SniPixmapList attentionIconPixmap() const { return {}; }
    QString attentionMovieName() const { return {}; }
    Zalos::Core::Platform::SniToolTip toolTip() const;

public Q_SLOTS:
    void Activate(int x, int y);
    void SecondaryActivate(int x, int y);
    void ContextMenu(int x, int y);
    void Scroll(int delta, const QString& orientation);
    void ProvideXdgActivationToken(const QString& token);

Q_SIGNALS:
    void NewTitle();
    void NewIcon();
    void NewToolTip();
    void NewStatus(const QString& status);

private:
    SystemTrayController* m_owner{nullptr};
};

class DbusMenuAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version FINAL)
    Q_PROPERTY(QString TextDirection READ textDirection FINAL)
    Q_PROPERTY(QString Status READ status FINAL)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath FINAL)

public:
    explicit DbusMenuAdaptor(SystemTrayController* owner);
    uint version() const { return 3u; }
    QString textDirection() const { return QStringLiteral("ltr"); }
    QString status() const { return QStringLiteral("normal"); }
    QStringList iconThemePath() const { return {}; }

public Q_SLOTS:
    uint GetLayout(int parentId, int recursionDepth, const QStringList& propertyNames,
                   Zalos::Core::Platform::DbusMenuLayoutItem& layout);
    Zalos::Core::Platform::DbusMenuItemPropsList GetGroupProperties(const QList<int>& ids,
                                                                    const QStringList& propertyNames);
    QDBusVariant GetProperty(int id, const QString& name);
    void Event(int id, const QString& eventId, const QDBusVariant& data, uint timestamp);
    QList<int> EventGroup(const Zalos::Core::Platform::DbusMenuEventList& events);
    bool AboutToShow(int id);
    void AboutToShowGroup(const QList<int>& ids, QList<int>& updatesNeeded, QList<int>& idErrors);

Q_SIGNALS:
    void ItemsPropertiesUpdated(const Zalos::Core::Platform::DbusMenuItemPropsList& updatedProps,
                                const Zalos::Core::Platform::DbusMenuRemovedPropsList& removedProps);
    void LayoutUpdated(uint revision, int parentId);
    void ItemActivationRequested(int id, uint timestamp);

private:
    SystemTrayController* m_owner{nullptr};
};

}

class SystemTrayController : public QObject {
    Q_OBJECT

public:
    static inline SystemTrayController* s_instance = nullptr;
    explicit SystemTrayController(QObject* parent = nullptr);
    ~SystemTrayController() override;

    static void registerDbusTypes();
    static bool isSystemTrayAvailable();
    static QByteArray toSniArgb32(const QImage& image);

    void setUnreadCount(int count, bool hasMentions = false);
    void setHasMentions(bool hasMentions);
    void setAntiPeeking(bool on);
    void setLanguage(const QString& lang);
    void setWindowVisible(bool visible);
    void setCallState(const QString& phase, const QString& peerName, bool isVideo,
                      bool muted, bool speakerOff, bool cameraOn, bool screenShareOn);
    QString callPhase() const { return m_callPhase; }
    QString toolTip() const { return m_toolTip; }
    bool isVisible() const { return m_registered || m_hostAvailable; }
    QImage badgeImage() const { return m_badgeImage; }
    SniPixmapList sniPixmaps() const;
    SniToolTip sniToolTip() const;
    uint menuRevision() const { return m_menuRevision; }
    DbusMenuLayoutItem menuLayout(int parentId, int recursionDepth, const QStringList& propertyNames) const;
    DbusMenuItemPropsList menuGroupProperties(const QList<int>& ids, const QStringList& propertyNames) const;
    QVariant menuProperty(int id, const QString& name) const;
    QVariantList menuSnapshot() const;
    bool triggerMenuItem(int id);
    void activateFromHost() { emit toggleWindowRequested(); }

    QAction* callStatusAction() const { return m_callStatusAction; }
    QAction* callAcceptAction() const { return m_callAcceptAction; }
    QAction* callAcceptNoCamAction() const { return m_callAcceptNoCamAction; }
    QAction* callRejectAction() const { return m_callRejectAction; }
    QAction* callMuteAction() const { return m_callMuteAction; }
    QAction* callSpeakerAction() const { return m_callSpeakerAction; }
    QAction* callCameraAction() const { return m_callCameraAction; }
    QAction* callScreenShareAction() const { return m_callScreenShareAction; }
    QAction* callHangupAction() const { return m_callHangupAction; }

signals:
    void toggleWindowRequested();
    void quitRequested();
    void toggleDarkModeRequested();
    void antiPeekingToggled(bool on);
    void showCallWindowRequested();
    void acceptCallRequested(bool withoutCamera);
    void rejectCallRequested();
    void hangupCallRequested();
    void toggleCallMuteRequested();
    void toggleCallSpeakerRequested();
    void toggleCallCameraRequested();
    void toggleCallScreenShareRequested();

public slots:
    void showMessage(const QString& title, const QString& message);

private slots:
    void registerWithWatcher();

private:
    void setupMenu();
    void setupDbus();
    QImage createBadgeImage(int count, bool hasMentions);
    QVariantMap actionProperties(const QAction* action, const QStringList& filter) const;
    void notifyMenuChanged();
    void refreshIconAndTooltip();
    void retranslate();
    QString L(const QString& vi, const QString& en) const { return m_language == QLatin1String("en") ? en : vi; }

    Detail::StatusNotifierAdaptor* m_sniAdaptor{nullptr};
    Detail::DbusMenuAdaptor* m_menuAdaptor{nullptr};
    QDBusServiceWatcher* m_watcher{nullptr};
    QString m_serviceName;
    bool m_busObjectsRegistered{false};
    bool m_registered{false};
    bool m_hostAvailable{false};
    QImage m_badgeImage;
    QString m_toolTip;
    uint m_menuRevision{1};
    QList<QAction*> m_menuActions;

    int m_unreadCount{0};
    bool m_hasMentions{false};
    bool m_antiPeeking{false};
    QString m_callPhase;
    QString m_callPeerName;
    bool m_callIsVideo{false};
    bool m_callMuted{false};
    bool m_callSpeakerOff{false};
    bool m_callCameraOn{false};
    bool m_callScreenShareOn{false};
    QAction* m_callStatusAction{nullptr};
    QAction* m_callAcceptAction{nullptr};
    QAction* m_callAcceptNoCamAction{nullptr};
    QAction* m_callRejectAction{nullptr};
    QAction* m_callMuteAction{nullptr};
    QAction* m_callSpeakerAction{nullptr};
    QAction* m_callCameraAction{nullptr};
    QAction* m_callScreenShareAction{nullptr};
    QAction* m_callHangupAction{nullptr};
    QAction* m_callSeparator{nullptr};
    QAction* m_antiPeekAction{nullptr};
    QAction* m_openAction{nullptr};
    QAction* m_themeAction{nullptr};
    QAction* m_separator2{nullptr};
    QAction* m_quitAction{nullptr};
    bool m_windowVisible{true};
    QString m_language{QStringLiteral("vi")};
    QImage m_baseImage;
    int m_lastBadgeCount{-1};
    bool m_lastBadgeMentions{false};
    bool m_lastBadgeAntiPeeking{false};
    QString m_lastBadgeCallPhase;
};

}

Q_DECLARE_METATYPE(Zalos::Core::Platform::SniPixmap)
Q_DECLARE_METATYPE(Zalos::Core::Platform::SniPixmapList)
Q_DECLARE_METATYPE(Zalos::Core::Platform::SniToolTip)
Q_DECLARE_METATYPE(Zalos::Core::Platform::DbusMenuLayoutItem)
Q_DECLARE_METATYPE(Zalos::Core::Platform::DbusMenuItemProps)
Q_DECLARE_METATYPE(Zalos::Core::Platform::DbusMenuItemPropsList)
Q_DECLARE_METATYPE(Zalos::Core::Platform::DbusMenuRemovedProps)
Q_DECLARE_METATYPE(Zalos::Core::Platform::DbusMenuRemovedPropsList)
Q_DECLARE_METATYPE(Zalos::Core::Platform::DbusMenuEvent)
Q_DECLARE_METATYPE(Zalos::Core::Platform::DbusMenuEventList)
