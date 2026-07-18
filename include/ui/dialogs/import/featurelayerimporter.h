/*!
 * \file   featurelayerimporter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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

namespace openswmmvis::import {

/*! Everything the worker thread needs to read the source features —
 *  plain data, no QObject/layer pointers cross the thread boundary. */
struct SourceSpec {
    QString filePath;      ///< OGR datasource path
    QString layerName;     ///< OGR sublayer name; empty = first layer
    QString filterExpr;    ///< current OGR attribute filter of the layer
    bool    selectedOnly = false;
    QSet<long long> selectedFids;   ///< honored when selectedOnly
    QString sourceWkt;     ///< source-layer CRS WKT; empty = none
    QString targetWkt;     ///< model-layer CRS WKT; empty = none
};

class FeatureLayerImporter : public QObject
{
    Q_OBJECT

public:
    FeatureLayerImporter(GISVectorLayer *source,
                         SWMMModelLayer *target,
                         MapCanvas      *canvas,
                         ImportMapping   mapping,
                         QObject        *parent = nullptr);

    [[nodiscard]] const ImportMapping &mapping() const { return m_mapping; }
    void setMapping(const ImportMapping &m) { m_mapping = m; }

    /*! MAIN thread — capture the worker inputs from the source layer. */
    [[nodiscard]] SourceSpec sourceSpec() const;

    /*! MAIN thread — snapshot existing node/link/gage names, types, and
     *  geometry from the target layer's engine. */
    [[nodiscard]] ModelSnapshot captureModelSnapshot() const;

    /*! WORKER-safe — open \p spec.filePath read-only with its own GDAL
     *  handle, iterate features (honoring filter/selection), transform
     *  every coordinate source-CRS → model-CRS, and reduce to plain
     *  SourceFeatures. On open failure returns an empty list and sets
     *  \p errorOut. */
    [[nodiscard]] static QVector<SourceFeature>
        readSourceFeatures(const SourceSpec &spec, QString *errorOut = nullptr);

    /*! MAIN thread — apply \p plan as ONE undo macro on the canvas
     *  stack. Returns a copy of the plan with engine-level failures
     *  downgraded to Error and the executed counts recomputed.
     *  \p progress (optional) receives (done, total) after each item;
     *  return false from it to stop pushing further items (already
     *  pushed items stay, as one undoable macro). */
    ImportPlan execute(const ImportPlan &plan,
                       const std::function<bool(int, int)> &progress = {});

private:
    GISVectorLayer *m_source = nullptr;
    SWMMModelLayer *m_target = nullptr;
    MapCanvas      *m_canvas = nullptr;
    ImportMapping   m_mapping;
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_FEATURELAYERIMPORTER_H
