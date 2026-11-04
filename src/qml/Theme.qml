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
    readonly property color accent: "#0068FF"
    readonly property color activeness: _dark ? "#66A6FF" : "#0068FF"
    readonly property color inactiveSubtle: _dark ? "#595959" : "#B5B5B5"
    readonly property color inactiveDisabled: _dark ? "#333333" : "#DBDBDB"
    readonly property color activeDisabled: _dark ? "#00378A" : "#ADD1FF"
    readonly property color textOnColor: "#FFFFFF"
    readonly property color linkColor: _dark ? "#66A6FF" : "#0068FF"
    readonly property color mediaPlaceholder: _dark ? "#1D2025" : "#EBECF0"
    readonly property color mediaHover: Qt.rgba(0, 0, 0, 0.2)
    readonly property color timestampPill: _dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.2)
    readonly property color reactionBackground: _dark ? "#1D2025" : "#FFFFFF"
    readonly property color dropOverlay: _dark ? "#001029" : "#F0F7FF"
    readonly property color downloadedGreen: "#2B974F"

    readonly property string fontFamily: ThemeManager.systemFontFamily.length > 0 ? ThemeManager.systemFontFamily : Qt.application.font.family

    readonly property int railWidth: 64
    readonly property int sidebarWidth: 344
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
