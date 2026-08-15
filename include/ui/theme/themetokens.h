#ifndef THEMETOKENS_H
#define THEMETOKENS_H

/*!
 * \file themetokens.h
 *
 * UI redesign P2 — the design-token layer: one semantic color vocabulary
 * generating both the light and dark chrome themes. Chrome code consumes
 * roles (error, hint, banner, plot axis, ...) instead of hex literals so
 * the two schemes stay in lockstep and contrast is enforceable by test.
 *
 * Every text-role/surface pairing here meets WCAG AA (≥ 4.5:1 for text,
 * ≥ 3:1 for non-text UI glyphs) — tests/gui/test_theme_contrast.cpp
 * fails any edit that regresses that.
 *
 * Data symbology (color ramps, categorical palettes, series cycles) is
 * NOT part of this vocabulary — those encode data, not chrome, and stay
 * untouched by theming.
 */

#include <QColor>

namespace openswmmvis::ui {

struct ThemeColors {
    // Surfaces (back to front) and primary content.
    QColor surfaceWindow;    ///< window / toolbar background
    QColor surfaceRaised;    ///< views, editors, cards (QPalette::Base)
    QColor surfaceSunken;    ///< wells, alternate rows
    QColor text;             ///< primary text on any surface tier
    QColor hintText;         ///< secondary/placeholder text (AA on surfaces)
    QColor border;           ///< separators, frames

    // Interaction.
    QColor accent;           ///< brand/interactive color (links, checked)
    QColor accentText;       ///< text painted on accent fills
    QColor focusRing;        ///< keyboard-focus outline (≥ 3:1 on surfaces)
    QColor selectionFill;    ///< item-view selection background
    QColor selectionText;    ///< text on selectionFill

    // Semantic status (used as text colors on surfaces).
    QColor error;
    QColor warning;
    QColor success;
    QColor info;

    // Inline banners (message strips inside dialogs).
    QColor bannerErrorBg,   bannerErrorFg;
    QColor bannerSuccessBg, bannerSuccessFg;
    QColor bannerInfoBg,    bannerInfoFg;

    // Plot chrome (profile/mesh plot widgets — axes, not data series).
    QColor plotBackground;
    QColor plotAxis;
    QColor plotGrid;
    QColor plotConduit;
    QColor plotNodeBar;
    QColor plotNodeLabel;

    // Map canvas chrome.
    QColor canvasSelectionHighlight;
};

inline const ThemeColors &lightColors()
{
    static const ThemeColors c = {
        /* surfaceWindow */ QColor(0xF5, 0xF5, 0xF7),
        /* surfaceRaised */ QColor(0xFF, 0xFF, 0xFF),
        /* surfaceSunken */ QColor(0xE9, 0xE9, 0xED),
        /* text          */ QColor(0x1D, 0x1D, 0x1F),
        /* hintText      */ QColor(0x5A, 0x5A, 0x60),
        /* border        */ QColor(0xC9, 0xC9, 0xCE),

        /* accent        */ QColor(0x0A, 0x66, 0xC2),
        /* accentText    */ QColor(0xFF, 0xFF, 0xFF),
        /* focusRing     */ QColor(0x0A, 0x66, 0xC2),
        /* selectionFill */ QColor(0x0A, 0x66, 0xC2),
        /* selectionText */ QColor(0xFF, 0xFF, 0xFF),

        /* error         */ QColor(0xB3, 0x26, 0x1E),
        /* warning       */ QColor(0x7A, 0x59, 0x00),
        /* success       */ QColor(0x1E, 0x6B, 0x34),
        /* info          */ QColor(0x0A, 0x66, 0xC2),

        /* bannerErrorBg   */ QColor(0xF9, 0xDE, 0xDC),
        /* bannerErrorFg   */ QColor(0x7A, 0x17, 0x10),
        /* bannerSuccessBg */ QColor(0xDF, 0xF0, 0xE1),
        /* bannerSuccessFg */ QColor(0x14, 0x4D, 0x26),
        /* bannerInfoBg    */ QColor(0xDE, 0xE9, 0xF7),
        /* bannerInfoFg    */ QColor(0x11, 0x3A, 0x66),

        /* plotBackground */ QColor(0xFF, 0xFF, 0xFF),
        /* plotAxis       */ QColor(0x55, 0x55, 0x5B),
        /* plotGrid       */ QColor(0xDC, 0xDC, 0xE0),
        /* plotConduit    */ QColor(0x33, 0x33, 0x33),
        /* plotNodeBar    */ QColor(0x22, 0x22, 0x22),
        /* plotNodeLabel  */ QColor(0x10, 0x10, 0x10),

        /* canvasSelectionHighlight */ QColor(0x00, 0xB8, 0xD9),
    };
    return c;
}

inline const ThemeColors &darkColors()
{
    static const ThemeColors c = {
        /* surfaceWindow */ QColor(0x1E, 0x1E, 0x22),
        /* surfaceRaised */ QColor(0x2A, 0x2A, 0x30),
        /* surfaceSunken */ QColor(0x17, 0x17, 0x1A),
        /* text          */ QColor(0xE8, 0xE8, 0xEA),
        /* hintText      */ QColor(0xA5, 0xA5, 0xAD),
        /* border        */ QColor(0x3F, 0x3F, 0x46),

        /* accent        */ QColor(0x4C, 0x9A, 0xFF),
        /* accentText    */ QColor(0x0B, 0x1E, 0x36),
        /* focusRing     */ QColor(0x4C, 0x9A, 0xFF),
        /* selectionFill */ QColor(0x4C, 0x9A, 0xFF),
        /* selectionText */ QColor(0x0B, 0x1E, 0x36),

        /* error         */ QColor(0xF2, 0x72, 0x6B),
        /* warning       */ QColor(0xE0, 0xA9, 0x3E),
        /* success       */ QColor(0x5D, 0xBB, 0x7A),
        /* info          */ QColor(0x4C, 0x9A, 0xFF),

        /* bannerErrorBg   */ QColor(0x3A, 0x15, 0x13),
        /* bannerErrorFg   */ QColor(0xF5, 0xB8, 0xB3),
        /* bannerSuccessBg */ QColor(0x12, 0x30, 0x1C),
        /* bannerSuccessFg */ QColor(0xA8, 0xDD, 0xB8),
        /* bannerInfoBg    */ QColor(0x10, 0x2A, 0x4A),
        /* bannerInfoFg    */ QColor(0xA9, 0xCC, 0xF5),

        /* plotBackground */ QColor(0x23, 0x23, 0x27),
        /* plotAxis       */ QColor(0xA5, 0xA5, 0xAD),
        /* plotGrid       */ QColor(0x3A, 0x3A, 0x40),
        /* plotConduit    */ QColor(0xC8, 0xC8, 0xCC),
        /* plotNodeBar    */ QColor(0xD8, 0xD8, 0xDC),
        /* plotNodeLabel  */ QColor(0xEF, 0xEF, 0xF2),

        /* canvasSelectionHighlight */ QColor(0x33, 0xD6, 0xF5),
    };
    return c;
}

}   // namespace openswmmvis::ui

#endif // THEMETOKENS_H
