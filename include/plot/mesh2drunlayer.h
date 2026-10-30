/**
 * @file      mesh2drunlayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice CF.3 — IRunLayer adapter wrapping a SWMM2DResultsLayer.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::plot
{

/*! \brief Saved-results cell extraction that runs on any thread.
 *
 *  Prepared on the GUI thread by Mesh2DRunLayer::takeExtractionJob(). It opens
 *  its own reader on \ref path and carries copies of the geometry it needs, so
 *  it shares no HDF5 handle or layer state with the GUI (the reader serializes
 *  the non-thread-safe HDF5 library itself). */
struct Mesh2DExtractionJob {
    QString path;
    QString fileRevision; ///< adapter cache key when prepared
    quint64 sourceRevision = 0;
    QVector<SeriesRequest> requests; ///< cell series, full history
    std::vector<float> zBed;         ///< empty when unavailable (HGL uses z = 0)
    std::vector<double> cellArea;    ///< m²; empty unless a rain depth/mean is requested
    std::vector<unsigned char> cellNv;
    double dryDepth = 0.0;
    int frames = 0; ///< frame count for progress

    /**
     * @brief Test whether the detached worker job has any queued requests.
     * @return True for an empty request vector; does not validate the path or geometry.
     */
    bool isEmpty() const { return requests.isEmpty(); }

    /*! \brief Extract every request into \p out. Checks \p cancel before each
     *  frame and counts frames read in \p framesDone. Returns false when
     *  cancelled or the file cannot be opened; \p out is then incomplete.
     * @param[out] out Replaced with one default response per job request, then populated in request order. A failed/cancelled run may retain partial data with ok still false.
     * @param[in] cancel Atomic flag checked before opening and before each frame; set from another thread to request cancellation.
     * @param[in,out] framesDone Relaxed atomic progress counter, incremented for each valid-time frame processed; not reset by run().
     * @return False for pre-cancellation, file-open/no-frame failure or cancellation during extraction; true means the frame loop completed, not that every series succeeded. Invalid cell requests receive per-response errors.
     * @note Opens its own HDF5 source and never accesses the originating layer. Caller must keep the immutable job and its geometry snapshots alive; this method does not itself verify that the file still matches fileRevision. acceptExtraction() verifies revision against the adapter later.
     */
    bool run(QVector<SeriesData> &out, const std::atomic<bool> &cancel,
             std::atomic<int> &framesDone) const;
};

class Mesh2DRunLayer final : public IRunLayer
{
public:
    /**
     * @brief Bind an existing mesh result layer without acquiring ownership.
     * @param[in] layer Optional borrowed QObject layer, guarded against destruction by QPointer. Cache/deferred-job operations belong on the GUI thread.
     * @note Mutable caches/queues are unsynchronized despite const query methods. Worker access is supported only through a detached Mesh2DExtractionJob.
     */
    explicit Mesh2DRunLayer(SWMM2DResultsLayer *layer);
    /**
     * @brief Release adapter caches without deleting the layer or stopping detached jobs owned by the host.
     */
    ~Mesh2DRunLayer() override = default;

    /**
     * @brief Inspect the guarded layer binding.
     * @return Borrowed result layer or null after destruction.
     */
    SWMM2DResultsLayer *layer() const { return m_layer.data(); }

    // IRunLayer
    /**
     * @brief Report guarded mesh-source metadata.
     * @return Layer name, or (2D layer closed) when missing.
     */
    QString scenarioName() const override;
    /**
     * @brief Report guarded mesh-source metadata.
     * @return SI unconditionally. Depth/HGL are metres; rainfall intensity/depth are converted to mm/hr and mm, while catalog variables retain metadata units.
     */
    UnitSystem unitSystem() const override;
    /**
     * @brief Report guarded mesh-source metadata.
     * @return First source timestamp converted to SWMM/OLE-Automation days, or NaN if unavailable/invalid.
     */
    double startDateJulian() const override;
    /**
     * @brief Report guarded mesh-source metadata.
     * @return Current source timeCount(), or zero if source/layer is unavailable.
     */
    int periodCount() const override;
    /**
     * @brief Report guarded mesh-source metadata.
     * @return Whole-second difference between the first two valid source times, or zero when unavailable. Unordered times are not rejected and can yield a negative step.
     */
    int reportStepSeconds() const override;
    /**
     * @brief Report guarded mesh-source metadata.
     * @return mesh2d:// followed by the current layer name, or empty when closed. This naming token is not a unique file/content identity.
     */
    QString persistenceKey() const override;

    /**
     * @brief Resolve one fixed mesh result, using saved-file caching or the direct live tail path.
     * @param[in] ref Cell identity, edge with local slot, or vertex for depth/HGL; source-local indices are validated by the applicable path.
     * @param[in] attr Fixed quantity compatible with ref.kind.
     * @param[in,out] out firstPeriod requests a clamped [firstPeriod,timeCount) tail; samples/ok/error are replaced, while direct-path early failures may leave a prior periodCount. Times are SWMM/OLE-Automation days.
     * @note Saved-file cell full-history requests can return Loading/Loading cancelled instead of data when deferral is enabled. Live tails read immediately. Invalid timestamps/unreadable cell frames are skipped; dry reconstruction/catalog status gaps can yield NaN values. ok denotes a completed tail or a nonempty timestamp vector, not that every value is finite.
     */
    void getSeriesAt(const ObjectRef &ref, PlotAttribute attr, SeriesData &out) const override;
    /*! Fixed attributes as before; a 2D catalog variable resolves through
     *  the batch path (cache, worker deferral and live tails included).
     * @param[in] ref Source-local mesh identity; catalog variables require a cell.
     * @param[in] descriptor Mesh variable key/metadata or fixed result. Species are refused by the base interface; fixed descriptors dispatch through the enum overload.
     * @param[in,out] out Tail cursor and response, following getSeriesBatch() semantics for catalog variables and the fixed overload otherwise.
     * @note Catalog values are not converted from their source units. Availability is resolved from the current source catalog at extraction time.
     */
    void getSeriesAt(const ObjectRef &ref, const ResultDescriptor &descriptor,
                     SeriesData &out) const override;

    /*! Mesh2DCell: the fixed cell attributes plus every time-varying face
     *  variable the source's catalog carries (groundwater terms,
     *  infiltration, coupling flux, 2D species) that no fixed attribute
     *  already covers. Other kinds: the fixed list.
     * @param[in] kind Object kind; only Mesh2DCell appends dynamic face variables.
     * @return Fixed-kind list, plus reported/held catalog variables with positive frameCount whose datasets are not already covered by fixed depth/head/velocity/rain quantities. Unknown units produce an empty unit string. Fixed choices are not filtered through supportsAttribute().
     */
    QVector<ResultDescriptor> resultDescriptorsForKind(ObjectRef::Kind kind) const override;

    /**
     * @param[in] attr Candidate fixed result identifier.
     * @return True for depth/HGL even without a current source; velocity/edge quantities require edge-flux availability and rainfall quantities their respective face datasets. Other fixed identifiers return false. Does not validate object indices, edge geometry or individual readable frames.
     */
    bool supportsAttribute(PlotAttribute attr) const override;
    /**
     * @param[in] requests Ordered object/descriptor/cursor requests; source/layer metadata must remain stable during the synchronous call.
     * @param[out] out Replaced with one response per request. Cell requests share dataset frame reads; edge/vertex/incompatible requests use direct per-series resolution.
     * @note Clamps negative/too-large cursors for reading without rewriting firstPeriod. Full saved-file cell histories use revision-keyed cache/deferred queues; live sources/tails bypass this cache. Direct cache growth has a 64 MiB numeric-buffer budget, while accepted worker results are retained without that eviction limit.
     * @note Catalog invalid statuses become NaN gaps; unreadable frames are skipped. Rainfall means use prior readable cumulative samples and can span a missing frame. A zero-length clamped tail succeeds empty, and missing/unsupported requests report individual errors. No source mutation occurs but adapter caches/queues change.
     */
    void getSeriesBatch(const QVector<SeriesRequest> &requests,
                        QVector<SeriesData> &out) const override;

    /*! \brief Interactive consumers (the comparison plot) turn this on so an
     *  uncached saved-file cell series is queued for a worker instead of read
     *  on the calling thread; it resolves as "Loading…" until accepted.
     *  Live sources, tails and vertex/edge series are always read directly.
     * @param[in] on Whether future uncached saved-HDF5 cell requests with firstPeriod exactly zero should queue instead of reading synchronously.
     * @note Changes only the policy flag; does not dispatch jobs, clear existing queues/cache or cancel in-flight work. GUI thread only.
     */
    void setDeferFileReads(bool on) { m_deferFileReads = on; }
    /**
     * @brief Inspect the saved-file full-history deferral policy.
     * @return Stored flag, initially false; live/tail/edge/vertex queries ignore it.
     */
    bool deferFileReads() const { return m_deferFileReads; }

    /*! \brief Move the queued series into a job (GUI thread). They stay
     *  "Loading…" until \ref acceptExtraction or \ref cancelExtraction.
     * @return Owned path/request/geometry snapshot, empty when no saved-HDF5 source or no queue exists. Queued requests move into in-flight state; no thread is started here.
     * @note Revalidates revision first, potentially dropping stale cache/queues. Request order follows the cell/token ordered set. Worker creation can trigger synchronous geometry-cache reads. Host must run, then accept/cancel the job on the GUI thread.
     */
    Mesh2DExtractionJob takeExtractionJob();
    /*! \brief Cache a finished job's results (GUI thread). Returns false and
     *  drops them when the source changed after the job was prepared.
     * @param[in] job Job snapshot originally prepared by this adapter.
     * @param[in] results Request-order responses of exactly job.requests.size(); failures are cached too.
     * @return False for an empty/stale job or count mismatch after cache revision validation. True installs uncached entries and removes their in-flight keys, without emitting signals. Existing cached entries win and numeric memory is counted without eviction.
     * @note Does not inspect cancellation status or enforce that every response is successful. Host should pass only its intended completed result set and then refresh views.
     */
    bool acceptExtraction(const Mesh2DExtractionJob &job, const QVector<SeriesData> &results);
    /*! \brief A cancelled job's series resolve as "Loading cancelled" until
     *  \ref retryCancelled queues them again.
     * @param[in] job Prepared snapshot whose in-flight requests should become cancelled. Stale revisions are ignored after cache validation.
     * @note Moves only keys still in flight into cancelled state, giving Loading cancelled on future deferred requests. Does not signal a worker atomic cancellation flag; the host controls that separately.
     */
    void cancelExtraction(const Mesh2DExtractionJob &job);
    /**
     * @brief Clear cancellation refusals so the next deferred query can queue them again.
     * @note Does not itself enqueue requests, start a worker, validate source revision or emit notifications. GUI thread only.
     */
    void retryCancelled();
    /**
     * @brief Inspect retained cancellation keys.
     * @return Whether the cancelled set is nonempty without first validating the current source revision; later queries can clear stale state.
     */
    bool hasCancelled() const { return !m_cancelled.empty(); }

private:
    /*! (cell, token): the token is the PlotAttribute number for a fixed
     *  attribute, or "v:" + variable key for a 2D catalog variable. */
    using SeriesKey = std::pair<int, QString>;
    /**
     * @brief Encode the descriptor portion of a per-cell cache key.
     * @param[in] d Mesh catalog or fixed descriptor; caller excludes species.
     * @return v: plus exact variable key for a mesh variable, otherwise decimal fixed-attribute enum. Display metadata does not enter identity.
     */
    static QString seriesToken_(const ResultDescriptor &d);
    /*! Descriptor for each catalog-variable token seen, so a queued key can
     *  be turned back into a request (takeExtractionJob). */
    mutable std::map<QString, ResultDescriptor> m_variableDescs;
    /**
     * @brief Resolve a fixed mesh/edge/vertex tail synchronously without per-series cache/queue handling.
     * @param[in] ref Source-local cell/edge/vertex identity.
     * @param[in] attr Compatible fixed result. Vertex supports only depth/HGL; edges only flow/unit-width flux.
     * @param[in,out] out Preserves firstPeriod; clears samples/error/ok and sets periodCount after validating available periods. An exhausted tail succeeds empty.
     * @note Cell depth uses scalar reads; HGL adds mean cell vertex Z (zero fallback if unavailable). Vertex depth uses incident-cell weights one for triangles, 0.75 for quads, and HGL adds vertex Z. Edge flow is m3/s and unit-width flux divides by static edge length to m2/s; invalid/read-failed edge/vertex samples retain NaN gaps at valid times. No ownership transfer occurs.
     */
    void getSeriesAtUncached(const ObjectRef &ref, PlotAttribute attr, SeriesData &out) const;
    /**
     * @brief Drop stale per-series/cache/queue state when layer/source or saved-file revision changes.
     * @note Compares sourceRevision and, for HDF5, absolute path/size/mtime/timeCount/historyGeneration/dryDepth. A change clears numeric cache and pending/in-flight/cancelled sets and marks geometry caches unready. Does not cancel an already detached worker or clear remembered variable descriptors.
     */
    void validateSourceCache_() const;
    bool m_deferFileReads = false;
    // Cleared with the cache when the source changes (validateSourceCache_).
    mutable std::set<SeriesKey> m_deferred; ///< queued, not yet in a job
    mutable std::set<SeriesKey> m_inFlight; ///< in a job not yet accepted
    mutable std::set<SeriesKey> m_cancelled;
    mutable quint64 m_sourceRevision = ~quint64(0);
    mutable QString m_fileRevision;
    mutable std::map<SeriesKey, SeriesData> m_seriesCache;
    mutable std::size_t m_cacheBytes = 0;
    /*! \brief Cached bed elevation per triangle = mean of vertex z's, and
     *  planimetric cell area (m², shoelace over the cell's SI vertices).
     * @note Lazily reads SI source cells and vertex elevations. Z is the arithmetic mean of 3/4 vertex values, treating unavailable Z indices as zero; areas sum absolute triangle-fan areas in square metres. Failure leaves the cache unready for a later retry. Requires stable consistent X/Y coordinate arrays; no source ownership is retained.
     */
    void ensureZBedCache_() const;

    /*! \brief Cached per-vertex incident-triangle adjacency + per-vertex bed
     *  elevation, built once from the source geometry. Used to interpolate a
     *  depth/HGL time series at a mesh vertex (mean of the triangles touching
     *  it).
     * @note Lazily reads source cells/vertex Z and caches valid vertex-to-cell incidence, cell vertex counts and float-copied Z. Read failure leaves the cache unready for retry. Cache invalidation is performed by validateSourceCache_(), not this helper.
     */
    void ensureVertexAdjCache_() const;

    /*! \brief Reconstruct (Vx, Vy) for one cell from its \p nv edge fluxes
     *  (3 for a triangle, 4 for a quad; slots \c mesh::edgeSlot(cell, e)) and
     *  time-invariant edge geometry using the closed-form RT0 least-squares
     *  solve. Returns false (NaN-filled) for dry cells.
     * @param[in] triIdx Nonnegative cell index in fixed-stride edge arrays.
     * @param[in] nv Cell edge count, three or four.
     * @param[in] flux Per-cell edge-slot volumetric fluxes as supplied by the source.
     * @param[in] edge_len Parallel strictly positive edge lengths.
     * @param[in] edge_nx Parallel outward-normal X components.
     * @param[in] edge_ny Parallel outward-normal Y components.
     * @param[in] depth Cell water depth, used only for comparison with dryDepth.
     * @param[in] dryDepth Dry cutoff in the same depth units.
     * @param[out] vx_out X normal-equation solution, initialized NaN on entry.
     * @param[out] vy_out Y solution, likewise NaN on rejected input.
     * @return False for depth below cutoff, invalid edge count, short arrays, nonpositive lengths or near-singular normal matrix. True after solving the two-component least-squares system; nonfinite inputs are not comprehensively rejected.
     * @note The implemented right-hand side is flux/edge_len clamped to [-10,10], and depth is not a divisor. Output units therefore follow that quotient even though callers present the result as velocity.
     */
    static bool reconstructVelocityAtCell_(int triIdx, int nv, const std::vector<float> &flux,
                                           const std::vector<float> &edge_len,
                                           const std::vector<float> &edge_nx,
                                           const std::vector<float> &edge_ny, double depth,
                                           double dryDepth, double &vx_out, double &vy_out);

    /*! \brief The frame loop shared by the direct batch path and
     *  Mesh2DExtractionJob: every request in \p pending is a valid cell
     *  series on \p src; each needed frame of each dataset is read once.
     * @param[in] src Stable source with cell catalog and timestamp/frame reads. Caller serializes its non-thread-safe access as required.
     * @param[in] requests All ordered requests; pending entries must have valid cell indices and supported fixed/catalog descriptors.
     * @param[in] pending Indices into requests/out to process, copied for filtering.
     * @param[in] zBed Optional cell bed means in metres; missing entries use zero for HGL.
     * @param[in] cellArea Optional positive cell plan areas in m2, required for rain depth/mean.
     * @param[in] cellNv Optional per-cell edge counts; missing entries default to three.
     * @param[in] dryDepth Reconstruction dryness threshold in metres.
     * @param[in,out] out Pre-sized to requests with cleared samples/errors/ok for pending entries. Appends values/time pairs and updates firstPeriod/periodCount; completed nonempty series get ok true even with all-NaN values.
     * @param[in] cancel Optional relaxed atomic cancellation flag, checked before each frame.
     * @param[in,out] framesDone Optional relaxed progress counter incremented per valid-time processed frame, without reset.
     * @return False only on observed cancellation, retaining partial unfinished output. True for loop completion including per-request dataset/key/geometry refusals. Catalog data is raw metadata units, rain rate/depth becomes mm/hr/mm, volume m3, depth/HGL metres. No common source-generation consistency check is made in the loop.
     */
    static bool extractCellFrames_(IMesh2DSource &src, const QVector<SeriesRequest> &requests,
                                   QVector<int> pending, const std::vector<float> &zBed,
                                   const std::vector<double> &cellArea,
                                   const std::vector<unsigned char> &cellNv, double dryDepth,
                                   QVector<SeriesData> &out, const std::atomic<bool> *cancel,
                                   std::atomic<int> *framesDone);
    friend struct Mesh2DExtractionJob;

    QPointer<SWMM2DResultsLayer> m_layer;
    mutable std::vector<float> m_zBed;      ///< [cellCount], cached on first use.
    mutable std::vector<double> m_cellArea; ///< [cellCount] m², with m_zBed.
    mutable bool m_zBedReady = false;

    mutable std::vector<std::vector<int>> m_vertexTris; ///< [vtxCount] incident CELL indices.
    mutable std::vector<unsigned char>
        m_cellNv;                         ///< [cellCount] vertex count (3|4), with m_vertexTris.
    mutable std::vector<float> m_vertexZ; ///< [vtxCount] bed elevation at the vertex.
    mutable bool m_vertexAdjReady = false;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_MESH2DRUNLAYER_H
