import QtQuick
import QtQuick.Effects

MultiEffect {
    property alias blurAmount: effect.blur
    property alias blurMaxRadius: effect.blurMax
    id: effect
    autoPaddingEnabled: false
    clip: true
    blurEnabled: true
    blur: 0.65
    blurMax: 48
}


