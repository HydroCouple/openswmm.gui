/**
 * @file      qmlrulelistio.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Save / load RuleLists as QGIS .qml style files (Slice Z.17b).
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
 *         Unsupported details (some are silently omitted or approximated):
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_QMLRULELISTIO_H
#define OPENSWMM_RENDER_QMLRULELISTIO_H

#include "render/rulelistio.h" // reuse RuleListIoResult

#include <QString>

namespace OpenSWMM::Render
{

class RuleList;

namespace QmlRuleListIO
{

/** @brief Export the supported RuleList subset to a QGIS QML document.
 * @param list Borrowed list read synchronously; null/empty writes an empty
 * singleSymbol envelope. One rule exports its SingleSymbolRenderer style;
 * multiple rules export a flat RuleRenderer with filters, labels and positive
 * scale denominators rounded to integers. Rule visibility and decorations are omitted.
 * @param path Destination opened with truncate; existing content is replaced
 * immediately, with no atomic-save/rollback protection or extension enforcement.
 * @return ok after writing without a reported QFile error, otherwise error.
 * Warnings cover unsupported renderer fallbacks, but unsupported symbol kinds
 * and properties can be lost silently. Only selected simple marker/line/fill
 * colors and sizes are written; numeric units are not converted to QGIS units.
 * Symbol opacity is exported as one; filters are copied without translation. */
[[nodiscard]] RuleListIoResult save(const RuleList *list, const QString &path);

/** @brief Replace a list with best-effort simple-symbol rules parsed from QML.
 * @param path Source XML file; the extension and root/schema are not validated.
 * @param[in,out] list Required target on its owning Qt thread. Null, open failure
 * or XML parse failure returns an error before changing it. After parsing, clear()
 * destroys old rules and append() emits normal list notifications; construction
 * exceptions are not caught and have no rollback guarantee.
 * @return ok plus rulesLoaded and optional unsupported/missing-renderer warnings.
 * RuleRenderer entries are flattened in XML order; missing symbol references
 * produce the default SingleSymbolRenderer. Otherwise one Imported rule uses
 * symbol "0" for singleSymbol or an arbitrary QHash-first symbol as fallback.
 * Unknown layer classes become SimpleMarker silently. Only recognized color,
 * width and size properties are read; numbers are unchecked and units are not
 * converted. Valid XML with no renderer/symbol still replaces the list with a
 * default Imported rule and warning. Duplicate symbol names keep the last value. */
[[nodiscard]] RuleListIoResult load(const QString &path, RuleList *list);

} // namespace QmlRuleListIO

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QMLRULELISTIO_H
