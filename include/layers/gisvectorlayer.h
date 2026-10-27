/**
 * @file      gisvectorlayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Map layer backed by an OGR vector dataset (Shapefile, GeoJSON, GeoPackage, …),
 *         rendering features as QGraphicsItems in the canvas overlay.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef GISVECTORLAYER_H
#define GISVECTORLAYER_H

#include "layers/openswmmvislayer.h"
#include "render/iattributeprovider.h" // Slice DM.3
#include "render/labelconfig.h"

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPolygonF>
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
class GisVectorSymbolAdapter; // persistent symbol adapter — styleSubjects()

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class RuleList; // Slice B.3 — see m_ruleList below.
}

/*!
 * \struct GISVectorSymbol
 * \brief Rendering symbology for a vector layer.
 * \details Supports point (marker), line, and polygon rendering styles.
 */
struct GISVectorSymbol {
    // ----- Point / marker -------------------------------------------------
    enum MarkerShape { Circle, Square, Triangle, Diamond, Star, Cross };

    MarkerShape markerShape = Circle;
    double markerSize = 6.0; /*!< Marker diameter in pixels. */
    QColor markerFill = QColor(Qt::red);
    QColor markerOutline = QColor(Qt::darkRed);
    double markerOutlineW = 1.0;

    // ----- Line -----------------------------------------------------------
    // Width is in screen pixels (cosmetic pen applied at populate time).
    // Default 2.0 — temporary until the theming component (Slice AC) takes
    // over per-attribute styling.
    QPen linePen = QPen(QColor(Qt::blue), 2.0);

    // ----- Polygon --------------------------------------------------------
    QBrush polygonFill = QBrush(QColor(100, 149, 237, 160)); // cornflower blue
    QPen polygonOutline = QPen(QColor(Qt::darkBlue), 2.0);

    // ----- Labels ---------------------------------------------------------
    bool showLabels = false;
    QString labelField; /*!< OGR field name used for labels. */
    QFont labelFont;
    QColor labelColor = Qt::black;

    /*! Slice X.19 — full LabelConfig (halo, placement, scale-range).
     *  Mirrors the legacy showLabels/labelField/labelFont/labelColor
     *  via GISVectorLayer::setLabelConfig; preserved here so when /
     *  if the GIS-vector .oswp persistence path lands, the richer
     *  fields ride along through the same symbol JSON. */
    OpenSWMM::Render::LabelConfig labelConfig;

    /**
     * @brief Encode marker, line, polygon and label settings as an owned JSON value.
     * @return Persistence object with ARGB colours and full labelConfig. Pen cap/join/custom dashes, brush patterns/textures and legacy font styling are not fully serialized; no validation or disk write occurs.
     */
    [[nodiscard]] QJsonObject toJson() const;
    /**
     * @brief Apply permissive persisted symbology to this value.
     * @param[in] j Present traditional fields replace current values; absent fields retain them. Invalid colours retain their previous values, and numeric/enum ranges are not checked. A present labelConfig is decoded separately from legacy label fields.
     * @note No signals, file access, renderer update or transactional validation occurs.
     */
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
                       public OpenSWMM::Render::IAttributeProvider // Slice DM.3
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Render::IAttributeProvider) // Slice DM.3
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString layerName READ ogrLayerName NOTIFY layerNameChanged)
    Q_PROPERTY(int featureCount READ featureCount NOTIFY featureCountChanged)
    Q_PROPERTY(QString filterExpr READ filterExpression WRITE setFilterExpression NOTIFY
                   filterExpressionChanged)
    Q_PROPERTY(GISVectorSymbol symbol READ symbol WRITE setSymbol NOTIFY symbolChanged)

public:
    /**
     * @brief Construct a vector source with a default single-symbol rule.
     * @param[in] filePath Datasource passed synchronously to GDAL when nonempty; empty defers loading.
     * @param[in] layerName Exact OGR sublayer name, or empty for index zero.
     * @param[in] parent Optional workspace QObject owner.
     * @note Use the GUI thread for QObject/scene operations and serialize all access to the owned OGR handle. GDAL registration is process-wide; construction may scan the source extent.
     */
    explicit GISVectorLayer(const QString &filePath, const QString &layerName = {},
                            OpenSWMMVisWorkspace *parent = nullptr);

    /**
     * @brief Close the dataset and coordinate transform, invalidating borrowed OGR handles.
     * @note Remove this layer from its scene before destruction; scene-item pointers are tracked separately from QObject children.
     */
    ~GISVectorLayer() override;

    /**
         * @brief Open and inspect a datasource on a worker, then adopt its handle on the GUI thread.
         * @param[in] filePath Copied GDAL datasource path/connection string.
         * @param[in] layerName Exact sublayer name, or empty for index zero.
         * @note Completion replaces/closes the old dataset even on failure and emits openFinished(). Deleted layers cause the orphan result to be closed. No request cancellation/order guard is provided; serialize overlapping opens if latest-request semantics are required. Existing scene items and stored filters are not automatically rebuilt/reapplied by adoption.
         */
    void openAsync(const QString &filePath, const QString &layerName = {});

    // ----- Multi-layer enumeration (sublayer picker) ----------------------

    /*!
     * \brief Lightweight description of one OGR layer inside a datasource,
     *        produced by \ref enumerateSublayers() without fully loading it.
     */
    struct OgrSublayerInfo {
        QString name;                ///< OGR layer name (GetName()).
        QString geometryType;        ///< Human label, e.g. "Polygon", "3D Line String".
        long long featureCount = -1; ///< GetFeatureCount(); -1 if unknown.
        QString crsDescription;      ///< From GetSpatialRef(); empty if none.
        int index = -1;              ///< GetLayer(i) ordinal.
    };

    /**
         * @brief Inspect all readable vector sublayers using a temporary read-only GDAL handle.
         * @param[in] filePath GDAL datasource path/connection string.
         * @param[out] errorOut Optional message destination assigned only when the datasource cannot be opened; success leaves prior text intact.
         * @return Owned descriptions in dataset ordinal order, skipping null layer handles; empty for failure or no layers. Counts force a scan on some drivers, so this synchronous call may be expensive. The temporary dataset is closed before returning.
         */
    [[nodiscard]] static QList<OgrSublayerInfo> enumerateSublayers(const QString &filePath,
                                                                   QString *errorOut = nullptr);

    // ----- Dataset info ---------------------------------------------------

    /**
         * @return Absolute filesystem interpretation of the stored datasource path, or empty when unset; not canonicalized or checked for existence.
         */
    [[nodiscard]] QString filePath() const;

    /**
         * @return Actual name of the last successfully adopted OGR sublayer, by value. A failed reopen can leave this metadata while the handle is closed.
         */
    [[nodiscard]] QString ogrLayerName() const;

    /**
         * @return Borrowed mutable OGR layer handle or null when closed, invalidated by close/reopen/destruction.
         * @note Despite this const accessor, OGR iteration/filter state is mutable. Serialize direct callers with painting, selection and editing; ownership remains with this layer.
         */
    [[nodiscard]] class OGRLayer *ogrLayer() const { return m_ogrLayer; }

    /**
         * @brief Append attribute-filtered polygon exteriors and holes in canvas scene coordinates.
         * @param[in,out] out Existing path retained and extended with each polygon ring of at least three points; nonpolygon geometry is ignored. Fill rule is not changed.
         * @note Uses the cached layer-to-canvas transform and flips Y; it does not establish a target CRS itself. Temporarily clears/restores the OGR spatial filter but advances its read cursor. No transform-error result is exposed. Serialize with other users of the same handle.
         */
    void appendScenePolygonsTo(QPainterPath &out) const;

    /**
         * @return Current OGR count under active attribute/spatial filters, zero when closed, or negative when the driver cannot supply a non-forced count; narrowed to int. Does not force a scan.
         */
    [[nodiscard]] int featureCount() const;

    /**
         * @return Owned physical OGR field names in schema order, empty when closed; geometry and synthetic fid are not added.
         */
    [[nodiscard]] QStringList fieldNames() const;

    // ----- Self-description for the Layer Properties dialog ----------------
    /**
     * @return Absolute-path source description, or an in-memory placeholder for empty path; no IO or existence check.
     */
    [[nodiscard]] QString sourceDescription() const override;
    /**
     * @return Owned display metadata for the open source: feature count, field count and names. Counts reflect current OGR filters; no schema ownership escapes.
     */
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    // ----- IAttributeProvider (Slice DM.3) --------------------------------
    //
    // GIS layers have no SWMM category. The interface's `cat` arg is
    // ignored; we always return the OGR field list (the same data
    // fieldNames() exposes) wrapped as AttributeField. All entries
    // are isDynamic=false — vector attributes don't change per
    // animation frame.
    /**
     * @brief Expose physical vector columns to generic attribute selectors.
     * @param[in] cat Ignored because OGR fields are not divided into SWMM categories.
     * @return Owned schema descriptors, all static: integer types map to LongLong, real to Double, dates to QDateTime and other types to QString. Empty when closed.
     */
    [[nodiscard]] QVector<OpenSWMM::Render::AttributeField>
    availableAttributes(OpenSWMMVis::SwmmCategory cat) const override;

    // ----- Filtering ------------------------------------------------------

    /**
         * @return Stored OGR WHERE-clause text. Open sources retain only accepted expressions;
         * a closed source can retain pending text until the next successful adoption.
         */
    [[nodiscard]] QString filterExpression() const;

    /**
         * @brief Request an OGR attribute filter and invalidate cached scene/mask geometry.
         * @param[in] expr Raw WHERE clause; empty requests clearing the filter.
         * @note Open sources use setValidatedFilterExpression() and preserve their previous filter
         * on refusal. Closed sources retain pending text for the next adoption. Effective changes
         * mark the scene dirty and emit filterExpressionChanged()/repaintRequested(); no
         * featureCountChanged() is emitted. Call the validated overload for a refusal diagnostic.
         */
    void setFilterExpression(const QString &expr);

    /** @brief Apply a vector attribute filter only after the current OGR source accepts it.
     * @param[in] expr Raw WHERE clause; empty clears the current attribute filter.
     * @param[out] error Optional refusal detail, cleared on success.
     * @return True for accepted expressions/no-ops; false when closed or rejected by GDAL.
     * @note GUI-thread/serialized source operation. A rejected expression restores the previous
     * applied filter and does not change stored text or publish a filter-change notification.
     * A successful change invalidates scene/mask caches before notifying observers. */
    bool setValidatedFilterExpression(const QString &expr, QString *error = nullptr);

    // ----- Symbology ------------------------------------------------------

    /**
     * @return Value copy of the typed scene-item symbology; renderer rules and inherited labelConfig can contain additional independent state.
     */
    [[nodiscard]] GISVectorSymbol symbol() const;
    /**
     * @brief Replace typed item symbology and request a scene rebuild.
     * @param[in] symbol Value copied without range/colour validation or equality suppression.
     * @note Resynchronizes an existing symbol adapter, marks scene dirty and always emits symbolChanged()/repaintRequested(). Does not rewrite the rule renderer or inherited label configuration.
     */
    void setSymbol(const GISVectorSymbol &symbol);

    /*! \brief Slice X.18 — full label configuration.
     *
     *         Wraps the legacy `GISVectorSymbol::showLabels` /
     *         `labelField` / `labelFont` / `labelColor` fields plus
     *         halo, placement, and scale-range so the Labels tab of
     *         LayerStyleDialog has somewhere coherent to write. */
    // VS.10 — labelConfig() inherited from OpenSWMMVisLayer; only the setter
    // is overridden to mirror fields onto the legacy GISVectorSymbol bag.
    /**
     * @brief Store inherited label policy and mirror changed legacy label fields into typed symbology.
     * @param[in] cfg Complete label settings; equality with inherited state is a no-op.
     * @note Changes to show/field/font/colour update the symbol bag and emit symbolChanged(); changes only to richer settings can leave symbol().labelConfig unchanged. Label changes mark scene content dirty so labels are rebuilt with current text/style/visibility. Base handling emits labelConfigChanged()/repaintRequested().
     */
    void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg) override;

    /**
         * @return Owned schema column names for label pickers, empty without a readable layer definition; no source scan or mutation.
         */
    [[nodiscard]] QStringList ogrFieldNames() const;

    /*! Slice U-7 — expose the GISVectorSymbol via a QObject adapter as the
     *  single styleable subject for the unified LayerStyleDialog. */
    /**
     * @brief Expose the typed symbol bag through one persistent QObject adapter.
     * @return Owned subject wrappers borrowing a lazily created layer-owned adapter. Wrappers must not outlive the layer; writes route to setSymbol() and reads resynchronize from current typed symbology.
     */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
    styleSubjects() override;

    // ----- Renderer (Slice BI Phase 8.13.6.6 + Slice B.3) -----------------
    // API plumbing only — paint loop still consults m_symbol directly.
    // Sub-phase 8.13.6.4 (deferred until Slice BB ColorRamp lands) will
    // refactor the paint loop to consult the renderer instead. Slice B.3
    // migrated the renderer's home to the m_ruleList's active Rule —
    // renderer() / setRenderer() are facades over the Rule.

    /**
         * @return Borrowed renderer of the active rule, or null if no active rule exists. Invalidated by rule/renderer replacement or layer destruction. Scene item painting currently uses the typed symbol bag.
         */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const;

    /**
         * @brief Transfer a renderer into the active rule.
         * @param[in] r Exclusively owned fresh renderer; null is ignored. If no active rule exists, the supplied object is destroyed without installation. Never wrap an already owned raw renderer pointer.
         * @note Rule changes can synchronize a SingleSymbol renderer into the typed symbol and emit rendererChanged(); this facade also emits rendererChanged(), so listeners must tolerate multiple notifications. No per-feature renderer dispatch is added to scene painting.
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
    /**
     * @return Borrowed layer-owned mutable rule model, initially containing one default rule; modifications can remove the active rule. No ownership transfer.
     */
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    /**
     * @return Borrowed layer-owned read-only rule model, valid until layer destruction.
     */
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

    // ----- Selection ------------------------------------------------------

    /**
         * @return Value copy of requested selected FIDs, which may include missing/negative IDs; does not query geometry existence.
         */
    [[nodiscard]] QSet<long long> selectedFeatureIds() const;

    /**
         * @brief Replace selection state and request item reconstruction.
         * @param[in] ids Raw feature IDs copied without source lookup/validation.
         * @note Always marks dirty and emits selectionChanged()/repaintRequested(), including equal sets.
         */
    void setSelectedFeatureIds(const QSet<long long> &ids);

    /**
         * @brief Store an empty selection through setSelectedFeatureIds(), always notifying and marking the scene dirty.
         */
    void clearSelection();

    /**
         * @brief Intersect source features against an optionally inverse-projected rectangular search.
         * @param[in] rectCanvasCrs Finite canvas-map bounds, with ordinary positive Y. Uses the current cached transform, not a new CRS argument.
         * @return Owned FIDs passing OGR envelope prefilter and exact intersection with a source-axis-aligned rectangle. With reprojection that rectangle bounds inverse-transformed corners, so it may include features outside the original canvas rectangle. Empty when closed/no matches.
         * @note Inverse transform failure falls back to original bounds. Preserves the previous spatial filter and resets reading; attribute filtering remains active. Serialize OGR access.
         */
    [[nodiscard]] QSet<long long> featureIdsInRect(const MapExtent &rectCanvasCrs) const;

    /*! \brief FIDs of features whose geometry intersects the polygon in
     *         canvas CRS, including boundary touches. Applies the inverse
     *         layer transform and preserves the existing spatial filter. */
    /**
     * @brief Intersect features with a canvas polygon after inverse projection.
     * @param[in] polygonCanvasCrs Finite canvas-map polygon, ordinary positive Y; fewer than three points returns empty.
     * @param[in] canvasSRS Optional borrowed target CRS used to update the cached transform; null leaves an existing transform intact.
     * @return Owned matching FIDs, including boundary intersections; empty for closed source, inverse-transform failure or no matches.
     * @note Uses envelope prefilter then exact source-geometry intersection, preserves spatial filter and resets the read cursor; current attribute filter remains active. Serialize handle access.
     */
    [[nodiscard]] QSet<long long>
    featureIdsInPolygon(const QPolygonF &polygonCanvasCrs,
                        const SpatialReferenceSystem *canvasSRS = nullptr) const;

    /**
         * @return Scene-bounding-box centres of current selected scene items; multipart geometry can contribute several anchors per FID. Empty before population/without selected painted items. Does not query or reproject the datasource.
         */
    [[nodiscard]] QVector<QPointF> selectedFeatureAnchors() const;

    // ----- Identify -------------------------------------------------------

    /**
         * @brief Read attributes of features within a canvas-coordinate click radius.
         * @param[in] mapX Finite canvas X, or source X when canvasSRS is null.
         * @param[in] mapY Finite ordinary map Y in the same frame as mapX, not inverted scene Y.
         * @param[in] canvasSRS Borrowed canvas CRS used to establish the source-to-canvas transform. Null selects the legacy source-coordinate tolerance-box behavior, independent of cached rendering transforms.
         * @param[in] tolerance Finite nonnegative radius in canvas units; a half-width/height in source units when canvasSRS is null.
         * @return Owned attribute maps, nearest geometry first in canvas mode with stable source-order ties; source iteration order in legacy mode. Empty for a closed source, invalid input/CRS, failed required transform or no matches. Includes fid, which a physical field of the same name can overwrite.
         * @note Canvas queries inverse-project a sampled bounding box for the OGR prefilter, then measure exact geometry distance in canvas coordinates. Preserves the previous spatial filter, resets reading and retains the attribute filter. Serialize with other handle users.
         */
    [[nodiscard]] QList<QVariantMap> identifyAt(double mapX, double mapY,
                                                const SpatialReferenceSystem *canvasSRS,
                                                double tolerance = 1e-6) const;

    /**
         * @brief Identify against a source-coordinate tolerance box.
         * @param[in] mapX Finite source-coordinate X.
         * @param[in] mapY Finite source-coordinate Y, not inverted scene Y.
         * @param[in] tolerance Nonnegative source-coordinate search half-width/height.
         * @return identifyAt(mapX,mapY,nullptr,tolerance), preserving source iteration order and any prior spatial/attribute filters. A cached rendering transform does not change the input frame.
         */
    [[nodiscard]] QList<QVariantMap> identifyAt(double mapX, double mapY,
                                                double tolerance = 1e-6) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    /**
     * @brief Append scene items for the entire attribute-filtered vector source.
     * @param[in] scene Required borrowed destination scene when the layer is open/visible; owns added graphics items until depopulateScene() deletes them.
     * @param[in] canvasExtent Ignored; scene culling handles viewport changes without restricting loaded features.
     * @param[in] canvasSRS Optional borrowed canvas CRS establishes the cached transform; absent CRS retains existing transform/assumes compatible coordinates.
     * @note GUI-thread operation. Clears any spatial filter, reads supported single/multi point/line/polygon geometry, flips Y and adds typed-symbol items. Enabled labels use the configured field or token template (FID fallback), numeric precision and shared LabelPainter at a projected feature centroid. Label items retain screen-pixel font/halo/placement, honor layer opacity/visibility and configured scale bounds, and do not intercept mouse selection. Scene culling applies; GIS labels have no additional collision-pruning policy. Call depopulateScene() first to avoid duplicates. Transform failures have no return channel. Successful population clears the dirty flag; closed/hidden layers return early.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @brief Remove and delete this layer's tracked scene items.
     * @param[in] scene Borrowed scene containing those items; null or empty tracked list is a no-op.
     * @note GUI thread only. Clears tracked pointers and marks scene dirty after deletion; external deletion of tracked items is unsupported.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @brief Rebuild dirty items or update their opacity in place.
     * @param[in] scene Required borrowed destination scene for a dirty visible/open layer; see populateScene().
     * @param[in] canvasExtent Forwarded to population, where it is ignored.
     * @param[in] canvasSRS Optional borrowed canvas CRS used only when rebuilding.
     * @note GUI thread only. Dirty refresh deletes/repopulates all tracked items and finally clears the dirty flag even if population skipped a hidden/closed layer. A clean refresh changes only item opacity.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] newCanvasSRS Optional borrowed new target CRS; null cannot establish a new transform.
     * @note Invalidates mask geometry/transform cache, updates the transform if possible and marks scene dirty. Does not rebuild items or emit a repaint request itself.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

signals:
    /**
     * @brief Notify the associated layer-state transition.
     * @param[in] path Stored datasource path from a successful adoption, which may remain relative.
     */
    void filePathChanged(const QString &path);
    /**
     * @brief Notify the associated layer-state transition.
     * @param[in] name Actual adopted OGR layer name.
     */
    void layerNameChanged(const QString &name);
    /**
     * @brief Notify the associated layer-state transition.
     * @param[in] count Current non-forced filtered count after successful adoption; not emitted for every filter/edit.
     */
    void featureCountChanged(int count);
    /**
     * @brief Notify the associated layer-state transition.
     * @param[in] expr New requested raw attribute expression, regardless of GDAL acceptance.
     */
    void filterExpressionChanged(const QString &expr);
    /**
     * @brief Notify the associated layer-state transition.
     * @param[in] symbol New copied typed symbol state, before repaint request on setSymbol().
     */
    void symbolChanged(const GISVectorSymbol &symbol);

    // VS.10 — labelConfigChanged() is inherited from OpenSWMMVisLayer.
    /**
     * @brief Notify the associated layer-state transition.
     * @param[in] selectedIds New raw selected-ID set, including unchanged/missing IDs.
     */
    void selectionChanged(const QSet<long long> &selectedIds);
    /**
         * @brief Notify renderer replacement or rule-driven changes; one operation can emit multiple notifications.
         * @note Typed scene symbology is synchronized only for supported SingleSymbol renderers.
         */
    void rendererChanged();

    /**
         * @brief Notify the associated layer-state transition.
         * @param[in] ok True when the worker returned both dataset/layer handles; false after a failed adoption has closed the old dataset.
         */
    void openFinished(bool ok);

    /**
         * @brief Notify the associated layer-state transition.
         * @param[in] filePath Stored source path with no declared CRS; emitted once per dataset lifecycle when a usable canvas CRS is requested.
         */
    void crsAssumed(const QString &filePath);

private:
    // Worker-thread payload for openAsync(): GDALOpenEx + GetLayer + extent +
    // CRS produce this POD (no QObject state), folded into the layer on the
    // GUI thread by applyOpenResult(). Defined in the .cpp.
    struct OpenResult;
    /**
         * @brief Open a datasource and collect extent/CRS without touching QObject state.
         * @param[in] filePath Copied GDAL datasource path.
         * @param[in] layerName Exact name, or empty for first sublayer.
         * @param[in] openFlags GDALOpenEx flags; zero substitutes vector/read-only.
         * @return Transfer payload owning a dataset handle with a borrowed layer pointer, or null handles after failure. Caller must adopt or GDALClose the returned dataset exactly once. Extent/count work can scan the source.
         */
    [[nodiscard]] static OpenResult doOpenWork(const QString &filePath, const QString &layerName,
                                               unsigned openFlags);
    /**
     * @brief Adopt a worker payload into the GUI-owned layer.
     * @param[in] r Transfer payload whose raw dataset ownership passes to this layer despite the const reference; do not close/reuse it after adoption.
     * @note Always invalidates mask cache and closes prior handles. Failure leaves no dataset but may retain old path/name/extent/SRS metadata; success updates available extent/CRS/name and emits path/name/count signals. Does not rebuild items, reapply filters or emit openFinished() itself.
     */
    void applyOpenResult(const OpenResult &r);

protected:
    /**
         * @param[in] flags GDALOpenEx flags copied for subsequent opens, without reopening the current handle.
         * @note Defaults to vector/read-only; subclasses needing updates must construct with an empty path, set flags, then open explicitly because base construction opens synchronously.
         */
    void setOpenFlags(unsigned flags) { m_openFlags = flags; }
    /**
     * @return Stored GDALOpenEx flags for future synchronous/asynchronous opens; no handle access.
     */
    [[nodiscard]] unsigned openFlags() const { return m_openFlags; }

    /**
         * @return Borrowed mutable dataset or null when closed, invalidated by reopen/close/destruction. Serialize access with OGR/scene operations; layer retains closure responsibility.
         */
    [[nodiscard]] GDALDataset *dataset() const { return m_dataset; }

    /**
         * @brief Force the next refreshScene() to rebuild items after a source edit; emits no notification and performs no IO.
         */
    void markSceneDirty() { m_needsRebuild = true; }

    /**
         * @brief Synchronously open and adopt a datasource using current open flags.
         * @param[in] filePath GDAL datasource path/connection string.
         * @param[in] layerName Exact OGR name, or empty for index zero.
         * @note GUI-thread adoption can block on extent scans. Closes old source even on failure; no error return/openFinished() signal or automatic scene rebuild is provided.
         */
    void openDataset(const QString &filePath, const QString &layerName);

    //! Closes whatever is open, releasing the dataset.
    /**
     * @brief Release owned transform/dataset, clear borrowed OGR handle and reset CRS warning/cache state.
     * @note Does not delete scene items or clear stored path/name/extent/SRS/filter metadata. Borrowed dataset/layer pointers become invalid.
     */
    void closeDataset();

private:
    /**
     * @brief Replace the owned layer-to-canvas coordinate transformation.
     * @param[in] canvasSRS Optional borrowed destination CRS; absent/invalid/equivalent CRS or closed source leaves no transform.
     * @note Destroys the previous transform first; creation failure silently leaves null. Caller manages cache key and serializes mutable const-state access.
     */
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS) const;

    /**
         * @brief Cache a layer-to-canvas transformation for a usable target CRS.
         * @param[in] canvasSRS Optional borrowed destination CRS; absent/invalid input leaves any existing transform unchanged.
         * @note Unchanged nonempty WKT skips rebuilding, including after a failed transform creation. A source without CRS assumes compatible coordinates and emits crsAssumed() once. Mutable cache/filter consumers require serialized access.
         */
    void ensureTransform(const SpatialReferenceSystem *canvasSRS) const;

    /**
         * @brief Translate supported active SingleSymbol rule settings into the typed item symbol.
         * @note Reads the first symbol layer only, retaining typed fallback values for missing/invalid properties. Unsupported renderers are ignored; equal derived fields suppress setSymbol(). Composite layers/renderer opacity are not applied here.
         */
    void syncSymbolFromRenderer();

    QString m_filePath;
    QString m_ogrLayerName;
    QString m_filterExpr;
    GISVectorSymbol m_symbol;
    // Persistent symbol adapter (adapter-ownership refactor) — lazily built
    // by styleSubjects(), owned via QObject parenting.
    GisVectorSymbolAdapter *m_symbolAdapter = nullptr;
    // VS.10 — m_labelConfig moved to OpenSWMMVisLayer (base owns it now).
    QSet<long long> m_selectedIds;

    /*! GDALOpenEx flags; see \ref setOpenFlags. Stored rather than virtual
     *  because \ref openDataset is reachable from the constructor, where a
     *  virtual would not dispatch to the subclass. */
    unsigned m_openFlags = 0; // seeded in the ctor

    GDALDataset *m_dataset = nullptr; /*!< Owned GDAL dataset. */
    OGRLayer *m_ogrLayer = nullptr;   /*!< Non-owning pointer into dataset. */
    mutable OGRCoordinateTransformation *m_transform =
        nullptr; /*!< Owned; layer CRS → canvas CRS. */

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
    std::unique_ptr<OpenSWMM::Render::RuleList> m_ruleList;

    // Dirty flag — skip scene rebuild when only the view extent changed
    bool m_needsRebuild = true;

    // Owned QGraphicsItems currently in the scene — avoids O(n) scene scan
    // in depopulateScene (which previously used dynamic_cast on every item).
    QList<QGraphicsItem *> m_sceneItems;
};

Q_DECLARE_METATYPE(GISVectorLayer *)
Q_DECLARE_METATYPE(GISVectorSymbol)

#endif // GISVECTORLAYER_H
