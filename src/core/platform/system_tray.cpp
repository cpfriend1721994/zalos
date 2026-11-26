#include "system_tray.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QtEndian>
#include <atomic>

namespace Zalos::Core::Platform {

namespace {
std::atomic<quint64> g_sniSeq{0};
}

QDBusArgument& operator<<(QDBusArgument& arg, const SniPixmap& p) {
    arg.beginStructure();
    arg << p.width << p.height << p.data;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, SniPixmap& p) {
    arg.beginStructure();
    arg >> p.width >> p.height >> p.data;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const SniToolTip& t) {
    arg.beginStructure();
    arg << t.iconName << t.iconPixmap << t.title << t.subTitle;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, SniToolTip& t) {
    arg.beginStructure();
    arg >> t.iconName >> t.iconPixmap >> t.title >> t.subTitle;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuLayoutItem& item) {
    arg.beginStructure();
    arg << item.id << item.properties << item.children;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuLayoutItem& item) {
    arg.beginStructure();
    arg >> item.id >> item.properties >> item.children;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuItemProps& props) {
    arg.beginStructure();
    arg << props.id << props.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuItemProps& props) {
    arg.beginStructure();
    arg >> props.id >> props.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuRemovedProps& props) {
    arg.beginStructure();
    arg << props.id << props.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuRemovedProps& props) {
    arg.beginStructure();
    arg >> props.id >> props.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const DbusMenuEvent& ev) {
    arg.beginStructure();
    arg << ev.id << ev.eventId << ev.data << ev.timestamp;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, DbusMenuEvent& ev) {
    arg.beginStructure();
    arg >> ev.id >> ev.eventId >> ev.data >> ev.timestamp;
    arg.endStructure();
    return arg;
}

namespace Detail {

StatusNotifierAdaptor::StatusNotifierAdaptor(SystemTrayController* owner)
    : QDBusAbstractAdaptor(owner), m_owner(owner) {}

SniPixmapList StatusNotifierAdaptor::iconPixmap() const { return m_owner ? m_owner->sniPixmaps() : SniPixmapList{}; }
SniToolTip StatusNotifierAdaptor::toolTip() const { return m_owner ? m_owner->sniToolTip() : SniToolTip{}; }
void StatusNotifierAdaptor::Activate(int, int) {
    if (m_owner) m_owner->activateFromHost();
}
void StatusNotifierAdaptor::SecondaryActivate(int, int) {
    if (m_owner) m_owner->activateFromHost();
}
void StatusNotifierAdaptor::ContextMenu(int, int) {}
void StatusNotifierAdaptor::Scroll(int, const QString&) {}
void StatusNotifierAdaptor::ProvideXdgActivationToken(const QString& token) {
    if (!token.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
}

DbusMenuAdaptor::DbusMenuAdaptor(SystemTrayController* owner)
    : QDBusAbstractAdaptor(owner), m_owner(owner) {}

uint DbusMenuAdaptor::GetLayout(int parentId, int recursionDepth, const QStringList& propertyNames,
                                DbusMenuLayoutItem& layout) {
    if (!m_owner) {
        layout = {};
        return 1u;
    }
    layout = m_owner->menuLayout(parentId, recursionDepth, propertyNames);
    return m_owner->menuRevision();
}

DbusMenuItemPropsList DbusMenuAdaptor::GetGroupProperties(const QList<int>& ids, const QStringList& propertyNames) {
    return m_owner ? m_owner->menuGroupProperties(ids, propertyNames) : DbusMenuItemPropsList{};
}

QDBusVariant DbusMenuAdaptor::GetProperty(int id, const QString& name) {
    return QDBusVariant(m_owner ? m_owner->menuProperty(id, name) : QVariant());
}

void DbusMenuAdaptor::Event(int id, const QString& eventId, const QDBusVariant&, uint) {
    if (m_owner && eventId == QLatin1String("clicked")) m_owner->triggerMenuItem(id);
}

QList<int> DbusMenuAdaptor::EventGroup(const DbusMenuEventList& events) {
    if (m_owner) {
        for (const DbusMenuEvent& ev : events) {
            if (ev.eventId == QLatin1String("clicked")) m_owner->triggerMenuItem(ev.id);
        }
    }
    return {};
}

bool DbusMenuAdaptor::AboutToShow(int) { return false; }

void DbusMenuAdaptor::AboutToShowGroup(const QList<int>&, QList<int>& updatesNeeded, QList<int>& idErrors) {
    updatesNeeded.clear();
    idErrors.clear();
}

}

void SystemTrayController::registerDbusTypes() {
    static const bool registered = []() {
        qDBusRegisterMetaType<SniPixmap>();
        qDBusRegisterMetaType<SniPixmapList>();
        qDBusRegisterMetaType<SniToolTip>();
        qDBusRegisterMetaType<DbusMenuLayoutItem>();
        qDBusRegisterMetaType<DbusMenuItemProps>();
        qDBusRegisterMetaType<DbusMenuItemPropsList>();
        qDBusRegisterMetaType<DbusMenuRemovedProps>();
        qDBusRegisterMetaType<DbusMenuRemovedPropsList>();
        qDBusRegisterMetaType<DbusMenuEvent>();
        qDBusRegisterMetaType<DbusMenuEventList>();
        return true;
    }();
    Q_UNUSED(registered);
}

bool SystemTrayController::isSystemTrayAvailable() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("/StatusNotifierWatcher"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg << QStringLiteral("org.kde.StatusNotifierWatcher")
        << QStringLiteral("IsStatusNotifierHostRegistered");
    const QDBusReply<QDBusVariant> reply = bus.call(msg, QDBus::Block, 200);
    return reply.isValid() && reply.value().variant().toBool();
}

QByteArray SystemTrayController::toSniArgb32(const QImage& image) {
    if (image.isNull()) return {};
    const QImage img = image.format() == QImage::Format_ARGB32
        ? image
        : image.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    QByteArray out(w * h * 4, Qt::Uninitialized);
    auto* dst = reinterpret_cast<uchar*>(out.data());
    for (int y = 0; y < h; ++y) {
        const auto* src = reinterpret_cast<const quint32*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            qToBigEndian<quint32>(src[x], dst);
            dst += 4;
        }
    }
    return out;
}

SystemTrayController::SystemTrayController(QObject* parent)
    : QObject(parent) {
    s_instance = this;
    QObject::connect(this, &QObject::destroyed, [](QObject* gone) {
        if (static_cast<QObject*>(s_instance) == gone) s_instance = nullptr;
    });
    setupMenu();
    m_badgeImage = createBadgeImage(0, false);
    retranslate();
    setupDbus();
}

SystemTrayController::~SystemTrayController() {
    shutdown();
}

void SystemTrayController::shutdown() {
    if (m_watcher) {
        m_watcher->disconnect(this);
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    if (m_busObjectsRegistered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (bus.isConnected()) {
            bus.disconnect(QStringLiteral("org.kde.StatusNotifierWatcher"), QStringLiteral("/StatusNotifierWatcher"),
                           QStringLiteral("org.kde.StatusNotifierWatcher"), QStringLiteral("StatusNotifierHostRegistered"),
                           this, SLOT(registerWithWatcher()));
            bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
            bus.unregisterObject(QStringLiteral("/MenuBar"));
            if (!m_serviceName.isEmpty()) bus.unregisterService(m_serviceName);
        }
        m_busObjectsRegistered = false;
    }
    m_serviceName.clear();
    m_registered = false;
    m_hostAvailable = false;
}

void SystemTrayController::setupDbus() {
    registerDbusTypes();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    m_sniAdaptor = new Detail::StatusNotifierAdaptor(this);
    m_menuAdaptor = new Detail::DbusMenuAdaptor(this);

    const bool sniOk = bus.registerObject(QStringLiteral("/StatusNotifierItem"), this,
                                          QDBusConnection::ExportAdaptors);
    const bool menuOk = bus.registerObject(QStringLiteral("/MenuBar"), this,
                                           QDBusConnection::ExportAdaptors);
    m_busObjectsRegistered = sniOk || menuOk;
    if (!sniOk) return;

    const QString candidate = QStringLiteral("org.kde.StatusNotifierItem-%1-%2")
                                  .arg(QCoreApplication::applicationPid())
                                  .arg(++g_sniSeq);
    if (bus.registerService(candidate)) m_serviceName = candidate;

    m_watcher = new QDBusServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"), bus,
                                        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForOwnerChange,
                                        this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &SystemTrayController::registerWithWatcher);
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString&, const QString&, const QString& newOwner) {
                m_registered = false;
                if (!newOwner.isEmpty()) registerWithWatcher();
            });
    bus.connect(QStringLiteral("org.kde.StatusNotifierWatcher"), QStringLiteral("/StatusNotifierWatcher"),
                QStringLiteral("org.kde.StatusNotifierWatcher"), QStringLiteral("StatusNotifierHostRegistered"),
                this, SLOT(registerWithWatcher()));
    registerWithWatcher();
}

void SystemTrayController::registerWithWatcher() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !m_busObjectsRegistered) return;
    m_hostAvailable = isSystemTrayAvailable();
    const QString arg = m_serviceName.isEmpty() ? QStringLiteral("/StatusNotifierItem") : m_serviceName;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("/StatusNotifierWatcher"),
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("RegisterStatusNotifierItem"));
    msg << arg;
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 200);
    m_registered = reply.type() == QDBusMessage::ReplyMessage;
}

void SystemTrayController::setupMenu() {
    auto addAct = [this](bool checkable = false) {
        auto* a = new QAction(this);
        if (checkable) a->setCheckable(true);
        m_menuActions.append(a);
        return a;
    };
    auto addSep = [this]() {
        auto* a = new QAction(this);
        a->setSeparator(true);
        m_menuActions.append(a);
        return a;
    };

    m_callStatusAction = addAct();
    connect(m_callStatusAction, &QAction::triggered, this, &SystemTrayController::showCallWindowRequested);

    m_callAcceptAction = addAct();
    connect(m_callAcceptAction, &QAction::triggered, this, [this]() { emit acceptCallRequested(false); });

    m_callAcceptNoCamAction = addAct();
    connect(m_callAcceptNoCamAction, &QAction::triggered, this, [this]() { emit acceptCallRequested(true); });

    m_callRejectAction = addAct();
    connect(m_callRejectAction, &QAction::triggered, this, &SystemTrayController::rejectCallRequested);

    m_callMuteAction = addAct(true);
    connect(m_callMuteAction, &QAction::triggered, this, &SystemTrayController::toggleCallMuteRequested);

    m_callSpeakerAction = addAct(true);
    connect(m_callSpeakerAction, &QAction::triggered, this, &SystemTrayController::toggleCallSpeakerRequested);

    m_callCameraAction = addAct(true);
    connect(m_callCameraAction, &QAction::triggered, this, &SystemTrayController::toggleCallCameraRequested);

    m_callScreenShareAction = addAct(true);
    connect(m_callScreenShareAction, &QAction::triggered, this, &SystemTrayController::toggleCallScreenShareRequested);

    m_callHangupAction = addAct();
    connect(m_callHangupAction, &QAction::triggered, this, &SystemTrayController::hangupCallRequested);

    m_callSeparator = addSep();
    for (QAction* a : {m_callStatusAction, m_callAcceptAction, m_callAcceptNoCamAction, m_callRejectAction,
                       m_callMuteAction, m_callSpeakerAction, m_callCameraAction, m_callScreenShareAction,
                       m_callHangupAction, m_callSeparator}) {
        a->setVisible(false);
    }

    m_openAction = addAct();
    connect(m_openAction, &QAction::triggered, this, &SystemTrayController::toggleWindowRequested);

    m_settingsAction = addAct();
    connect(m_settingsAction, &QAction::triggered, this, &SystemTrayController::openSettingsRequested);

    m_notificationsAction = addAct(true);
    m_notificationsAction->setChecked(m_notificationsEnabled);
    connect(m_notificationsAction, &QAction::triggered, this, &SystemTrayController::notificationsToggled);

    m_themeAction = addAct();
    connect(m_themeAction, &QAction::triggered, this, &SystemTrayController::toggleDarkModeRequested);

    m_antiPeekAction = addAct(true);
    connect(m_antiPeekAction, &QAction::triggered, this, &SystemTrayController::antiPeekingToggled);

    m_separator2 = addSep();

    m_quitAction = addAct();
    connect(m_quitAction, &QAction::triggered, this, &SystemTrayController::quitRequested);
}

SniPixmapList SystemTrayController::sniPixmaps() const {
    if (m_badgeImage.isNull()) return {};
    return {SniPixmap{m_badgeImage.width(), m_badgeImage.height(), toSniArgb32(m_badgeImage)}};
}

SniToolTip SystemTrayController::sniToolTip() const {
    SniToolTip tip;
    const int nl = m_toolTip.indexOf(QLatin1Char('\n'));
    if (nl < 0) {
        tip.title = m_toolTip;
    } else {
        tip.title = m_toolTip.left(nl);
        tip.subTitle = m_toolTip.mid(nl + 1);
    }
    return tip;
}

QVariantMap SystemTrayController::actionProperties(const QAction* action, const QStringList& filter) const {
    QVariantMap props;
    auto want = [&filter](const QString& k) { return filter.isEmpty() || filter.contains(k); };
    if (action->isSeparator()) {
        if (want(QStringLiteral("type"))) props.insert(QStringLiteral("type"), QStringLiteral("separator"));
        if (want(QStringLiteral("visible"))) props.insert(QStringLiteral("visible"), action->isVisible());
        return props;
    }
    if (want(QStringLiteral("label"))) props.insert(QStringLiteral("label"), action->text());
    if (want(QStringLiteral("visible"))) props.insert(QStringLiteral("visible"), action->isVisible());
    if (want(QStringLiteral("enabled"))) props.insert(QStringLiteral("enabled"), action->isEnabled());
    if (action->isCheckable()) {
        if (want(QStringLiteral("toggle-type"))) props.insert(QStringLiteral("toggle-type"), QStringLiteral("checkmark"));
        if (want(QStringLiteral("toggle-state"))) props.insert(QStringLiteral("toggle-state"), action->isChecked() ? 1 : 0);
    }
    return props;
}

DbusMenuLayoutItem SystemTrayController::menuLayout(int parentId, int recursionDepth,
                                                    const QStringList& propertyNames) const {
    DbusMenuLayoutItem root;
    root.id = parentId;
    if (parentId != 0) return root;
    root.properties.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
    if (recursionDepth == 0) return root;
    for (int i = 0; i < m_menuActions.size(); ++i) {
        DbusMenuLayoutItem child;
        child.id = i + 1;
        child.properties = actionProperties(m_menuActions.at(i), propertyNames);
        root.children.append(QVariant::fromValue(child));
    }
    return root;
}

DbusMenuItemPropsList SystemTrayController::menuGroupProperties(const QList<int>& ids,
                                                                const QStringList& propertyNames) const {
    DbusMenuItemPropsList list;
    for (int id : ids) {
        if (id == 0) {
            list.append(DbusMenuItemProps{0, QVariantMap{{QStringLiteral("children-display"), QStringLiteral("submenu")}}});
        } else if (id > 0 && id <= m_menuActions.size()) {
            list.append(DbusMenuItemProps{id, actionProperties(m_menuActions.at(id - 1), propertyNames)});
        }
    }
    return list;
}

QVariant SystemTrayController::menuProperty(int id, const QString& name) const {
    if (id == 0) {
        if (name == QLatin1String("children-display")) return QStringLiteral("submenu");
        return {};
    }
    if (id < 1 || id > m_menuActions.size()) return {};
    return actionProperties(m_menuActions.at(id - 1), {name}).value(name);
}

QVariantList SystemTrayController::menuSnapshot() const {
    QVariantList out;
    for (int i = 0; i < m_menuActions.size(); ++i) {
        QVariantMap entry = actionProperties(m_menuActions.at(i), {});
        entry.insert(QStringLiteral("id"), i + 1);
        out.append(entry);
    }
    return out;
}

bool SystemTrayController::triggerMenuItem(int id) {
    if (id < 1 || id > m_menuActions.size()) return false;
    QAction* a = m_menuActions.at(id - 1);
    if (!a || a->isSeparator() || !a->isEnabled()) return false;
    a->trigger();
    notifyMenuChanged();
    return true;
}

void SystemTrayController::notifyMenuChanged() {
    ++m_menuRevision;
    if (m_menuAdaptor && m_busObjectsRegistered) emit m_menuAdaptor->LayoutUpdated(m_menuRevision, 0);
}

void SystemTrayController::setLanguage(const QString& lang) {
    const QString next = lang == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("vi");
    if (m_language == next) return;
    m_language = next;
    retranslate();
}

void SystemTrayController::setCallState(const QString& phase, const QString& peerName, bool isVideo,
                                        bool muted, bool speakerOff, bool cameraOn, bool screenShareOn) {
    const bool active = !phase.isEmpty() && phase != QLatin1String("ended");
    const QString normalizedPhase = active ? phase : QString();
    const QString normalizedPeer = active ? peerName : QString();
    if (m_callPhase == normalizedPhase && m_callPeerName == normalizedPeer
        && m_callIsVideo == isVideo && m_callMuted == muted
        && m_callSpeakerOff == speakerOff && m_callCameraOn == cameraOn
        && m_callScreenShareOn == screenShareOn) {
        return;
    }
    m_callPhase = normalizedPhase;
    m_callPeerName = normalizedPeer;
    m_callIsVideo = active && isVideo;
    m_callMuted = active && muted;
    m_callSpeakerOff = active && speakerOff;
    m_callCameraOn = active && cameraOn;
    m_callScreenShareOn = active && screenShareOn;
    retranslate();
}

void SystemTrayController::retranslate() {
    const bool active = !m_callPhase.isEmpty() && m_callPhase != QLatin1String("ended");
    const bool incoming = m_callPhase == QLatin1String("incoming");
    const bool inCall = active && !incoming;
    const QString peer = m_callPeerName.isEmpty() ? QStringLiteral("Zalo") : m_callPeerName;
    if (m_callStatusAction) {
        m_callStatusAction->setVisible(active);
        m_callStatusAction->setText(
            incoming ? L(QStringLiteral("Cuộc gọi đến: %1 (Mở cửa sổ)"), QStringLiteral("Incoming call: %1 (Open window)")).arg(peer)
                     : L(QStringLiteral("Đang gọi: %1 (Mở cửa sổ)"), QStringLiteral("In call: %1 (Open window)")).arg(peer));
    }
    if (m_callAcceptAction) {
        m_callAcceptAction->setVisible(incoming);
        m_callAcceptAction->setText(m_callIsVideo ? L(QStringLiteral("Trả lời bằng video"), QStringLiteral("Answer with video"))
                                                  : L(QStringLiteral("Trả lời cuộc gọi"), QStringLiteral("Answer call")));
    }
    if (m_callAcceptNoCamAction) {
        m_callAcceptNoCamAction->setVisible(incoming && m_callIsVideo);
        m_callAcceptNoCamAction->setText(L(QStringLiteral("Trả lời không mở camera"), QStringLiteral("Answer without camera")));
    }
    if (m_callRejectAction) {
        m_callRejectAction->setVisible(incoming);
        m_callRejectAction->setText(L(QStringLiteral("Từ chối cuộc gọi"), QStringLiteral("Decline call")));
    }
    if (m_callMuteAction) {
        m_callMuteAction->setVisible(inCall);
        m_callMuteAction->setChecked(m_callMuted);
        m_callMuteAction->setText(L(QStringLiteral("Tắt micro"), QStringLiteral("Mute microphone")));
    }
    if (m_callSpeakerAction) {
        m_callSpeakerAction->setVisible(inCall);
        m_callSpeakerAction->setChecked(m_callSpeakerOff);
        m_callSpeakerAction->setText(L(QStringLiteral("Tắt loa"), QStringLiteral("Mute speaker")));
    }
    if (m_callCameraAction) {
        m_callCameraAction->setVisible(inCall && m_callIsVideo);
        m_callCameraAction->setChecked(m_callCameraOn);
        m_callCameraAction->setText(L(QStringLiteral("Bật camera"), QStringLiteral("Camera on")));
    }
    if (m_callScreenShareAction) {
        m_callScreenShareAction->setVisible(m_callPhase == QLatin1String("connected"));
        m_callScreenShareAction->setChecked(m_callScreenShareOn);
        m_callScreenShareAction->setText(L(QStringLiteral("Chia sẻ màn hình"), QStringLiteral("Share screen")));
    }
    if (m_callHangupAction) {
        m_callHangupAction->setVisible(inCall);
        m_callHangupAction->setText(L(QStringLiteral("Kết thúc cuộc gọi"), QStringLiteral("End call")));
    }
    if (m_callSeparator) m_callSeparator->setVisible(active);

    if (m_openAction) {
        m_openAction->setText(m_windowVisible ? L(QStringLiteral("Ẩn Zalos"), QStringLiteral("Hide Zalos"))
                                              : L(QStringLiteral("Mở Zalos"), QStringLiteral("Open Zalos")));
    }
    if (m_settingsAction) m_settingsAction->setText(L(QStringLiteral("Cài đặt"), QStringLiteral("Settings")));
    if (m_notificationsAction) m_notificationsAction->setText(L(QStringLiteral("Thông báo"), QStringLiteral("Notifications")));
    if (m_themeAction) m_themeAction->setText(L(QStringLiteral("Chuyển chế độ Sáng / Tối"), QStringLiteral("Toggle light / dark mode")));
    if (m_antiPeekAction) m_antiPeekAction->setText(L(QStringLiteral("Chống nhìn lén"), QStringLiteral("Anti-peeking")));
    if (m_quitAction) m_quitAction->setText(L(QStringLiteral("Thoát hoàn toàn"), QStringLiteral("Quit")));
    notifyMenuChanged();
    refreshIconAndTooltip();
}

void SystemTrayController::setWindowVisible(bool visible) {
    if (m_windowVisible == visible) return;
    m_windowVisible = visible;
    if (m_openAction) {
        m_openAction->setText(visible ? L(QStringLiteral("Ẩn Zalos"), QStringLiteral("Hide Zalos"))
                                      : L(QStringLiteral("Mở Zalos"), QStringLiteral("Open Zalos")));
    }
    notifyMenuChanged();
}

void SystemTrayController::setUnreadCount(int count, bool hasMentions) {
    if (m_unreadCount == count && m_hasMentions == hasMentions) return;
    m_unreadCount = count;
    m_hasMentions = hasMentions;
    refreshIconAndTooltip();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        QDBusMessage signal = QDBusMessage::createSignal(
            QStringLiteral("/com/canonical/unity/launcherentry/zalos"),
            QStringLiteral("com.canonical.Unity.LauncherEntry"),
            QStringLiteral("Update"));
        QVariantMap properties;
        properties.insert(QStringLiteral("count"), qint64(m_unreadCount));
        properties.insert(QStringLiteral("count-visible"), m_unreadCount > 0);
        properties.insert(QStringLiteral("urgent"), m_hasMentions);
        signal << QStringLiteral("application://zalos.desktop") << properties;
        bus.send(signal);
    }
}

void SystemTrayController::setHasMentions(bool hasMentions) {
    if (m_hasMentions == hasMentions) return;
    m_hasMentions = hasMentions;
    refreshIconAndTooltip();
}

void SystemTrayController::setAntiPeeking(bool on) {
    if (m_antiPeekAction && m_antiPeekAction->isChecked() != on) {
        m_antiPeekAction->setChecked(on);
        notifyMenuChanged();
    }
    if (m_antiPeeking == on) return;
    m_antiPeeking = on;
    refreshIconAndTooltip();
}

void SystemTrayController::setNotificationsEnabled(bool on) {
    if (m_notificationsAction && m_notificationsAction->isChecked() != on) {
        m_notificationsAction->setChecked(on);
        notifyMenuChanged();
    }
    m_notificationsEnabled = on;
}

void SystemTrayController::refreshIconAndTooltip() {
    const QString callKey = (!m_callPhase.isEmpty() && m_callPhase != QLatin1String("ended"))
        ? (m_callPhase == QLatin1String("connected") ? QStringLiteral("connected") : QStringLiteral("active"))
        : QString();
    if (m_unreadCount != m_lastBadgeCount || m_hasMentions != m_lastBadgeMentions
        || m_antiPeeking != m_lastBadgeAntiPeeking || callKey != m_lastBadgeCallPhase) {
        m_lastBadgeCount = m_unreadCount;
        m_lastBadgeMentions = m_hasMentions;
        m_lastBadgeAntiPeeking = m_antiPeeking;
        m_lastBadgeCallPhase = callKey;
        m_badgeImage = createBadgeImage(m_unreadCount, m_hasMentions);
        if (m_sniAdaptor && m_busObjectsRegistered) emit m_sniAdaptor->NewIcon();
    }
    QString tip;
    if (!m_callPhase.isEmpty() && m_callPhase != QLatin1String("ended")) {
        const QString peer = m_callPeerName.isEmpty() ? QStringLiteral("Zalo") : m_callPeerName;
        if (m_callPhase == QLatin1String("incoming")) {
            tip += L(QStringLiteral("Cuộc gọi đến: %1\n"), QStringLiteral("Incoming call: %1\n")).arg(peer);
        } else if (m_callPhase == QLatin1String("connected")) {
            tip += L(QStringLiteral("Đang trong cuộc gọi: %1%2\n"), QStringLiteral("In call: %1%2\n"))
                       .arg(peer, m_callMuted ? L(QStringLiteral(" (Đã tắt micro)"), QStringLiteral(" (Muted)")) : QString());
        } else {
            tip += L(QStringLiteral("Đang gọi: %1\n"), QStringLiteral("Calling: %1\n")).arg(peer);
        }
    }
    tip += m_unreadCount > 0
        ? L(QStringLiteral("Zalos - Có %1 tin nhắn chưa đọc"), QStringLiteral("Zalos - %1 unread messages")).arg(m_unreadCount)
        : L(QStringLiteral("Zalos - Ứng dụng chat bảo mật cho Linux"), QStringLiteral("Zalos - private chat for Linux"));
    if (m_hasMentions) tip += L(QStringLiteral("\nCó tin nhắn nhắc tên bạn (@)"), QStringLiteral("\nYou were mentioned (@)"));
    if (m_antiPeeking) tip += L(QStringLiteral("\nĐang bật chống nhìn lén"), QStringLiteral("\nAnti-peeking is on"));
    if (m_toolTip != tip) {
        m_toolTip = tip;
        if (m_sniAdaptor && m_busObjectsRegistered) emit m_sniAdaptor->NewToolTip();
    }
}

void SystemTrayController::showMessage(const QString& title, const QString& message) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    call << QStringLiteral("Zalos") << quint32(0) << QStringLiteral("zalos") << title << message
         << QStringList{} << QVariantMap{} << qint32(3000);
    bus.asyncCall(call);
}

QImage SystemTrayController::createBadgeImage(int count, bool hasMentions) {
    if (m_baseImage.isNull()) {
        QIcon baseIcon(QStringLiteral(":/Zalos/assets/icons/zalos.svg"));
        QImage base = baseIcon.pixmap(64, 64).toImage().convertToFormat(QImage::Format_ARGB32);
        if (base.isNull() || base.size().isEmpty()) {
            base = QImage(64, 64, QImage::Format_ARGB32);
            base.fill(Qt::transparent);
            QPainter p(&base);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor("#0068FF"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(4, 4, 56, 56);
            p.setPen(Qt::white);
            QFont f = p.font();
            f.setPixelSize(34);
            f.setBold(true);
            p.setFont(f);
            p.drawText(QRect(4, 4, 56, 56), Qt::AlignCenter, QStringLiteral("Z"));
        }
        m_baseImage = base;
    }
    QImage image = m_baseImage.copy();

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    if (count > 0) {
        const QString badgeText = (count > 99) ? QStringLiteral("99+") : QString::number(count);

        const int fontSize = (badgeText.length() >= 3) ? 22 : (badgeText.length() == 2 ? 26 : 28);
        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(fontSize);
        badgeFont.setBold(true);
        badgeFont.setStyleStrategy(QFont::PreferAntialias);
        painter.setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        const int textWidth = fm.horizontalAdvance(badgeText);
        const int badgeHeight = 40;
        const int badgeWidth = qMin(62, qMax(badgeHeight, textWidth + 16));

        const int badgeRight = 64;
        const int badgeBottom = 64;
        const int badgeX = qMax(2, badgeRight - badgeWidth);
        const int badgeY = badgeBottom - badgeHeight;
        const QRectF badgeRect(badgeX, badgeY, badgeWidth, badgeHeight);

        const QColor badgeColor = hasMentions ? QColor("#DC2626") : QColor("#EF4444");

        painter.setBrush(badgeColor);
        painter.setPen(QPen(QColor(15, 23, 42, 230), 2.0));
        painter.drawRoundedRect(badgeRect, badgeHeight / 2.0, badgeHeight / 2.0);

        painter.setPen(Qt::white);
        painter.drawText(badgeRect, Qt::AlignCenter, badgeText);
    }

    if (m_antiPeeking) {
        painter.setBrush(QColor("#081B3A"));
        painter.setPen(QPen(Qt::white, 2));
        painter.drawEllipse(2, 36, 26, 26);
        painter.setPen(QPen(Qt::white, 2.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(8, 44.5, 14, 9));
        painter.drawEllipse(QRectF(13, 47, 4, 4));
        painter.drawLine(QPointF(7, 56), QPointF(23, 42));
    }

    if (!m_callPhase.isEmpty() && m_callPhase != QLatin1String("ended")) {
        const QColor callColor = m_callPhase == QLatin1String("connected") ? QColor("#22C55E") : QColor("#F59E0B");
        painter.setBrush(callColor);
        painter.setPen(QPen(QColor(15, 23, 42, 230), 2.0));
        painter.drawEllipse(QRectF(36, 2, 26, 26));
        QPainterPath handset;
        handset.moveTo(43.5, 11.5);
        handset.quadTo(44.5, 17.5, 51.5, 19.5);
        handset.lineTo(54.0, 17.2);
        handset.lineTo(51.8, 15.6);
        handset.lineTo(50.2, 16.8);
        handset.quadTo(47.2, 15.5, 45.8, 12.6);
        handset.lineTo(47.0, 11.0);
        handset.lineTo(45.4, 9.0);
        handset.closeSubpath();
        painter.setBrush(Qt::white);
        painter.setPen(QPen(Qt::white, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(handset);
    }

    return image;
}

}
