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
    m_trayIcon->setIcon(createBadgeIcon(0, false));
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

    m_antiPeekAction = m_trayMenu->addAction("Chống nhìn lén");
    m_antiPeekAction->setCheckable(true);
    connect(m_antiPeekAction, &QAction::triggered, this, &SystemTrayController::antiPeekingToggled);

    m_trayMenu->addSeparator();

    QAction* quitAction = m_trayMenu->addAction("Thoát hoàn toàn");
    connect(quitAction, &QAction::triggered, this, &SystemTrayController::quitRequested);

    m_trayIcon->setContextMenu(m_trayMenu);
}

void SystemTrayController::setUnreadCount(int count, bool hasMentions) {
    if (m_unreadCount == count && m_hasMentions == hasMentions) return;
    m_unreadCount = count;
    m_hasMentions = hasMentions;
    refreshIconAndTooltip();
}

void SystemTrayController::setHasMentions(bool hasMentions) {
    if (m_hasMentions == hasMentions) return;
    m_hasMentions = hasMentions;
    refreshIconAndTooltip();
}

void SystemTrayController::setAntiPeeking(bool on) {
    if (m_antiPeekAction) m_antiPeekAction->setChecked(on);
    if (m_antiPeeking == on) return;
    m_antiPeeking = on;
    refreshIconAndTooltip();
}

void SystemTrayController::refreshIconAndTooltip() {
    m_trayIcon->setIcon(createBadgeIcon(m_unreadCount, m_hasMentions));
    QString tip = m_unreadCount > 0 ? QString("Zalos - Có %1 tin nhắn chưa đọc").arg(m_unreadCount)
                                    : QStringLiteral("Zalos - Ứng dụng chat bảo mật cho Linux");
    if (m_hasMentions) tip += QStringLiteral("\nCó tin nhắn nhắc tên bạn (@)");
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

QIcon SystemTrayController::createBadgeIcon(int count, bool hasMentions) {
    QIcon baseIcon(":/Zalos/assets/icons/zalos.svg");
    QPixmap pixmap = baseIcon.pixmap(64, 64);
    if (pixmap.isNull()) {
        pixmap = QPixmap(64, 64);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#0068FF"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(4, 4, 56, 56);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(34);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(4, 4, 56, 56), Qt::AlignCenter, "Z");
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    if (count > 0) {
        QString badgeText = count > 99 ? QStringLiteral("99+") : QString::number(count);

        QFont badgeFont = painter.font();
        badgeFont.setPixelSize(18);
        badgeFont.setBold(true);
        painter.setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        int textWidth = fm.horizontalAdvance(badgeText);
        int badgeHeight = 28;
        int badgeWidth = qMax(badgeHeight, textWidth + 14);

        int badgeRight = 64;
        int badgeBottom = 64;
        int badgeX = badgeRight - badgeWidth;
        int badgeY = badgeBottom - badgeHeight;
        QRect badgeRect(badgeX, badgeY, badgeWidth, badgeHeight);

        QColor badgeColor = hasMentions ? QColor("#EF4444") : QColor("#707984");

        painter.setBrush(badgeColor);
        painter.setPen(QPen(QColor(10, 25, 47, 180), 1.5));
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

    return QIcon(pixmap);
}

}
