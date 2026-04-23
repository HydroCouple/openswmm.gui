/*!
 * \file   gisrasterlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */

#ifndef GISRASTERLAYER_H
#define GISRASTERLAYER_H

#include "layers/openswmmvislayer.h"

#include <QColor>
#include <QGradientStops>
#include <QImage>
#include <QString>

// Forward-declare GDAL types
class GDALDataset;
class OGRCoordinateTransformation;
class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;
class RasterTileItem;

/*!
 * \struct RasterColorRamp
 * \brief Defines a gradient mapping from raster value → display colour.
 */
struct RasterColorRamp
{
    double           minValue  = 0.0;
    double           maxValue  = 1.0;
    QGradientStops   stops;    /*!< Sorted list of (position [0..1], QColor) pairs. */
    bool             clampMin  = false; /*!< When true, values below minValue are transparent. */
    bool             clampMax  = false; /*!< When true, values above maxValue are transparent. */

    /*!
     * \brief Returns the interpolated colour for a normalised position in [0,1].
     */
    [[nodiscard]] QColor colorAt(double normalisedPos) const;

    /*!
     * \brief Returns the colour for a raw data value.
     */
    [[nodiscard]] QColor colorForValue(double value) const;

    /*!
     * \brief A standard grayscale ramp from black (min) to white (max).
     */
    [[nodiscard]] static RasterColorRamp grayscale(double min = 0.0, double max = 255.0);

    /*!
     * \brief A viridis-like perceptually-uniform ramp.
     */
    [[nodiscard]] static RasterColorRamp viridis(double min = 0.0, double max = 1.0);
};

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

public:

    explicit GISRasterLayer(const QString &filePath,
                            OpenSWMMVisWorkspace *parent = nullptr);

    ~GISRasterLayer() override;

    // ----- Dataset info ---------------------------------------------------

    [[nodiscard]] QString filePath()    const;
    [[nodiscard]] int     bandCount()   const;
    [[nodiscard]] int     renderBand()  const;
    [[nodiscard]] double  noDataValue() const;

    /*!
     * \brief Sets the 1-based band index to render (only applies to single-band mode).
     */
    void setRenderBand(int band);

    /*!
     * \brief Returns true when the dataset has a known no-data value.
     */
    [[nodiscard]] bool hasNoDataValue() const;

    // ----- Colour ramp ---------------------------------------------------

    [[nodiscard]] RasterColorRamp colorRamp() const;
    void setColorRamp(const RasterColorRamp &ramp);

    /*!
     * \brief Calculates the data min/max from the raster and updates the ramp range.
     * \details This may be slow for large datasets; runs in a worker thread.
     */
    void autoStretchColorRamp();

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

signals:
    void filePathChanged(const QString &path);
    void renderBandChanged(int band);
    void noDataValueChanged(double value);
    void colorRampChanged(const RasterColorRamp &ramp);

private:
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
    RasterColorRamp  m_colorRamp;

    GDALDataset     *m_dataset    = nullptr;  /*!< Owned GDAL dataset. */

    // Tile cache
    QImage           m_cachedTile;
    MapExtent        m_cacheExtent;
    int              m_cacheWidth  = 0;
    int              m_cacheHeight = 0;

    // Persistent scene item (kept visible as placeholder until new warp completes)
    RasterTileItem  *m_sceneItem  = nullptr;
};

Q_DECLARE_METATYPE(GISRasterLayer *)
Q_DECLARE_METATYPE(RasterColorRamp)

#endif // GISRASTERLAYER_H
