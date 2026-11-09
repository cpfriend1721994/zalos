#include "system_tray.h"

#include <QJSEngine>
#include <QApplication>
#include <QPainter>
#include <QFont>
#include <QStyle>
#include <QDBusConnection>
#include <QDBusMessage>

namespace Zalos::Core::Platform {

SystemTrayController::SystemTrayController(QObject* parent)
    : QObject(parent) {
    s_instance = this;
    QObject::connect(this, &QObject::destroyed, [](QObject* gone) {
        if (static_cast<QObject*>(s_instance) == gone) s_instance = nullptr;
    });
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(createBadgeIcon(0, false));
    setupMenu();
    retranslate();

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

    m_openAction = m_trayMenu->addAction(QString());
    connect(m_openAction, &QAction::triggered, this, &SystemTrayController::toggleWindowRequested);

    m_themeAction = m_trayMenu->addAction(QString());
    connect(m_themeAction, &QAction::triggered, this, &SystemTrayController::toggleDarkModeRequested);

    m_antiPeekAction = m_trayMenu->addAction(QString());
    m_antiPeekAction->setCheckable(true);
    connect(m_antiPeekAction, &QAction::triggered, this, &SystemTrayController::antiPeekingToggled);

    m_trayMenu->addSeparator();

    m_quitAction = m_trayMenu->addAction(QString());
    connect(m_quitAction, &QAction::triggered, this, &SystemTrayController::quitRequested);

    m_trayIcon->setContextMenu(m_trayMenu);
}

void SystemTrayController::setLanguage(const QString& lang) {
    const QString next = lang == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("vi");
    if (m_language == next) return;
    m_language = next;
    retranslate();
}

void SystemTrayController::retranslate() {
    if (m_openAction) m_openAction->setText(L(QStringLiteral("Mở Zalos"), QStringLiteral("Open Zalos")));
    if (m_themeAction) m_themeAction->setText(L(QStringLiteral("Chuyển chế độ Sáng / Tối"), QStringLiteral("Toggle light / dark mode")));
    if (m_antiPeekAction) m_antiPeekAction->setText(L(QStringLiteral("Chống nhìn lén"), QStringLiteral("Anti-peeking")));
    if (m_quitAction) m_quitAction->setText(L(QStringLiteral("Thoát hoàn toàn"), QStringLiteral("Quit")));
    refreshIconAndTooltip();
}

void SystemTrayController::setWindowVisible(bool visible) {
    if (m_openAction) {
        m_openAction->setText(visible ? L(QStringLiteral("Ẩn Zalos"), QStringLiteral("Hide Zalos"))
                                      : L(QStringLiteral("Mở Zalos"), QStringLiteral("Open Zalos")));
    }
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
    if (m_antiPeekAction) m_antiPeekAction->setChecked(on);
    if (m_antiPeeking == on) return;
    m_antiPeeking = on;
    refreshIconAndTooltip();
}

void SystemTrayController::refreshIconAndTooltip() {
    m_trayIcon->setIcon(createBadgeIcon(m_unreadCount, m_hasMentions));
    QString tip = m_unreadCount > 0
        ? L(QStringLiteral("Zalos - Có %1 tin nhắn chưa đọc"), QStringLiteral("Zalos - %1 unread messages")).arg(m_unreadCount)
        : L(QStringLiteral("Zalos - Ứng dụng chat bảo mật cho Linux"), QStringLiteral("Zalos - private chat for Linux"));
    if (m_hasMentions) tip += L(QStringLiteral("\nCó tin nhắn nhắc tên bạn (@)"), QStringLiteral("\nYou were mentioned (@)"));
    if (m_antiPeeking) tip += L(QStringLiteral("\nĐang bật chống nhìn lén"), QStringLiteral("\nAnti-peeking is on"));
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

    return QIcon(pixmap);
}

}
