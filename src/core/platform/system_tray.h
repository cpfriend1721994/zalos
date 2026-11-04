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
    static inline SystemTrayController* s_instance = nullptr;
    explicit SystemTrayController(QObject* parent = nullptr);
    ~SystemTrayController() override;

    void setUnreadCount(int count, bool hasMentions = false);
    void setHasMentions(bool hasMentions);
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
    QIcon createBadgeIcon(int count, bool hasMentions);
    void refreshIconAndTooltip();

    QSystemTrayIcon* m_trayIcon{nullptr};
    QMenu* m_trayMenu{nullptr};
    int m_unreadCount{0};
    bool m_hasMentions{false};
    bool m_antiPeeking{false};
    QAction* m_antiPeekAction{nullptr};
};

}
