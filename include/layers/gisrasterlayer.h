/**
 * @file      gisrasterlayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Map layer backed by a GDAL raster dataset with configurable
 *         colour-ramp rendering and on-demand viewport warping.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef GISRASTERLAYER_H
#define GISRASTERLAYER_H

#include "layers/tilepyramidlayer.h"
#include "render/colorramp.h"
#include "render/renderers/palettedrasterrenderer.h"

#include <QHash>
#include <QImage>
#include <QFutureWatcher>
#include <QList>
#include <QMutex>
#include <QPair>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include <atomic>
#include <memory>
#include <vector>

// Forward-declare GDAL types
class GDALDataset;
class OGRCoordinateTransformation;
class OGRSpatialReference;
class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;
class RasterTileItem;

namespace OpenSWMM::Render
{
class IRasterRenderer;
}

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
 *          Colouring is delegated to the layer's IRasterRenderer (see
 *          rasterRenderer()): a GraduatedRasterRenderer (continuous stretch or
 *          classified ramp over one band), a PalettedRasterRenderer
 *          (categorical values, incl. an embedded GDAL colour table) or a
 *          MultiBandColorRenderer (RGB(A) composite of three / four Byte bands).
 */
class GISRasterLayer : public TilePyramidLayer
{
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(int bandCount READ bandCount CONSTANT)
    Q_PROPERTY(int renderBand READ renderBand WRITE setRenderBand NOTIFY renderBandChanged)
    Q_PROPERTY(double noDataValue READ noDataValue NOTIFY noDataValueChanged)

    // Slice U-7 — Q_CLASSINFO groups for the unified LayerStyleDialog.
    Q_CLASSINFO("group:filePath", "Source")
    Q_CLASSINFO("group:bandCount", "Source")
    Q_CLASSINFO("group:renderBand", "Display")
    Q_CLASSINFO("group:noDataValue", "Display")

public:
    /**
     * @brief Create a read-only raster with independent pooled tile workers and a grayscale renderer.
     * @param[in] filePath GDAL datasource path, opened synchronously when nonempty; empty defers IO.
     * @param[in] parent Optional workspace QObject owner.
     * @note Registers GDAL and limits tile workers to min(4,max(1,idealThreadCount)). GUI-thread configuration/queue operations are separate from mutex-protected primary-handle queries and base-cache worker reads. Construction may compute band statistics and schedule an automatic external overview build.
     */
    explicit GISRasterLayer(const QString &filePath, OpenSWMMVisWorkspace *parent = nullptr);

    /**
     * @brief Drain active tile warps before closing their pooled handles, primary dataset and owned CRS/probe resources.
     * @note May block until tile workers finish. Independent open/overview workers use guarded completion and close orphan handles; borrowed renderer/style wrappers become invalid.
     */
    ~GISRasterLayer() override;

    /*!
     * \brief Asynchronously open \p filePath. GDALOpenEx + metadata read +
     *        ComputeStatistics run on a worker thread; the layer is populated
     *        on the GUI thread and \ref openFinished(bool) fires on completion
     *        (\c ok==false ⇒ open failed). Construct the layer with an empty
     *        path first, then call this; the synchronous \ref openDataset path
     *        (used by tests / project restore) is unchanged.
     * @param[in] filePath Datasource path copied into a worker open/statistics scan.
     * @note Owner-thread completion drains old tile workers and replaces the source even when new open fails, then emits openFinished(). Requests have no latest-generation ordering guard; serialize overlapping opens for deterministic adoption. A destroyed layer closes the orphan dataset.
     */
    void openAsync(const QString &filePath);

    // ----- Dataset info ---------------------------------------------------

    /**
     * @return Stored successful path interpreted as an absolute filesystem path, or empty; no canonicalization/existence check.
     */
    [[nodiscard]] QString filePath() const;
    /**
     * @return Current primary dataset band count or zero when closed; this accessor itself does not take the dataset mutex, so serialize with close/reopen.
     */
    [[nodiscard]] int bandCount() const;
    /**
     * @return Raw1-based single-band selection, initially one; invalid values can be stored.
     */
    [[nodiscard]] int renderBand() const;
    /**
     * @return Cached selected/open-time nodata scalar; consult hasNoDataValue() because this value alone is not a validity flag.
     */
    [[nodiscard]] double noDataValue() const;

    /*!
     * \brief Heuristically detects the vertical unit of the raster's elevation
     *        values from the embedded CRS metadata.
     * \return \c "m" for metre-based or geographic CRS (most global DEMs),
     *         \c "ft" for US-customary projected CRS (state plane feet, etc.).
     *         Returns \c "m" when no CRS metadata is present.
     * @note Heuristic only: geographic/unknown/unrecognized CRS defaults to metres; linear scale near0.3048 selects feet. Does not inspect sample magnitudes, convert values or guarantee an independently correct vertical datum. Reads the primary handle without its own mutex; serialize source replacement.
     */
    [[nodiscard]] QString detectVerticalUnit() const;

    /*!
     * \brief Sets the 1-based band index to render on the single-band colour
     *        path. Re-reads that band's NoData value and, when the live
     *        renderer is a GraduatedRasterRenderer, re-seeds its data range
     *        and classification from the band's statistics / sample.
     * @param[in] band Raw1-based source index; equal value is a no-op, but out-of-range values are stored rather than rejected.
     * @note Reads nodata under primary mutex, updates cached nodata flags, reseeds a graduated renderer from approximate range/sample when available, invalidates tiles and emits renderBandChanged(), conditional noDataValueChanged(), then repaintRequested(). Does not emit rasterRendererChanged() for that in-place reseeding. RGB renderer source-band choices are independent.
     */
    void setRenderBand(int band);

    /*!
     * \brief Returns true when the dataset has a known no-data value.
     * @return Whether cached selected-band nodata metadata is marked present; not a test of any sample value.
     */
    [[nodiscard]] bool hasNoDataValue() const;

    // ----- Self-description for the Layer Properties dialog ----------------
    /**
     * @return Absolute stored path or in-memory placeholder, without source IO.
     */
    [[nodiscard]] QString sourceDescription() const override;
    /**
     * @return Owned display rows for band count, cached nodata and heuristic vertical unit; no band values are converted.
     */
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    // ----- Per-band data access (symbology inputs) -------------------------
    // All read the primary dataset under m_datasetMutex and cache per band
    // (caches drop on close / re-open). Pure: no signals are emitted.

    /*!
     * \brief Approximate (min, max) of \p band from GDAL statistics
     *        (PAM-cached when available). (NaN, NaN) when unavailable.
     * @param[in] band 1-based source band; invalid/closed/statistics error returns NaN pair.
     * @return Owned approximate native-unit min/max cached by band; first lookup can compute GDAL statistics while holding dataset mutex and may update GDAL PAM metadata. No Qt notifications.
     */
    [[nodiscard]] QPair<double, double> bandRange(int band) const;

    /*!
     * \brief Decimated, overview-aware value sample of \p band — a target of
     *        \p maxSamples finite, non-NoData values on a regular grid over
     *        the whole raster. Feeds Quantile / Jenks / StdDev classification
     *        and unique-value discovery.
     * @param[in] band 1-based source band.
     * @param[in] maxSamples Positive target grid budget used only for the first uncached read of this band. Later calls return cached values regardless of a new budget, even nonpositive.
     * @return Owned finite/non-nodata sample in decimated row-major order, or empty for invalid source/budget/read failure. Grid preserves approximate aspect with each axis at least one pixel, so extremely narrow rasters can exceed the requested target count. Samples remain in raw band units.
     * @note Holds dataset mutex through overview-aware nearest-neighbour read/filter/cache; rare classes may be missed. Cache identity excludes budget and clears on close/reopen.
     */
    [[nodiscard]] QVector<double> sampleValues(int band, int maxSamples = 200000) const;

    /*!
     * \brief Distinct integer values observed in sampleValues(\p band),
     *        ascending, truncated to \p cap entries. Decimated — rare classes
     *        may be missed.
     * @param[in] band 1-based source band sampled using default sampleValues() budget/cache.
     * @param[in] cap Positive maximum returned count; zero/negative leaves it uncapped.
     * @return Sorted distinct lround(sample) integers, truncated to smallest cap values. Not an exact integer-type or full-raster class inventory; rounded values must be int-representable.
     */
    [[nodiscard]] QList<int> uniqueValues(int band, int cap = 256) const;

    /*! \brief True when band 1 carries a GDAL colour table (paletted raster).
     * @return Whether band one has a nonnull GDAL colour table, under primary mutex; false when closed/missing. No signal or palette ownership transfer.
     */
    [[nodiscard]] bool hasColorTable() const;

    /*!
     * \brief Band 1's GDAL colour table as paletted classes (value, label =
     *        value, RGBA). Tables larger than 32 entries are filtered to the
     *        values actually observed in the band sample so unused palette
     *        slots do not flood the legend.
     * @return Owned palette classes. Tables up to32 entries are returned whole; larger tables intersect rounded sampled band-one values, but an empty intersection falls back to the whole table. Sampling can omit rare real classes.
     * @note Table copy/read uses primary mutex, releases it before sampleValues() takes that mutex itself; no GDAL pointer escapes.
     */
    [[nodiscard]] QList<OpenSWMM::Render::PalettedRasterRenderer::Class> colorTableClasses() const;

    /*! \brief True when band 1 is 8-bit (GDT_Byte) — the only type the RGB
     *         composite path renders without saturating.
     * @return Whether band one alone is GDT_Byte under primary mutex, false when closed; does not prove all RGB candidate bands share that type.
     */
    [[nodiscard]] bool isByteRaster() const;

    /*! Slice U-7 — surface this raster's Q_PROPERTYs as the single
     *  styleable subject for the unified LayerStyleDialog. */
    /**
     * @return Owned subject wrappers borrowing this layer QObject for Source/Display properties. Wrappers must not outlive the layer; no data ownership transfer.
     */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
    styleSubjects() override;

    // ----- Raster renderer (Slice BI Phase 8.13.6.7; P5/R-1 full switch) ---
    // The raster renderer is the §J.2 seam every raster paint path goes
    // through: warpToCanvas() colourises single-band tiles via
    // m_rasterRenderer->colorForValue(), and a MultiBandColorRenderer
    // switches the warp onto the RGB composite path. The open-time default
    // is chosen from the dataset (colour table ⇒ Paletted; ≥3 Byte bands ⇒
    // MultiBandColor; otherwise Graduated continuous grayscale over the band
    // statistics). Persisted / restored by StyleFileIO + ProjectSerializer.

    /*!
     * \brief The IRasterRenderer that will drive this layer's warp pass.
     * \details Constructed eagerly as a default GraduatedRasterRenderer so
     *          callers never have to null-check.  Owned by the layer; do not
     *          delete.
     * @return Borrowed layer-owned live renderer, eagerly nonnull and invalidated by replacement/destruction. Edit on GUI thread then call notifyRasterRendererEdited(); worker warps use independent clones.
     */
    [[nodiscard]] OpenSWMM::Render::IRasterRenderer *rasterRenderer() const;

    /*!
     * \brief Replaces the current raster renderer.
     * \details The layer takes ownership.  Null pointers are silently
     *          rejected (the method no-ops) so rasterRenderer() never
     *          returns null.  Emits \ref rasterRendererChanged() when the
     *          pointer actually changes.
     * @param[in] r Exclusively owned fresh nonnull renderer; null is ignored. Never wrap an already layer-owned raw pointer.
     * @note Replacement under primary mutex invalidates colourized/queued tiles and generation, then emits rasterRendererChanged()/repaintRequested(). Already running warps complete but their old-generation image is discarded.
     */
    void setRasterRenderer(std::unique_ptr<OpenSWMM::Render::IRasterRenderer> r);

    /*!
     * \brief Call after mutating the live renderer in place (through its own
     *        setters) so cached tiles are dropped, \ref rasterRendererChanged()
     *        fires and a repaint is requested — the same consequences as
     *        \ref setRasterRenderer().
     * @note GUI-thread operation always invalidates tiles/queue/generation and emits renderer/repaint notifications; caller has already completed live renderer mutation. No renderer clone or automatic reclassification is done here.
     */
    void notifyRasterRendererEdited();

    // ----- Pixel query ----------------------------------------------------

    /*!
     * \brief Returns the pixel value at the given geographic coordinate.
     * \param mapX        X in canvas CRS.
     * \param mapY        Y in canvas CRS.
     * \param canvasSRS   Canvas CRS.
     * \param band        1-based band index.
     * \param ok          Set to true if a valid value was found.
     * @return Raw finite native-unit value from the full-resolution containing source pixel, or NaN for closed/invalid band/extent/geotransform/CRS conversion/outside/nodata/read failure. No bilinear blend, colourization, hillshade or vertical conversion.
     * @note Optional ok is initialized false and set true only on finite non-nodata success. Null/missing CRS references assume source-coordinate compatibility; different usable CRS uses an identity-pointer-keyed cached transform. Holds primary dataset mutex throughout. Caller must keep canvas/inherited SRS objects stable during lookup.
     */
    [[nodiscard]] double valueAt(double mapX, double mapY, const SpatialReferenceSystem *canvasSRS,
                                 int band = 1, bool *ok = nullptr) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    /**
     * @return True, selecting the tile-backed raster buffer path.
     */
    [[nodiscard]] bool isRasterLayer() const override { return true; }

    /*! \brief Marks this raster as a basemap (added via Add Basemap ▸ Local File).
     *  Also retags the layer type to SWMMImageryLayer so it lands in the
     *  Basemaps layer-tree category.
     * @param[in] on New full-extent exclusion policy.
     * @note Always stores flag and forwards imagery/raster layer type to the base setter; does not alter coverage/CRS, tile pixels or emit its own basemap notification.
     */
    void setIsBasemap(bool on);
    /**
     * @return Stored full-extent exclusion flag, initially false.
     */
    [[nodiscard]] bool isBasemapLayer() const override { return m_isBasemap; }

    /**
     * @brief Queue/launch fixed-grid canvas tiles, reopening handles after overview completion when required.
     * @param[in] extent Finite positive canvas-map bounds with tile indices representable as int.
     * @param[in] viewportSize Width determines map-units/pixel; nonpositive width falls back1024. Height is unused for level selection.
     * @param[in] srs Optional borrowed canvas CRS copied for later per-tile snapshots; null retains the previous template if one exists.
     * @note GUI-thread operation returns for closed/hidden source. A pending overview reload drains workers, reopens primary/pool and invalidates tiles before scheduling; failed primary reopen returns. Ordinary scheduling caps viewport request span at4096tiles.
     */
    void fetchCache(const MapExtent &extent, const QSize &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    /**
     * @brief Composite already-produced tiles without source IO or worker scheduling.
     * @param[in] painter Required active target for a drawable viewport; existing painter opacity/state is left for the caller to manage.
     * @param[in] extent Finite positive canvas-map bounds with representable tile indices, matching cache CRS generation.
     * @param[in] imageSize Positive logical pixel dimensions; width chooses level.
     * @param[in] srs Unused optional CRS because tiles were already warped.
     * @note Worker-safe base-cache resolution copies images under mutex, then draws without that lock. Does not explicitly apply layer visibility/opacity or renderer edits. Resolves nearest containing fallback within64times target width; missing images remain unpainted. Invalid/over4096tile views return.
     */
    void render(QPainter *painter, const MapExtent &extent, const QSize &imageSize,
                const SpatialReferenceSystem *srs) override;

    /**
     * @param[in] scene Forwarded optional scene, unused by current refreshScene().
     * @param[in] canvasExtent Forwarded unused map bounds.
     * @param[in] canvasSRS Forwarded unused optional CRS.
     * @note Legacy no-op path delegates to refreshScene(); raster buffers are used instead of graphics items.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] scene Optional scene; a matching tracked legacy placeholder is removed/deleted, another-scene placeholder is only forgotten.
     * @note Current render/populate path creates no scene placeholder. GUI-thread operation.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @param[in] scene Unused optional scene.
     * @param[in] canvasExtent Unused map bounds.
     * @param[in] canvasSRS Unused optional CRS.
     * @note No-op compatibility override; fetchCache()/render() own raster production/display.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] newCanvasSRS Unused optional new CRS.
     * @note GUI-thread call invalidates cache/queue/generation so old in-flight results are dropped. A later fetchCache() supplies the actual new CRS snapshot; no repaint is emitted here.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- VS.6 — hillshade relief overlay (single-band DTM rasters) ---------
    // When enabled, warpToCanvas() composites a hillshade lighting factor over
    // the colour-ramped pixels using the warped elevation grid. Parameters
    // mirror HillshadeSymbolLayerSpec; stored as primitives to keep this
    // header free of the render/ spec include.
    /**
     * @return Whether scalar tile production applies relief shading, initially false.
     */
    [[nodiscard]] bool hillshadeEnabled() const { return m_hillshadeEnabled; }
    /**
     * @param[in] on Whether scalar tile production composites lighting.
     * @note Equal value is a no-op. Change invalidates tiles/generation and requests repaint; RGB composite tiles do not use hillshade. No typed hillshade signal.
     */
    void setHillshadeEnabled(bool on);
    /**
     * @return Stored clockwise-from-north light azimuth degrees, initially315; not normalized.
     */
    [[nodiscard]] double hillshadeAzimuthDeg() const { return m_hillshadeAzimuthDeg; }
    /**
     * @return Stored light elevation degrees above horizontal, initially45; not clamped.
     */
    [[nodiscard]] double hillshadeAltitudeDeg() const { return m_hillshadeAltitudeDeg; }
    /**
     * @return Stored vertical-to-horizontal gradient scale factor, initially one.
     */
    [[nodiscard]] double hillshadeZFactor() const { return m_hillshadeZFactor; }
    /**
     * @return Stored blend strength, initially0.5; meaningful range0..1 is caller policy, not enforced.
     */
    [[nodiscard]] double hillshadeStrength() const { return m_hillshadeStrength; }
    /**
     * @param[in] azimuthDeg Finite light azimuth degrees clockwise from north.
     * @param[in] altitudeDeg Finite elevation degrees above horizon, normally0..90.
     * @param[in] zFactor Finite scale converting vertical gradients to canvas horizontal units; choose consistent units.
     * @param[in] strength Finite blend weight, normally0..1.
     * @note Raw values assigned without clamp/equality checks. If hillshade enabled, always invalidates tiles/repaints; otherwise stores silently until later enabling. No source conversion or renderer notification.
     */
    void setHillshadeParams(double azimuthDeg, double altitudeDeg, double zFactor, double strength);

signals:
    /**
     * @param[in] path Stored successful datasource path, possibly relative; emitted during adoption.
     */
    void filePathChanged(const QString &path);
    /**
     * @param[in] band New raw1-based requested index after explicit setter, including invalid indices.
     */
    void renderBandChanged(int band);
    /**
     * @param[in] value New cached nodata scalar after explicit band change, possiblyNaN when absent; open adoption does not emit this signal.
     */
    void noDataValueChanged(double value);
    /*! \brief Emitted when setRasterRenderer() swaps the renderer pointer or
     *         notifyRasterRendererEdited() reports an in-place edit.
     * @note Emitted for replacement/explicit notifyRasterRendererEdited(), after cache generation invalidation; direct setRenderBand() graduated reseeding does not emit it.
     */
    void rasterRendererChanged();

    /*! \brief Emitted on the GUI thread when \ref openAsync() completes.
     * @param[in] ok True when asynchronous worker returned a dataset; false after failed adoption closed the previous source.
     */
    void openFinished(bool ok);

    /*! \brief A background overview (`.ovr` pyramid) build has started for
     *         this raster. \p name is the display file name.
     * @param[in] name Display filename whose external overview build has just been scheduled.
     */
    void overviewBuildStarted(const QString &name);
    /*! \brief The background overview build finished (ok ⇒ pyramids ready and
     *         the render will pick them up on the next repaint).
     * @param[in] ok Whether GDALBuildOverviews succeeded; true schedules handle reload on a later fetch, not proof reload has completed.
     */
    void overviewBuildFinished(bool ok);

private:
    // Worker-thread payload for openAsync(): the GDAL open + metadata read +
    // band-stats scan produce this POD (no QObject state), which
    // applyOpenResult() then folds into the layer on the GUI thread. Defined
    // in the .cpp.
    struct OpenResult;
    /**
     * @param[in] filePath Datasource path copied into a newly opened read-only GDAL handle.
     * @return Transfer payload owning that handle with copied geometry/CRS/band-one nodata/type/palette/approximate statistics, or null dataset on open failure. Caller must adopt or close exactly once.
     * @note No QObject state touched; extent calculation uses origin/x/y pixel steps and ignores rotation/shear. Statistics can scan/cache via GDAL.
     */
    [[nodiscard]] static OpenResult doOpenWork(const QString &filePath);
    /**
     * @param[in] r Transfer payload; const raw handle ownership passes to this layer and must not be separately closed/reused.
     * @note GUI-thread operation drains/closes old pool and primary handle even for failure. Success opens new pool, applies available extent/CRS/nodata, emits path, replaces renderer with palette/RGB/graduated default and can schedule overview preprocessing. Missing new metadata can retain prior cached extent/SRS/nodata/band selection; no strict reset or rollback occurs.
     */
    void applyOpenResult(const OpenResult &r);

    /**
     * @param[in] filePath Datasource path opened/statistically inspected/adopted synchronously, possibly blocking.
     * @note GUI-thread helper replaces old source even on failure, with no error return/openFinished() signal; uses applyOpenResult() notifications/overview side effects.
     */
    void openDataset(const QString &filePath);
    /**
     * @brief Close the primary GDAL handle and clear per-band range/sample caches under its mutex.
     * @note Tile handles/slots, stored path/extent/SRS/nodata/band and cached probe-transform keys are not reset here. Caller must separately drain/close the pool as needed.
     */
    void closeDataset();
    /**
     * @note GUI-thread operation clears produced tiles, queue and queued/inflight key set then increments generation. Running warps are not cancelled; their stale results are rejected. Same-key requeue can cause harmless duplicate work. No repaint signal is emitted by this helper.
     */
    void invalidateCache();

    // ── Overview (pyramid) preprocessing — Phase 1 ────────────────────────
    /*! \brief If this raster is large, lacks internal overviews, and the
     *         auto-build preference is on, kick off a background `.ovr` build.
     *         Called on the GUI thread after a successful open.
     * @note GUI-thread guard requires open source/no current build, readable band one with no overviews, enabled auto-build preference and at least8millionpixels. Starts a background sidecar build, otherwise no-op. Does not create a full in-memory pyramid synchronously.
     */
    void maybeBuildOverviews();
    /*! \brief Launch the background GDALBuildOverviews on a worker (separate
     *         read-only handle ⇒ external `.ovr` sidecar; source untouched).
     *         On completion flags a dataset reload so the render thread reopens
     *         m_dataset and sees the new pyramids.
     * @brief Schedule external GDAL overview preprocessing using a separate read-only handle.
     * @note Requires open source with band one and no competing build. Copies path/dimensions, emits started and uses powers-of-two decimation through a<=256pixel top overview. Palette-index band uses NEAREST, otherwise AVERAGE. Writes external .ovr/PAM artifacts according to driver behavior, not source samples.
     * @note Guarded GUI completion emits finished; success marks reload pending, invalidates tile generation and requests repaint. No cancellation/source-generation guard; reopen during a build can let completion flag the currently adopted layer.
     */
    void buildOverviewsAsync();

    // Snapshot of everything a worker-thread tile warp reads, taken on the
    // GUI thread at launch so the worker never dereferences live layer state
    // (the renderer is a clone() — zero locking in the colourise loop).
    struct WarpParams {
        int renderBand = 1;
        // RGB composite path (MultiBandColorRenderer): `bands` lists the
        // source bands feeding R, G, B (and optionally A); otherwise it holds
        // the single render band and the colourise loop runs.
        bool rgb = false;
        QVector<int> bands;
        bool hasNoData = false;
        double noDataValue = 0.0;
        bool hillshadeEnabled = false;
        double hillshadeAzimuthDeg = 315.0;
        double hillshadeAltitudeDeg = 45.0;
        double hillshadeZFactor = 1.0;
        double hillshadeStrength = 0.5;
        std::shared_ptr<const OpenSWMM::Render::IRasterRenderer> renderer;
    };
    /**
     * @return Owned immutable worker settings with a clone of the live raster renderer. A valid3-band MultiBandColor selection chooses RGB plus optional alpha; otherwise selects raw renderBand. Nodata/hillshade flags copied from current layer state.
     * @note GUI-thread snapshot requires stable renderer/primary metadata; this helper itself takes no mutex and returns no GDAL handle.
     */
    [[nodiscard]] WarpParams snapshotWarpParams() const;

    /*!
     * \brief Warps \p src to match \p canvasSRS and the requested extent/size.
     * \details Pure w.r.t. the borrowed \p src handle + \p params snapshot —
     *          safe to run on N workers concurrently (one GDAL handle each).
     * \returns A QImage in ARGB32 format ready to be painted.
     * @param[in] src Required borrowed GDAL handle reserved exclusively for this warp, not closed here. Null returns null image.
     * @param[in] canvasExtent Finite positive target map bounds.
     * @param[in] canvasSRS Optional stable borrowed target CRS; absent/failed explicit reprojection can fall back to geotransform-only assumptions.
     * @param[in] pixelWidth,pixelHeight Positive output pixel dimensions with practical allocation/product limits.
     * @param[in] params Stable immutable band/renderer/nodata/hillshade snapshot, with valid band indices for src.
     * @return Owned ARGB32 image or null for explicit setup failure. Scalar destination usesFloat64/NaN nodata and renderer colours; RGB usesByte channels (values can saturate) with optional alpha.
     * @note Uses an overview-aware bilinear-decimated window when possible, then GDAL warp with approximate transformer0.125destinationpixel tolerance. Warp execution/final RasterIO statuses are unchecked, so nonnull image is not a full-success guarantee. All live-layer settings are avoided; parallel callers need separate handles/CRS objects/snapshots.
     */
    [[nodiscard]] QImage warpToCanvas(GDALDataset *src, const MapExtent &canvasExtent,
                                      const SpatialReferenceSystem *canvasSRS, int pixelWidth,
                                      int pixelHeight, const WarpParams &params) const;

    /*! \brief Phase 2 — build a small in-memory source holding ONLY the
     *         canvas-extent window of \p src, read at (roughly) the view's
     *         resolution so GDAL serves it from the nearest overview instead of
     *         traversing the full-resolution image. warpToCanvas() then warps
     *         this small source. Bands: single-band ⇒ [render band] as band 1;
     *         RGB ⇒ params.bands (R,G,B[,A]) as 1..outBands. In the source
     *         CRS with the window's geotransform.
     *         Returns nullptr on no-overlap / non-northup / failure, so the
     *         caller falls back to the full-resolution warp.
     * @param[in] src Borrowed exclusive readable dataset with at least one band and valid geotransform.
     * @param[in] canvasExtent Finite positive target bounds, transformed by four corners only when CRSs differ.
     * @param[in] canvasSRS Optional borrowed target CRS; missing/failed transform assumes compatible source coordinates.
     * @param[in] pixelWidth,pixelHeight Positive target dimensions for approximate overview-aware decimation.
     * @param[in] outBands Positive output band count, consistent with params mapping.
     * @param[in] isRGB True chooses params.bands, false repeats params.renderBand.
     * @param[in] params Immutable source-band/nodata settings.
     * @return Caller-owned GDAL MEM dataset with selected bands remapped to1..outBands, source-CRS window geotransform and bilinear decimated pixels; caller GDALClose()s it. Null for unsupported rotated/sheared/missing transform geometry, no overlap, allocation/band/IO failure.
     * @note Uses band-one type for all output bands and selected nodata for each. Four-corner envelope is approximate under curved reprojection and transform failure is not a distinct error.
     */
    [[nodiscard]] GDALDataset *buildWindowedSource(GDALDataset *src, const MapExtent &canvasExtent,
                                                   const SpatialReferenceSystem *canvasSRS,
                                                   int pixelWidth, int pixelHeight, int outBands,
                                                   bool isRGB, const WarpParams &params) const;

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
    struct TileReq {
        int level;
        int col;
        int row;
    };
    // Worker → GUI payload for one produced tile. `gen` is the cache
    // generation captured at launch; results from before an invalidateCache()
    // are dropped instead of polluting the fresh cache.
    struct TileResult {
        QString key;
        QImage img;
        MapExtent extent;
        int level = 0;
        quint64 gen = 0;
    };
    /*! \brief Discrete pyramid level for a canvas resolution (map units/pixel):
     *         level = round(log2(mupp)); tile resolution = 2^level.
     * @param[in] mapUnitsPerPixel Desired positive finite canvas resolution.
     * @return Rounded log2 resolution, or zero for nonpositive/nonfinite input; tile pixel size at level is2^level canvas units.
     */
    [[nodiscard]] static int levelForResolution(double mapUnitsPerPixel);
    /*! \brief Canvas-CRS extent of tile (level,col,row) on the fixed grid.
     * @param[in] level Finite-power-of-two representable pyramid exponent.
     * @param[in] col,row Signed grid indices around map origin; endpoints/products must remain representable.
     * @return Owned positive-Y canvas-map bounds with span256*2^level; no coordinate/overflow validation.
     */
    [[nodiscard]] static MapExtent tileExtent(int level, int col, int row);
    /**
     * @param[in] level Finite-power-of-two representable pyramid exponent.
     * @param[in] col,row Signed grid indices around map origin; endpoints/products must remain representable.
     * @return Owned exact level/column/row key string; style/CRS isolation is supplied by generation invalidation, not embedded in the key.
     */
    [[nodiscard]] static QString tileKey(int level, int col, int row);
    /*! \brief Ensure the tiles covering \p extent at the view's level are
     *         cached or queued for production.
     * @param[in] extent Finite positive canvas bounds with representable grid indices.
     * @param[in] viewportSize Width chooses nearest log2 resolution; nonpositive width substitutes1024.
     * @param[in] canvasSRS Optional borrowed CRS copied as current template; null retains previous template.
     * @note GUI-thread queues uncached/unqueued viewport tiles in centre-out order after seed tiles, at most4096viewport cells. Does not launch workers itself or remove older offscreen queued requests; invalidation resets the queue.
     */
    void requestTiles(const MapExtent &extent, const QSize &viewportSize,
                      const SpatialReferenceSystem *canvasSRS);
    /*! \brief Cold-start backstop: once per cache generation, front-queue the
     *         coarsest-level tiles covering the whole raster (≤ ~4×4) so the
     *         render fallback always has a stand-in to cut from.
     * @note GUI-thread operation once per cache generation uses source extent (four-corner target conversion when possible) to front-queue a coarse grid covering it in at most16tiles. Failed transformation uses original bounds. Cached/queued duplicates skipped; later seed eviction does not reseed until invalidation, so fallback availability is not guaranteed.
     */
    void enqueueSeedTiles();
    /*! \brief Pump: launch queued tiles into idle slots until all N slots are
     *         busy (or the queue / handle pool is exhausted).
     * @note GUI-thread pump reserves idle pooled handle/slot, clones current CRS/settings, captures generation and launches independent fixed256square warps on the dedicated pool. Returns for closed source/empty queue/no handle; no source request cancellation occurs.
     */
    void startNextTiles();
    /*! \brief GUI-thread completion of slot \p slotIndex: cache the tile,
     *         return the handle, repaint, re-pump.
     * @param[in] slotIndex Current zero-based slot ordinal; invalid/not-busy is ignored.
     * @note GUI completion folds result, always requests repaint and launches queued work into idle slots. Synchronously drained queued completions must be discarded by recreating/destroying watchers before returning to event loop.
     */
    void onSlotFinished(int slotIndex);

    // ── Parallel tile production — N-slot GDAL handle pool ───────────────
    // N read-only handles to the same file; a slot borrows one for the warp
    // duration. Acquire (startNextTiles) and release (onSlotFinished) both run
    // on the GUI thread, so the free-list needs no mutex; workers only *use*
    // their borrowed handle.
    struct TileSlot {
        std::unique_ptr<QFutureWatcher<TileResult>> watcher;
        bool busy = false;
        GDALDataset *handle = nullptr;         // borrowed from m_freeHandles
        SpatialReferenceSystem *srs = nullptr; // per-tile clone; freed on finish
    };
    /*! \brief Open the N pooled read-only handles + create the slots/watchers.
     *         Closes any previous pool first (requires no busy slot).
     * @note GUI-thread operation first closes an idle old pool, then opens up to configured concurrent read-only handles and one watcher per success. Partial open reduces concurrency, total failure leaves no slots. Requires no busy slot; does not launch queue entries.
     */
    void openHandlePool();
    /*! \brief Destroy the slots (dropping any queued stale completions) and
     *         GDALClose all pooled handles. Requires no busy slot.
     * @note GUI-thread operation requires no busy slot, destroys watchers to drop queued completion signals, clears free list and GDALCloses every owned pool handle. Does not close primary source or clear queue/cache itself.
     */
    void closeHandlePool();
    /*! \brief Synchronously wait out every in-flight tile warp and fold in its
     *         completion. Callers must recreate or destroy the slots (via
     *         openHandlePool/closeHandlePool) before returning to the event
     *         loop, so the watchers' now-stale queued signals never fire.
     * @note Blocks the caller GUI thread until every busy warp finishes, folding cache/resources without repumping/repaint. Must recreate/destroy slots before event-loop return to prevent stale watcher signals. Does not cancel work or destroy primary/pool handles.
     */
    void drainTileSlots();
    /*! \brief Fold one finished slot's result into the cache and free the
     *         slot's per-tile resources.
     * @param[in,out] slot Busy slot whose watcher future is complete; takes result, deletes per-tile CRS, returns borrowed handle to free list and marks idle.
     * @note Caches only matching-generation image (base ignores null), removes queued identity even when failed/stale. Owner-thread helper emits no repaint and assumes no external slot access.
     */
    void finishSlot(TileSlot &slot);

    /*! \brief Band 1's GDAL colour table, unfiltered. Caller holds
     *         m_datasetMutex. Empty when there is none. */
    /**
     * @return Owned unfiltered band-one palette classes, empty for closed/missing palette.
     * @note Caller must already hold m_datasetMutex; helper does not acquire it. No raw table/band pointers escape.
     */
    [[nodiscard]] QList<OpenSWMM::Render::PalettedRasterRenderer::Class>
    readColorTableLocked() const;

    QString m_filePath;
    int m_renderBand = 1;
    bool m_isBasemap = false; /*!< See setIsBasemap(). */
    double m_noDataValue = std::numeric_limits<double>::quiet_NaN();
    bool m_hasNoData = false;

    // Per-band symbology inputs (see bandRange / sampleValues). Guarded by
    // m_datasetMutex; cleared whenever the dataset closes or re-opens.
    mutable QHash<int, QPair<double, double>> m_bandRangeCache;
    mutable QHash<int, QVector<double>> m_sampleCache;

    // VS.6 — hillshade relief overlay parameters (see public accessors).
    bool m_hillshadeEnabled = false;
    double m_hillshadeAzimuthDeg = 315.0;
    double m_hillshadeAltitudeDeg = 45.0;
    double m_hillshadeZFactor = 1.0;
    double m_hillshadeStrength = 0.5;

    GDALDataset *m_dataset = nullptr; /*!< Owned GDAL dataset. */

    // Guards m_dataset for the paths that still share the primary handle:
    // valueAt() (single-pixel probe), closeDataset(), and the post-.ovr-build
    // reload/reopen. Tile warps no longer take it — each worker reads through
    // its own pooled handle with a WarpParams snapshot.
    mutable QMutex m_datasetMutex;

    // valueAt()'s cached canvas→raster probe transform (guarded by
    // m_datasetMutex). Keyed on the two OGRSpatialReference identities;
    // nullptr CT with matching keys means the CRSs are the same.
    mutable OGRCoordinateTransformation *m_probeCT = nullptr;
    mutable const OGRSpatialReference *m_probeCTCanvasKey = nullptr;
    mutable const OGRSpatialReference *m_probeCTRasterKey = nullptr;
    // Set after a background .ovr build; consumed by the next fetchCache()
    // (GUI thread), which reopens m_dataset so the new overviews become visible.
    std::atomic<bool> m_datasetReloadPending{false};
    bool m_overviewBuildInFlight = false;

    // Tile pyramid state. The tile cache itself lives in the TilePyramidLayer
    // base (mutex-guarded: render() reads from the MapRenderJob worker while
    // the GUI thread inserts). Everything below is GUI-thread only.
    QList<TileReq> m_tileQueue;                     // FIFO of tiles still to produce
    QSet<QString> m_queuedKeys;                     // de-dup: queued ∪ in-flight
    quint64 m_cacheGeneration = 0;                  // bumped by invalidateCache()
    quint64 m_seedGeneration = ~0ULL;               // last gen seeds were queued
    SpatialReferenceSystem *m_currentSRS = nullptr; // latest canvas-CRS snapshot (template)

    // Parallel production (see TileSlot above). m_tilePool bounds tile warps
    // to N threads and keeps them off the global pool used by openAsync /
    // overview builds.
    int m_maxConcurrentTiles = 1;
    QThreadPool m_tilePool;
    std::vector<GDALDataset *> m_poolHandles; // owns the N pooled handles
    QList<GDALDataset *> m_freeHandles;       // GUI-thread free-list
    std::vector<TileSlot> m_slots;

    // Persistent scene item (kept visible as placeholder until new warp completes)
    RasterTileItem *m_sceneItem = nullptr;

    // Single source of truth for raster colouring (P5/R-1). Initialised
    // eagerly in the ctor (a grayscale GraduatedRasterRenderer) so
    // rasterRenderer() never returns null; applyOpenResult() replaces it with
    // the dataset-appropriate default, and warpToCanvas() colourises through
    // a clone of it.
    std::unique_ptr<OpenSWMM::Render::IRasterRenderer> m_rasterRenderer;
};

Q_DECLARE_METATYPE(GISRasterLayer *)
// Q_DECLARE_METATYPE(RasterColorRamp) is in render/colorramp.h (Slice BB-α).

#endif // GISRASTERLAYER_H
