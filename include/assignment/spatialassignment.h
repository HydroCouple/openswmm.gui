/**
 * @file      spatialassignment.h
 * @brief     Phase 3b (C8 / S7 GUI half): spatial assignment of scoped mesh rows and point
 *            sources.
 *
 * One service behind the spatial assignment dialog for the rows that follow the GLOBAL < TAG <
 * CELL scope model: `[2D_COVERAGES]`, `[2D_LOADINGS]`, `[2D_CURB_LENGTH]`,
 * `[2D_INITIAL_QUALITY]` and (as TAG rows) `[GW_INITIAL_QUALITY]`. Values come from the dialog's
 * manual, feature-layer and raster routes; this file turns them into reviewed rows, either one
 * CELL row per cell or one TAG row for cells that are tagged for it. Reading, writing,
 * read-back and restore of the rows go through the Phase 3a `Mesh2DSectionRowModel`, so the
 * section API is bound once. Point features become `[GW_SOURCES]` rows through
 * `previewGroundwaterTransport` (target PointSources).
 *
 * Preview functions are engine-free and may run on a worker thread; apply/undo run on the
 * engine's thread, check that nothing changed since the preview, and restore the previous rows
 * and tags when a write fails.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_SPATIALASSIGNMENT_H
#define OPENSWMMVIS_SPATIALASSIGNMENT_H

#include "mesh/meshresult.h"
#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include <openswmm/engine/openswmm_engine.h>

#include <QMap>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <functional>

namespace openswmmvis::assignment
{
using SectionRow = ui::Mesh2DSectionRowModel::Row;
using RowSection = ui::Mesh2DSectionRowModel::Section;

/** One section's rows plus the engine cell tags they resolve against. */
struct RowAssignmentSnapshot {
    RowSection section = RowSection::Coverages;
    QList<SectionRow> rows;
    QStringList cellTags; // engine [2D_TRIANGLES]/[2D_QUADS] TAG per cell
    QStringList landUses, names;
    int cellCount = 0;
    /** Rows, tags and cell count (not the registries) decide staleness. */
    bool operator==(const RowAssignmentSnapshot &o) const
    {
        return section == o.section && rows == o.rows && cellTags == o.cellTags &&
               cellCount == o.cellCount;
    }
};

/**
 * @brief Read one section's rows (through Mesh2DSectionRowModel) and every cell tag.
 * @return True on a complete read; false leaves @p out unchanged and sets @p error.
 */
bool readRowAssignmentSnapshot(SWMM_Engine engine, RowSection section, RowAssignmentSnapshot *out,
                               QString *error = nullptr);

/** Read every engine cell tag; empty on failure. */
QStringList engineCellTags(SWMM_Engine engine, QString *error = nullptr);

/**
 * @brief Name the sections whose TAG rows mention each tag ([2D_AQUIFER], the scoped
 * surface-quality and groundwater-transport rows, [GW_SOURCES]); used to say in a preview what a
 * retagged cell stops inheriting.
 */
QMap<QString, QStringList> tagReferences(SWMM_Engine engine);

enum class RowWriteStrategy {
    Automatic, ///< TAG when every assigned value is equal and no other tag is overwritten.
    CellRows,  ///< One CELL row per cell.
    TagRow     ///< Tag the cells and write one TAG row.
};

struct RowAssignmentRequest {
    RowAssignmentSnapshot before;
    QVector<int> cells;                     // distinct 0-based cells
    QVector<double> values;                 // value sections, parallel to cells; NaN = NoData
    QVector<QMap<QString, double>> percents; // [2D_COVERAGES]: land use -> percent; empty = NoData
    bool skipNoData = false;
    QString name; // species for loadings / initial quality
    int zone = 0; // [GW_INITIAL_QUALITY]: SAT 0, UNSAT 1
    RowWriteStrategy strategy = RowWriteStrategy::Automatic;
    QString tag;            // TAG name; empty = a generated unused name
    bool allowRetag = false; // allow moving cells out of another tag
    QMap<QString, QStringList> tagUsage; // tagReferences() result, for the conflict text
};

struct RowAssignmentPreview {
    RowAssignmentSnapshot before, after;
    bool tagRow = false;
    QString tag;
    QVector<int> cells, skippedCells, unchangedCells;
    QStringList oldValues, newValues; // display text, parallel to cells
    QVector<int> retagged;            // cells whose tag changes
    QStringList retaggedFrom;         // their previous tags, parallel to retagged
    int removedCellRows = 0;          // CELL rows a TAG row replaces
    QStringList notes;                // strategy choice and tag conflicts
    QString error;
};

/**
 * @brief Build reviewed rows for one section from per-cell values, without the engine.
 * @param[in] request Snapshot, cells, values/percents and the write strategy.
 * @param[in] cancelled Optional callback; true returns an error preview.
 * @return before/after snapshots; error set (and after == before) when refused.
 * @details CELL rows upsert by key and skip cells whose effective value is already equal. A TAG
 * row needs one value for every cell, refuses a tag that cells outside the scope carry, and
 * removes CELL rows of the same key that would hide it. Moving cells out of another tag is
 * refused unless allowRetag; Automatic then falls back to CELL rows and says why.
 */
RowAssignmentPreview previewRowAssignment(const RowAssignmentRequest &request,
                                          std::function<bool()> cancelled = {});

struct RowAssignmentResult {
    bool success = false, changed = false, rollbackComplete = true;
    QString error;
};
/**
 * @brief Write a preview's tags and rows after checking the engine still equals its before.
 * @param[in] beforeWrite Optional hook given a 1-based write ordinal (tag writes, then the row
 * commit); false injects a failure before that write.
 * @return success; on failure the previous tags and rows are restored and rollbackComplete says
 * whether that was verified. changed stays true once anything was written.
 */
RowAssignmentResult applyRowAssignment(SWMM_Engine engine, const RowAssignmentPreview &preview,
                                       std::function<bool(int)> beforeWrite = {});
/** Restore the before rows and tags when the engine still equals the preview's after. */
RowAssignmentResult undoRowAssignment(SWMM_Engine engine, const RowAssignmentPreview &preview);

/** Display text of a coverage percent set, e.g. "Urban 60 %, Grass 40 %". */
QString coverageText(const QMap<QString, double> &percents);

/**
 * @brief Parse a class lookup such as "1=Urban, 2=Grass" (case-insensitive land-use names).
 * @return class key -> land use; empty for blank text. Unknown land uses set @p error.
 */
QMap<QString, QString> parseClassLookup(const QString &text, const QStringList &landUses,
                                        QString *error);
/**
 * @brief Turn class area fractions (0..1 of the cell) into land-use percents.
 * @param[in] lookup Class -> land use; empty means a class key is itself a land-use name.
 * @param[out] unmapped Fraction of the cell covered by classes that map to no land use.
 */
QMap<QString, double> sharesToPercents(const QMap<QString, double> &fractions,
                                       const QMap<QString, QString> &lookup,
                                       const QStringList &landUses, double *unmapped);

/** Point features turned into [GW_SOURCES] rows. */
struct PointSourceJob {
    QString path, layerName, filter, assignedSourceCrsWkt, meshCrsWkt;
    bool filterBySelection = false;
    QSet<long long> selectedIds;
    QString nameField, flowField;
    QStringList termFields; // one per species-term template; empty = keep its constant
    double scale = 1, offset = 0; // applied to the flow field
    QVector<mesh::MeshVertex> vertices;
    QVector<mesh::MeshTriangle> cells;
    QSet<int> scope; // cells a source may land in
    bool byXy = false;
};
struct PointSourceFeature {
    long long fid = -1;
    QString name;
    QPointF xy;          // mesh CRS
    int cell = -1;       // containing cell (lowest index on a shared edge)
    int xyCell = -1;     // engine XY placement: nearest cell centroid
    bool sharedEdge = false;
    double flow = 0;
    QVector<double> termValues; // parallel to termFields (NaN where the field is empty)
    QString problem;            // non-empty: not assigned (outside mesh/scope, bad value)
};
struct PointSourceSample {
    bool pointLayer = false; // false: the layer is not points (caller samples polygons)
    QVector<PointSourceFeature> features;
    QString error;
    bool cancelled = false;
};
/**
 * @brief Read point features, transform them into the mesh CRS and place each in a cell.
 * @details Off the GUI thread; opens its own GDAL handles. A missing/invalid CRS refuses the job.
 */
PointSourceSample samplePointSources(const PointSourceJob &job,
                                     std::function<bool()> cancelled = {});

/** The engine's XY rule: index of the cell with the nearest area centroid (first on ties). */
int nearestCellCentroid(const QVector<mesh::MeshVertex> &vertices,
                        const QVector<mesh::MeshTriangle> &cells, const QPointF &p);
} // namespace openswmmvis::assignment

#endif
