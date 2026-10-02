/*!
 * \file   mesh2drunlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice CF.3 — IRunLayer adapter wrapping a SWMM2DResultsLayer.
 *
 * Lets the Comparison Plot Dialog treat a 2D mesh results layer as a
 * first-class RunSource, identical in surface to a 1D `.out` adapter.
 * The model holds a `shared_ptr<IRunLayer>`; the adapter holds a
 * `QPointer<SWMM2DResultsLayer>` and resolves it on every call so a
 * project-close cleanly invalidates outstanding plot series.
 *
 * Series resolution per attribute:
 *   - Mesh2DDepth      → IMesh2DSource::readDepthsAt over all time idx
 *   - Mesh2DHGL        → depth + z_bed (mean of triangle vertex z's,
 *                        cached on the layer)
 *   - Mesh2DVelocityX  → RT0 reconstruction from edge fluxes
 *   - Mesh2DVelocityY  → "
 *   - Mesh2DVelocityMag→ sqrt(Vx² + Vy²)
 *   - Mesh2DRainfall   → /Mesh2_face_rainfall (m/s → mm/hr)
 *   - Mesh2DRainVolume → /Mesh2_face_rain_cum (cumulative m³ per cell)
 *   - Mesh2DRainDepth  → rain_cum ÷ cell area (m → mm)
 *   - Mesh2DRainfallAvg→ Δrain_cum ÷ (cell area · Δt) over the preceding
 *                        interval (m/s → mm/hr); the first frame has no
 *                        interval and yields no sample
 *
 * Time axis: simulated wall-clock times are pulled from the source's
 * `simTimeAt(timeIdx)`. The adapter converts back to SWMM Julian days
 * for the model's common timeline.
 */
#ifndef OPENSWMMVIS_PLOT_MESH2DRUNLAYER_H
#define OPENSWMMVIS_PLOT_MESH2DRUNLAYER_H

#include "plot/irunlayer.h"

#include <QPointer>
#include <atomic>
#include <map>
#include <set>

class SWMM2DResultsLayer;
class IMesh2DSource;

namespace openswmmvis::plot {

/*! \brief Saved-results cell extraction that runs on any thread.
 *
 *  Prepared on the GUI thread by Mesh2DRunLayer::takeExtractionJob(). It opens
 *  its own reader on \ref path and carries copies of the geometry it needs, so
 *  it shares no HDF5 handle or layer state with the GUI (the reader serializes
 *  the non-thread-safe HDF5 library itself). */
struct Mesh2DExtractionJob {
    QString                    path;
    QString                    fileRevision;   ///< adapter cache key when prepared
    quint64                    sourceRevision = 0;
    QVector<SeriesRequest>     requests;       ///< cell series, full history
    std::vector<float>         zBed;           ///< empty when unavailable (HGL uses z = 0)
    std::vector<double>        cellArea;       ///< m²; empty unless a rain depth/mean is requested
    std::vector<unsigned char> cellNv;
    double                     dryDepth = 0.0;
    int                        frames   = 0;   ///< frame count for progress

    bool isEmpty() const { return requests.isEmpty(); }

    /*! \brief Extract every request into \p out. Checks \p cancel before each
     *  frame and counts frames read in \p framesDone. Returns false when
     *  cancelled or the file cannot be opened; \p out is then incomplete. */
    bool run(QVector<SeriesData>& out, const std::atomic<bool>& cancel,
             std::atomic<int>& framesDone) const;
};

class Mesh2DRunLayer final : public IRunLayer
{
public:
    explicit Mesh2DRunLayer(SWMM2DResultsLayer *layer);
    ~Mesh2DRunLayer() override = default;

    SWMM2DResultsLayer *layer() const { return m_layer.data(); }

    // IRunLayer
    QString    scenarioName()      const override;
    UnitSystem unitSystem()        const override;
    double     startDateJulian()   const override;
    int        periodCount()       const override;
    int        reportStepSeconds() const override;
    QString    persistenceKey()    const override;

    void getSeriesAt(const ObjectRef& ref,
                     PlotAttribute attr,
                     SeriesData& out) const override;
    /*! Fixed attributes as before; a 2D catalog variable resolves through
     *  the batch path (cache, worker deferral and live tails included). */
    void getSeriesAt(const ObjectRef& ref,
                     const ResultDescriptor& descriptor,
                     SeriesData& out) const override;

    /*! Mesh2DCell: the fixed cell attributes plus every time-varying face
     *  variable the source's catalog carries (groundwater terms,
     *  infiltration, coupling flux, 2D species) that no fixed attribute
     *  already covers. Other kinds: the fixed list. */
    QVector<ResultDescriptor> resultDescriptorsForKind(ObjectRef::Kind kind) const override;

    bool supportsAttribute(PlotAttribute attr) const override;
    void getSeriesBatch(const QVector<SeriesRequest>& requests,
                        QVector<SeriesData>& out) const override;

    /*! \brief Interactive consumers (the comparison plot) turn this on so an
     *  uncached saved-file cell series is queued for a worker instead of read
     *  on the calling thread; it resolves as "Loading…" until accepted.
     *  Live sources, tails and vertex/edge series are always read directly. */
    void setDeferFileReads(bool on) { m_deferFileReads = on; }
    bool deferFileReads() const { return m_deferFileReads; }

    /*! \brief Move the queued series into a job (GUI thread). They stay
     *  "Loading…" until \ref acceptExtraction or \ref cancelExtraction. */
    Mesh2DExtractionJob takeExtractionJob();
    /*! \brief Cache a finished job's results (GUI thread). Returns false and
     *  drops them when the source changed after the job was prepared. */
    bool acceptExtraction(const Mesh2DExtractionJob& job, const QVector<SeriesData>& results);
    /*! \brief A cancelled job's series resolve as "Loading cancelled" until
     *  \ref retryCancelled queues them again. */
    void cancelExtraction(const Mesh2DExtractionJob& job);
    void retryCancelled();
    bool hasCancelled() const { return !m_cancelled.empty(); }

private:
    /*! (cell, token): the token is the PlotAttribute number for a fixed
     *  attribute, or "v:" + variable key for a 2D catalog variable. */
    using SeriesKey = std::pair<int, QString>;
    static QString seriesToken_(const ResultDescriptor& d);
    /*! Descriptor for each catalog-variable token seen, so a queued key can
     *  be turned back into a request (takeExtractionJob). */
    mutable std::map<QString, ResultDescriptor> m_variableDescs;
    void getSeriesAtUncached(const ObjectRef& ref, PlotAttribute attr, SeriesData& out) const;
    void validateSourceCache_() const;
    bool m_deferFileReads = false;
    // Cleared with the cache when the source changes (validateSourceCache_).
    mutable std::set<SeriesKey> m_deferred;    ///< queued, not yet in a job
    mutable std::set<SeriesKey> m_inFlight;    ///< in a job not yet accepted
    mutable std::set<SeriesKey> m_cancelled;
    mutable quint64 m_sourceRevision = ~quint64(0);
    mutable QString m_fileRevision;
    mutable std::map<SeriesKey, SeriesData> m_seriesCache;
    mutable std::size_t m_cacheBytes = 0;
    /*! \brief Cached bed elevation per triangle = mean of vertex z's, and
     *  planimetric cell area (m², shoelace over the cell's SI vertices). */
    void ensureZBedCache_() const;

    /*! \brief Cached per-vertex incident-triangle adjacency + per-vertex bed
     *  elevation, built once from the source geometry. Used to interpolate a
     *  depth/HGL time series at a mesh vertex (mean of the triangles touching
     *  it). */
    void ensureVertexAdjCache_() const;

    /*! \brief Reconstruct (Vx, Vy) for one cell from its \p nv edge fluxes
     *  (3 for a triangle, 4 for a quad; slots \c mesh::edgeSlot(cell, e)) and
     *  time-invariant edge geometry using the closed-form RT0 least-squares
     *  solve. Returns false (NaN-filled) for dry cells. */
    static bool reconstructVelocityAtCell_(int triIdx, int nv,
                                           const std::vector<float>& flux,
                                           const std::vector<float>& edge_len,
                                           const std::vector<float>& edge_nx,
                                           const std::vector<float>& edge_ny,
                                           double depth,
                                           double dryDepth,
                                           double& vx_out,
                                           double& vy_out);

    /*! \brief The frame loop shared by the direct batch path and
     *  Mesh2DExtractionJob: every request in \p pending is a valid cell
     *  series on \p src; each needed frame of each dataset is read once. */
    static bool extractCellFrames_(IMesh2DSource& src,
                                   const QVector<SeriesRequest>& requests,
                                   QVector<int> pending,
                                   const std::vector<float>& zBed,
                                   const std::vector<double>& cellArea,
                                   const std::vector<unsigned char>& cellNv,
                                   double dryDepth,
                                   QVector<SeriesData>& out,
                                   const std::atomic<bool>* cancel,
                                   std::atomic<int>* framesDone);
    friend struct Mesh2DExtractionJob;

    QPointer<SWMM2DResultsLayer> m_layer;
    mutable std::vector<float>   m_zBed;     ///< [cellCount], cached on first use.
    mutable std::vector<double>  m_cellArea; ///< [cellCount] m², with m_zBed.
    mutable bool                 m_zBedReady = false;

    mutable std::vector<std::vector<int>> m_vertexTris;  ///< [vtxCount] incident CELL indices.
    mutable std::vector<unsigned char>    m_cellNv;      ///< [cellCount] vertex count (3|4), with m_vertexTris.
    mutable std::vector<float>            m_vertexZ;     ///< [vtxCount] bed elevation at the vertex.
    mutable bool                          m_vertexAdjReady = false;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_MESH2DRUNLAYER_H
