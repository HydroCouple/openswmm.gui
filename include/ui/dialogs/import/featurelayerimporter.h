/**
 * @file      featurelayerimporter.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — controller between the import dialog (view) and the
 *            model side (GISVectorLayer source, SWMMModelLayer target).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — controller between the import dialog
 * (view) and the model side (GISVectorLayer source, SWMMModelLayer
 * target). Splits the work into three phases so the dialog can run the
 * dry run off the GUI thread (MeshGenerationDialog idiom):
 *
 *   1. sourceSpec() / captureModelSnapshot() — MAIN thread; reads the
 *      layers/engine into plain data.
 *   2. readSourceFeatures() [static] + buildImportPlan() [pure, see
 *      importplanning.h] — WORKER-safe; opens its own GDAL dataset so
 *      the canvas's OGR handle (and its spatial filter) is untouched.
 *   3. execute() — MAIN thread; pushes one undo macro of existing
 *      Add*Commands + SetAdapterPropertiesCommand / geometry commands.
 *
 * No widget dependencies — unit-testable headless.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_FEATURELAYERIMPORTER_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_FEATURELAYERIMPORTER_H

#include "ui/dialogs/import/importmapping.h"
#include "ui/dialogs/import/importplan.h"
#include "ui/dialogs/import/importplanning.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

class GISVectorLayer;
class MapCanvas;
class SWMMModelLayer;

namespace openswmmvis::import
{

/*! Everything the worker thread needs to read the source features —
 *  plain data, no QObject/layer pointers cross the thread boundary. */
struct SourceSpec {
    QString filePath;   ///< OGR datasource path
    QString layerName;  ///< OGR sublayer name; empty = first layer
    QString filterExpr; ///< current OGR attribute filter of the layer
    bool selectedOnly = false;
    QSet<long long> selectedFids; ///< honored when selectedOnly
    QString sourceWkt;            ///< source-layer CRS WKT; empty = none
    QString targetWkt;            ///< model-layer CRS WKT; empty = none
};

class FeatureLayerImporter : public QObject
{
    Q_OBJECT

public:
    /**
     * @param[in] source Optional borrowed RAW GIS layer, must outlive every capture/execute/name
     *   lookup.
     * @param[in] target Optional borrowed RAW SWMM source, must outlive capture/execute and
     *   engine/command callbacks.
     * @param[in] canvas Optional borrowed RAW canvas, execute requires live undo stack; no headless
     *   direct fallback.
     * @param[in] mapping Moved independent mapping/options value; source schema/target
     *   compatibility not checked at construction.
     * @param[in] parent Optional QObject owner, does not own source/target/canvas through these
     *   arguments.
     * @brief Store controller context only, no data read/planning/engine write. Capture/execute GUI
     *   thread, static independent-handle reader can run worker after GDAL setup.
     */
    FeatureLayerImporter(GISVectorLayer *source, SWMMModelLayer *target, MapCanvas *canvas,
                         ImportMapping mapping, QObject *parent = nullptr);

    /**
     * @return Borrowed owned mapping reference, valid until controller destruction/replacement;
     *   copy for worker context, no source ownership.
     */
    [[nodiscard]] const ImportMapping &mapping() const { return m_mapping; }
    /**
     * @param[in] m Mapping copied wholesale, no validation, preview regeneration or
     *   target/source-kind rebind.
     * @note GUI-thread store, no source/engine write or notification. execute uses CURRENT
     *   mapping.kind/updateGeometry rather than immutable plan-embedded kind; preserve plan
     *   compatibility.
     */
    void setMapping(const ImportMapping &m) { m_mapping = m; }

    /*! MAIN thread — capture the worker inputs from the source layer.
     * @return Plain-data snapshot: source file/sub-layer/filter, mapping selectedOnly and source
     *   selected FIDs, source/target WKT. Missing source yields all-default spec including no
     *   target WKT; absent target yields empty target WKT.
     * @note Main-thread source/engine context access, no OGR iteration/model mutation; raw pointers
     *   must remain alive until capture finishes. Result owns values, independent worker may
     *   outlive original layer after capture. Current source filter string captured, not canvas
     *   spatial filter.
     */
    [[nodiscard]] SourceSpec sourceSpec() const;

    /*! MAIN thread — snapshot existing node/link/gage names, types, and
     *  geometry from the target layer's engine.
     * @return New node/gage coordinate/type and link type/endpoint/interior snapshot in target
     *   model CRS, empty missing target/engine. IDs missing skipped; coordinate/type API errors
     *   mostly ignored with initialized 0 defaults. Link interior comes from target cached
     *   polyline, assumes engine/cache coherent.
     * @note Main-thread serial engine reads, no engine/model write, layer ownership pin or
     *   transactional consistency across reentrant edits. Worker owns result values only;
     *   regenerate after model changes.
     */
    [[nodiscard]] ModelSnapshot captureModelSnapshot() const;

    /*! WORKER-safe — open \p spec.filePath read-only with its own GDAL
     *  handle, iterate features (honoring filter/selection), transform
     *  every coordinate source-CRS → model-CRS, and reduce to plain
     *  SourceFeatures. On open failure returns an empty list and sets
     *  \p errorOut.
     * @param[in] spec Independent plain-data source path/layer/filter/selection/WKT, caller
     *   establishes GDAL driver availability and concurrent-dataset support.
     * @param[out] errorOut Optional explanation set ONLY datasource-open or missing-layer failure;
     *   not cleared on success, not used for filter/CRS/feature-transform/read errors.
     * @return In-source-order included features with attribute copies and XY geometry,
     *   unusable/multipart/unsupported geometries retained with geometryOk=false. Point/LineString
     *   or single-part MultiPoint/MultiLineString accepted, Z/M discarded, line length<2 left for
     *   planner. Empty may mean no features/filter/selection or open failure.
     * @note Open own read-only GDAL handle, so canvas handle/filter untouched. Attribute-filter
     *   return ignored. Transform only when BOTH WKTs parse and differ; absent/invalid WKT or
     *   failed CT creation silently leaves native coordinates. Per-point Transform result/finite
     *   values unchecked. No target-kind geometry-family validation here, no cancellation/progress,
     *   no source/engine write. Dataset/features/transformation released before return;
     *   exceptions/driver failures not wrapped as errorOut.
     */
    [[nodiscard]] static QVector<SourceFeature> readSourceFeatures(const SourceSpec &spec,
                                                                   QString *errorOut = nullptr);

    /*! MAIN thread — apply \p plan as ONE undo macro on the canvas
     *  stack. Returns a copy of the plan with engine-level failures
     *  downgraded to Error and the executed counts recomputed.
     *  \p progress (optional) receives (done, total) before each item;
     *  return false from it to stop after the current item (already
     *  pushed items stay, as one undoable macro).
     * @param[in] plan Borrowed prior dry-run snapshot copied, must match CURRENT mapping kind/model
     *   state. Auto-node name/position arrays must be parallel, no length check.
     * @param[in] progress Optional synchronous callback invoked with 1-based (done,total) BEFORE
     *   each item, including Skip/Error. False still allows CURRENT Create/Update item to execute,
     *   then stops; already skipped/error item stops immediately. Callback must preserve raw
     *   controller context and stack, no unsafe concurrent reentry.
     * @return Plan copy with detected creation/missing-update-adapter/heat failures changed to
     *   Error and counters recounted. Missing target/canvas/stack returns original copy unchanged
     *   without error/recount. On stop, unprocessed tail retains planned Create/Update actions and
     *   therefore counters do NOT represent confirmed executed totals.
     * @note GUI-thread sequential pushes inside one undo macro, no BulkEdit scope or all-or-nothing
     *   transaction. Create auto-junctions first, then target object/properties; failure leaves
     *   prior pushed work for undo, including orphan endpoints after failed link. Existing-name
     *   index used as creation success probe; gage probe creates unowned adapter without releasing
     *   it in current code. Generic adapter command setter outcomes not inspected. Update never
     *   rewires links or moves gages, can silently skip missing geometry target if no attribute
     *   probe fails. Heat uses validated shared funnel but failure does not roll back prior
     *   object/property writes. End macro and invalidate scene/overlay even partial stop. Commands
     *   own replay lifetime separately; no immediate project/file save, cancellation-tail
     *   correction or error dialog.
     */
    ImportPlan execute(const ImportPlan &plan, const std::function<bool(int, int)> &progress = {});

private:
    GISVectorLayer *m_source = nullptr;
    SWMMModelLayer *m_target = nullptr;
    MapCanvas *m_canvas = nullptr;
    ImportMapping m_mapping;
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_FEATURELAYERIMPORTER_H
