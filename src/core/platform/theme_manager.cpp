#include "theme_manager.h"

#include <QJSEngine>
#include <QGuiApplication>
#include <QFont>
#include <QFontDatabase>
#include <QProcessEnvironment>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusArgument>
#include <QPalette>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QFileSystemWatcher>
#include <QDebug>

namespace Zalos::Core::Platform {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent) {
    s_instance = this;
    QObject::connect(this, &QObject::destroyed, [](QObject* gone) {
        if (static_cast<QObject*>(s_instance) == gone) s_instance = nullptr;
    });
    QSettings settings("Zalos", "ZalosApp");
    m_themeMode = settings.value("appearance/themeMode", 2).toInt();

    initSystemThemeDetection();
    detectSystemFont();

    if (m_themeMode == 0) {
        setDarkMode(false);
    } else if (m_themeMode == 1) {
        setDarkMode(true);
    } else {
        setDarkMode(m_systemIsDark);
    }
    syncApplicationPalette();
}

void ThemeManager::setThemeMode(int mode) {
    m_themeMode = mode;
    QSettings settings("Zalos", "ZalosApp");
    settings.setValue("appearance/themeMode", m_themeMode);
    emit themeModeChanged(m_themeMode);

    if (m_themeMode == 0) {
        setDarkMode(false);
    } else if (m_themeMode == 1) {
        setDarkMode(true);
    } else {
        detectGnomeOrPortalTheme();
        detectKdeTheme();
        detectSystemAccentColor();
        setDarkMode(m_systemIsDark);
    }
    syncApplicationPalette();
    emit themeColorsChanged();
}

void ThemeManager::setDarkMode(bool dark) {
    if (m_isDarkMode != dark) {
        m_isDarkMode = dark;
        syncApplicationPalette();
        emit darkModeChanged(m_isDarkMode);
        emit themeColorsChanged();
    }
}

void ThemeManager::setSystemFontFamily(const QString& family) {
    if (m_systemFontFamily != family && !family.isEmpty()) {
        m_systemFontFamily = family;
        QFont font = QGuiApplication::font();
        font.setFamily(m_systemFontFamily);
        QGuiApplication::setFont(font);
        emit systemFontFamilyChanged(m_systemFontFamily);
    }
}

void ThemeManager::syncApplicationPalette() {
    QPalette pal;
    const QColor bg = backgroundColor();
    const QColor txt = textColor();
    const QColor base = inputFillColor();
    const QColor card = cardBackgroundColor();
    const QColor accent = accentColor();
    const QColor placeholder = placeholderColor();

    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::WindowText, txt);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, card);
    pal.setColor(QPalette::Text, txt);
    pal.setColor(QPalette::Button, card);
    pal.setColor(QPalette::ButtonText, txt);
    pal.setColor(QPalette::PlaceholderText, placeholder);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, m_isDarkMode ? QColor("#2B2E33") : QColor("#FFFFFF"));
    pal.setColor(QPalette::ToolTipText, txt);
    pal.setColor(QPalette::Mid, borderColor());

    const QColor disabledTxt = m_isDarkMode ? QColor("#666D77") : QColor("#9AA4B2");
    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledTxt);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledTxt);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledTxt);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, m_isDarkMode ? QColor("#2A3E59") : QColor("#BDD7FF"));

    QGuiApplication::setPalette(pal);
}

void ThemeManager::toggleTheme() {
    setThemeMode(m_isDarkMode ? 0 : 1);
}

bool ThemeManager::isWayland() const {
    const QString platformName = QGuiApplication::platformName();
    if (platformName.contains("wayland", Qt::CaseInsensitive)) {
        return true;
    }
    const auto env = QProcessEnvironment::systemEnvironment();
    return env.value("XDG_SESSION_TYPE").compare("wayland", Qt::CaseInsensitive) == 0;
}

bool ThemeManager::isGtkEnvironment() const {
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value("XDG_CURRENT_DESKTOP").toLower();
    return desktop.contains("gnome") || desktop.contains("ubuntu") || desktop.contains("unity") || desktop.contains("cinnamon") || desktop.contains("xfce");
}

QColor ThemeManager::resolveColor(const QString& lightZalo, const QString& darkZalo, const QColor& systemColor) const {
    if (m_themeMode == 0) {
        return QColor(lightZalo);
    }
    if (m_themeMode == 1) {
        return QColor(darkZalo);
    }
    if (systemColor.isValid()) {
        return systemColor;
    }
    return m_isDarkMode ? QColor(darkZalo) : QColor(lightZalo);
}

QColor ThemeManager::primaryColor() const {
    if (m_themeMode == 2 && m_systemAccentColor.isValid()) {
        return m_systemAccentColor;
    }
    return QColor("#0068FF");
}

QColor ThemeManager::accentColor() const {
    if (m_themeMode == 0) return QColor("#0068FF");
    if (m_themeMode == 1) return QColor("#005AE0");
    if (m_systemAccentColor.isValid()) return m_systemAccentColor;
    return m_isDarkMode ? QColor("#3384FF") : QColor("#0068FF");
}

QColor ThemeManager::accentHoverColor() const {
    if (m_themeMode == 0) return QColor("#0045AD");
    if (m_themeMode == 1) return QColor("#3384FF");
    return m_isDarkMode ? accentColor().lighter(115) : accentColor().darker(115);
}

QColor ThemeManager::accentTonalColor() const {
    if (m_themeMode == 0) return QColor("#E5F1FF");
    if (m_themeMode == 1) return QColor("#00378A");
    QColor acc = accentColor();
    return m_isDarkMode ? QColor(acc.red(), acc.green(), acc.blue(), 60)
                        : QColor(acc.red(), acc.green(), acc.blue(), 35);
}

QColor ThemeManager::accentTonalTextColor() const {
    if (m_themeMode == 0) return QColor("#0045AD");
    if (m_themeMode == 1) return QColor("#8FBFFF");
    return accentColor();
}

QColor ThemeManager::railBackgroundColor() const {
    if (m_themeMode == 0) return QColor("#005AE0");
    if (m_themeMode == 1) return QColor("#121416");
    if (m_isDarkMode) {
        QColor win = m_systemPalette.color(QPalette::Window);
        return win.isValid() ? win.darker(120) : QColor("#121416");
    }
    return accentColor().darker(115);
}

QColor ThemeManager::chatBackgroundColor() const {
    return resolveColor("#EBECF0", "#16191D", m_systemPalette.color(QPalette::Base));
}

QColor ThemeManager::backgroundColor() const {
    return resolveColor("#FFFFFF", "#22262B", m_systemPalette.color(QPalette::Window));
}

QColor ThemeManager::surfaceColor() const {
    return resolveColor("#FFFFFF", "#22262B", m_systemPalette.color(QPalette::Window));
}

QColor ThemeManager::cardBackgroundColor() const {
    return resolveColor("#EBECF0", "#1D2025", m_systemPalette.color(QPalette::AlternateBase));
}

QColor ThemeManager::textColor() const {
    return resolveColor("#081B3A", "#DFE2E7", m_systemPalette.color(QPalette::WindowText));
}

QColor ThemeManager::secondaryTextColor() const {
    if (m_themeMode == 0) return QColor("#5A6981");
    if (m_themeMode == 1) return QColor("#9FACBC");
    QColor t = m_systemPalette.color(QPalette::WindowText);
    return t.isValid() ? QColor(t.red(), t.green(), t.blue(), 175) : (m_isDarkMode ? QColor("#9FACBC") : QColor("#5A6981"));
}

QColor ThemeManager::borderColor() const {
    if (m_themeMode == 0) return QColor("#26000000");
    if (m_themeMode == 1) return QColor("#1AFFFFFF");
    return m_isDarkMode ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 36);
}

QColor ThemeManager::chatBubbleMine() const {
    if (m_themeMode == 0) return QColor("#E5F1FF");
    if (m_themeMode == 1) return QColor("#1F344D");
    QColor acc = accentColor();
    return m_isDarkMode ? QColor(acc.red(), acc.green(), acc.blue(), 75)
                        : QColor(acc.red(), acc.green(), acc.blue(), 40);
}

QColor ThemeManager::chatBubbleTheir() const {
    if (m_themeMode == 0) return QColor("#FFFFFF");
    if (m_themeMode == 1) return QColor("#282D33");
    if (m_isDarkMode) {
        QColor win = m_systemPalette.color(QPalette::Window);
        return win.isValid() ? win.lighter(115) : QColor("#282D33");
    }
    return m_systemPalette.color(QPalette::Base);
}

QColor ThemeManager::hoverColor() const {
    if (m_themeMode == 0) return QColor("#F1F2F4");
    if (m_themeMode == 1) return QColor("#0DFFFFFF");
    return m_isDarkMode ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 15);
}

QColor ThemeManager::selectedColor() const {
    if (m_themeMode == 0) return QColor("#E5F1FF");
    if (m_themeMode == 1) return QColor("#1F344D");
    QColor acc = accentColor();
    return QColor(acc.red(), acc.green(), acc.blue(), m_isDarkMode ? 50 : 35);
}

QColor ThemeManager::pinnedColor() const {
    if (m_themeMode == 0) return QColor("#08081B3A");
    if (m_themeMode == 1) return QColor("#26000000");
    return m_isDarkMode ? QColor(0, 0, 0, 45) : QColor(0, 0, 0, 15);
}

QColor ThemeManager::inputFillColor() const {
    return resolveColor("#EBECF0", "#16191D", m_systemPalette.color(QPalette::Base));
}

QColor ThemeManager::inputFillHoverColor() const {
    return resolveColor("#E5E7EB", "#1D2025", m_systemPalette.color(QPalette::AlternateBase));
}

QColor ThemeManager::dividerColor() const {
    if (m_themeMode == 0) return QColor("#26000000");
    if (m_themeMode == 1) return QColor("#1AFFFFFF");
    return m_isDarkMode ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 26);
}

QColor ThemeManager::placeholderColor() const {
    return resolveColor("#77859C", "#738296", m_systemPalette.color(QPalette::PlaceholderText));
}

QColor ThemeManager::iconColor() const {
    if (m_themeMode == 0) return QColor("#5A6981");
    if (m_themeMode == 1) return QColor("#9FACBC");
    return secondaryTextColor();
}

QColor ThemeManager::linkColor() const {
    if (m_themeMode == 0) return QColor("#005AE0");
    if (m_themeMode == 1) return QColor("#66A6FF");
    QColor l = m_systemPalette.color(QPalette::Link);
    return l.isValid() ? l : accentColor();
}

QColor ThemeManager::badgeColor() const {
    return QColor(m_isDarkMode ? "#E94949" : "#C31818");
}

QColor ThemeManager::successColor() const {
    return QColor(m_isDarkMode ? "#6ACD8B" : "#37B361");
}

QColor ThemeManager::errorBackgroundColor() const {
    return QColor(m_isDarkMode ? "#3D1515" : "#FBE4E4");
}

QColor ThemeManager::warningBackgroundColor() const {
    return QColor(m_isDarkMode ? "#3A2F0A" : "#FFF7E0");
}

QColor ThemeManager::infoBackgroundColor() const {
    return selectedColor();
}

QColor ThemeManager::railHoverColor() const {
    if (m_themeMode == 0) return QColor("#26000000");
    if (m_themeMode == 1) return QColor("#1AFFFFFF");
    return m_isDarkMode ? QColor(255, 255, 255, 25) : QColor(0, 0, 0, 35);
}

QColor ThemeManager::railSelectedColor() const {
    if (m_themeMode == 0) return QColor("#40000000");
    if (m_themeMode == 1) return QColor("#0DFFFFFF");
    return m_isDarkMode ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 60);
}

QColor ThemeManager::timestampColor() const {
    if (m_themeMode == 0) return QColor("#33000000");
    if (m_themeMode == 1) return QColor("#0DFFFFFF");
    return m_isDarkMode ? QColor(255, 255, 255, 25) : QColor(0, 0, 0, 50);
}

QColor ThemeManager::reactionBackgroundColor() const {
    return cardBackgroundColor();
}

QColor ThemeManager::uiAccentColor() const {
    if (m_themeMode == 0) return QColor("#0068FF");
    if (m_themeMode == 1) return QColor("#0068FF");
    return accentColor();
}

QColor ThemeManager::activenessColor() const {
    if (m_themeMode == 0) return QColor("#0068FF");
    if (m_themeMode == 1) return QColor("#66A6FF");
    return accentColor();
}

QColor ThemeManager::uiLinkColor() const {
    if (m_themeMode == 0) return QColor("#0068FF");
    if (m_themeMode == 1) return QColor("#66A6FF");
    QColor l = m_systemPalette.color(QPalette::Link);
    return l.isValid() ? l : accentColor();
}

QColor ThemeManager::reactionSurfaceColor() const {
    if (m_themeMode == 0) return QColor("#FFFFFF");
    if (m_themeMode == 1) return QColor("#1D2025");
    QColor b = m_systemPalette.color(QPalette::Base);
    return b.isValid() ? b : (m_isDarkMode ? QColor("#1D2025") : QColor("#FFFFFF"));
}

QColor ThemeManager::inactiveSubtleColor() const {
    if (m_themeMode == 0) return QColor("#B5B5B5");
    if (m_themeMode == 1) return QColor("#595959");
    QColor p = m_systemPalette.color(QPalette::PlaceholderText);
    return p.isValid() ? p : (m_isDarkMode ? QColor("#595959") : QColor("#B5B5B5"));
}

QColor ThemeManager::inactiveDisabledColor() const {
    if (m_themeMode == 0) return QColor("#DBDBDB");
    if (m_themeMode == 1) return QColor("#333333");
    QColor t = m_systemPalette.color(QPalette::WindowText);
    return t.isValid() ? QColor(t.red(), t.green(), t.blue(), 90)
                       : (m_isDarkMode ? QColor("#333333") : QColor("#DBDBDB"));
}

QColor ThemeManager::activeDisabledColor() const {
    if (m_themeMode == 0) return QColor("#ADD1FF");
    if (m_themeMode == 1) return QColor("#00378A");
    QColor acc = accentColor();
    return QColor(acc.red(), acc.green(), acc.blue(), m_isDarkMode ? 130 : 150);
}

QColor ThemeManager::dropOverlayColor() const {
    if (m_themeMode == 0) return QColor("#F0F7FF");
    if (m_themeMode == 1) return QColor("#001029");
    QColor acc = accentColor();
    return QColor(acc.red(), acc.green(), acc.blue(), m_isDarkMode ? 45 : 28);
}

void ThemeManager::initSystemThemeDetection() {
    // Snapshot the platform palette (populated by the Qt/GTK platform theme plugin) before
    // syncApplicationPalette() overwrites QGuiApplication::setPalette(); mode 2 reads from here so
    // roles we never re-set (e.g. Link) survive and stay stable across theme toggles.
    m_systemPalette = QGuiApplication::palette();
    detectGnomeOrPortalTheme();
    detectKdeTheme();
    detectSystemAccentColor();

    QDBusConnection::sessionBus().connect(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "SettingChanged",
        this,
        SLOT(detectGnomeOrPortalTheme())
    );

    const QString kdeGlobalsPath = QDir::homePath() + "/.config/kdeglobals";
    if (QFile::exists(kdeGlobalsPath)) {
        m_kdeWatcher = new QFileSystemWatcher(this);
        m_kdeWatcher->addPath(kdeGlobalsPath);
        connect(m_kdeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
            detectKdeTheme();
            detectSystemAccentColor();
        });
    }
}

void ThemeManager::detectSystemAccentColor() {
    QColor detectedAccent;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "Read"
    );
    msg << "org.freedesktop.appearance" << "accent-color";

    QDBusReply<QDBusVariant> reply = QDBusConnection::sessionBus().call(msg);
    if (reply.isValid()) {
        QVariant v = reply.value().variant();
        if (v.canConvert<QDBusVariant>()) {
            v = v.value<QDBusVariant>().variant();
        }
        if (v.canConvert<QDBusArgument>()) {
            const QDBusArgument arg = v.value<QDBusArgument>();
            if (arg.currentType() == QDBusArgument::StructureType) {
                double r = 0, g = 0, b = 0;
                arg.beginStructure();
                arg >> r >> g >> b;
                arg.endStructure();
                if (r >= 0.0 && r <= 1.0 && g >= 0.0 && g <= 1.0 && b >= 0.0 && b <= 1.0) {
                    detectedAccent = QColor::fromRgbF(r, g, b);
                }
            }
        }
    }

    if (!detectedAccent.isValid()) {
        const QString kdeGlobalsPath = QDir::homePath() + "/.config/kdeglobals";
        if (QFile::exists(kdeGlobalsPath)) {
            QSettings kdeSettings(kdeGlobalsPath, QSettings::IniFormat);
            kdeSettings.beginGroup("General");
            QString accentStr = kdeSettings.value("AccentColor").toString();
            kdeSettings.endGroup();
            if (!accentStr.isEmpty()) {
                const auto parts = accentStr.split(',');
                if (parts.size() >= 3) {
                    detectedAccent = QColor(parts[0].trimmed().toInt(),
                                            parts[1].trimmed().toInt(),
                                            parts[2].trimmed().toInt());
                }
            }
        }
    }

    if (!detectedAccent.isValid()) {
        QColor highlight = m_systemPalette.color(QPalette::Highlight);
        if (highlight.isValid() && highlight != Qt::black && highlight != Qt::white) {
            detectedAccent = highlight;
        }
    }

    if (detectedAccent.isValid() && detectedAccent != m_systemAccentColor) {
        m_systemAccentColor = detectedAccent;
        emit systemAccentColorChanged(m_systemAccentColor);
        if (m_themeMode == 2) {
            emit themeColorsChanged();
        }
    }
}

void ThemeManager::detectKdeTheme() {
    const QString kdeGlobalsPath = QDir::homePath() + "/.config/kdeglobals";
    if (!QFile::exists(kdeGlobalsPath)) {
        return;
    }

    QSettings kdeSettings(kdeGlobalsPath, QSettings::IniFormat);
    kdeSettings.beginGroup("Colors:Window");
    QString bgNormal = kdeSettings.value("BackgroundNormal").toString();
    kdeSettings.endGroup();

    if (!bgNormal.isEmpty()) {
        const auto parts = bgNormal.split(',');
        if (parts.size() >= 3) {
            int r = parts[0].trimmed().toInt();
            int g = parts[1].trimmed().toInt();
            int b = parts[2].trimmed().toInt();
            double luminance = 0.299 * r + 0.587 * g + 0.114 * b;
            m_systemIsDark = (luminance < 128.0);
            if (m_themeMode == 2) {
                setDarkMode(m_systemIsDark);
            }
        }
    }
}

void ThemeManager::detectGnomeOrPortalTheme() {
    detectSystemAccentColor();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "Read"
    );
    msg << "org.freedesktop.appearance" << "color-scheme";

    QDBusReply<QDBusVariant> reply = QDBusConnection::sessionBus().call(msg);
    if (reply.isValid()) {
        QVariant v = reply.value().variant();
        if (v.canConvert<QDBusVariant>()) {
            v = v.value<QDBusVariant>().variant();
        }
        bool ok = false;
        uint value = v.toUInt(&ok);
        if (ok) {
            if (value == 1) {
                m_systemIsDark = true;
                if (m_themeMode == 2) setDarkMode(true);
                return;
            } else if (value == 2) {
                m_systemIsDark = false;
                if (m_themeMode == 2) setDarkMode(false);
                return;
            }
        }
    }

    QDBusMessage gnomePortalMsg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "Read"
    );
    gnomePortalMsg << "org.gnome.desktop.interface" << "color-scheme";

    QDBusReply<QDBusVariant> gnomePortalReply = QDBusConnection::sessionBus().call(gnomePortalMsg);
    if (gnomePortalReply.isValid()) {
        QVariant v = gnomePortalReply.value().variant();
        if (v.canConvert<QDBusVariant>()) {
            v = v.value<QDBusVariant>().variant();
        }
        QString scheme = v.toString();
        if (!scheme.isEmpty()) {
            m_systemIsDark = scheme.contains("dark", Qt::CaseInsensitive);
            if (m_themeMode == 2) {
                setDarkMode(m_systemIsDark);
            }
            return;
        }
    }

    const QString gtkTheme = qEnvironmentVariable("GTK_THEME");
    if (!gtkTheme.isEmpty()) {
        m_systemIsDark = gtkTheme.contains("dark", Qt::CaseInsensitive);
        if (m_themeMode == 2) {
            setDarkMode(m_systemIsDark);
        }
    }
}

QString ThemeManager::familyFromFontSpec(const QString& spec) {
    QString s = spec.trimmed();
    if (s.contains(',')) s = s.section(',', 0, 0).trimmed();
    QStringList words = s.split(' ', Qt::SkipEmptyParts);
    bool sizeOk = false;
    if (!words.isEmpty()) words.last().toDouble(&sizeOk);
    if (sizeOk) words.removeLast();
    static const QStringList kStyleWords{
        "thin", "hairline", "extralight", "ultralight", "light", "semilight", "book", "regular", "normal", "medium",
        "semibold", "demibold", "bold", "extrabold", "ultrabold", "heavy", "black", "italic", "oblique",
        "condensed", "semicondensed", "extracondensed", "expanded"};
    while (words.size() > 1 && kStyleWords.contains(words.last().toLower())) words.removeLast();
    return words.join(' ');
}

void ThemeManager::detectSystemFont() {
    QString detectedFont;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "Read"
    );
    msg << "org.gnome.desktop.interface" << "font-name";

    QDBusReply<QDBusVariant> reply = QDBusConnection::sessionBus().call(msg);
    if (reply.isValid()) detectedFont = familyFromFontSpec(reply.value().variant().toString());

    if (detectedFont.isEmpty()) {
        const QString kdeGlobalsPath = QDir::homePath() + "/.config/kdeglobals";
        if (QFile::exists(kdeGlobalsPath)) {
            QSettings kdeSettings(kdeGlobalsPath, QSettings::IniFormat);
            kdeSettings.beginGroup("General");
            QString kdeFont = kdeSettings.value("font").toString();
            kdeSettings.endGroup();
            if (!kdeFont.isEmpty()) {
                detectedFont = familyFromFontSpec(kdeFont);
            }
        }
    }

    if (!detectedFont.isEmpty() && !QFontDatabase::hasFamily(detectedFont)) {
        qInfo() << "System font family" << detectedFont << "is not installed; using the default font";
        detectedFont.clear();
    }

    if (detectedFont.isEmpty()) {
        detectedFont = QGuiApplication::font().family();
    }
    if (detectedFont.isEmpty()) {
        detectedFont = "Noto Sans";
    }

    setSystemFontFamily(detectedFont);
}

}
