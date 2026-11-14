#pragma once

#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

#include <QObject>
#include <QColor>
#include <QPalette>
#include <QString>
#include <QFileSystemWatcher>
#include <QTimer>

namespace Zalos::Core::Platform {

class ThemeManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool isDarkMode READ isDarkMode WRITE setDarkMode NOTIFY darkModeChanged FINAL)
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged FINAL)
    Q_PROPERTY(QString systemFontFamily READ systemFontFamily WRITE setSystemFontFamily NOTIFY systemFontFamilyChanged FINAL)
    Q_PROPERTY(QColor systemAccentColor READ systemAccentColor NOTIFY systemAccentColorChanged FINAL)
    Q_PROPERTY(bool isWayland READ isWayland CONSTANT FINAL)
    Q_PROPERTY(bool isGtkEnvironment READ isGtkEnvironment CONSTANT FINAL)
    Q_PROPERTY(QColor primaryColor READ primaryColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor railBackgroundColor READ railBackgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor chatBackgroundColor READ chatBackgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor surfaceColor READ surfaceColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor cardBackgroundColor READ cardBackgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor secondaryTextColor READ secondaryTextColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor borderColor READ borderColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor chatBubbleMine READ chatBubbleMine NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor chatBubbleTheir READ chatBubbleTheir NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor hoverColor READ hoverColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor hoverClearColor READ hoverClearColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor railHoverClearColor READ railHoverClearColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor selectedColor READ selectedColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor pinnedColor READ pinnedColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor inputFillColor READ inputFillColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor inputFillHoverColor READ inputFillHoverColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor dividerColor READ dividerColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor placeholderColor READ placeholderColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor iconColor READ iconColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor accentHoverColor READ accentHoverColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor accentTonalColor READ accentTonalColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor accentTonalTextColor READ accentTonalTextColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor linkColor READ linkColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor badgeColor READ badgeColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor successColor READ successColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor errorBackgroundColor READ errorBackgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor warningBackgroundColor READ warningBackgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor infoBackgroundColor READ infoBackgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor railHoverColor READ railHoverColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor railSelectedColor READ railSelectedColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor timestampColor READ timestampColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor reactionBackgroundColor READ reactionBackgroundColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor uiAccentColor READ uiAccentColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor activenessColor READ activenessColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor uiLinkColor READ uiLinkColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor reactionSurfaceColor READ reactionSurfaceColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor inactiveSubtleColor READ inactiveSubtleColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor inactiveDisabledColor READ inactiveDisabledColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor activeDisabledColor READ activeDisabledColor NOTIFY themeColorsChanged FINAL)
    Q_PROPERTY(QColor dropOverlayColor READ dropOverlayColor NOTIFY themeColorsChanged FINAL)

public:
    static inline ThemeManager* s_instance = nullptr;
    enum ThemeMode {
        ZaloLight = 0,
        ZaloDark = 1,
        SystemTheme = 2
    };
    Q_ENUM(ThemeMode)

    explicit ThemeManager(QObject* parent = nullptr);

    bool isDarkMode() const { return m_isDarkMode; }
    Q_INVOKABLE void setDarkMode(bool dark);

    int themeMode() const { return m_themeMode; }
    Q_INVOKABLE void setThemeMode(int mode);

    QString systemFontFamily() const { return m_systemFontFamily; }
    Q_INVOKABLE void setSystemFontFamily(const QString& family);
    static QString familyFromFontSpec(const QString& spec);

    QColor systemAccentColor() const { return m_systemAccentColor; }

    bool isWayland() const;
    bool isGtkEnvironment() const;

    QColor primaryColor() const;
    QColor railBackgroundColor() const;
    QColor chatBackgroundColor() const;
    QColor backgroundColor() const;
    QColor surfaceColor() const;
    QColor cardBackgroundColor() const;
    QColor textColor() const;
    QColor secondaryTextColor() const;
    QColor borderColor() const;
    QColor chatBubbleMine() const;
    QColor chatBubbleTheir() const;
    QColor hoverColor() const;
    QColor hoverClearColor() const { QColor c = hoverColor(); c.setAlpha(0); return c; }
    QColor railHoverClearColor() const { QColor c = railHoverColor(); c.setAlpha(0); return c; }
    QColor selectedColor() const;
    QColor pinnedColor() const;
    QColor inputFillColor() const;
    QColor inputFillHoverColor() const;
    QColor dividerColor() const;
    QColor placeholderColor() const;
    QColor iconColor() const;
    QColor accentColor() const;
    QColor accentHoverColor() const;
    QColor accentTonalColor() const;
    QColor accentTonalTextColor() const;
    QColor linkColor() const;
    QColor badgeColor() const;
    QColor successColor() const;
    QColor errorBackgroundColor() const;
    QColor warningBackgroundColor() const;
    QColor infoBackgroundColor() const;
    QColor railHoverColor() const;
    QColor railSelectedColor() const;
    QColor timestampColor() const;
    QColor reactionBackgroundColor() const;

    QColor uiAccentColor() const;
    QColor activenessColor() const;
    QColor uiLinkColor() const;
    QColor reactionSurfaceColor() const;
    QColor inactiveSubtleColor() const;
    QColor inactiveDisabledColor() const;
    QColor activeDisabledColor() const;
    QColor dropOverlayColor() const;

    Q_INVOKABLE void toggleTheme();

    static bool kdeDarkFromGlobals(const QByteArray& content, bool* known);
    static QColor kdeAccentFromGlobals(const QByteArray& content);

signals:
    void darkModeChanged(bool isDark);
    void themeModeChanged(int mode);
    void themeColorsChanged();
    void systemFontFamilyChanged(const QString& family);
    void systemAccentColorChanged(const QColor& color);

public slots:
    void detectGnomeOrPortalTheme();
    void detectSystemAccentColor();

private:
    void initSystemThemeDetection();
    void detectKdeTheme();
    void watchKdeGlobals();
    void detectSystemFont();
    void syncApplicationPalette();
    QColor resolveColor(const QString& lightZalo, const QString& darkZalo, const QColor& systemColor = QColor()) const;

    int m_themeMode{2};
    bool m_isDarkMode{false};
    bool m_systemIsDark{false};
    QColor m_systemAccentColor;
    QPalette m_systemPalette;
    QString m_systemFontFamily;
    QFileSystemWatcher* m_kdeWatcher{nullptr};
    QTimer m_kdeDebounce;
};

}
