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

#include "layers/openswmmvislayer.h"
#include "render/colorramp.h"

#include <QImage>
#include <QString>

#include <memory>

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
class GISRasterLayer : public OpenSWMMVisLayer
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

    /*!
     * \brief Warps the dataset to match \p canvasSRS and the requested extent/size.
     * \returns A QImage in ARGB32 format ready to be painted.
     */
    [[nodiscard]] QImage warpToCanvas(const MapExtent &canvasExtent,
                                      const SpatialReferenceSystem *canvasSRS,
                                      int pixelWidth,
                                      int pixelHeight) const;

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

    // Float64 warped value cache — same spatial extent as m_cachedTile / m_cacheExtent
    // but stores raw elevation values (not color-rendered).  Populated by warpToCanvas
    // for single-band rasters (mutable because warpToCanvas is const).
    mutable QVector<double>  m_rawValueCache;
    mutable int              m_rawCacheWidth  = 0;
    mutable int              m_rawCacheHeight = 0;

    GDALDataset     *m_dataset    = nullptr;  /*!< Owned GDAL dataset. */

    // Tile cache
    QImage           m_cachedTile;
    MapExtent        m_cacheExtent;
    int              m_cacheWidth  = 0;
    int              m_cacheHeight = 0;

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
