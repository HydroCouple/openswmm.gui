#ifndef THEMEHELPERS_H
#define THEMEHELPERS_H

/*!
 * \file themehelpers.h
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
 */

#include <QString>
#include <QWidget>

#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

namespace openswmmvis::ui::theme {

enum class Banner { Info, Success, Error };

/*! Secondary/hint text via the palette role — theme-live, no stylesheet. */
inline void applyHintRole(QWidget *w)
{
    if (w)
        w->setForegroundRole(QPalette::PlaceholderText);
}

/*! Theme-live hint styling for stylesheet-based sites (Mid == hintText). */
inline QString hintStyle()
{
    return QStringLiteral("color: palette(mid);");
}

inline QString hintItalicStyle()
{
    return QStringLiteral("color: palette(mid); font-style: italic;");
}

/*! Validation-error text color of the current theme. */
inline QString errorTextStyle()
{
    return QStringLiteral("color: %1;")
        .arg(ThemeManager::instance()->colors().error.name(QColor::HexRgb));
}

/*! Validation-error border of the current theme. */
inline QString errorBorderStyle()
{
    return QStringLiteral("border: 1px solid %1;")
        .arg(ThemeManager::instance()->colors().error.name(QColor::HexRgb));
}

/*! Iteration 2 (D5) — bold section heading for pseudo-headings that used
 *  to be raw <b> tags or ad-hoc bold stylesheets. Palette-driven text
 *  color, so it stays theme-live. */
inline QString sectionHeadingStyle()
{
    return QStringLiteral("font-weight: bold;");
}

/*! Iteration 2 (D5) — field-background fill for a failed-validation
 *  input (the tokenized replacement for hardcoded light-pink fills that
 *  were illegible on the dark theme). Transient-state string, same
 *  refresh policy as errorTextStyle(). */
inline QString errorFillStyle()
{
    const ThemeColors &c = ThemeManager::instance()->colors();
    return QStringLiteral("background-color: %1; color: %2;")
        .arg(c.bannerErrorBg.name(QColor::HexRgb),
             c.bannerErrorFg.name(QColor::HexRgb));
}

/*! Background+foreground pair for an inline banner strip; append
 *  site-specific padding/radius as needed. */
inline QString bannerStyle(Banner kind)
{
    const ThemeColors &c = ThemeManager::instance()->colors();
    QColor bg, fg;
    switch (kind) {
    case Banner::Error:   bg = c.bannerErrorBg;   fg = c.bannerErrorFg;   break;
    case Banner::Success: bg = c.bannerSuccessBg; fg = c.bannerSuccessFg; break;
    case Banner::Info:    bg = c.bannerInfoBg;    fg = c.bannerInfoFg;    break;
    }
    return QStringLiteral("background-color: %1; color: %2;")
        .arg(bg.name(QColor::HexRgb), fg.name(QColor::HexRgb));
}

}   // namespace openswmmvis::ui::theme

#endif // THEMEHELPERS_H
