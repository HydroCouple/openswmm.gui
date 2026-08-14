/*!
 * \file   xsecticonrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Procedural cross-section shape icons.
 *
 * Slice SP.3 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * Replaces the 26 hand-drawn `resources/images/*_xsect.svg` thumbnails with
 * outlines sampled from the engine, so the palette tiles and the live preview
 * can never disagree, the icons follow the theme foreground, and shapes that
 * never got artwork (BASKETHANDLE, SEMICIRCULAR, CUSTOM, FORCE_MAIN, DUMMY)
 * get a correct tile for free.
 *
 * Icons are drawn at a nominal unit depth with representative widths (see
 * nominalGeomsFor()), not at the link's real dimensions — a palette tile shows
 * what the shape *is*, and rendering a 0.15 m pipe and a 3 m box culvert to
 * their true relative scale would make most tiles unreadable.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_XSECTICONRENDERER_H
#define OPENSWMMVIS_SECTIONVIEW_XSECTICONRENDERER_H

#include <QIcon>
#include <QPalette>
#include <QSize>

namespace openswmmvis::sectionview {

/*!
 * \brief Icon for a SWMM_XSectShape, rendered from engine geometry.
 *
 * Results are cached per (shape, size, palette-foreground) in QPixmapCache, so
 * repopulating the palette on every dialog open is cheap.
 *
 * \returns A generic placeholder icon for shapes with no drawable geometry
 *          (SWMM_XSECT_DUMMY) or when the engine declines the nominal geoms —
 *          never a null icon, so the list always has something to draw.
 */
[[nodiscard]] QIcon xsectShapeIcon(int shape, const QSize &size,
                                   const QPalette &palette);

/*!
 * \brief Representative geom1..geom4 used to draw \p shape at unit depth.
 *
 * Exposed so tests can assert every surfaced shape produces a valid engine
 * handle — a shape whose nominal geoms are rejected would silently fall back
 * to the placeholder tile.
 */
void nominalGeomsFor(int shape, double &geom1, double &geom2,
                     double &geom3, double &geom4);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_XSECTICONRENDERER_H
