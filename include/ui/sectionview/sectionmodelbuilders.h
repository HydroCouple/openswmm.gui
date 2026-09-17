/*!
 * \file   sectionmodelbuilders.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Engine state → SectionDiagramModel.
 *
 * Slices SP.3 / SP.4 / SP.5 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * Free functions, not a class: each one reads the engine (and, for the editor
 * preview, uncommitted dialog values) and returns a plain value. No QWidget,
 * no ownership, no caching — which is what makes them testable headlessly and
 * safe to call on every `changed()` tick.
 *
 * Length units are whatever the engine reports; callers pass the label
 * (UnitSystem::instance()->lengthLabel()) so the diagrams stay unit-agnostic.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_SECTIONMODELBUILDERS_H
#define OPENSWMMVIS_SECTIONVIEW_SECTIONMODELBUILDERS_H

#include <QString>

#include <openswmm/engine/openswmm_engine.h>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview {

/*!
 * Cap on the automatic vertical exaggeration of a link profile.
 *
 * Exposed so the Section View dock can state it without hard-coding a second
 * copy in its tooltip.
 */
[[nodiscard]] double profileMaxExaggeration() noexcept;

/*! Unit-presentation context shared by every builder. */
struct DiagramUnits
{
    QString lengthLabel = QStringLiteral("m");  //!< "ft" or "m".
    bool    si          = true;                 //!< Drives the engine handle's units.
};

/*!
 * \brief True-shape cross-section of a link, with dimensions + elevations.
 *
 * Draws the section outline sampled from the engine, dimensions the full depth
 * and max width, and leaders the invert / crown elevations taken from each end
 * node's invert plus the link's offset at that end. A sloping run reports both
 * ends ("100.00 / 98.00 ft"); a flat one collapses to a single value.
 *
 * \returns A model whose `emptyText` explains the gap (pump, DUMMY section,
 *          unresolvable tabulated geometry) when there is nothing to draw.
 */
[[nodiscard]] SectionDiagramModel buildLinkSection(SWMM_Engine engine,
                                                   int linkIdx,
                                                   const DiagramUnits &units);

/*!
 * \brief Longitudinal profile of a conduit between its two nodes.
 *
 * Shows both structures (rim → invert), the ground line, the sloping barrel
 * drawn at its true crown/invert elevations including offsets, the upstream
 * offset dimension, and length / slope along the barrel axis.
 *
 * Uses independent axis scaling (`uniformScale = false`) — a 120 m reach at a
 * 0.25 % slope is unreadable at true aspect.
 */
[[nodiscard]] SectionDiagramModel buildLinkProfile(SWMM_Engine engine,
                                                   int linkIdx,
                                                   const DiagramUnits &units);

/*!
 * \brief Node profile: structure, rim, invert, and every connecting link drawn
 *        at its own invert offset, plus a plan-view inset of link headings.
 *
 * Inbound links (the node is their `to` node) are stubbed to the left,
 * outbound to the right. Links are ordered by invert so the drawing reads like
 * a real manhole schedule.
 *
 * \param maxLinks  Cap on drawn connections; the footer notes any remainder.
 */
[[nodiscard]] SectionDiagramModel buildNodeProfile(SWMM_Engine engine,
                                                   int nodeIdx,
                                                   const DiagramUnits &units,
                                                   int maxLinks = 8);

/*!
 * \brief Live preview for the cross-section editor, built from uncommitted
 *        dialog values rather than from the engine's stored link.
 *
 * \param shape             SWMM_XSectShape code.
 * \param geom1..geom4      Current spin-box values.
 * \param units             Unit context.
 * \param highlightOrdinal  1..4 to accent that geom's dimension line, 0 for none.
 *
 * IRREGULAR / STREET / CUSTOM carry an index in geom1 rather than a dimension;
 * pass the resolved sampler through buildSamplerPreview() instead.
 */
[[nodiscard]] SectionDiagramModel buildXsectEditorPreview(
    int shape, double geom1, double geom2, double geom3, double geom4,
    const DiagramUnits &units, int highlightOrdinal = 0);

class XsectSampler;   // fwd

/*!
 * \brief Preview for an already-constructed sampler (tabulated shapes).
 *
 * Same drawing as buildXsectEditorPreview() minus the per-geom dimensions,
 * which are meaningless for a transect / street / shape-curve section.
 */
[[nodiscard]] SectionDiagramModel buildSamplerPreview(const XsectSampler &sampler,
                                                      const QString &title,
                                                      const QString &subtitle,
                                                      const DiagramUnits &units);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_SECTIONMODELBUILDERS_H
