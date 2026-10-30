/**
 * @file      xsecticonrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Procedural cross-section shape icons.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_XSECTICONRENDERER_H
#define OPENSWMMVIS_SECTIONVIEW_XSECTICONRENDERER_H

#include <QIcon>
#include <QPalette>
#include <QSize>

namespace openswmmvis::sectionview
{

/*!
 * \brief Icon for a SWMM_XSectShape, rendered from engine geometry.
 *
 * Results are cached per (shape, size, palette-foreground) in QPixmapCache, so
 * repopulating the palette on every dialog open is cheap.
 *
 * \returns A generic placeholder icon for shapes with no drawable geometry
 *          (SWMM_XSECT_DUMMY) or when the engine declines the nominal geoms —
 *          never a null icon, so the list always has something to draw.
 * @param[in] shape SWMM_XSectShape code; invalid/undrawable geometry uses placeholder.
 * @param[in] size Requested logical icon size; invalid/empty defaults 112×84.
 * @param[in] palette Theme roles for stroke/fill; cache key uses WindowText/Base/Highlight RGBA.
 * @return Owned implicitly shared icon from QPixmapCache or newly drawn nominal geometry;
 *   placeholder on unavailable/degenerate geometry. Allocation failure can still yield null despite
 *   old never-null summary.
 * @note GUI-thread only(QPixmap/cache). Valid geometry rendered at DPR 2, 128 depth samples, 5
 *   logical-pixel inset, aspect-fit and Y-flip; dimensions are REPRESENTATIVE, not model scale.
 *   Open hydraulic sections omit top stroke, STREET follows engine closed flag here. Cache
 *   insertion failure ignored; placeholder is not inserted. No engine/model edits.
 */
[[nodiscard]] QIcon xsectShapeIcon(int shape, const QSize &size, const QPalette &palette);

/*!
 * \brief Representative geom1..geom4 used to draw \p shape at unit depth.
 *
 * Exposed so tests can assert every surfaced shape produces a valid engine
 * handle — a shape whose nominal geoms are rejected would silently fall back
 * to the placeholder tile.
 * @param[in] shape Shape code, unknown/tabulated codes receive defaults rather than valid
 *   standalone table.
 * @param[out] geom1 Unit full-depth value 1.
 * @param[out] geom2 Representative width/sediment depth, FORCE_MAIN roughness 100, otherwise 0 when
 *   unused.
 * @param[out] geom3 Representative bottom radius/triangular height/power exponent/side slope
 *   depending on shape, else 0.
 * @param[out] geom4 Representative right trapezoid run:rise 1, else 0.
 * @note Every output assigned; dimensions relative to unit depth, no table lookup/model access.
 *   Tabulated shapes need tabulatedIconSampler, DUMMY/unknown may remain undrawable.
 */
void nominalGeomsFor(int shape, double &geom1, double &geom2, double &geom3, double &geom4);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_XSECTICONRENDERER_H
