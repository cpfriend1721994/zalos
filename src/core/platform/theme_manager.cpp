#include "theme_manager.h"

#include <QJSEngine>
#include <QGuiApplication>
#include <QFont>
#include <QFontDatabase>
#include <QRawFont>
#include <QProcessEnvironment>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDBusArgument>
#include <QPointer>
#include <QPalette>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDebug>
#include <cmath>

namespace Zalos::Core::Platform {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent) {
    s_instance = this;
    QObject::connect(this, &QObject::destroyed, [](QObject* gone) {
        if (static_cast<QObject*>(s_instance) == gone) s_instance = nullptr;
    });
    QSettings settings;
    m_themeMode = settings.value("appearance/themeMode", 2).toInt();
    m_systemIsDark = settings.value("appearance/cachedSystemDark", false).toBool();
    m_systemPalette = QGuiApplication::palette();
    if (paletteIsDark(m_systemPalette)) {
        m_systemIsDark = true;
    }
    const QString cachedAccent = settings.value("appearance/cachedAccentColor").toString();
    if (!cachedAccent.isEmpty()) {
        const QColor c(cachedAccent);
        if (c.isValid()) m_systemAccentColor = c;
    }
    settings.remove("appearance/cachedSystemFont");
    const QString cachedFont = installedFamilyName(settings.value("appearance/cachedDesktopFont").toString());
    if (isUsableUiFamily(cachedFont)) {
        setSystemFontFamily(cachedFont);
    }

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
    QSettings settings;
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
        applyEmojiFallbackToFamily(m_systemFontFamily);
        QFont font = QGuiApplication::font();
        const QString fallback = fallbackUiFamily();
        QStringList families{m_systemFontFamily};
        if (!fallback.isEmpty() && fallback.compare(m_systemFontFamily, Qt::CaseInsensitive) != 0) families << fallback;
        font.setFamilies(families);
        QGuiApplication::setFont(font);
        emit systemFontFamilyChanged(m_systemFontFamily);
    }
}

namespace {

QString& cachedEmojiFallbackFamily() {
    static QString family;
    return family;
}

QString findBundledFontPath(const QString& fileName) {
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates{
        appDir + QStringLiteral("/../share/zalos/fonts/") + fileName,
        appDir + QStringLiteral("/assets/fonts/") + fileName,
        appDir + QStringLiteral("/../assets/fonts/") + fileName,
    };
    const QString located = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                   QStringLiteral("zalos/fonts/") + fileName);
    if (!located.isEmpty()) candidates << located;
#ifdef ZALOS_SOURCE_DIR
    candidates << QStringLiteral(ZALOS_SOURCE_DIR "/assets/fonts/") + fileName;
#endif
    for (const QString& path : candidates) {
        if (!path.isEmpty() && QFileInfo::exists(path)) return path;
    }
    return {};
}

QString findBundledEmojiFontPath() {
    return findBundledFontPath(QStringLiteral("ZalosEmoji-CBDT.ttf"));
}

}

QString ThemeManager::registerEmojiFallbackFont() {
    QString& family = cachedEmojiFallbackFamily();
    if (family.isEmpty()) {
        const QString path = findBundledEmojiFontPath();
        if (!path.isEmpty()) {
            const int id = QFontDatabase::addApplicationFont(path);
            if (id >= 0) {
                const QStringList families = QFontDatabase::applicationFontFamilies(id);
                if (!families.isEmpty()) family = families.first();
            }
        }
    }
    if (!family.isEmpty()) {
        const QStringList bases{
            QGuiApplication::font().family(),
            QStringLiteral("Sans Serif"),
            QStringLiteral("sans-serif"),
            QStringLiteral("Inter"),
            QStringLiteral("Noto Sans"),
            QStringLiteral("Cantarell"),
            QStringLiteral("Ubuntu"),
            QStringLiteral("Liberation Sans"),
            QStringLiteral("DejaVu Sans"),
        };
        for (const QString& base : bases) {
            applyEmojiFallbackToFamily(base);
        }
    }
    return family;
}

QString ThemeManager::installedFamilyName(const QString& family) {
    const QString wanted = family.trimmed();
    if (wanted.isEmpty()) return {};
    for (const QString& entry : QFontDatabase::families()) {
        QString name = entry;
        const int bracket = name.indexOf(QLatin1String(" ["));
        if (bracket > 0) name.truncate(bracket);
        if (name.compare(wanted, Qt::CaseInsensitive) == 0) return name;
    }
    return {};
}

bool ThemeManager::isEmojiOrSymbolFamily(const QString& family) {
    const QString f = family.trimmed();
    if (f.isEmpty()) return false;
    if (f.contains(QLatin1String("emoji"), Qt::CaseInsensitive)) return true;
    const QString& bundledEmoji = cachedEmojiFallbackFamily();
    if (!bundledEmoji.isEmpty() && f.compare(bundledEmoji, Qt::CaseInsensitive) == 0) return true;
    static const QStringList kSymbolFamilies{
        QStringLiteral("Symbol"), QStringLiteral("Standard Symbols PS"), QStringLiteral("Standard Symbols L"),
        QStringLiteral("OpenSymbol"), QStringLiteral("Webdings"), QStringLiteral("Wingdings"),
        QStringLiteral("Noto Sans Symbols"), QStringLiteral("Noto Sans Symbols 2"), QStringLiteral("Noto Sans Math"),
        QStringLiteral("Noto Music")};
    for (const QString& s : kSymbolFamilies) {
        if (f.compare(s, Qt::CaseInsensitive) == 0) return true;
    }
    const QList<QFontDatabase::WritingSystem> systems = QFontDatabase::writingSystems(f);
    return systems.size() == 1 && systems.first() == QFontDatabase::Symbol;
}

bool ThemeManager::isUsableUiFamily(const QString& family) {
    const QString name = installedFamilyName(family);
    if (name.isEmpty() || isEmojiOrSymbolFamily(name)) return false;
    QFont probe(name);
    probe.setStyleStrategy(QFont::NoFontMerging);
    const QRawFont raw = QRawFont::fromFont(probe);
    if (!raw.isValid()) return false;
    for (const char16_t c : {u'a', u'\u1EA1', u'\u01B0', u'\u01A1', u'\u0111', u'\u1EC5'}) {
        if (!raw.supportsCharacter(QChar(c))) return false;
    }
    return true;
}

QString ThemeManager::chooseUiFont(const QStringList& specs, const std::function<bool(const QString&)>& usable,
                                   const QString& fallback) {
    for (const QString& spec : specs) {
        const QString family = familyFromFontSpec(spec);
        if (!family.isEmpty() && usable && usable(family)) return family;
    }
    return fallback;
}

QString ThemeManager::fallbackUiFamily() {
    static QString family;
    if (!family.isEmpty()) return family;
    const QString noto = QStringLiteral("Noto Sans");
    const QString installed = installedFamilyName(noto);
    if (!installed.isEmpty()) {
        family = installed;
        return family;
    }
    const QString path = findBundledFontPath(QStringLiteral("NotoSans-VF.ttf"));
    if (!path.isEmpty()) {
        const int id = QFontDatabase::addApplicationFont(path);
        const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
        if (!families.isEmpty()) {
            family = families.first();
            return family;
        }
    }
    return QGuiApplication::font().family();
}

void ThemeManager::applyEmojiFallbackToFamily(const QString& family) {
    const QString& emojiFamily = cachedEmojiFallbackFamily();
    if (emojiFamily.isEmpty() || family.isEmpty() || family == emojiFamily) return;
    QStringList subs = QFont::substitutes(family);
    if (!subs.contains(emojiFamily)) {
        QFont::insertSubstitution(family, emojiFamily);
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

bool ThemeManager::paletteIsDark(const QPalette& pal) {
    const QColor win = pal.color(QPalette::Window);
    if (!win.isValid()) return false;
    return 0.299 * win.red() + 0.587 * win.green() + 0.114 * win.blue() < 128.0;
}

QColor ThemeManager::systemPaletteColor(QPalette::ColorRole role) const {
    const QColor win = m_systemPalette.color(QPalette::Window);
    if (!win.isValid() || paletteIsDark(m_systemPalette) != m_isDarkMode) return QColor();
    return m_systemPalette.color(role);
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
        QColor win = systemPaletteColor(QPalette::Window);
        return win.isValid() ? win.darker(120) : QColor("#121416");
    }
    return accentColor().darker(115);
}

QColor ThemeManager::chatBackgroundColor() const {
    if (m_themeMode == 0) return QColor("#EBECF0");
    if (m_themeMode == 1) return QColor("#16191D");
    if (m_isDarkMode) {
        QColor base = systemPaletteColor(QPalette::Base);
        return base.isValid() ? base : QColor("#16191D");
    }
    QColor win = systemPaletteColor(QPalette::Window);
    if (!win.isValid()) return QColor("#EBECF0");
    return win.lightness() > 240 ? win.darker(106) : win;
}

QColor ThemeManager::backgroundColor() const {
    return resolveColor("#FFFFFF", "#22262B", systemPaletteColor(QPalette::Window));
}

QColor ThemeManager::surfaceColor() const {
    return resolveColor("#FFFFFF", "#22262B", systemPaletteColor(QPalette::Window));
}

QColor ThemeManager::cardBackgroundColor() const {
    return resolveColor("#EBECF0", "#1D2025", systemPaletteColor(QPalette::AlternateBase));
}

QColor ThemeManager::textColor() const {
    return resolveColor("#081B3A", "#DFE2E7", systemPaletteColor(QPalette::WindowText));
}

QColor ThemeManager::secondaryTextColor() const {
    if (m_themeMode == 0) return QColor("#5A6981");
    if (m_themeMode == 1) return QColor("#9FACBC");
    QColor t = systemPaletteColor(QPalette::WindowText);
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
        QColor win = systemPaletteColor(QPalette::Window);
        return win.isValid() ? win.lighter(115) : QColor("#282D33");
    }
    QColor base = systemPaletteColor(QPalette::Base);
    return base.isValid() ? base : QColor("#FFFFFF");
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
    if (m_themeMode == 0) return QColor("#EBECF0");
    if (m_themeMode == 1) return QColor("#16191D");
    if (m_isDarkMode) {
        QColor base = systemPaletteColor(QPalette::Base);
        return base.isValid() ? base : QColor("#16191D");
    }
    const QColor surface = surfaceColor();
    QColor alt = systemPaletteColor(QPalette::AlternateBase);
    if (alt.isValid() && qAbs(alt.lightness() - surface.lightness()) >= 10) return alt;
    if (surface.isValid()) return surface.lightness() > 240 ? surface.darker(106) : surface.darker(104);
    return QColor("#EBECF0");
}

QColor ThemeManager::inputFillHoverColor() const {
    return resolveColor("#E5E7EB", "#1D2025", systemPaletteColor(QPalette::AlternateBase));
}

QColor ThemeManager::dividerColor() const {
    if (m_themeMode == 0) return QColor("#26000000");
    if (m_themeMode == 1) return QColor("#1AFFFFFF");
    return m_isDarkMode ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 26);
}

bool isReadableMuted(const QColor& fg, bool darkMode) {
    if (!fg.isValid() || fg.alpha() < 100) return false;
    return darkMode ? fg.lightness() >= 95 : fg.lightness() <= 190;
}

QColor ThemeManager::placeholderColor() const {
    if (m_themeMode == 0) return QColor("#77859C");
    if (m_themeMode == 1) return QColor("#738296");
    QColor p = systemPaletteColor(QPalette::PlaceholderText);
    if (isReadableMuted(p, m_isDarkMode)) return p;
    return m_isDarkMode ? QColor("#8A96A8") : QColor("#77859C");
}

QColor ThemeManager::iconColor() const {
    if (m_themeMode == 0) return QColor("#5A6981");
    if (m_themeMode == 1) return QColor("#9FACBC");
    return secondaryTextColor();
}

QColor ThemeManager::linkColor() const {
    if (m_themeMode == 0) return QColor("#005AE0");
    if (m_themeMode == 1) return QColor("#66A6FF");
    QColor l = systemPaletteColor(QPalette::Link);
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
    QColor l = systemPaletteColor(QPalette::Link);
    return l.isValid() ? l : accentColor();
}

QColor ThemeManager::reactionSurfaceColor() const {
    if (m_themeMode == 0) return QColor("#FFFFFF");
    if (m_themeMode == 1) return QColor("#1D2025");
    QColor b = systemPaletteColor(QPalette::Base);
    return b.isValid() ? b : (m_isDarkMode ? QColor("#1D2025") : QColor("#FFFFFF"));
}

QColor ThemeManager::inactiveSubtleColor() const {
    if (m_themeMode == 0) return QColor("#B5B5B5");
    if (m_themeMode == 1) return QColor("#595959");
    QColor p = systemPaletteColor(QPalette::PlaceholderText);
    if (isReadableMuted(p, m_isDarkMode)) return p;
    return m_isDarkMode ? QColor("#6E7785") : QColor("#B5B5B5");
}

QColor ThemeManager::inactiveDisabledColor() const {
    if (m_themeMode == 0) return QColor("#DBDBDB");
    if (m_themeMode == 1) return QColor("#333333");
    QColor t = systemPaletteColor(QPalette::WindowText);
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
    detectGnomeOrPortalTheme();
    detectKdeTheme();
    detectSystemAccentColor();

    QDBusConnection::sessionBus().connect(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "SettingChanged",
        this,
        SLOT(onPortalSettingChanged(QString, QString, QDBusVariant))
    );

    watchKdeGlobals();
}

namespace {

QString kdeGlobalsPath() {
    return QDir::homePath() + QStringLiteral("/.config/kdeglobals");
}

QString kdeValue(const QByteArray& content, const QString& group, const QString& key) {
    QString current;
    const QStringList lines = QString::fromUtf8(content).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            current = line.mid(1, line.size() - 2);
            continue;
        }
        if (current != group) continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        if (line.left(eq).trimmed() == key) return line.mid(eq + 1).trimmed();
    }
    return QString();
}

QColor kdeRgb(const QString& value) {
    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() < 3) return QColor();
    bool okR = false, okG = false, okB = false;
    const int r = parts[0].trimmed().toInt(&okR);
    const int g = parts[1].trimmed().toInt(&okG);
    const int b = parts[2].trimmed().toInt(&okB);
    if (!okR || !okG || !okB) return QColor();
    return QColor(r, g, b);
}

}

bool ThemeManager::kdeDarkFromGlobals(const QByteArray& content, bool* known) {
    if (known) *known = false;
    const QColor bg = kdeRgb(kdeValue(content, QStringLiteral("Colors:Window"), QStringLiteral("BackgroundNormal")));
    if (bg.isValid()) {
        if (known) *known = true;
        return 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue() < 128.0;
    }
    QString scheme = kdeValue(content, QStringLiteral("General"), QStringLiteral("ColorScheme"));
    if (scheme.isEmpty()) scheme = kdeValue(content, QStringLiteral("KDE"), QStringLiteral("LookAndFeelPackage"));
    if (scheme.isEmpty()) return false;
    if (known) *known = true;
    return scheme.contains(QLatin1String("dark"), Qt::CaseInsensitive);
}

QColor ThemeManager::kdeAccentFromGlobals(const QByteArray& content) {
    return kdeRgb(kdeValue(content, QStringLiteral("General"), QStringLiteral("AccentColor")));
}

void ThemeManager::watchKdeGlobals() {
    const QString path = kdeGlobalsPath();
    const QString dir = QFileInfo(path).absolutePath();
    if (!QFile::exists(path) && !QDir(dir).exists()) return;
    if (!m_kdeWatcher) {
        m_kdeWatcher = new QFileSystemWatcher(this);
        m_kdeDebounce.setSingleShot(true);
        m_kdeDebounce.setInterval(150);
        connect(&m_kdeDebounce, &QTimer::timeout, this, [this]() {
            const QString file = kdeGlobalsPath();
            if (QFile::exists(file) && !m_kdeWatcher->files().contains(file)) m_kdeWatcher->addPath(file);
            detectKdeTheme();
            detectSystemAccentColor();
            detectSystemFont();
        });
        connect(m_kdeWatcher, &QFileSystemWatcher::fileChanged, &m_kdeDebounce, qOverload<>(&QTimer::start));
        connect(m_kdeWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString&) {
            const QString file = kdeGlobalsPath();
            if (QFile::exists(file) && !m_kdeWatcher->files().contains(file)) m_kdeDebounce.start();
        });
    }
    if (QFile::exists(path)) m_kdeWatcher->addPath(path);
    if (QDir(dir).exists()) m_kdeWatcher->addPath(dir);
}

void ThemeManager::applySystemAccentColor(const QColor& detectedAccent) {
    QColor next = detectedAccent;
    if (!next.isValid()) {
        QFile kdeGlobals(kdeGlobalsPath());
        if (kdeGlobals.open(QIODevice::ReadOnly)) next = kdeAccentFromGlobals(kdeGlobals.readAll());
    }
    if (!next.isValid()) {
        QColor highlight = m_systemPalette.color(QPalette::Highlight);
        if (highlight.isValid() && highlight != Qt::black && highlight != Qt::white) {
            next = highlight;
        }
    }
    if (next.isValid() && next != m_systemAccentColor) {
        m_systemAccentColor = next;
        emit systemAccentColorChanged(m_systemAccentColor);
        if (m_themeMode == 2) {
            emit themeColorsChanged();
        }
    }
}

namespace {

QVariant unwrapDbusVariant(QVariant v) {
    for (int i = 0; i < 4 && v.canConvert<QDBusVariant>(); ++i) {
        v = v.value<QDBusVariant>().variant();
    }
    return v;
}

}

bool ThemeManager::gtkDarkFromSettingsIni(const QByteArray& content, bool* known) {
    if (known) *known = false;
    bool hasSetting = false;
    bool preferDark = false;
    bool hasPreferDark = false;
    bool themeDark = false;
    const QStringList lines = QString::fromUtf8(content).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))
            || line.startsWith(QLatin1Char('['))) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed().toLower();
        if (key == QLatin1String("gtk-application-prefer-dark-theme")) {
            hasSetting = true;
            hasPreferDark = true;
            preferDark = (val == QLatin1String("1") || val == QLatin1String("true") || val == QLatin1String("yes"));
        } else if (key == QLatin1String("gtk-theme-name")) {
            hasSetting = true;
            themeDark = val.contains(QLatin1String("dark"));
        }
    }
    if (!hasSetting) return false;
    if (known) *known = true;
    return (hasPreferDark && preferDark) || themeDark;
}

bool ThemeManager::portalColorSchemeFromVariant(const QVariant& raw, bool* known) {
    if (known) *known = false;
    const QVariant v = unwrapDbusVariant(raw);
    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) return false;
        if (s.contains(QLatin1String("dark"), Qt::CaseInsensitive)) {
            if (known) *known = true;
            return true;
        }
        if (s.compare(QLatin1String("default"), Qt::CaseInsensitive) == 0
            || s.contains(QLatin1String("light"), Qt::CaseInsensitive)) {
            if (known) *known = true;
            return false;
        }
        return false;
    }
    bool ok = false;
    const uint value = v.toUInt(&ok);
    if (!ok) return false;
    if (value == 1) {
        if (known) *known = true;
        return true;
    }
    if (value == 2) {
        if (known) *known = true;
        return false;
    }
    return false;
}

QColor ThemeManager::portalAccentFromVariant(const QVariant& raw) {
    auto colorFromRgb = [](double r, double g, double b) -> QColor {
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) return QColor();
        if (r < 0.0 || r > 1.0 || g < 0.0 || g > 1.0 || b < 0.0 || b > 1.0) return QColor();
        return QColor::fromRgbF(r, g, b);
    };
    const QVariant v = unwrapDbusVariant(raw);
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        if (arg.currentType() != QDBusArgument::StructureType) return QColor();
        double r = -1.0, g = -1.0, b = -1.0;
        arg.beginStructure();
        arg >> r >> g >> b;
        arg.endStructure();
        return colorFromRgb(r, g, b);
    }
    if (v.typeId() == QMetaType::QVariantList) {
        const QVariantList list = v.toList();
        if (list.size() < 3) return QColor();
        bool okR = false, okG = false, okB = false;
        const double r = list.at(0).toDouble(&okR);
        const double g = list.at(1).toDouble(&okG);
        const double b = list.at(2).toDouble(&okB);
        if (!okR || !okG || !okB) return QColor();
        return colorFromRgb(r, g, b);
    }
    return QColor();
}

void ThemeManager::detectSystemAccentColor() {
    if (!m_systemAccentColor.isValid()) {
        applySystemAccentColor(QColor());
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "Read"
    );
    msg << "org.freedesktop.appearance" << "accent-color";

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg, 3000);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    QPointer<ThemeManager> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [self, watcher]() {
        watcher->deleteLater();
        if (!self) return;
        QColor detectedAccent;
        QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (reply.isValid()) {
            detectedAccent = portalAccentFromVariant(reply.value().variant());
        }
        if (detectedAccent.isValid()) {
            QSettings().setValue("appearance/cachedAccentColor", detectedAccent.name());
            self->applySystemAccentColor(detectedAccent);
        } else {
            self->applySystemAccentColor(QColor());
        }
    });
}

void ThemeManager::detectKdeTheme() {
    QFile file(kdeGlobalsPath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray raw = file.readAll();
    bool known = false;
    const bool dark = kdeDarkFromGlobals(raw, &known);
    const QColor winBg = kdeRgb(kdeValue(raw, QStringLiteral("Colors:Window"), QStringLiteral("BackgroundNormal")));
    const QColor winFg = kdeRgb(kdeValue(raw, QStringLiteral("Colors:Window"), QStringLiteral("ForegroundNormal")));
    const QColor viewBg = kdeRgb(kdeValue(raw, QStringLiteral("Colors:View"), QStringLiteral("BackgroundNormal")));
    const QColor viewAlt = kdeRgb(kdeValue(raw, QStringLiteral("Colors:View"), QStringLiteral("BackgroundAlternate")));
    const QColor viewMuted = kdeRgb(kdeValue(raw, QStringLiteral("Colors:View"), QStringLiteral("ForegroundInactive")));
    if (winBg.isValid()) m_systemPalette.setColor(QPalette::Window, winBg);
    if (winFg.isValid()) m_systemPalette.setColor(QPalette::WindowText, winFg);
    if (viewBg.isValid()) m_systemPalette.setColor(QPalette::Base, viewBg);
    if (viewAlt.isValid()) m_systemPalette.setColor(QPalette::AlternateBase, viewAlt);
    if (viewMuted.isValid()) m_systemPalette.setColor(QPalette::PlaceholderText, viewMuted);
    if (known) {
        m_systemIsDark = dark;
        QSettings().setValue("appearance/cachedSystemDark", m_systemIsDark);
        if (m_themeMode == 2) setDarkMode(m_systemIsDark);
    }
    if (m_themeMode == 2) {
        syncApplicationPalette();
        emit themeColorsChanged();
    }
}

void ThemeManager::applyPortalDarkMode(bool dark) {
    m_systemIsDark = dark;
    QSettings().setValue("appearance/cachedSystemDark", m_systemIsDark);
    if (m_themeMode == 2) setDarkMode(m_systemIsDark);
}

bool ThemeManager::fallbackLocalDarkMode() {
    const QString gtkTheme = qEnvironmentVariable("GTK_THEME");
    if (!gtkTheme.isEmpty()) {
        applyPortalDarkMode(gtkTheme.contains("dark", Qt::CaseInsensitive));
        return true;
    }
    const QStringList gtkFiles{
        QDir::homePath() + QStringLiteral("/.config/gtk-4.0/settings.ini"),
        QDir::homePath() + QStringLiteral("/.config/gtk-3.0/settings.ini")
    };
    for (const QString& path : gtkFiles) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        bool known = false;
        const bool dark = gtkDarkFromSettingsIni(file.readAll(), &known);
        if (known) {
            applyPortalDarkMode(dark);
            return true;
        }
    }
    QFile kdeFile(kdeGlobalsPath());
    if (kdeFile.open(QIODevice::ReadOnly)) {
        bool known = false;
        const bool dark = kdeDarkFromGlobals(kdeFile.readAll(), &known);
        if (known) {
            applyPortalDarkMode(dark);
            return true;
        }
    }
    return false;
}

void ThemeManager::detectGnomeOrPortalTheme() {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "Read"
    );
    msg << "org.freedesktop.appearance" << "color-scheme";

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg, 3000);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    QPointer<ThemeManager> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [self, watcher]() {
        watcher->deleteLater();
        if (!self) return;
        QDBusPendingReply<QDBusVariant> reply = *watcher;
        const bool portalReplied = reply.isValid();
        if (portalReplied) {
            bool known = false;
            const bool dark = portalColorSchemeFromVariant(reply.value().variant(), &known);
            if (known) {
                self->applyPortalDarkMode(dark);
                return;
            }
        }

        QDBusMessage gnomePortalMsg = QDBusMessage::createMethodCall(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.Settings",
            "Read"
        );
        gnomePortalMsg << "org.gnome.desktop.interface" << "color-scheme";

        QDBusPendingCall gnomePending = QDBusConnection::sessionBus().asyncCall(gnomePortalMsg, 3000);
        auto* gnomeWatcher = new QDBusPendingCallWatcher(gnomePending, self);
        connect(gnomeWatcher, &QDBusPendingCallWatcher::finished, self, [self, gnomeWatcher, portalReplied]() {
            gnomeWatcher->deleteLater();
            if (!self) return;
            QDBusPendingReply<QDBusVariant> gnomeReply = *gnomeWatcher;
            if (gnomeReply.isValid()) {
                bool known = false;
                const bool dark = portalColorSchemeFromVariant(gnomeReply.value().variant(), &known);
                if (known) {
                    self->applyPortalDarkMode(dark);
                    return;
                }
            }
            if (!self->fallbackLocalDarkMode() && portalReplied) {
                self->applyPortalDarkMode(false);
            }
        });
    });
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

namespace {

bool isKdeSession() {
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP")).toLower();
    return desktop.contains(QLatin1String("kde"))
           || desktop.contains(QLatin1String("plasma"))
           || !env.value(QStringLiteral("KDE_FULL_SESSION")).isEmpty();
}

QString readKdeFontSpec() {
    QFile kdeGlobals(kdeGlobalsPath());
    if (!kdeGlobals.open(QIODevice::ReadOnly)) return QString();
    return kdeValue(kdeGlobals.readAll(), QStringLiteral("General"), QStringLiteral("font"));
}

QString userFontChoice() {
    return QSettings().value("appearance/fontFamily").toString();
}

QString pickUiFont(const QStringList& specs) {
    const QString chosen = ThemeManager::chooseUiFont(specs, &ThemeManager::isUsableUiFamily,
                                                      ThemeManager::fallbackUiFamily());
    const QString installed = ThemeManager::installedFamilyName(chosen);
    return installed.isEmpty() ? chosen : installed;
}

}

#ifdef ZALOS_TESTING
QString ThemeManager::resolveFontSpecForTesting(const QString& spec) {
    return pickUiFont({spec});
}
#endif

void ThemeManager::applyDetectedFont(const QString& family) {
    if (family.isEmpty()) return;
    QSettings settings;
    if (isUsableUiFamily(family)) settings.setValue("appearance/cachedDesktopFont", family);
    else settings.remove("appearance/cachedDesktopFont");
    setSystemFontFamily(family);
}

void ThemeManager::detectSystemFont() {
    const QString user = userFontChoice();
    const QString kdeSpec = readKdeFontSpec();
    if (isKdeSession()) {
        applyDetectedFont(pickUiFont({user, kdeSpec}));
        return;
    }
    if (m_systemFontFamily.isEmpty() || !isUsableUiFamily(m_systemFontFamily)) {
        applyDetectedFont(pickUiFont({user, kdeSpec}));
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings",
        "Read"
    );
    msg << "org.gnome.desktop.interface" << "font-name";

    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg, 3000);
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    QPointer<ThemeManager> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [self, watcher, user, kdeSpec]() {
        watcher->deleteLater();
        if (!self) return;
        QString portalSpec;
        QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (reply.isValid()) portalSpec = unwrapDbusVariant(reply.value().variant()).toString();
        self->applyDetectedFont(pickUiFont({user, portalSpec, kdeSpec}));
    });
}

void ThemeManager::onPortalSettingChanged(const QString& ns, const QString& key) {
    onPortalSettingChanged(ns, key, QDBusVariant());
}

void ThemeManager::onPortalSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value) {
    const QVariant raw = value.variant();
    if (key == QLatin1String("color-scheme")) {
        if (raw.isValid()) {
            bool known = false;
            const bool dark = portalColorSchemeFromVariant(raw, &known);
            if (known) {
                applyPortalDarkMode(dark);
                return;
            }
        }
        detectGnomeOrPortalTheme();
    } else if (key == QLatin1String("accent-color")) {
        if (raw.isValid()) {
            const QColor detectedAccent = portalAccentFromVariant(raw);
            if (detectedAccent.isValid()) {
                QSettings().setValue("appearance/cachedAccentColor", detectedAccent.name());
                applySystemAccentColor(detectedAccent);
                return;
            }
        }
        detectSystemAccentColor();
    } else if (key == QLatin1String("font-name")) {
        if (raw.isValid() && !isKdeSession()) {
            applyDetectedFont(pickUiFont({userFontChoice(), unwrapDbusVariant(raw).toString()}));
            return;
        }
        detectSystemFont();
    } else if (ns == QLatin1String("org.freedesktop.appearance")) {
        detectGnomeOrPortalTheme();
        detectSystemAccentColor();
    } else if (ns == QLatin1String("org.gnome.desktop.interface")) {
        detectGnomeOrPortalTheme();
        detectSystemFont();
    }
}

}
