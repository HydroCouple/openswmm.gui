/*!
 * \file   swmm2dresultslayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice CF.MVP — renders 2D surface-routing depth as a colour-mapped overlay
 * on the canvas. The layer is data-source agnostic: during a run the source
 * is an `EngineMesh2DSource` driven by SimulationRunner ticks; after the run
 * the source swaps to an `HDF5Mesh2DSource` reading the engine's CF/UGRID
 * HDF5 file so the user can scrub a time slider back to peak inundation.
 *
 * Rendering reuses the `SceneTri` paint pattern from `SWMM2DMeshLayer`
 * (Slice AU). The only differences:
 *   - Per-triangle colour comes from depth, not elevation, via the
 *     inundation colour ramp.
 *   - Dry cells (depth < DRY_DEPTH) draw with alpha = 0 so the underlying
 *     SWMM2DMeshLayer terrain shows through.
 *   - No hillshade pass — the wet "sheen" is intentionally flat colour.
 */
#ifndef OPENSWMMVIS_LAYERS_SWMM2DRESULTSLAYER_H
#define OPENSWMMVIS_LAYERS_SWMM2DRESULTSLAYER_H

#include "layers/openswmmvislayer.h"
#include "map/mapextent.h"

#include <QDateTime>
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include <array>
#include <memory>
#include <vector>

class QGraphicsScene;
class QGraphicsItem;
class SWMM2DResultsGraphicsItem;
class SWMM2DVelocityArrowsItem;

namespace openswmmvis::io { class Mesh2DH5Reader; }
namespace OpenSWMM::Render { class IFeatureRenderer; }

// ---------------------------------------------------------------------------
// IMesh2DSource — data-source abstraction shared by live + replay modes
// ---------------------------------------------------------------------------

/*!
 * \brief Abstract source of mesh geometry + per-triangle depth time series.
 *
 * Two concrete implementations live in this header/.cpp pair:
 *  - `EngineMesh2DSource` — live, fed by SimulationRunner each progress tick.
 *  - `HDF5Mesh2DSource`   — post-run, reads the engine's CF/UGRID HDF5.
 */
class IMesh2DSource
{
public:
    virtual ~IMesh2DSource() = default;

    /*! \brief Geometry counts. Stable for the lifetime of the source. */
    virtual int vertexCount()   const = 0;
    virtual int triangleCount() const = 0;

    /*! \brief Latest known time-step count (grows during live mode). */
    virtual int timeCount() const = 0;

    /*! \brief Fetch mesh geometry. Resizes outputs. */
    virtual bool readMeshGeometry(std::vector<double>& vx,
                                   std::vector<double>& vy,
                                   std::vector<double>& vz,
                                   std::vector<std::array<int, 3>>& tris) = 0;

    /*! \brief Fetch per-triangle depth at \p timeIdx. Resizes \p depths to triangleCount(). */
    virtual bool readDepthsAt(int timeIdx, std::vector<float>& depths) = 0;

    /*! \brief Wall-clock sim time at \p timeIdx (invalid if out of range or unknown). */
    virtual QDateTime simTimeAt(int timeIdx) const { (void)timeIdx; return {}; }

    /*!
     * \brief Fetch per-edge signed normal flux at \p timeIdx.
     * \param flux  Resized to \c triangleCount()*3, indexed \c [tri*3 + localEdge].
     *              Units m² s⁻¹; positive flows outward through the edge's
     *              outward normal.
     * \returns true on success. Default implementation returns false so callers
     *          can probe whether the source supports flux data without an error.
     */
    virtual bool readEdgeFluxAt(int timeIdx, std::vector<float>& flux)
    {
        (void)timeIdx; (void)flux;
        return false;
    }

    /*!
     * \brief Fetch time-invariant edge geometry (length + outward unit normal).
     * \param length Resized to \c triangleCount()*3 (m).
     * \param nx,ny  Resized to \c triangleCount()*3 (dimensionless).
     * \returns true on success. Default returns false.
     */
    virtual bool readEdgeGeometry(std::vector<float>& length,
                                  std::vector<float>& nx,
                                  std::vector<float>& ny)
    {
        (void)length; (void)nx; (void)ny;
        return false;
    }
};

// ---------------------------------------------------------------------------
// EngineMesh2DSource — live source driven by SimulationRunner ticks
// ---------------------------------------------------------------------------

class EngineMesh2DSource : public IMesh2DSource
{
public:
    /*!
     * \brief Construct with already-queried mesh geometry.
     *
     * The runner builds this on the GUI thread immediately after the engine
     * finishes its 2D initialization, querying `swmm_2d_vertex_get_xyz_bulk`
     * / `swmm_2d_triangle_get_vertices` against the in-process engine handle.
     * The depth vector starts empty and is appended to with `pushDepths()`.
     */
    EngineMesh2DSource(std::vector<double>            vx,
                       std::vector<double>            vy,
                       std::vector<double>            vz,
                       std::vector<std::array<int,3>> tris);

    /*!
     * \brief Append one tick's worth of per-triangle depth.
     *
     * Pushed from `SimulationRunner::twoDDepthsAvailable` via queued connection
     * — always on the GUI thread, so no synchronization is needed.
     */
    void pushDepths(std::vector<float> depths,
                    QDateTime simTime,
                    double elapsedSec);

    /*!
     * \brief Append one tick's worth of per-edge signed normal flux.
     *
     * Mirrors \ref pushDepths but writes to the flux slot of the most recent
     * tick. Expected size is \c triangleCount()*3. If called before
     * \c pushDepths for the same tick, the runner buffers the flux into the
     * pending slot and \c pushDepths will pair them. Empty flux vectors are
     * accepted (older engines without \c swmm_2d_get_edge_flux_bulk skip the
     * push entirely; see SimulationRunner CF.2.4 dlsym gating).
     */
    void pushFlux(std::vector<float> flux,
                  QDateTime simTime,
                  double elapsedSec);

    /*!
     * \brief Install time-invariant edge geometry queried via
     * \c swmm_2d_edge_get_geometry_bulk once at twoDInitialized. Sizes are
     * \c triangleCount()*3 each. Optional — when not called, the source
     * advertises no edge geometry (readEdgeGeometry returns false).
     */
    void setEdgeGeometry(std::vector<float> length,
                         std::vector<float> nx,
                         std::vector<float> ny);

    // IMesh2DSource
    int  vertexCount()   const override { return static_cast<int>(vx_.size()); }
    int  triangleCount() const override { return static_cast<int>(tris_.size()); }
    int  timeCount()     const override { return static_cast<int>(history_.size()); }
    bool readMeshGeometry(std::vector<double>& vx,
                          std::vector<double>& vy,
                          std::vector<double>& vz,
                          std::vector<std::array<int, 3>>& tris) override;
    bool readDepthsAt(int timeIdx, std::vector<float>& depths) override;
    QDateTime simTimeAt(int timeIdx) const override;

    bool readEdgeFluxAt(int timeIdx, std::vector<float>& flux) override;
    bool readEdgeGeometry(std::vector<float>& length,
                          std::vector<float>& nx,
                          std::vector<float>& ny) override;

private:
    std::vector<double>              vx_, vy_, vz_;
    std::vector<std::array<int,3>>   tris_;

    struct Tick {
        std::vector<float> depths;
        std::vector<float> flux;       ///< [tri*3 + localEdge]; empty when source has no flux feed.
        QDateTime          sim_time;
        double             elapsed_sec = 0.0;
    };
    std::vector<Tick> history_;

    // Time-invariant edge geometry; populated once at twoDInitialized via
    // setEdgeGeometry. Empty when the engine lacks the bulk geometry API.
    std::vector<float> edge_length_;
    std::vector<float> edge_nx_;
    std::vector<float> edge_ny_;
};

// ---------------------------------------------------------------------------
// HDF5Mesh2DSource — post-run source backed by Mesh2DH5Reader
// ---------------------------------------------------------------------------

class HDF5Mesh2DSource : public IMesh2DSource
{
public:
    HDF5Mesh2DSource();
    ~HDF5Mesh2DSource() override;

    bool open(const QString& path);
    const QString& path() const noexcept { return path_; }

    // IMesh2DSource
    int  vertexCount()   const override;
    int  triangleCount() const override;
    int  timeCount()     const override;
    bool readMeshGeometry(std::vector<double>& vx,
                          std::vector<double>& vy,
                          std::vector<double>& vz,
                          std::vector<std::array<int, 3>>& tris) override;
    bool readDepthsAt(int timeIdx, std::vector<float>& depths) override;
    QDateTime simTimeAt(int timeIdx) const override;

    bool readEdgeFluxAt(int timeIdx, std::vector<float>& flux) override;
    bool readEdgeGeometry(std::vector<float>& length,
                          std::vector<float>& nx,
                          std::vector<float>& ny) override;

    /*! \brief Anchor wall-clock time for the simulation start (so /time
     *  (seconds since start) maps back to a QDateTime for the global slider). */
    void setSimulationStart(QDateTime t) { sim_start_ = t; }

private:
    QString                                            path_;
    std::unique_ptr<openswmmvis::io::Mesh2DH5Reader>   reader_;
    QDateTime                                          sim_start_;
};

// ---------------------------------------------------------------------------
// SWMM2DResultsLayer
// ---------------------------------------------------------------------------

class SWMM2DResultsLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
public:
    explicit SWMM2DResultsLayer(const QString& name = QStringLiteral("2D Results"),
                                 OpenSWMMVisWorkspace* parent = nullptr);
    ~SWMM2DResultsLayer() override;

    /*!
     * \brief Swap the data source. Triggers a one-time mesh-geometry rebuild;
     * the current time index is clamped to the new source's `timeCount()-1`.
     *
     * Lifecycle: a run typically calls `setSource(EngineMesh2DSource)` once
     * at `twoDInitialized`, then `setSource(HDF5Mesh2DSource)` again at
     * `finished` to swap to the on-disk file for scrubbing.
     */
    void setSource(std::unique_ptr<IMesh2DSource> source);

    IMesh2DSource* source() noexcept { return source_.get(); }
    const IMesh2DSource* source() const noexcept { return source_.get(); }

    /*!
     * \brief Current time index displayed on the canvas. -1 = no frame yet.
     */
    int currentTimeIndex() const noexcept { return current_time_idx_; }
    void setCurrentTimeIndex(int t);

    /*!
     * \brief Re-query `source()->timeCount()` and emit `timeRangeChanged` if
     * it grew. Used by the runner's per-tick refresh during live mode.
     */
    void refreshTimeRange();

    /*! \brief Dry-cell depth threshold in metres. Cells below this draw with alpha 0. */
    double dryDepth() const noexcept { return dry_depth_; }
    void   setDryDepth(double d);

    /*! \brief Upper end of the colour ramp (metres). Auto-tracks the global max
     *         depth seen so far across all loaded ticks unless explicitly set. */
    double maxDepth() const noexcept { return max_depth_; }
    void   setMaxDepth(double d);

    // ----- Velocity vector overlay (CF.2) -----------------------------------

    /*! \brief Whether centroid arrow glyphs are drawn over the depth fill. */
    bool   velocityVectorsVisible() const noexcept { return velocity_visible_; }
    void   setVelocityVectorsVisible(bool v);

    /*! \brief Overall alpha applied to all arrow glyphs, 0..1. Default 0.9. */
    qreal  velocityOpacity() const noexcept { return velocity_opacity_; }
    void   setVelocityOpacity(qreal alpha);

    /*! \brief Per-glyph pixel scale: \c arrow_length_px = \p scale * log1p(vmag / vmagRef).
     *  Default 30 — a 1 m/s velocity renders ~21 px at any zoom. */
    double velocityArrowScale() const noexcept { return velocity_arrow_scale_; }
    void   setVelocityArrowScale(double scale);

    /*! \brief Upper end of the velocity colour ramp (m/s). Auto-tracks the
     *  running max unless explicitly set. */
    double maxVelocity() const noexcept { return max_velocity_; }
    void   setMaxVelocity(double v);

    /*! \brief Whether the active source produced both edge geometry and a
     *  flux slice — i.e. whether the velocity overlay has data to render. */
    bool   hasVelocityData() const noexcept { return have_velocity_; }

    // ----- Renderer (Slice BI Phase 8.13.6.6) -----------------------------
    // API plumbing only — the existing paint path still uses dry_depth_ /
    // max_depth_ directly.  Sub-phase 8.13.6.4 (deferred until Slice BB
    // ColorRamp lands) will swap the paint loop to consult m_renderer.

    /*!
     * \brief The IFeatureRenderer that will drive this layer's paint pass.
     * \details Constructed eagerly as a default GraduatedRenderer because
     *          this layer is fundamentally a continuous-attribute (depth)
     *          colour-mapped layer.  Owned by the layer; never null.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  Null pointers are silently
     *          rejected.  Emits \ref rendererChanged() when the pointer
     *          actually changes.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    /*!
     * \brief Statistic on the current frame — useful for the status-bar peak readout.
     * \returns {peakDepth, peakTriIdx} of the currently displayed frame, or
     * {0, -1} if no frame is loaded.
     */
    std::pair<float, int> currentPeak() const;

    /*!
     * \brief Set the current frame by nearest simulation time. Used to keep the
     * 2D layer in step with the existing AnimationController's 1D playback —
     * one slider drives both layers.
     */
    void setCurrentSimTime(QDateTime t);

    // ----- Cell selection / picking (CF.3) ----------------------------------

    /*!
     * \brief Return triangle indices whose scene-space centroid falls inside
     *        \p sceneRect.  Linear scan over m_sceneTris — fine for meshes
     *        up to ~100k tris; bigger meshes may want spatial indexing.
     */
    [[nodiscard]] QVector<int> pickCellsInRect(const QRectF& sceneRect) const;

    /*!
     * \brief Return triangle indices whose scene-space centroid falls inside
     *        \p scenePoly (odd-even fill rule). Used by lasso-select.
     */
    [[nodiscard]] QVector<int> pickCellsInPolygon(const QPolygonF& scenePoly) const;

    /*! \brief Return the triangle whose vertices contain \p scenePt, or -1.
     *  Used by single-click cell pick and canvas-right-click hit test. */
    [[nodiscard]] int pickCellAt(const QPointF& scenePt) const;

    /*! \brief Replace the highlight set. Triggers an Overlay repaint via the
     *  layer's existing invalidate path. */
    void highlightCells(const QSet<int>& triIdxSet);

    /*! \brief Add to / clear the highlight set. */
    void clearHighlights();

    /*! \brief Current highlight set (read-only). */
    [[nodiscard]] const QSet<int>& highlightedCells() const noexcept { return m_highlighted; }

    // ----- OpenSWMMVisLayer interface ----------------------------------------

    void populateScene(QGraphicsScene* scene,
                        const MapExtent& canvasExtent,
                        const SpatialReferenceSystem* canvasSRS) override;

    void depopulateScene(QGraphicsScene* scene) override;

    void refreshScene(QGraphicsScene* scene,
                       const MapExtent& canvasExtent,
                       const SpatialReferenceSystem* canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem* newCanvasSRS) override;

    // ----- Scene caches (public for the graphics item) -----------------------

    /*! Per-triangle scene-space vertices + per-tri animated state. */
    struct SceneTri {
        QPointF a, b, c;
        QPointF centroid;       ///< Scene-space centroid; cached at rebuildSceneGeometry_.
        float   depth = 0.0f;
        float   vx    = 0.0f;   ///< Scene-space velocity x (m/s; sign flipped to match scene Y).
        float   vy    = 0.0f;   ///< Scene-space velocity y (m/s).
        float   vmag  = 0.0f;   ///< |v| in m/s, computed in model coords.
    };

    QRectF             m_sceneBBox;
    QVector<SceneTri>  m_sceneTris;

signals:
    /*! Emitted when `source()->timeCount()` changes (either via setSource or refreshTimeRange). */
    void timeRangeChanged(int lo, int hi);

    /*! Emitted whenever the current frame changes (setCurrentTimeIndex). */
    void currentTimeChanged(int t);

    /*! CF.3 — emitted on plot↔canvas hover sync. */
    void cellHovered(int triIdx);

    /*! CF.3 — emitted when the highlight set changes. */
    void highlightedCellsChanged();

    /*! \brief Emitted when setRenderer() swaps the renderer pointer. */
    void rendererChanged();

private:
    void rebuildSceneGeometry_();   ///< Recompute scene-space triangle vertices + centroids; refresh cached edge geometry.
    void applyCurrentDepths_();     ///< Copy `current_depths_` into the SceneTri buffer.
    void applyCurrentFlux_();       ///< Run RT0 reconstruction → write vx/vy/vmag into SceneTri.

    std::unique_ptr<IMesh2DSource> source_;
    std::vector<double>            vx_, vy_, vz_;
    std::vector<std::array<int,3>> tris_;
    std::vector<float>             current_depths_;

    // CF.2 — per-tick flux + time-invariant edge geometry pulled once from the source.
    std::vector<float>             current_flux_;     ///< [tri*3 + localEdge], m^2/s.
    std::vector<float>             edge_length_;      ///< [tri*3], m.
    std::vector<float>             edge_nx_;          ///< [tri*3], dimensionless.
    std::vector<float>             edge_ny_;          ///< [tri*3], dimensionless.
    bool                           have_edge_geom_   = false;
    bool                           have_velocity_    = false;

    int                            current_time_idx_ = -1;
    double                         dry_depth_        = 0.005;
    double                         max_depth_        = 0.5;   // auto-grown
    bool                           max_depth_user_set_ = false;

    // Velocity overlay state.
    // CF.2 transitional default: arrows ON so the overlay is eyeball-able
    // before the layer-panel UI lands. Flip back to false once a UI toggle
    // exists.
    bool                           velocity_visible_     = true;
    qreal                          velocity_opacity_     = 0.9;
    double                         velocity_arrow_scale_ = 30.0;   // px per log1p(vmag/vRef)
    double                         max_velocity_         = 1.0;    // m/s, auto-grown
    bool                           max_velocity_user_set_ = false;

    SWMM2DResultsGraphicsItem*     graphics_item_    = nullptr;
    SWMM2DVelocityArrowsItem*      arrows_item_      = nullptr;

    // CF.3 — selection / highlight state.
    QSet<int>                      m_highlighted;

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Initialised eagerly in
    // the ctor (default GraduatedRenderer) so renderer() never returns
    // null.  Paint refactor deferred until Slice BB ColorRamp ships.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;
};

#endif // OPENSWMMVIS_LAYERS_SWMM2DRESULTSLAYER_H
