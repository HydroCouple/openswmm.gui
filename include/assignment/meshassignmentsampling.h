/**
 * @file      meshassignmentsampling.h
 * @brief     Shared raster and vector sampling for mesh parameter assignments.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_MESHASSIGNMENTSAMPLING_H
#define OPENSWMMVIS_MESHASSIGNMENTSAMPLING_H
#include "mesh/meshinfil.h"
#include "mesh/naturalnbinterpolator.h"
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QPromise>
#include <functional>
namespace openswmmvis::assignment
{
struct MeshAssignmentSampling {
    /*! \brief Which source the dialog opens on. */
    enum class Source { Raster, Vector };

    /*! \brief What the assignment writes. */
    enum class Mode {
        SingleNumeric,   ///< One source value → one numeric cell parameter.
        MultiNumeric,    ///< N source values → N numeric cell parameters.
        ClassifiedInfil, ///< Key (or key pair) → a whole mesh::InfilRow.
        SingleCategory   ///< One categorical key per cell (e.g. snowpack class).
    };

    /*! \brief How one cell's source value is obtained. */
    enum class Sampling {
        Centroid,         ///< Point sample at the cell centroid (original).
        OverlayAuto,      ///< Majority or area-weighted mean, by source type.
        AreaWeightedMean, ///< Mean over the cell's footprint (continuous).
        Majority,         ///< Largest covering share wins (categorical).
        NaturalNeighbour  ///< Sibson / Laplace over scattered points.
    };

    /*! \brief Where the assignment lands (GUI plan §3.4 "Write as", engine D-I3). */
    enum class WriteTarget {
        CellOverrides, ///< Per-cell [2D_INFILTRATION] override rows.
        RegionDefaults ///< One [2D_INFILTRATION_DEFAULTS] row per region tag.
    };

    // -----------------------------------------------------------------------
    // Data exchanged with the sampling worker.
    //
    // Both structs are plain data: no QObject, no layer pointer, no GDAL
    // handle. collectJob() fills the Job on the GUI thread; the worker
    // re-opens every source by path on its own thread.
    // -----------------------------------------------------------------------

    /*! \brief Everything one sampling pass needs. */
    struct Job {
        Mode mode = Mode::SingleNumeric;
        Source source = Source::Raster;
        Sampling sampling = Sampling::Centroid;

        /*! Cells in scope. \ref centroids is parallel to it; \ref triVerts
         *  holds 4 corners per entry — a quad's four, or a triangle's three
         *  with the first repeated in slot 3 (only filled for overlay
         *  sampling). */
        QVector<int> triangles;
        QVector<QPointF> centroids;
        QVector<QPointF> triVerts;

        bool strictCrs = false;      // Refuse missing CRS and any failed transform.
        bool rejectOverlaps = false; // Groundwater refuses ambiguous coverage.
        QString assignedSourceCrsWkt;
        QString meshCrsWkt; ///< Mesh layer CRS; "" = assume source CRS.

        // ---- Raster source ----
        QString rasterPath;
        QVector<int> bands; ///< Parallel to \ref targetKeys (numeric modes).
        int keyBand1 = 1;   ///< Classified mode: band supplying key 1.
        int keyBand2 = 0;   ///< Classified mode: 0 = single key.
        double scale = 1.0;
        double offset = 0.0;

        // ---- Vector source ----
        QString vectorPath;
        QString vectorLayerName;
        QString vectorFilterExpr;
        QStringList fields; ///< Parallel to \ref targetKeys (numeric modes).
        QString keyField1;  ///< Classified mode.
        QString keyField2;  ///< Classified mode; "" = single key.
        bool filterBySelection = false;
        QSet<long long> selectedIds;

        // ---- Numeric targets ----
        QVector<QByteArray> targetKeys;
        QVector<double> targetMin; ///< Parallel to \ref targetKeys.
        QVector<double> targetMax; ///< Parallel to \ref targetKeys.

        // ---- Classified infiltration ----
        mesh::InfilLookupTable table;

        // ---- Natural neighbour ----
        mesh::NaturalNeighbourInterpolator::Variant nnVariant =
            mesh::NaturalNeighbourInterpolator::Variant::Sibson;
    };

    /*! \brief Outcome of one sampling pass over the in-scope cells. */
    struct SampleResult {
        QVector<int> triangles; ///< Cells that received a value.
        /*! Numeric modes: one row per target key, each parallel to
         *  \ref triangles. NaN = that target got nothing for that cell. */
        QVector<QVector<double>> values;
        /*! Classified mode: parallel to \ref triangles. */
        QVector<mesh::InfilRow> rows;
        /*! Classified mode: the source key text each cell resolved to,
         *  parallel to \ref triangles. Drives the tag-correspondence check. */
        QStringList keys;

        int skippedNoData = 0;     ///< NoData / outside the source.
        int skippedNonNumeric = 0; ///< Field value not a number.
        int skippedRange = 0;      ///< Outside the parameter's range.
        int unmatchedKeys = 0;     ///< Classified: fell through to the fallback row.
        int scanned = 0;           ///< Cells considered.
        bool cancelled = false;
        /*! What Sampling::OverlayAuto resolved to, and why — reported so the
         *  user can see that a categorical source got majority rather than a
         *  meaningless average. Empty for the explicit choices. */
        QString resolvedSampling;
        QString error; ///< Non-empty aborts the pass.
    };
};
MeshAssignmentSampling::
    SampleResult
    /**
     * @brief Run a self-contained raster/vector sampling job on the calling thread.
     * @param[in] job Value-only job captured from GUI state; source paths are reopened locally and no QObject or engine handle is used. Bands are one-based; cell IDs and geometry arrays must stay index-aligned.
     * @param[in] cancelled Optional synchronous callback; true requests cancellation.
     * @param[in] progress Optional synchronous callback receiving processed and total cell counts; GUI callers must marshal UI updates to the GUI thread.
     * @return Sampled values in source units after scale/offset and target range checks, classification rows, skip counts and diagnostics. Nonempty error or cancelled prevents applying results.
     * @details May run on a worker thread. Each invocation owns its GDAL datasets and coordinate transforms; source files must remain accessible during the call.
     */
    sampleMeshAssignment(const MeshAssignmentSampling::Job &job,
                         std::function<bool()> cancelled = {},
                         std::function<void(int, int)> progress = {});
/**
 * @brief Adapt synchronous sampling to a Qt concurrent promise.
 * @param[in,out] promise Receives one SampleResult plus progress updates; cancellation is polled through isCanceled(). Caller owns promise lifetime.
 * @param[in] job Copied job whose paths and geometry are independent of GUI objects.
 * @details Intended for a worker thread; publishes a result even when sampling reports cancellation or an error.
 */
void runMeshAssignmentSampling(QPromise<MeshAssignmentSampling::SampleResult> &promise,
                               MeshAssignmentSampling::Job job);
}
#endif
