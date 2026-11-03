#pragma once

#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QPixmap>
#include <QPainter>

namespace Zalos::Core::Platform {

class SystemTrayController : public QObject {
    Q_OBJECT

public:
    // The instance main() (or a test) created; QML's "TrayController" singleton resolves to it.
    static inline SystemTrayController* s_instance = nullptr;
    explicit SystemTrayController(QObject* parent = nullptr);
    ~SystemTrayController() override;

    void setUnreadCount(int count);
    // Reflects the anti-peeking state: checked menu item, eye mark on the icon, tooltip line.
    void setAntiPeeking(bool on);
    bool isVisible() const;

signals:
    void toggleWindowRequested();
    void quitRequested();
    void toggleDarkModeRequested();
    void antiPeekingToggled(bool on);

public slots:
    void showMessage(const QString& title, const QString& message);

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void setupMenu();
    QIcon createBadgeIcon(int count);
    void refreshIconAndTooltip();

    QSystemTrayIcon* m_trayIcon{nullptr};
    QMenu* m_trayMenu{nullptr};
    int m_unreadCount{0};
    bool m_antiPeeking{false};
    QAction* m_antiPeekAction{nullptr};
};

} // namespace Zalos::Core::Platform

