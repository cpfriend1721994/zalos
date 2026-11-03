import QtQuick
import QtQuick.Effects

// Rounded-corner mask for ZRoundedImage. MultiEffect needs Qt >= 6.5, so it is loaded through a Loader:
// on Qt 6.4 (Ubuntu 24.04) the Loader fails quietly and the image keeps square corners.
MultiEffect {
    maskEnabled: true
    maskThresholdMin: 0.5
    maskSpreadAtMin: 1.0
}
