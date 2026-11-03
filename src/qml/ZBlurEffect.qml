import QtQuick
import QtQuick.Effects

// Blurred, darkened backdrop (call window). MultiEffect needs Qt >= 6.5, so it is loaded through a Loader:
// on Qt 6.4 (Ubuntu 24.04) the Loader fails quietly and the stage stays plain black.
MultiEffect {
    blurEnabled: true
    blur: 1.0
    blurMax: 64
    brightness: -0.68
}
