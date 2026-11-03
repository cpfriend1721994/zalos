#include "system_tray.h"

#include <QJSEngine>
#include <QApplication>
#include <QPainter>
#include <QFont>
#include <QStyle>

namespace Zalos::Core::Platform {

SystemTrayController::SystemTrayController(QObject* parent)
    : QObject(parent) {
    s_instance = this;
    QObject::connect(this, &QObject::destroyed, [](QObject* gone) {
        if (static_cast<QObject*>(s_instance) == gone) s_instance = nullptr;
    });
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(createBadgeIcon(0));
    m_trayIcon->setToolTip("Zalos - Ứng dụng chat bảo mật cho Linux");

    setupMenu();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &SystemTrayController::onTrayActivated);
    m_trayIcon->show();
}

SystemTrayController::~SystemTrayController() {
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
}

void SystemTrayController::setupMenu() {
    m_trayMenu = new QMenu();

    QAction* openAction = m_trayMenu->addAction("Mở Zalos");
    connect(openAction, &QAction::triggered, this, &SystemTrayController::toggleWindowRequested);

    QAction* themeAction = m_trayMenu->addAction("Chuyển chế độ Sáng / Tối");
    connect(themeAction, &QAction::triggered, this, &SystemTrayController::toggleDarkModeRequested);

    // Quick switch for anti-peeking (also Ctrl+Shift+H in the window and Settings > Privacy).
    m_antiPeekAction = m_trayMenu->addAction("Chống nhìn lén");
    m_antiPeekAction->setCheckable(true);
    connect(m_antiPeekAction, &QAction::triggered, this, &SystemTrayController::antiPeekingToggled);

    m_trayMenu->addSeparator();

    QAction* quitAction = m_trayMenu->addAction("Thoát hoàn toàn");
    connect(quitAction, &QAction::triggered, this, &SystemTrayController::quitRequested);

    m_trayIcon->setContextMenu(m_trayMenu);
}

void SystemTrayController::setUnreadCount(int count) {
    if (m_unreadCount == count) return;
    m_unreadCount = count;
    refreshIconAndTooltip();
}

void SystemTrayController::setAntiPeeking(bool on) {
    if (m_antiPeekAction) m_antiPeekAction->setChecked(on);
    if (m_antiPeeking == on) return;
    m_antiPeeking = on;
    refreshIconAndTooltip();
}

void SystemTrayController::refreshIconAndTooltip() {
    m_trayIcon->setIcon(createBadgeIcon(m_unreadCount));
    QString tip = m_unreadCount > 0 ? QString("Zalos - Có %1 tin nhắn chưa đọc").arg(m_unreadCount)
                                    : QStringLiteral("Zalos - Ứng dụng chat bảo mật cho Linux");
    if (m_antiPeeking) tip += QStringLiteral("\nĐang bật chống nhìn lén");
    m_trayIcon->setToolTip(tip);
}

bool SystemTrayController::isVisible() const {
    return m_trayIcon && m_trayIcon->isVisible();
}

void SystemTrayController::showMessage(const QString& title, const QString& message) {
    if (m_trayIcon) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 3000);
    }
}

void SystemTrayController::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        emit toggleWindowRequested();
    }
}

QIcon SystemTrayController::createBadgeIcon(int count) {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw main circle / app logo
    painter.setBrush(QColor("#0068FF"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(4, 4, 56, 56, 16, 16);

    // Draw "Z" letter in center
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(34);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(4, 4, 56, 56), Qt::AlignCenter, "Z");

    // Draw red badge circle if count > 0
    if (count > 0) {
        painter.setBrush(QColor("#FF3B30"));
        painter.setPen(QPen(Qt::white, 2));
        painter.drawEllipse(36, 4, 24, 24);

        painter.setPen(Qt::white);
        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(12);
        badgeFont.setBold(true);
        painter.setFont(badgeFont);

        QString badgeText = count > 99 ? "99+" : QString::number(count);
        painter.drawText(QRect(36, 4, 24, 24), Qt::AlignCenter, badgeText);
    }

    // Anti-peeking on: dark disc with a crossed eye at the bottom-left corner.
    if (m_antiPeeking) {
        painter.setBrush(QColor("#081B3A"));
        painter.setPen(QPen(Qt::white, 2));
        painter.drawEllipse(2, 36, 26, 26);
        painter.setPen(QPen(Qt::white, 2.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(8, 44.5, 14, 9));    // eye outline
        painter.drawEllipse(QRectF(13, 47, 4, 4));      // pupil
        painter.drawLine(QPointF(7, 56), QPointF(23, 42)); // slash
    }

    return QIcon(pixmap);
}

} // namespace Zalos::Core::Platform

