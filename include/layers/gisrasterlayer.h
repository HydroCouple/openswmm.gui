/*!
 * \file   gisrasterlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Map layer backed by a GDAL raster dataset with configurable
 *         colour-ramp rendering and on-demand viewport warping.
 */

#ifndef GISRASTERLAYER_H
#define GISRASTERLAYER_H

#include "layers/tilepyramidlayer.h"
#include "render/colorramp.h"

#include <QImage>
#include <QFutureWatcher>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>
#include <vector>

// Forward-declare GDAL types
class GDALDataset;
class OGRCoordinateTransformation;
class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;
class RasterTileItem;

namespace OpenSWMM::Render { class IRasterRenderer; }

// RasterColorRamp is defined in render/colorramp.h (relocated 2026-05-24,
// Slice BB-α). Kept at global scope for back-compat with existing callers
// + Q_DECLARE_METATYPE.

/*!
 * \class GISRasterLayer
 * \brief A map layer backed by a GDAL raster dataset.
 * \details Reads any GDAL-supported raster format (GeoTIFF, NetCDF, HDF5 …),
 *          warps the requested tile into the canvas CRS using GDALWarp, renders
 *          the result with a configurable colour ramp, and caches the warped tile
 *          until the extent or CRS changes.
 *
 *          Single-band datasets are rendered via a colour ramp. Three-band (RGB)
 *          or four-band (RGBA) datasets bypass the ramp and render as-is.
 */
class GISRasterLayer : public TilePyramidLayer
{
    Q_OBJECT
    Q_PROPERTY(QString  filePath    READ filePath    NOTIFY filePathChanged)
    Q_PROPERTY(int      bandCount   READ bandCount   CONSTANT)
    Q_PROPERTY(int      renderBand  READ renderBand  WRITE setRenderBand NOTIFY renderBandChanged)
    Q_PROPERTY(double   noDataValue READ noDataValue NOTIFY noDataValueChanged)
    Q_PROPERTY(RasterColorRamp colorRamp READ colorRamp WRITE setColorRamp NOTIFY colorRampChanged)

    // Slice U-7 — Q_CLASSINFO groups for the unified LayerStyleDialog.
    Q_CLASSINFO("group:filePath",    "Source")
    Q_CLASSINFO("group:bandCount",   "Source")
    Q_CLASSINFO("group:renderBand",  "Display")
    Q_CLASSINFO("group:noDataValue", "Display")
    Q_CLASSINFO("group:colorRamp",   "Color ramp")

public:

    explicit GISRasterLayer(const QString &filePath,
                            OpenSWMMVisWorkspace *parent = nullptr);

    ~GISRasterLayer() override;

    /*!
     * \brief Asynchronously open \p filePath. GDALOpenEx + metadata read +
     *        ComputeStatistics run on a worker thread; the layer is populated
     *        on the GUI thread and \ref openFinished(bool) fires on completion
     *        (\c ok==false ⇒ open failed). Construct the layer with an empty
     *        path first, then call this; the synchronous \ref openDataset path
     *        (used by tests / project restore) is unchanged.
     */
    void openAsync(const QString &filePath);

    // ----- Dataset info ---------------------------------------------------

    [[nodiscard]] QString filePath()    const;
    [[nodiscard]] int     bandCount()   const;
    [[nodiscard]] int     renderBand()  const;
    [[nodiscard]] double  noDataValue() const;

    /*!
     * \brief Heuristically detects the vertical unit of the raster's elevation
     *        values from the embedded CRS metadata.
     * \return \c "m" for metre-based or geographic CRS (most global DEMs),
     *         \c "ft" for US-customary projected CRS (state plane feet, etc.).
     *         Returns \c "m" when no CRS metadata is present.
     */
    [[nodiscard]] QString detectVerticalUnit() const;

    /*!
     * \brief Sets the 1-based band index to render (only applies to single-band mode).
     */
    void setRenderBand(int band);

    /*!
     * \brief Returns true when the dataset has a known no-data value.
     */
    [[nodiscard]] bool hasNoDataValue() const;

    // ----- Self-description for the Layer Properties dialog ----------------
    [[nodiscard]] QString sourceDescription() const override;
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    // ----- Colour ramp ---------------------------------------------------

    [[nodiscard]] RasterColorRamp colorRamp() const;
    void setColorRamp(const RasterColorRamp &ramp);

    /*!
     * \brief Calculates the data min/max from the raster and updates the ramp range.
     * \details This may be slow for large datasets; runs in a worker thread.
     */
    void autoStretchColorRamp();

    /*! Slice U-7 — surface this raster's Q_PROPERTYs as the single
     *  styleable subject for the unified LayerStyleDialog. */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
        styleSubjects() override;

    // ----- Raster renderer (Slice BI Phase 8.13.6.7; P5/R-1 full switch) ---
    // The raster renderer is the §J.2 seam every raster paint path goes
    // through. As of P5/R-1 warpToCanvas() colourises via
    // m_rasterRenderer->colorForValue(); the legacy m_colorRamp field is
    // retired and colorRamp()/setColorRamp() project to/from the renderer.

    /*!
     * \brief The IRasterRenderer that will drive this layer's warp pass.
     * \details Constructed eagerly as a default
     *          SingleBandPseudoColorRenderer so callers never have to
     *          null-check.  Owned by the layer; do not delete.
     */
    [[nodiscard]] OpenSWMM::Render::IRasterRenderer *rasterRenderer() const;

    /*!
     * \brief Replaces the current raster renderer.
     * \details The layer takes ownership.  Null pointers are silently
     *          rejected (the method no-ops) so rasterRenderer() never
     *          returns null.  Emits \ref rasterRendererChanged() when the
     *          pointer actually changes.
     */
    void setRasterRenderer(std::unique_ptr<OpenSWMM::Render::IRasterRenderer> r);

    // ----- Pixel query ----------------------------------------------------

    /*!
     * \brief Returns the pixel value at the given geographic coordinate.
     * \param mapX        X in canvas CRS.
     * \param mapY        Y in canvas CRS.
     * \param canvasSRS   Canvas CRS.
     * \param band        1-based band index.
     * \param ok          Set to true if a valid value was found.
     */
    [[nodiscard]] double valueAt(double mapX, double mapY,
                                 const SpatialReferenceSystem *canvasSRS,
                                 int band = 1,
                                 bool *ok = nullptr) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    [[nodiscard]] bool isRasterLayer() const override { return true; }

    void fetchCache(const MapExtent &extent,
                    const QSize &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    void render(QPainter *painter,
                const MapExtent &extent,
                const QSize &imageSize,
                const SpatialReferenceSystem *srs) override;

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- VS.6 — hillshade relief overlay (single-band DTM rasters) ---------
    // When enabled, warpToCanvas() composites a hillshade lighting factor over
    // the colour-ramped pixels using the warped elevation grid. Parameters
    // mirror HillshadeSymbolLayerSpec; stored as primitives to keep this
    // header free of the render/ spec include.
    [[nodiscard]] bool hillshadeEnabled() const { return m_hillshadeEnabled; }
    void setHillshadeEnabled(bool on);
    [[nodiscard]] double hillshadeAzimuthDeg()  const { return m_hillshadeAzimuthDeg; }
    [[nodiscard]] double hillshadeAltitudeDeg() const { return m_hillshadeAltitudeDeg; }
    [[nodiscard]] double hillshadeZFactor()     const { return m_hillshadeZFactor; }
    [[nodiscard]] double hillshadeStrength()    const { return m_hillshadeStrength; }
    void setHillshadeParams(double azimuthDeg, double altitudeDeg,
                            double zFactor, double strength);

signals:
    void filePathChanged(const QString &path);
    void renderBandChanged(int band);
    void noDataValueChanged(double value);
    void colorRampChanged(const RasterColorRamp &ramp);
    /*! \brief Emitted when setRasterRenderer() swaps the renderer pointer. */
    void rasterRendererChanged();

    /*! \brief Emitted on the GUI thread when \ref openAsync() completes. */
    void openFinished(bool ok);

    /*! \brief A background overview (`.ovr` pyramid) build has started for
     *         this raster. \p name is the display file name. */
    void overviewBuildStarted(const QString &name);
    /*! \brief The background overview build finished (ok ⇒ pyramids ready and
     *         the render will pick them up on the next repaint). */
    void overviewBuildFinished(bool ok);

private:
    // Worker-thread payload for openAsync(): the GDAL open + metadata read +
    // band-stats scan produce this POD (no QObject state), which
    // applyOpenResult() then folds into the layer on the GUI thread. Defined
    // in the .cpp.
    struct OpenResult;
    [[nodiscard]] static OpenResult doOpenWork(const QString &filePath);
    void applyOpenResult(const OpenResult &r);

    void openDataset(const QString &filePath);
    void closeDataset();
    void invalidateCache();

    // ── Overview (pyramid) preprocessing — Phase 1 ────────────────────────
    /*! \brief If this raster is large, lacks internal overviews, and the
     *         auto-build preference is on, kick off a background `.ovr` build.
     *         Called on the GUI thread after a successful open. */
    void maybeBuildOverviews();
    /*! \brief Launch the background GDALBuildOverviews on a worker (separate
     *         read-only handle ⇒ external `.ovr` sidecar; source untouched).
     *         On completion flags a dataset reload so the render thread reopens
     *         m_dataset and sees the new pyramids. */
    void buildOverviewsAsync();

    // Snapshot of everything a worker-thread tile warp reads, taken on the
    // GUI thread at launch so the worker never dereferences live layer state
    // (the renderer is a clone() — zero locking in the colourise loop).
    struct WarpParams
    {
        int    renderBand  = 1;
        bool   hasNoData   = false;
        double noDataValue = 0.0;
        bool   hillshadeEnabled     = false;
        double hillshadeAzimuthDeg  = 315.0;
        double hillshadeAltitudeDeg = 45.0;
        double hillshadeZFactor     = 1.0;
        double hillshadeStrength    = 0.5;
        std::shared_ptr<const OpenSWMM::Render::IRasterRenderer> renderer;
    };
    [[nodiscard]] WarpParams snapshotWarpParams() const;

    /*!
     * \brief Warps \p src to match \p canvasSRS and the requested extent/size.
     * \details Pure w.r.t. the borrowed \p src handle + \p params snapshot —
     *          safe to run on N workers concurrently (one GDAL handle each).
     * \returns A QImage in ARGB32 format ready to be painted.
     */
    [[nodiscard]] QImage warpToCanvas(GDALDataset *src,
                                      const MapExtent &canvasExtent,
                                      const SpatialReferenceSystem *canvasSRS,
                                      int pixelWidth,
                                      int pixelHeight,
                                      const WarpParams &params) const;

    /*! \brief Phase 2 — build a small in-memory source holding ONLY the
     *         canvas-extent window of \p src, read at (roughly) the view's
     *         resolution so GDAL serves it from the nearest overview instead of
     *         traversing the full-resolution image. warpToCanvas() then warps
     *         this small source. Bands: single-band ⇒ [render band] as band 1;
     *         RGB ⇒ [1,2,3]. In the source CRS with the window's geotransform.
     *         Returns nullptr on no-overlap / non-northup / failure, so the
     *         caller falls back to the full-resolution warp. */
    [[nodiscard]] GDALDataset *buildWindowedSource(
        GDALDataset *src,
        const MapExtent &canvasExtent, const SpatialReferenceSystem *canvasSRS,
        int pixelWidth, int pixelHeight, int outBands, bool isRGB,
        const WarpParams &params) const;

    // ── Phase 3 — fixed-grid tile pyramid (off-thread, cached) ───────────
    // Tiles are 256² images in CANVAS CRS on a fixed power-of-2 grid, so a pan
    // reuses the tiles it already has and only the new edge tiles are produced.
    // Each tile is one warpToCanvas() of its canvas-CRS extent (overview-aware),
    // produced on up to N concurrent workers (one pooled GDAL handle each),
    // cached in the TilePyramidLayer base, and composited by render() with a
    // coarser-tile fallback for tiles still in production.
    static constexpr int kTilePx = 256;
    // Fallback stand-ins may be at most 2^6 = 64× coarser than the view level
    // (a 4-px sub-rect of a 256² ancestor).
    static constexpr double kMaxAncestorSpanRatio = 64.0;
    struct TileReq { int level; int col; int row; };
    // Worker → GUI payload for one produced tile. `gen` is the cache
    // generation captured at launch; results from before an invalidateCache()
    // are dropped instead of polluting the fresh cache.
    struct TileResult
    {
        QString   key;
        QImage    img;
        MapExtent extent;
        int       level = 0;
        quint64   gen   = 0;
    };
    /*! \brief Discrete pyramid level for a canvas resolution (map units/pixel):
     *         level = round(log2(mupp)); tile resolution = 2^level. */
    [[nodiscard]] static int levelForResolution(double mapUnitsPerPixel);
    /*! \brief Canvas-CRS extent of tile (level,col,row) on the fixed grid. */
    [[nodiscard]] static MapExtent tileExtent(int level, int col, int row);
    [[nodiscard]] static QString   tileKey(int level, int col, int row);
    /*! \brief Ensure the tiles covering \p extent at the view's level are
     *         cached or queued for production. */
    void requestTiles(const MapExtent &extent, const QSize &viewportSize,
                      const SpatialReferenceSystem *canvasSRS);
    /*! \brief Cold-start backstop: once per cache generation, front-queue the
     *         coarsest-level tiles covering the whole raster (≤ ~4×4) so the
     *         render fallback always has a stand-in to cut from. */
    void enqueueSeedTiles();
    /*! \brief Pump: launch queued tiles into idle slots until all N slots are
     *         busy (or the queue / handle pool is exhausted). */
    void startNextTiles();
    /*! \brief GUI-thread completion of slot \p slotIndex: cache the tile,
     *         return the handle, repaint, re-pump. */
    void onSlotFinished(int slotIndex);

    // ── Parallel tile production — N-slot GDAL handle pool ───────────────
    // N read-only handles to the same file; a slot borrows one for the warp
    // duration. Acquire (startNextTiles) and release (onSlotFinished) both run
    // on the GUI thread, so the free-list needs no mutex; workers only *use*
    // their borrowed handle.
    struct TileSlot
    {
        std::unique_ptr<QFutureWatcher<TileResult>> watcher;
        bool                    busy   = false;
        GDALDataset            *handle = nullptr;  // borrowed from m_freeHandles
        SpatialReferenceSystem *srs    = nullptr;  // per-tile clone; freed on finish
    };
    /*! \brief Open the N pooled read-only handles + create the slots/watchers.
     *         Closes any previous pool first (requires no busy slot). */
    void openHandlePool();
    /*! \brief Destroy the slots (dropping any queued stale completions) and
     *         GDALClose all pooled handles. Requires no busy slot. */
    void closeHandlePool();
    /*! \brief Synchronously wait out every in-flight tile warp and fold in its
     *         completion. Callers must recreate or destroy the slots (via
     *         openHandlePool/closeHandlePool) before returning to the event
     *         loop, so the watchers' now-stale queued signals never fire. */
    void drainTileSlots();
    /*! \brief Fold one finished slot's result into the cache and free the
     *         slot's per-tile resources. */
    void finishSlot(TileSlot &slot);

    QString          m_filePath;
    int              m_renderBand = 1;
    double           m_noDataValue = std::numeric_limits<double>::quiet_NaN();
    bool             m_hasNoData  = false;
    // P5/R-1 — m_colorRamp retired. The SingleBandPseudoColorRenderer
    // (m_rasterRenderer, below) is the single source of truth for raster
    // colouring; colorRamp()/setColorRamp() project to/from it.

    // VS.6 — hillshade relief overlay parameters (see public accessors).
    bool             m_hillshadeEnabled    = false;
    double           m_hillshadeAzimuthDeg = 315.0;
    double           m_hillshadeAltitudeDeg = 45.0;
    double           m_hillshadeZFactor    = 1.0;
    double           m_hillshadeStrength   = 0.5;

    GDALDataset     *m_dataset    = nullptr;  /*!< Owned GDAL dataset. */

    // Guards m_dataset for the paths that still share the primary handle:
    // valueAt() (single-pixel probe), closeDataset(), and the post-.ovr-build
    // reload/reopen. Tile warps no longer take it — each worker reads through
    // its own pooled handle with a WarpParams snapshot.
    mutable QMutex   m_datasetMutex;
    // Set after a background .ovr build; consumed by the next fetchCache()
    // (GUI thread), which reopens m_dataset so the new overviews become visible.
    std::atomic<bool> m_datasetReloadPending{false};
    bool              m_overviewBuildInFlight = false;

    // Tile pyramid state. The tile cache itself lives in the TilePyramidLayer
    // base (mutex-guarded: render() reads from the MapRenderJob worker while
    // the GUI thread inserts). Everything below is GUI-thread only.
    QList<TileReq>          m_tileQueue;         // FIFO of tiles still to produce
    QSet<QString>           m_queuedKeys;        // de-dup: queued ∪ in-flight
    quint64                 m_cacheGeneration = 0; // bumped by invalidateCache()
    quint64                 m_seedGeneration  = ~0ULL; // last gen seeds were queued
    SpatialReferenceSystem *m_currentSRS   = nullptr;  // latest canvas-CRS snapshot (template)

    // Parallel production (see TileSlot above). m_tilePool bounds tile warps
    // to N threads and keeps them off the global pool used by openAsync /
    // overview builds.
    int                        m_maxConcurrentTiles = 1;
    QThreadPool                m_tilePool;
    std::vector<GDALDataset *> m_poolHandles;  // owns the N pooled handles
    QList<GDALDataset *>       m_freeHandles;  // GUI-thread free-list
    std::vector<TileSlot>      m_slots;

    // Persistent scene item (kept visible as placeholder until new warp completes)
    RasterTileItem  *m_sceneItem  = nullptr;

    // Single source of truth for raster colouring (P5/R-1). Initialised
    // eagerly in the ctor (a SingleBandPseudoColorRenderer seeded from the
    // default grayscale ramp) so rasterRenderer() never returns null, and
    // warpToCanvas() colourises through it.
    std::unique_ptr<OpenSWMM::Render::IRasterRenderer> m_rasterRenderer;
};

Q_DECLARE_METATYPE(GISRasterLayer *)
// Q_DECLARE_METATYPE(RasterColorRamp) is in render/colorramp.h (Slice BB-α).

#endif // GISRASTERLAYER_H
