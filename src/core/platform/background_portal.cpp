#include "background_portal.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QFile>
#include <QStringList>

namespace Zalos::Core::Platform {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kRequestInterface = QStringLiteral("org.freedesktop.portal.Request");

}

BackgroundPortal::BackgroundPortal(QObject* parent) : QObject(parent) {}

bool BackgroundPortal::inFlatpak() {
    return QFile::exists(QStringLiteral("/.flatpak-info"));
}

void BackgroundPortal::clearRequest() {
    if (m_requestPath.isEmpty()) return;
    QDBusConnection::sessionBus().disconnect(kService, m_requestPath, kRequestInterface, QStringLiteral("Response"),
                                             this, SLOT(onResponse(uint,QVariantMap)));
    m_requestPath.clear();
}

void BackgroundPortal::requestAutostart(bool enable, const QString& reason) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit finished(false, false);
        return;
    }
    clearRequest();
    const QString token = QStringLiteral("zalos_bg_%1_%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(++m_seq);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(kService, m_requestPath, kRequestInterface, QStringLiteral("Response"), this,
                SLOT(onResponse(uint,QVariantMap)));

    QDBusMessage msg = QDBusMessage::createMethodCall(kService, QStringLiteral("/org/freedesktop/portal/desktop"),
                                                      QStringLiteral("org.freedesktop.portal.Background"),
                                                      QStringLiteral("RequestBackground"));
    msg << QString() << QVariantMap{{QStringLiteral("handle_token"), token},
                                    {QStringLiteral("reason"), reason},
                                    {QStringLiteral("autostart"), enable},
                                    {QStringLiteral("commandline"), QStringList{QStringLiteral("zalos"), QStringLiteral("--autostart")}},
                                    {QStringLiteral("dbus-activatable"), false}};
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (QDBusPendingReply<QDBusObjectPath>(*w).isError()) {
            clearRequest();
            emit finished(false, false);
        }
    });
}

void BackgroundPortal::onResponse(uint response, const QVariantMap& results) {
    clearRequest();
    emit finished(response == 0, response == 0 && results.value(QStringLiteral("autostart")).toBool());
}

}
