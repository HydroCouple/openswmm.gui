#ifndef THEMEHELPERS_H
#define THEMEHELPERS_H

/**
 * @file      themehelpers.h
 * @brief     UI redesign P4 — the one vocabulary for chrome styling that used to be scattered hex
 *            literals (three different "error reds", three "hint grays", hardcoded light banner
 *            backgrounds).
 *
 * UI redesign P4 — the one vocabulary for chrome styling that used to be
 * scattered hex literals (three different "error reds", three "hint
 * grays", hardcoded light banner backgrounds). Header-only, two flavors:
 *
 *  - Palette-role application (preferred, theme-live for free):
 *    applyHintRole() rides QPalette::PlaceholderText, which ThemeManager
 *    keeps tokenized, so hint labels recolor on theme flips with no
 *    reconnect machinery.
 *
 *  - Stylesheet builders for TRANSIENT states (validation feedback that
 *    toggles styles on and off): the string carries the token color of
 *    the moment it is applied. "color: palette(mid)" hints are theme-live
 *    (Mid is tokenized to hintText); error/banner strings refresh the
 *    next time the site re-applies them — acceptable for short-lived
 *    validation states, and vastly simpler than per-widget reconnects.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QString>
#include <QWidget>

#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

namespace openswmmvis::ui::theme
{

enum class Banner { Info, Success, Error };

/*! Secondary/hint text via the palette role — theme-live, no stylesheet.
 * @param[in,out] w Optional widget whose foreground role becomes PlaceholderText; null is ignored. No stylesheet or ownership changes occur.
 */
inline void applyHintRole(QWidget *w)
{
    if (w)
        w->setForegroundRole(QPalette::PlaceholderText);
}

/*! Theme-live hint styling for stylesheet-based sites (Mid == hintText).
 * @return Palette(mid) foreground stylesheet, which follows later palette changes.
 */
inline QString hintStyle()
{
    return QStringLiteral("color: palette(mid);");
}

/**
 * @brief Build a theme-live italic hint-text stylesheet.
 * @return Palette(mid) foreground plus italic font stylesheet, following later palette changes.
 */
inline QString hintItalicStyle()
{
    return QStringLiteral("color: palette(mid); font-style: italic;");
}

/*! Validation-error text color of the current theme.
 * @return Current error colour as an RGB foreground stylesheet; caller reapplies after theme changes.
 */
inline QString errorTextStyle()
{
    return QStringLiteral("color: %1;")
        .arg(ThemeManager::instance()->colors().error.name(QColor::HexRgb));
}

/*! Validation-error border of the current theme.
 * @return Current error colour as a one-pixel solid RGB border stylesheet.
 */
inline QString errorBorderStyle()
{
    return QStringLiteral("border: 1px solid %1;")
        .arg(ThemeManager::instance()->colors().error.name(QColor::HexRgb));
}

/*! Iteration 2 (D5) — bold section heading for pseudo-headings that used
 *  to be raw &lt;b&gt; tags or ad-hoc bold stylesheets. Palette-driven text
 *  color, so it stays theme-live.
 * @return Bold-font stylesheet without overriding colour.
 */
inline QString sectionHeadingStyle()
{
    return QStringLiteral("font-weight: bold;");
}

/*! Iteration 2 (D5) — field-background fill for a failed-validation
 *  input (the tokenized replacement for hardcoded light-pink fills that
 *  were illegible on the dark theme). Transient-state string, same
 *  refresh policy as errorTextStyle().
 * @return Current error-banner background/foreground RGB stylesheet pair.
 */
inline QString errorFillStyle()
{
    const ThemeColors &c = ThemeManager::instance()->colors();
    return QStringLiteral("background-color: %1; color: %2;")
        .arg(c.bannerErrorBg.name(QColor::HexRgb), c.bannerErrorFg.name(QColor::HexRgb));
}

/*! Background+foreground pair for an inline banner strip; append
 *  site-specific padding/radius as needed.
 * @param[in] kind Supported Info/Success/Error token pair; caller should not pass an unknown enum.
 * @return Current theme RGB background/foreground stylesheet. The returned string does not update automatically on later theme changes.
 */
inline QString bannerStyle(Banner kind)
{
    const ThemeColors &c = ThemeManager::instance()->colors();
    QColor bg, fg;
    switch (kind) {
    case Banner::Error:
        bg = c.bannerErrorBg;
        fg = c.bannerErrorFg;
        break;
    case Banner::Success:
        bg = c.bannerSuccessBg;
        fg = c.bannerSuccessFg;
        break;
    case Banner::Info:
        bg = c.bannerInfoBg;
        fg = c.bannerInfoFg;
        break;
    }
    return QStringLiteral("background-color: %1; color: %2;")
        .arg(bg.name(QColor::HexRgb), fg.name(QColor::HexRgb));
}

} // namespace openswmmvis::ui::theme

#endif // THEMEHELPERS_H
