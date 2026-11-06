/**
 * @file      sectionmodelbuilders.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Engine state → SectionDiagramModel.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_SECTIONMODELBUILDERS_H
#define OPENSWMMVIS_SECTIONVIEW_SECTIONMODELBUILDERS_H

#include <QString>

#include <openswmm/engine/openswmm_engine.h>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview
{

/*!
 * Cap on the automatic vertical exaggeration of a link profile.
 *
 * Exposed so the Section View dock can state it without hard-coding a second
 * copy in its tooltip.
 * @return Builtin automatic profile exaggeration ceiling 10 (V:H). Value only; explicit user VE can
 *   be handled separately by painter/host and no settings read occurs.
 */
[[nodiscard]] double profileMaxExaggeration() noexcept;

/*! Unit-presentation context shared by every builder. */
struct DiagramUnits {
    QString lengthLabel = QStringLiteral("m"); //!< "ft" or "m".
    bool si = true;                            //!< Drives the engine handle's units.
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
 * @param[in] engine Optional borrowed live model engine; null yields empty explanatory model. Host
 *   serializes queries with engine edits/runs/close.
 * @param[in] linkIdx Zero-based EXISTING link index; negative rejected, positive validity generally
 *   delegated to getters.
 * @param[in] units Presentation labels and SI selector must agree with engine display units; no
 *   general conversion of getters.
 * @return Independent uniform-scale section model with explanatory emptyText for no
 *   link/pump/unreadable/DUMMY/unresolved/degenerate geometry. No retained engine pointers or
 *   source mutation.
 * @note IRREGULAR preferentially uses surveyed stations with station multiplier and
 *   thalweg-relative origin, including bank markers, preserving asymmetry; generic width sampler
 *   otherwise mirrored. Tabulated geometry can depend on lifecycle, STREET input builds before
 *   resolution where available. End invert/crown leaders add node inverts+offsets and collapse
 *   values within 0.005 display units. Some getter failures retain initialized zeros; output is a
 *   presentation, not a validation report.
 */
[[nodiscard]] SectionDiagramModel buildLinkSection(SWMM_Engine engine, int linkIdx,
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
 * @param[in] engine Optional borrowed live model engine; null yields empty explanatory model. Host
 *   serializes queries with engine edits/runs/close.
 * @param[in] linkIdx Zero-based EXISTING link index; negative rejected, positive validity generally
 *   delegated to getters.
 * @param[in] units Presentation labels and SI selector must agree with engine display units; no
 *   general conversion of getters.
 * @return Independent longitudinal drawing of endpoint structures, barrel/pump, offsets,
 *   actual-length slope labels and other incident-link stubs/plan bearings. No engine
 *   mutation/retained handles; null/negative/bad endpoint lookup yields explanatory empty model.
 * @note Missing/nonpositive length uses nominal 1 or pump lift×1.6(min 1), disables VE annotation
 *   for synthetic run; nonpump slope labels can still use nominal run. Structure WIDTHS are
 *   schematic fractions of run, elevations use raw engine values. Geometry depth
 *   unresolved/pump/DUMMY can yield line/zero-height barrel. Auto VE uses target aspect 6, cap 10;
 *   painter resolves actual ratio later.
 * @note Collects all other incident links separately at EACH endpoint with no display-count cap
 *   (O(total links) per node); headings use endpoint straight bearing, not polyline
 *   tangent/geodesic. Unchecked optional getter failure retains defaults. Host preserves consistent
 *   model state throughout all queries.
 */
[[nodiscard]] SectionDiagramModel buildLinkProfile(SWMM_Engine engine, int linkIdx,
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
 * @param[in] engine Optional borrowed live engine, no concurrent mutation/close while querying.
 * @param[in] nodeIdx Zero-based existing node index; negative/null rejected, positive out-of-range
 *   not centrally checked.
 * @param[in] units Labels/SI convention must match engine display units; raw getters mostly not
 *   converted.
 * @return Independent nonuniform-scale node drawing; empty explanation only for null
 *   engine/negative index. Other getter failures may still return default schematic values, not an
 *   error status. No engine mutation, cache or retained handle.
 * @note Inbound left/outbound right, deepest connections first and equal inverts retain engine link
 *   order. Plan inset includes only retained connections with valid noncoincident coordinates;
 *   subtitle records total incident links and footer remainder. Horizontal chamber/stub widths
 *   schematic, storage silhouette normalized/floored from real area/shape data, junction corbel
 *   only when connections low enough.
 * @note Storage initial water clamped to max depth, outfall fixed/variable/free presentation and
 *   divider arrows are input-state illustrations, not simulation flow solving. Node LID
 *   layers/Richards state and bottom exchange statistics appended when readable; physical layer
 *   thickness converted mm→m or in→ft, aquifer head metres→display, balance/volume/rate/contact
 *   statistics retain explicit SI labels. No new engine run or time advance. Caller supplies valid
 *   finite dimensions including nonnegative max depth.
 */
[[nodiscard]] SectionDiagramModel buildNodeProfile(SWMM_Engine engine, int nodeIdx,
                                                   const DiagramUnits &units, int maxLinks = 8);

/*!
 * \brief Live preview for the cross-section editor, built from uncommitted
 *        dialog values rather than from the engine's stored link.
 *
 * \param shape             SWMM_XSectShape code.
 * \param units             Unit context.
 * \param highlightOrdinal  1 for depth, 2 for width; other values accent neither.
 *
 * IRREGULAR / STREET / CUSTOM require tabulated geometry;
 * pass the resolved sampler through buildSamplerPreview() instead.
 * @param[in] geom1 Full depth/diameter or first shape-specific value in units.si convention.
 * @param[in] geom2 Second shape-specific field, finite; unused 0.
 * @param[in] geom3 Third shape-specific field, finite; unused 0.
 * @param[in] geom4 Fourth shape-specific field, finite; unused 0.
 * @return Independent uniform-scale preview from current uncommitted inputs with full-property
 *   footer, emptyText when engine rejects or geometry absent. Does not mutate model/dialog or
 *   reserve geometry table; generic sampler mirrors width profile. No detailed validation errors.
 */
[[nodiscard]] SectionDiagramModel buildXsectEditorPreview(int shape, double geom1, double geom2,
                                                          double geom3, double geom4,
                                                          const DiagramUnits &units,
                                                          int highlightOrdinal = 0);

class XsectSampler; // fwd

/*!
 * \brief Preview for an already-constructed sampler (tabulated shapes).
 *
 * Same drawing as buildXsectEditorPreview() minus the per-geom dimensions,
 * which are meaningless for a transect / street / shape-curve section.
 * @param[in] sampler Borrowed independent sampler; must stay valid/unmoved throughout queries, no
 *   ownership transfer.
 * @param[in] title Copied title, including empty.
 * @param[in] subtitle Copied secondary caption.
 * @param[in] units Display label only for sampler output; sampler already fixes physical units, no
 *   conversion/rebuild.
 * @return Uniform-scale independent model with generic sampled outline/depth/width/footer,
 *   explanatory empty state if sampler/outline invalid. No per-geom highlight/extra dimensions,
 *   engine mutation or source-model lookup. Hydraulic open flag governs top, no STREET visual
 *   override here.
 */
[[nodiscard]] SectionDiagramModel buildSamplerPreview(const XsectSampler &sampler,
                                                      const QString &title, const QString &subtitle,
                                                      const DiagramUnits &units);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_SECTIONMODELBUILDERS_H
