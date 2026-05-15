/*!
 * \file   gisvectorlayer.h
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

#ifndef GISVECTORLAYER_H
#define GISVECTORLAYER_H

#include "layers/openswmmvislayer.h"

#include <QColor>
#include <QFont>
#include <QList>
#include <QPen>
#include <QBrush>
#include <QString>
#include <QVariantMap>

class QGraphicsItem;

// Forward-declare GDAL/OGR types
class GDALDataset;
class OGRLayer;
class OGRFeature;
class OGRCoordinateTransformation;

class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;

/*!
 * \struct GISVectorSymbol
 * \brief Rendering symbology for a vector layer.
 * \details Supports point (marker), line, and polygon rendering styles.
 */
struct GISVectorSymbol
{
    // ----- Point / marker -------------------------------------------------
    enum MarkerShape { Circle, Square, Triangle, Diamond, Star, Cross };

    MarkerShape markerShape    = Circle;
    double      markerSize     = 6.0;   /*!< Marker diameter in pixels. */
    QColor      markerFill     = QColor(Qt::red);
    QColor      markerOutline  = QColor(Qt::darkRed);
    double      markerOutlineW = 1.0;

    // ----- Line -----------------------------------------------------------
    // Width is in screen pixels (cosmetic pen applied at populate time).
    // Default 2.0 — temporary until the theming component (Slice AC) takes
    // over per-attribute styling.
    QPen        linePen        = QPen(QColor(Qt::blue), 2.0);

    // ----- Polygon --------------------------------------------------------
    QBrush      polygonFill    = QBrush(QColor(100, 149, 237, 160)); // cornflower blue
    QPen        polygonOutline = QPen(QColor(Qt::darkBlue), 2.0);

    // ----- Labels ---------------------------------------------------------
    bool        showLabels     = false;
    QString     labelField;             /*!< OGR field name used for labels. */
    QFont       labelFont;
    QColor      labelColor     = Qt::black;
};

/*!
 * \class GISVectorLayer
 * \brief A map layer backed by a GDAL/OGR vector dataset.
 * \details Loads any OGR-supported format (Shapefile, GeoPackage, GeoJSON, …),
 *          reads features, reprojects them to the canvas CRS on the fly using
 *          OGRCoordinateTransformation, and renders with a configurable symbol.
 *
 *          The layer also supports:
 *          - Per-field attribute queries (filter expression).
 *          - Feature selection tracking.
 *          - An identify operation returning attributes as QVariantMap.
 */
class GISVectorLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
    Q_PROPERTY(QString    filePath     READ filePath     NOTIFY filePathChanged)
    Q_PROPERTY(QString    layerName    READ ogrLayerName NOTIFY layerNameChanged)
    Q_PROPERTY(int        featureCount READ featureCount NOTIFY featureCountChanged)
    Q_PROPERTY(QString    filterExpr   READ filterExpression WRITE setFilterExpression
               NOTIFY filterExpressionChanged)
    Q_PROPERTY(GISVectorSymbol symbol  READ symbol WRITE setSymbol NOTIFY symbolChanged)

public:

    explicit GISVectorLayer(const QString &filePath,
                            const QString &layerName = {},
                            OpenSWMMVisWorkspace *parent = nullptr);

    ~GISVectorLayer() override;

    // ----- Dataset info ---------------------------------------------------

    /*!
     * \brief Returns the file path to the OGR dataset.
     */
    [[nodiscard]] QString filePath() const;

    /*!
     * \brief Returns the OGR sub-layer name opened within the dataset.
     */
    [[nodiscard]] QString ogrLayerName() const;

    /*!
     * \brief Read-only OGRLayer handle for callers that need to walk
     *        features directly (e.g. mesh-generator constraint pulls).
     *        Returns nullptr if the dataset is not open.
     */
    [[nodiscard]] class OGRLayer *ogrLayer() const { return m_ogrLayer; }

    /*!
     * \brief Returns the number of features currently visible (after filtering).
     */
    [[nodiscard]] int featureCount() const;

    /*!
     * \brief Returns the list of field (attribute column) names.
     */
    [[nodiscard]] QStringList fieldNames() const;

    // ----- Filtering ------------------------------------------------------

    /*!
     * \brief Returns the current OGR attribute filter expression.
     */
    [[nodiscard]] QString filterExpression() const;

    /*!
     * \brief Sets an OGR SQL WHERE-clause filter on the features.
     * \param expr  OGR attribute filter, e.g. "population > 1000000".
     *              Pass an empty string to remove the filter.
     */
    void setFilterExpression(const QString &expr);

    // ----- Symbology ------------------------------------------------------

    [[nodiscard]] GISVectorSymbol symbol() const;
    void setSymbol(const GISVectorSymbol &symbol);

    // ----- Selection ------------------------------------------------------

    /*!
     * \brief Returns the set of currently selected feature IDs.
     */
    [[nodiscard]] QSet<long long> selectedFeatureIds() const;

    /*!
     * \brief Selects the features with the given IDs (replacing any prior selection).
     */
    void setSelectedFeatureIds(const QSet<long long> &ids);

    /*!
     * \brief Clears all selected features.
     */
    void clearSelection();

    // ----- Identify -------------------------------------------------------

    /*!
     * \brief Returns attributes of features that intersect the given map point.
     * \param mapX      X coordinate in the canvas CRS.
     * \param mapY      Y coordinate in the canvas CRS.
     * \param canvasSRS Canvas CRS used for the point coordinates (may be nullptr).
     * \param tolerance Search tolerance in map units.
     * \returns         List of QVariantMaps, one per matching feature.
     */
    [[nodiscard]] QList<QVariantMap> identifyAt(double mapX, double mapY,
                                                const SpatialReferenceSystem *canvasSRS,
                                                double tolerance = 1e-6) const;

    /*!
     * \brief Convenience overload — no CRS needed (uses current layer CRS).
     */
    [[nodiscard]] QList<QVariantMap> identifyAt(double mapX, double mapY,
                                                double tolerance = 1e-6) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

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
    void layerNameChanged(const QString &name);
    void featureCountChanged(int count);
    void filterExpressionChanged(const QString &expr);
    void symbolChanged(const GISVectorSymbol &symbol);
    void selectionChanged(const QSet<long long> &selectedIds);

private:
    void openDataset(const QString &filePath, const QString &layerName);
    void closeDataset();
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS);

    QString                      m_filePath;
    QString                      m_ogrLayerName;
    QString                      m_filterExpr;
    GISVectorSymbol              m_symbol;
    QSet<long long>              m_selectedIds;

    GDALDataset                 *m_dataset   = nullptr; /*!< Owned GDAL dataset. */
    OGRLayer                    *m_ogrLayer  = nullptr; /*!< Non-owning pointer into dataset. */
    OGRCoordinateTransformation *m_transform = nullptr; /*!< Owned; layer CRS → canvas CRS. */

    // Dirty flag — skip scene rebuild when only the view extent changed
    bool                         m_needsRebuild = true;

    // Owned QGraphicsItems currently in the scene — avoids O(n) scene scan
    // in depopulateScene (which previously used dynamic_cast on every item).
    QList<QGraphicsItem *>       m_sceneItems;
};

Q_DECLARE_METATYPE(GISVectorLayer *)
Q_DECLARE_METATYPE(GISVectorSymbol)

#endif // GISVECTORLAYER_H
