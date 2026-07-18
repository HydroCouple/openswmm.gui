/*!
 * \file   importplanning.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — the pure planning core. Consumes
 * plain-data source features (already read from OGR and transformed
 * into the model-layer CRS by FeatureLayerImporter) plus a snapshot of
 * the existing model, and produces the dry-run ImportPlan.
 *
 * Deliberately Qt-Core-only (no GDAL, engine, or widget includes) so
 * the endpoint-resolution / conflict / coercion matrix is unit-testable
 * with the lightweight add_swmmvis_unit_test harness.
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

namespace openswmmvis::import {

/*! One source feature reduced to plain data. \c points is the full
 *  geometry in MODEL-layer CRS: for point kinds a single entry; for
 *  line kinds the ordered polyline (first = upstream, last =
 *  downstream). \c attrs maps OGR field name → value (null QVariant
 *  for OGR NULL cells). */
struct SourceFeature {
    long long   fid = -1;
    QVector<QPointF> points;
    QVariantMap attrs;
    bool        geometryOk = true;   ///< false = unusable geometry (empty,
                                     ///  multi-part, <2 line points, …)
    QString     geometryError;       ///< set when !geometryOk
};

/*! Snapshot of the existing model taken on the main thread before
 *  planning. All coordinates in the model-layer CRS. */
struct ModelSnapshot {
    QHash<QString, QPointF> nodes;        ///< node name → coordinate
    QHash<QString, int>     nodeTypes;    ///< node name → SWMM_NodeType
    QHash<QString, QPointF> gages;        ///< gage name → coordinate
    QHash<QString, int>     linkTypes;    ///< link name → SWMM_LinkType
    /*! Existing link endpoints + interior vertices (model CRS), used to
     *  detect geometryDiffers / endpointsDiffer for updates. */
    struct LinkGeom { QString from, to; QVector<QPointF> interior; };
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
 *  callers treat null as "leave engine default / not provided". */
[[nodiscard]] QVariant coerceValue(const QVariant &raw,
                                   const TargetAttribute &attr,
                                   bool *ok,
                                   QString *errorOut = nullptr);

/*! Build the dry-run plan. Pure function of its inputs.
 *  \p features are in source order; the returned plan has one item per
 *  feature, same order. */
[[nodiscard]] ImportPlan buildImportPlan(const ImportMapping &mapping,
                                         const ModelSnapshot &snapshot,
                                         const QVector<SourceFeature> &features);

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPLANNING_H
