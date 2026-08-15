#ifndef ICONFACTORY_H
#define ICONFACTORY_H

/*!
 * \file iconfactory.h
 *
 * UI redesign P3 — theme-aware icon rendering. The repo's ~120 chrome
 * SVGs hardcode a mid-gray glyph family (#777777 and friends), which is
 * unreadable on a dark theme. IconFactory::icon(alias) returns a QIcon
 * backed by ThemedIconEngine, which substitutes that gray family with
 * the current theme's glyph color AT RENDER TIME (per QIcon mode:
 * normal / active / selected / disabled) — the SVG files on disk stay
 * byte-identical, so the 130 pre-existing plain `QIcon(":/swmmvis/…")`
 * call sites keep rendering exactly as before and can migrate
 * opportunistically.
 *
 * Pixmaps are cached per (alias, scheme, mode, size, scale); a theme
 * flip changes the cache key, so repaints pick up recolored glyphs with
 * no explicit invalidation.
 */

#include <QIcon>
#include <QString>

namespace openswmmvis::ui {

class IconFactory
{
public:
    /*! Themed icon for a `:/swmmvis/` qrc alias (pass just the alias,
     *  e.g. "Open"). Returns a null QIcon when the alias does not
     *  resolve. */
    static QIcon icon(const QString &alias);
};

}   // namespace openswmmvis::ui

#endif // ICONFACTORY_H
