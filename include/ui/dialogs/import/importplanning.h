/**
 * @file      importplanning.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — the pure planning core.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — the pure planning core. Consumes
 * plain-data source features (already read from OGR and transformed
 * into the model-layer CRS by FeatureLayerImporter) plus a snapshot of
 * the existing model, and produces the dry-run ImportPlan.
 *
 * Deliberately Qt-Core-only (no GDAL, engine, or widget includes) so
 * the endpoint-resolution / conflict / coercion matrix is unit-testable
 * with the lightweight add_swmmvis_unit_test harness.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPLANNING_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPLANNING_H

#include "ui/dialogs/import/importmapping.h"
#include "ui/dialogs/import/importplan.h"

#include <QHash>
#include <QPointF>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace openswmmvis::import
{

/*! One source feature reduced to plain data. \c points is the full
 *  geometry in MODEL-layer CRS: for point kinds a single entry; for
 *  line kinds the ordered polyline (first = upstream, last =
 *  downstream). \c attrs maps OGR field name → value (null QVariant
 *  for OGR NULL cells). */
struct SourceFeature {
    long long fid = -1;
    QVector<QPointF> points;
    QVariantMap attrs;
    bool geometryOk = true; ///< false = unusable geometry (empty,
                            ///  multi-part, <2 line points, …)
    QString geometryError;  ///< set when !geometryOk
};

/*! Snapshot of the existing model taken on the main thread before
 *  planning. All coordinates in the model-layer CRS. */
struct ModelSnapshot {
    QHash<QString, QPointF> nodes; ///< node name → coordinate
    QHash<QString, int> nodeTypes; ///< node name → SWMM_NodeType
    QHash<QString, QPointF> gages; ///< gage name → coordinate
    QHash<QString, int> linkTypes; ///< link name → SWMM_LinkType
    /*! Existing link endpoints + interior vertices (model CRS), used to
     *  detect geometryDiffers / endpointsDiffer for updates. */
    struct LinkGeom {
        QString from, to;
        QVector<QPointF> interior;
    };
    QHash<QString, LinkGeom> linkGeoms;
};

/*! Coerce \p raw to the declared type of \p attr.
 *  - Double/Int: accepts numerics and numeric strings (C then current
 *    locale).
 *  - Enum (attr.enumChoices non-empty): accepts the integer code or a
 *    case-insensitive choice label.
 *  - QString: any convertible value, trimmed.
 *  Returns an invalid QVariant and sets \p errorOut on failure. Null
 *  input returns a null (but valid-type-free) QVariant with ok=true —
 *  callers treat null as "leave engine default / not provided".
 * @param[in] raw Borrowed candidate, invalid/null returns invalid QVariant with success. Numeric
 *   input must be finite/representable before integer rounding; wrapper does not guard overflow.
 * @param[in] attr Declared target type/enum labels; numeric values native target adapter units, no
 *   unit conversion.
 * @param[out] ok REQUIRED nonnull flag, true initially, false conversion/membership failure.
 * @param[out] errorOut Optional explanation set only on failure, not cleared on success/null.
 * @return Enum code from near-integral numeric match(tolerance 1e-9) or case-insensitive trimmed
 *   label; Double from numeric/bool/C-locale/current-locale text; Int from near-integral numeric,
 *   no explicit int-range check. Other types trimmed toString. No finite/range/required validation
 *   beyond enum membership and parsing, no source mutation.
 */
[[nodiscard]] QVariant coerceValue(const QVariant &raw, const TargetAttribute &attr, bool *ok,
                                   QString *errorOut = nullptr);

/*! Build the dry-run plan. Pure function of its inputs.
 *  \p features are in source order; the returned plan has one item per
 *  feature, same order.
 * @param[in] mapping Borrowed target mapping/options; unique identifier MUST bind a source COLUMN,
 *   constant name default ignored. selectedFeaturesOnly filtering belongs to caller, not performed
 *   here.
 * @param[in] snapshot Borrowed exact-case existing-name/type/geometry snapshot, coordinates in
 *   MODEL CRS. Assumed coherent; no engine access/revalidation.
 * @param[in] features Borrowed source-order features already transformed to MODEL CRS; point uses
 *   first coordinate, links require >=2 and ordered upstream→downstream. geometryOk gate trusted,
 *   no finite-coordinate/geometric validity checks.
 * @return One planned item per feature, same order, recounted Create/Update/Skip/Error counters; no
 *   engine/GDAL/UI/undo/file mutation.
 * @note Name and geometry/attribute coercion validated BEFORE conflicts, so even Skip-existing can
 *   error on bad incoming data. Duplicate guard reserves only prior successful Create/Update/Skip
 *   names, case-sensitive; failed row does not reserve. Raw blank/null source attribute falls back
 *   default; omitted/null leaves engine defaults. Heat values additionally range checked, other
 *   numeric ranges deferred. Existing type mismatch Update retains only invertElev/tag for nodes,
 *   tag for links, never converts type. updateAttributes=false clears attrs; updateGeometry detects
 *   coordinate 1e-6 component epsilon/interior-vertices only. Rain-gage position update remains
 *   unsupported in planner; links never rewire endpoints. No attr equality comparison, nonempty
 *   mapped attrs still plan Update.
 * @note New link endpoints resolve column exact name FIRST, then closest existing/pending node
 *   within squared tolerance, then auto-create junction at original endpoint. Constant endpoint
 *   binding defaults ignored. Negative tolerance squares to positive, ties depend QHash iteration.
 *   Auto names prefix+counter avoid node/pending/successful-source names, not gage/link namespaces.
 *   Pending auto nodes reused by later features; failure/equal endpoints removes auto nodes created
 *   for that row, counter not rewound. Caller must execute snapshot-compatible plan or regenerate
 *   after source/model changes.
 */
[[nodiscard]] ImportPlan buildImportPlan(const ImportMapping &mapping,
                                         const ModelSnapshot &snapshot,
                                         const QVector<SourceFeature> &features);

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPLANNING_H
