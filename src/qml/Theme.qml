pragma Singleton
import QtQuick
import Zalos

QtObject {
    id: root

    readonly property color zaloBlue: ThemeManager.accentColor
    readonly property color zaloBlueHover: ThemeManager.accentHoverColor
    readonly property color zaloBlueLight: ThemeManager.accentTonalColor
    readonly property color zaloBlueTonalText: ThemeManager.accentTonalTextColor
    readonly property color onlineGreen: ThemeManager.successColor
    readonly property color badgeRed: ThemeManager.badgeColor
    readonly property color goldStar: "#F5A623"

    readonly property bool _dark: ThemeManager.isDarkMode
    readonly property color accent: ThemeManager.uiAccentColor
    readonly property color activeness: ThemeManager.activenessColor
    readonly property color inactiveSubtle: ThemeManager.inactiveSubtleColor
    readonly property color inactiveDisabled: ThemeManager.inactiveDisabledColor
    readonly property color activeDisabled: ThemeManager.activeDisabledColor
    readonly property color textOnColor: "#FFFFFF"
    readonly property color linkColor: ThemeManager.uiLinkColor
    readonly property string linkStyle: "<style>a{color:" + ThemeManager.uiLinkColor + ";text-decoration:none}</style>"
    readonly property color mediaPlaceholder: ThemeManager.cardBackgroundColor
    readonly property color mediaHover: Qt.rgba(0, 0, 0, 0.2)
    readonly property color timestampPill: _dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.2)
    readonly property color reactionBackground: ThemeManager.reactionSurfaceColor
    readonly property color dropOverlay: ThemeManager.dropOverlayColor
    readonly property color downloadedGreen: "#2B974F"

    readonly property string fontFamily: ThemeManager.systemFontFamily.length > 0 ? ThemeManager.systemFontFamily : Qt.application.font.family

    readonly property int fontMicro: 9
    readonly property int fontTiny: 10
    readonly property int fontCaption: 11
    readonly property int fontSmall: 12
    readonly property int fontBody: 13
    readonly property int fontBodyLarge: 14
    readonly property int fontSubtitle: 15
    readonly property int fontTitle: 16
    readonly property int fontTitleLarge: 18
    readonly property int fontHeadline: 20
    readonly property int fontEmojiPicker: 22
    readonly property int fontHero: 24
    readonly property int fontDisplay: 28
    readonly property int fontEmojiStrike: 36

    readonly property int railWidth: 64
    readonly property int sidebarWidth: 368
    readonly property int titleBarHeight: 36

    readonly property int spacingTiny: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 14
    readonly property int spacingLarge: 20

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 16
    readonly property int radiusRound: 999
}
