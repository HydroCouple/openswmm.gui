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
namespace openswmmvis::assignment {
struct MeshAssignmentSampling {
    /*! \brief Which source the dialog opens on. */
    enum class Source { Raster, Vector };

    /*! \brief What the assignment writes. */
    enum class Mode {
        SingleNumeric,     ///< One source value → one numeric cell parameter.
        MultiNumeric,      ///< N source values → N numeric cell parameters.
        ClassifiedInfil    ///< Key (or key pair) → a whole mesh::InfilRow.
    };

    /*! \brief How one cell's source value is obtained. */
    enum class Sampling {
        Centroid,          ///< Point sample at the cell centroid (original).
        OverlayAuto,       ///< Majority or area-weighted mean, by source type.
        AreaWeightedMean,  ///< Mean over the cell's footprint (continuous).
        Majority,          ///< Largest covering share wins (categorical).
        NaturalNeighbour   ///< Sibson / Laplace over scattered points.
    };

    /*! \brief Where the assignment lands (GUI plan §3.4 "Write as", engine D-I3). */
    enum class WriteTarget {
        CellOverrides,     ///< Per-cell [2D_INFILTRATION] override rows.
        RegionDefaults     ///< One [2D_INFILTRATION_DEFAULTS] row per region tag.
    };

    // -----------------------------------------------------------------------
    // Data exchanged with the sampling worker.
    //
    // Both structs are plain data: no QObject, no layer pointer, no GDAL
    // handle. collectJob() fills the Job on the GUI thread; the worker
    // re-opens every source by path on its own thread.
    // -----------------------------------------------------------------------

    /*! \brief Everything one sampling pass needs. */
    struct Job
    {
        Mode     mode     = Mode::SingleNumeric;
        Source   source   = Source::Raster;
        Sampling sampling = Sampling::Centroid;

        /*! Cells in scope. \ref centroids is parallel to it; \ref triVerts
         *  holds 4 corners per entry — a quad's four, or a triangle's three
         *  with the first repeated in slot 3 (only filled for overlay
         *  sampling). */
        QVector<int>     triangles;
        QVector<QPointF> centroids;
        QVector<QPointF> triVerts;

        bool strictCrs=false; // Refuse missing CRS and any failed transform.
        bool rejectOverlaps=false; // Groundwater refuses ambiguous coverage.
        QString assignedSourceCrsWkt;
        QString meshCrsWkt;          ///< Mesh layer CRS; "" = assume source CRS.

        // ---- Raster source ----
        QString      rasterPath;
        QVector<int> bands;          ///< Parallel to \ref targetKeys (numeric modes).
        int          keyBand1 = 1;   ///< Classified mode: band supplying key 1.
        int          keyBand2 = 0;   ///< Classified mode: 0 = single key.
        double       scale    = 1.0;
        double       offset   = 0.0;

        // ---- Vector source ----
        QString         vectorPath;
        QString         vectorLayerName;
        QString         vectorFilterExpr;
        QStringList     fields;      ///< Parallel to \ref targetKeys (numeric modes).
        QString         keyField1;   ///< Classified mode.
        QString         keyField2;   ///< Classified mode; "" = single key.
        bool            filterBySelection = false;
        QSet<long long> selectedIds;

        // ---- Numeric targets ----
        QVector<QByteArray> targetKeys;
        QVector<double>     targetMin;   ///< Parallel to \ref targetKeys.
        QVector<double>     targetMax;   ///< Parallel to \ref targetKeys.

        // ---- Classified infiltration ----
        mesh::InfilLookupTable table;

        // ---- Natural neighbour ----
        mesh::NaturalNeighbourInterpolator::Variant nnVariant =
            mesh::NaturalNeighbourInterpolator::Variant::Sibson;
    };

    /*! \brief Outcome of one sampling pass over the in-scope cells. */
    struct SampleResult
    {
        QVector<int> triangles;             ///< Cells that received a value.
        /*! Numeric modes: one row per target key, each parallel to
         *  \ref triangles. NaN = that target got nothing for that cell. */
        QVector<QVector<double>> values;
        /*! Classified mode: parallel to \ref triangles. */
        QVector<mesh::InfilRow> rows;
        /*! Classified mode: the source key text each cell resolved to,
         *  parallel to \ref triangles. Drives the tag-correspondence check. */
        QStringList keys;

        int  skippedNoData     = 0;  ///< NoData / outside the source.
        int  skippedNonNumeric = 0;  ///< Field value not a number.
        int  skippedRange      = 0;  ///< Outside the parameter's range.
        int  unmatchedKeys     = 0;  ///< Classified: fell through to the fallback row.
        int  scanned           = 0;  ///< Cells considered.
        bool cancelled         = false;
        /*! What Sampling::OverlayAuto resolved to, and why — reported so the
         *  user can see that a categorical source got majority rather than a
         *  meaningless average. Empty for the explicit choices. */
        QString resolvedSampling;
        QString error;               ///< Non-empty aborts the pass.
    };

};
MeshAssignmentSampling::SampleResult sampleMeshAssignment(
    const MeshAssignmentSampling::Job &job,std::function<bool()> cancelled={},
    std::function<void(int,int)> progress={});
void runMeshAssignmentSampling(QPromise<MeshAssignmentSampling::SampleResult>&,
                              MeshAssignmentSampling::Job);
}
#endif
