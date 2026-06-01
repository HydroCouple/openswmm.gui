/*!
 * \file   qmlrulelistio.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Save / load RuleLists as QGIS .qml style files (Slice Z.17b).
 *
 *         Cross-tool interop for users who maintain styles in QGIS and
 *         want to share them with SWMMVis (or vice versa). The native
 *         .swmm-rule.json round-trip (Z.17-files) is the high-fidelity
 *         path; .qml is the lowest-common-denominator subset.
 *
 *         v1 coverage:
 *           - One Rule + SingleSymbolRenderer + SimpleMarker /
 *             SimpleLine / SimpleFill → QGIS singleSymbol renderer
 *           - Multiple Rules with SingleSymbolRenderers → QGIS
 *             RuleRenderer (one <rule> per Rule, our filterExpression
 *             passed through as the QGIS filter)
 *
 *         What's NOT covered (each emits a warning rather than failing):
 *           - Graduated / Categorized / RuleBased / Unclassed renderers
 *             on the SWMMVis side — export writes a fallback simple
 *             symbol; import treats them as singleSymbol fallback.
 *           - Z.6 raster symbol-layer kinds (RasterColorRamp,
 *             Hillshade, Contour, MeshEdge, MeshNode). QGIS has no
 *             direct equivalent at the symbol-layer level.
 *           - Data-defined overrides + symbol levels — round-trip
 *             through .swmm-rule.json instead.
 *
 *         The existing StyleFileIO::importQml path remains the legacy
 *         layer-primary-renderer entrypoint (used by the
 *         LayerStyleDialog "Import style…" button); QmlRuleListIO
 *         supplements it by targeting the Rule Model directly, which
 *         is what the Style Manager dialog (Z.17c) operates on.
 */

#ifndef OPENSWMM_RENDER_QMLRULELISTIO_H
#define OPENSWMM_RENDER_QMLRULELISTIO_H

#include "render/rulelistio.h"   // reuse RuleListIoResult

#include <QString>

namespace OpenSWMM::Render {

class RuleList;

namespace QmlRuleListIO {

/*! Write \p list to \p path as a QGIS .qml file. \p list may be null —
 *  produces an empty-renderer envelope, useful as a template. */
[[nodiscard]] RuleListIoResult save(const RuleList *list, const QString &path);

/*! Read a QGIS .qml file and populate \p list. Clears \p list first
 *  on success; leaves it unchanged on failure. */
[[nodiscard]] RuleListIoResult load(const QString &path, RuleList *list);

} // namespace QmlRuleListIO

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QMLRULELISTIO_H
