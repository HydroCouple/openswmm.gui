/*!
 * \file   gisvectorlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Map layer backed by an OGR vector dataset (Shapefile, GeoJSON, GeoPackage, …),
 *         rendering features as QGraphicsItems in the canvas overlay.
 */

#ifndef GISVECTORLAYER_H
#define GISVECTORLAYER_H

#include "layers/openswmmvislayer.h"
#include "render/iattributeprovider.h"   // Slice DM.3
#include "render/labelconfig.h"

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QString>
#include <QVariantMap>

#include <memory>

class QGraphicsItem;

// Forward-declare GDAL/OGR types
class GDALDataset;
class OGRLayer;
class OGRFeature;
class OGRCoordinateTransformation;

class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;
class GisVectorSymbolAdapter;   // persistent symbol adapter — styleSubjects()

namespace OpenSWMM::Render {
class IFeatureRenderer;
class RuleList;   // Slice B.3 — see m_ruleList below.
}

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

    /*! Slice X.19 — full LabelConfig (halo, placement, scale-range).
     *  Mirrors the legacy showLabels/labelField/labelFont/labelColor
     *  via GISVectorLayer::setLabelConfig; preserved here so when /
     *  if the GIS-vector .oswp persistence path lands, the richer
     *  fields ride along through the same symbol JSON. */
    OpenSWMM::Render::LabelConfig labelConfig;

    [[nodiscard]] QJsonObject toJson() const;
    void fromJson(const QJsonObject &j);
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
class GISVectorLayer : public OpenSWMMVisLayer,
                       public OpenSWMM::Render::IAttributeProvider  // Slice DM.3
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Render::IAttributeProvider)  // Slice DM.3
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

    /*!
     * \brief Asynchronously open \p filePath / \p layerName. GDALOpenEx +
     *        GetExtent (which forces a scan on some drivers) run on a worker
     *        thread; the layer is populated on the GUI thread and
     *        \ref openFinished(bool) fires on completion. Construct with an
     *        empty path first; the synchronous \ref openDataset path (tests /
     *        project restore) is unchanged.
     */
    void openAsync(const QString &filePath, const QString &layerName = {});

    // ----- Multi-layer enumeration (sublayer picker) ----------------------

    /*!
     * \brief Lightweight description of one OGR layer inside a datasource,
     *        produced by \ref enumerateSublayers() without fully loading it.
     */
    struct OgrSublayerInfo
    {
        QString   name;               ///< OGR layer name (GetName()).
        QString   geometryType;       ///< Human label, e.g. "Polygon", "3D Line String".
        long long featureCount = -1;  ///< GetFeatureCount(); -1 if unknown.
        QString   crsDescription;     ///< From GetSpatialRef(); empty if none.
        int       index = -1;         ///< GetLayer(i) ordinal.
    };

    /*!
     * \brief Enumerate the layers in a vector datasource without fully loading
     *        any of them. Opens read-only, reads each OGR layer's name /
     *        geometry / feature-count / CRS, then closes.
     * \param filePath  Datasource path (GeoPackage, File GDB, GML, …).
     * \param errorOut  Optional; set to a human-readable message on failure.
     * \return One entry per layer; empty on failure or when the path is not a
     *         readable vector datasource.
     */
    [[nodiscard]] static QList<OgrSublayerInfo>
        enumerateSublayers(const QString &filePath, QString *errorOut = nullptr);

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
     * \brief Slice Z.14-paint — append every polygon ring this layer
     *        currently exposes (post-attribute-filter) to \p out in
     *        canvas-scene coords (Y-flipped, same space the canvas
     *        QGraphicsItems paint into).
     *
     *        Multi-polygons contribute each sub-polygon's exterior +
     *        holes. Non-polygon geometries are skipped silently —
     *        callers (e.g. the mask-clip resolver) treat that as an
     *        empty result and fall back to unclipped paint.
     *
     *        Idempotent w.r.t. the spatial-filter rectangle the canvas
     *        has set on the underlying OGRLayer; the method clears the
     *        filter, iterates every feature, then restores the prior
     *        filter so concurrent canvas painting isn't disturbed.
     */
    void appendScenePolygonsTo(QPainterPath &out) const;

    /*!
     * \brief Returns the number of features currently visible (after filtering).
     */
    [[nodiscard]] int featureCount() const;

    /*!
     * \brief Returns the list of field (attribute column) names.
     */
    [[nodiscard]] QStringList fieldNames() const;

    // ----- Self-description for the Layer Properties dialog ----------------
    [[nodiscard]] QString sourceDescription() const override;
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    // ----- IAttributeProvider (Slice DM.3) --------------------------------
    //
    // GIS layers have no SWMM category. The interface's `cat` arg is
    // ignored; we always return the OGR field list (the same data
    // fieldNames() exposes) wrapped as AttributeField. All entries
    // are isDynamic=false — vector attributes don't change per
    // animation frame.
    [[nodiscard]] QVector<OpenSWMM::Render::AttributeField>
        availableAttributes(OpenSWMMVis::SwmmCategory cat) const override;

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

    /*! \brief Slice X.18 — full label configuration.
     *
     *         Wraps the legacy `GISVectorSymbol::showLabels` /
     *         `labelField` / `labelFont` / `labelColor` fields plus
     *         halo, placement, and scale-range so the Labels tab of
     *         LayerStyleDialog has somewhere coherent to write. */
    // VS.10 — labelConfig() inherited from OpenSWMMVisLayer; only the setter
    // is overridden to mirror fields onto the legacy GISVectorSymbol bag.
    void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg) override;

    /*! \brief Names of the OGR fields available for label expressions.
     *         Empty when no dataset has been opened yet.  Used by the
     *         Labels tab to populate the field combobox. */
    [[nodiscard]] QStringList ogrFieldNames() const;

    /*! Slice U-7 — expose the GISVectorSymbol via a QObject adapter as the
     *  single styleable subject for the unified LayerStyleDialog. */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
        styleSubjects() override;

    // ----- Renderer (Slice BI Phase 8.13.6.6 + Slice B.3) -----------------
    // API plumbing only — paint loop still consults m_symbol directly.
    // Sub-phase 8.13.6.4 (deferred until Slice BB ColorRamp lands) will
    // refactor the paint loop to consult the renderer instead. Slice B.3
    // migrated the renderer's home to the m_ruleList's active Rule —
    // renderer() / setRenderer() are facades over the Rule.

    /*!
     * \brief The IFeatureRenderer that will drive this layer's paint pass.
     * \details Constructed eagerly as a default SingleSymbolRenderer so
     *          callers never have to null-check.  Owned by the layer.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  A null pointer is rejected (the
     *          method silently no-ops).  Emits \ref rendererChanged() when
     *          the renderer pointer actually changes.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    // ----- Rule Model (Slice B.3, Phase B) --------------------------------
    //
    // GISVectorLayer is the first concrete layer to migrate to the Rule
    // Model. The layer owns a single-Rule RuleList that wraps the
    // renderer; `renderer()` and `setRenderer()` are facades over the
    // active Rule's owned IFeatureRenderer. LayerStyleDialog detects
    // `ruleList() != nullptr` and mounts RuleSymbologyTab in the
    // Symbology tab (Slice B.2 dispatch).
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

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

    /*!
     * \brief SVBC round B — fids of every feature whose GEOMETRY intersects
     *        \p rectCanvasCrs (OGR bbox prefilter, then precise Intersects).
     *        The rect arrives in canvas CRS and is inverse-transformed when
     *        the layer is reprojected. Any pre-existing spatial filter is
     *        saved and restored (clone dance).
     */
    [[nodiscard]] QSet<long long> featureIdsInRect(
        const MapExtent &rectCanvasCrs) const;

    /*!
     * \brief Scene-space centres of the selected features' items — beacon
     *        anchors for MapCanvas::flashSelection.
     */
    [[nodiscard]] QVector<QPointF> selectedFeatureAnchors() const;

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

    // VS.10 — labelConfigChanged() is inherited from OpenSWMMVisLayer.
    void selectionChanged(const QSet<long long> &selectedIds);
    /*! \brief Emitted when setRenderer() swaps the renderer pointer. */
    void rendererChanged();

    /*! \brief Emitted on the GUI thread when \ref openAsync() completes. */
    void openFinished(bool ok);

    /*!
     * \brief Emitted once when this layer's file declares no CRS and its
     *        coordinates are therefore assumed to already be in the canvas CRS.
     *
     * \details Not an error — local-coordinate data legitimately has no CRS,
     *          and this is what the layer has always done. It is surfaced so a
     *          layer that lands in the wrong place has a stated reason.
     */
    void crsAssumed(const QString &filePath);

private:
    // Worker-thread payload for openAsync(): GDALOpenEx + GetLayer + extent +
    // CRS produce this POD (no QObject state), folded into the layer on the
    // GUI thread by applyOpenResult(). Defined in the .cpp.
    struct OpenResult;
    [[nodiscard]] static OpenResult doOpenWork(const QString &filePath,
                                               const QString &layerName);
    void applyOpenResult(const OpenResult &r);

    void openDataset(const QString &filePath, const QString &layerName);
    void closeDataset();
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS) const;

    /*!
     * \brief Make \ref m_transform current for \p canvasSRS, building it on
     *        first use.
     *
     * \details rebuildTransform() used to be reachable only from
     *          onCanvasCRSChanged(), so a layer added to a canvas whose CRS
     *          never subsequently changed — the ordinary case — kept a null
     *          transform and drew its raw file coordinates as though they were
     *          already in the canvas CRS. Every entry point that is handed a
     *          canvas CRS calls this first, so the transform exists from the
     *          first paint. The canvas WKT is cached because populateScene()
     *          runs per repaint and OGRCreateCoordinateTransformation is far
     *          too expensive to redo per frame.
     *
     *          A layer whose file declares no CRS keeps a null transform: its
     *          coordinates are assumed to be in the canvas CRS already (the
     *          long-standing behaviour, and what local-coordinate data needs).
     *          That assumption is announced once via \ref crsAssumed so a
     *          misplaced layer is explicable instead of mysterious.
     */
    void ensureTransform(const SpatialReferenceSystem *canvasSRS) const;

    /*! \brief Derive a GISVectorSymbol from the active Rule's renderer
     *         and feed it through setSymbol(). Called when the Rule's
     *         renderer state changes (Symbology-tab edits via
     *         SymbolStyleAdapter). For SingleSymbol the derivation is
     *         a single symbolFor() call; per-feature dispatch (needed
     *         for Graduated / Categorized) lands in a follow-up. */
    void syncSymbolFromRenderer();

    QString                      m_filePath;
    QString                      m_ogrLayerName;
    QString                      m_filterExpr;
    GISVectorSymbol              m_symbol;
    // Persistent symbol adapter (adapter-ownership refactor) — lazily built
    // by styleSubjects(), owned via QObject parenting.
    GisVectorSymbolAdapter      *m_symbolAdapter = nullptr;
    // VS.10 — m_labelConfig moved to OpenSWMMVisLayer (base owns it now).
    QSet<long long>              m_selectedIds;

    GDALDataset                 *m_dataset   = nullptr; /*!< Owned GDAL dataset. */
    OGRLayer                    *m_ogrLayer  = nullptr; /*!< Non-owning pointer into dataset. */
    mutable OGRCoordinateTransformation *m_transform = nullptr; /*!< Owned; layer CRS → canvas CRS. */

    /*! WKT of the canvas CRS \ref m_transform was built for; empty when none
     *  has been built. Guards the per-repaint rebuild. */
    mutable QString m_transformCanvasWkt;

    /*! One-shot latch for the "file declares no CRS" notice (\ref crsAssumed). */
    mutable bool m_warnedNoCRS = false;

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Initialised eagerly in
    // the ctor (default SingleSymbolRenderer) so renderer() never returns
    // null.  Paint refactor deferred until Slice BB ColorRamp ships.
    //
    // Slice B.3 — renderer ownership migrated to m_ruleList. The
    // canonical home for the renderer is now the active Rule's owned
    // IFeatureRenderer. renderer() and setRenderer() are thin facades
    // over m_ruleList->activeRule()->renderer() — no per-layer unique_ptr
    // duplicate. Legend code paths (legendoverlay, legendlayertreemodel,
    // legendclasseditcommands) keep working through the facade.
    std::unique_ptr<OpenSWMM::Render::RuleList>         m_ruleList;

    // Dirty flag — skip scene rebuild when only the view extent changed
    bool                         m_needsRebuild = true;

    // Owned QGraphicsItems currently in the scene — avoids O(n) scene scan
    // in depopulateScene (which previously used dynamic_cast on every item).
    QList<QGraphicsItem *>       m_sceneItems;
};

Q_DECLARE_METATYPE(GISVectorLayer *)
Q_DECLARE_METATYPE(GISVectorSymbol)

#endif // GISVECTORLAYER_H
