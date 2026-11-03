pragma Singleton
import QtQuick
import Zalos // module types (ThemeManager) are not implicit for files under src/qml/

QtObject {
    id: root

    // Brand Colors
    // Brand colors follow Zalo PC's light/dark tokens (see ThemeManager).
    readonly property color zaloBlue: ThemeManager.accentColor
    readonly property color zaloBlueHover: ThemeManager.accentHoverColor
    readonly property color zaloBlueLight: ThemeManager.accentTonalColor
    // Text/icons on a zaloBlueLight (tonal) background: never zaloBlue there (blue on dark blue in dark mode).
    readonly property color zaloBlueTonalText: ThemeManager.accentTonalTextColor
    readonly property color onlineGreen: ThemeManager.successColor
    readonly property color badgeRed: ThemeManager.badgeColor
    readonly property color goldStar: "#F5A623"

    // Zalo PC control/message tokens (compact-app-pc.css :root .light / .dark), resolved per theme.
    readonly property bool _dark: ThemeManager.isDarkMode
    readonly property color accent: "#0068FF"                                   // --accent-blue-bg / --B55
    readonly property color activeness: _dark ? "#66A6FF" : "#0068FF"           // --background-activeness
    readonly property color inactiveSubtle: _dark ? "#595959" : "#B5B5B5"       // --background-inactiveness-subtle (NG40)
    readonly property color inactiveDisabled: _dark ? "#333333" : "#DBDBDB"     // --background-inactiveness-disabled
    readonly property color activeDisabled: _dark ? "#00378A" : "#ADD1FF"       // --background-activeness-disabled
    readonly property color textOnColor: "#FFFFFF"                              // --text-on-color
    readonly property color linkColor: _dark ? "#66A6FF" : "#0068FF"            // --cta-link-primary
    readonly property color mediaPlaceholder: _dark ? "#1D2025" : "#EBECF0"     // --layer-background-subtle
    readonly property color mediaHover: Qt.rgba(0, 0, 0, 0.2)                   // --media-message-hover
    readonly property color timestampPill: _dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.2) // --timestamp
    readonly property color reactionBackground: _dark ? "#1D2025" : "#FFFFFF"   // --reaction-background
    readonly property color dropOverlay: _dark ? "#001029" : "#F0F7FF"          // --B10
    readonly property color downloadedGreen: "#2B974F"                          // --G70

    // System-adaptive typography
    readonly property string fontFamily: ThemeManager.systemFontFamily.length > 0 ? ThemeManager.systemFontFamily : Qt.application.font.family

    // Standard Layout Dimensions
    readonly property int railWidth: 64
    readonly property int sidebarWidth: 344
    readonly property int titleBarHeight: 36

    // Responsive Spacing
    readonly property int spacingTiny: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 14
    readonly property int spacingLarge: 20

    // Radius
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 16
    readonly property int radiusRound: 999
}

