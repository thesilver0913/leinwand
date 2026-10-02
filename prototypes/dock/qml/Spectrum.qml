// SPDX-License-Identifier: GPL-3.0-or-later
// Spectrum 2 tokens, dark theme, desktop scale, resolved by hand from
// @adobe/spectrum-tokens 15.5.0 (Apache-2.0). The real app will generate this
// from the token JSON at build time (phase 1, M5).
pragma Singleton
import QtQuick

QtObject {
    readonly property color backgroundBase: "#111111"      // background-base-color
    readonly property color backgroundLayer1: "#1b1b1b"    // background-layer-1-color
    readonly property color backgroundLayer2: "#222222"    // background-layer-2-color
    readonly property color gray100: "#2c2c2c"
    readonly property color gray200: "#323232"
    readonly property color gray300: "#393939"
    readonly property color gray400: "#444444"
    readonly property color content: "#dbdbdb"             // neutral-content-color-default
    readonly property color contentHover: "#f2f2f2"        // neutral-content-color-hover
    readonly property color contentSubdued: "#afafaf"      // neutral-subdued-content-color-default
    readonly property color accent: "#4069fd"              // accent-background-color-default
    readonly property color accentContent: "#5681ff"       // accent-content-color-default
    readonly property color hoverOverlay: "#1cffffff"      // transparent-white-100
    readonly property color dropZone: "#5681ff"            // drop-zone-background-color

    readonly property int cornerRadiusSmall: 4             // corner-radius-small-default
    readonly property int fontSize75: 12                   // font-size-75
    readonly property int fontSize100: 14                  // font-size-100
    readonly property int componentHeight75: 24            // component-height-75
    readonly property int panelWidth: 260                  // standard-panel-width
    readonly property int panelMinimumWidth: 200           // standard-panel-minimum-width

    // Adobe Clean is not available to us; the spec uses Source Sans 3.
    readonly property string fontFamily: "Source Sans 3"
}
