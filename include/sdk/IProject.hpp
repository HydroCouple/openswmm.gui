/**
 * @file IProject.hpp
 * @brief Project data, coordinate conversion and spatial download-preview services.
 * @details Boundary and preview geometry uses RFC 7946 longitude/latitude. Polygon holes and multipart structure are preserved. Every method runs on the GUI thread and returns owning value copies.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "IModelInstance.hpp"
#include <QJsonObject>
#include <QPointF>

namespace OpenSWMM::Sdk
{
/** Project metadata and CRS services. Extension data is scoped by the host. */
class IProject
{
public:
    /** @brief Destroy a facade; extensions must not delete the host-owned project. */
    virtual ~IProject() = default;
    /** @brief Identify the current project facade. @return Persistent instance/project UUID or null after closure. */
    virtual QUuid id() const = 0;
    /** @brief Locate the project sidecar. @return Absolute .oswp path, inferred beside the input until first save. */
    virtual QString path() const = 0;
    /** @brief Locate reviewable project assets. @return Absolute containing directory or empty after closure. */
    virtual QString directory() const = 0;
    /** @brief Enumerate live model instances. @return Borrowed host-owned facades; closed instances are omitted. */
    virtual QList<IModelInstance *> instances() const = 0;
    /** @brief Read this extension's project-specific state.
     * @return Owning JSON from extensions.&lt;pluginId&gt;; other plugin state is hidden. */
    virtual QJsonObject extensionData() const = 0;
    /** @brief Replace this extension's persisted state and mark the project dirty.
     * @param[in] data Owning JSON copied into the .oswp extensions namespace. */
    virtual void setExtensionData(const QJsonObject &data) = 0;
    /** @brief Save a recovery project referencing an independent snapshot of the current model.
     * @param[out] error Optional lease, snapshot or project-write failure detail.
     * @return Absolute recovery .oswp path under the project/user-data recovery directory, or empty.
     * @note Does not replace the current project/input, clear its dirty state, or change its path. */
    virtual QString autosave(QString *error = nullptr) const = 0;
    /** @brief Read model coordinate reference metadata. @return WKT, or empty if no model CRS is available. */
    virtual QString crsWkt() const = 0;
    /** @brief Read the model CRS authority. @return Authority string such as EPSG:26918, or empty if unknown. */
    virtual QString crsAuthority() const = 0;
    /** @brief Transform a model-coordinate point to longitude/latitude.
     * @param[in] point X/Y in the model CRS and its native units.
     * @param[out] lonLat Required destination in WGS84 degrees, X=longitude and Y=latitude.
     * @return False on absent CRS or transformation failure; output remains unchanged. */
    virtual bool toLonLat(QPointF point, QPointF *lonLat) const = 0;
    /** @brief Transform longitude/latitude into model coordinates.
     * @param[in] lonLat WGS84 degrees in longitude,latitude order.
     * @param[out] point Required destination in the model CRS and its native units.
     * @return False on absent CRS or transformation failure; output remains unchanged. */
    virtual bool fromLonLat(QPointF lonLat, QPointF *point) const = 0;
    /** @brief Enumerate the project's current map layers without exposing private layer objects.
     * @return Owning maps with id, title and visible fields; an empty list follows closure. */
    virtual QList<QVariantMap> layers() const = 0;
    /** @brief Add a local GDAL-readable raster as a normal persistent map layer.
     * @param[in] path Existing downloaded/local raster path.
     * @param[in] title User-facing layer caption, or empty to keep its source-derived title.
     * @param[out] error Optional load failure detail.
     * @return Stable layer identity, or empty when the file cannot be opened.
     * @note GUI-thread operation; perform downloading and raster preparation in a job first. */
    virtual QString addRasterLayer(const QString &path, const QString &title,
                                   QString *error = nullptr) = 0;
    /** @brief Add a local vector datasource as a normal persistent feature layer.
     * @param[in] path Existing downloaded/local vector file, such as GeoPackage or GeoJSON.
     * @param[in] title User-facing caption, or empty for the source-derived title.
     * @param[out] error Optional load failure detail.
     * @return Stable layer identity, or empty when the first vector sublayer cannot be opened. */
    virtual QString addFeatureLayer(const QString &path, const QString &title,
                                    QString *error = nullptr) = 0;
    /** RFC 7946 GeoJSON polygon in longitude/latitude, sampled along viewport edges. */
    /** @brief Capture the current visible map area as RFC 7946 polygon geometry.
     * @param[out] error Optional CRS/transformation failure detail.
     * @return Owning geometry in WGS84 degrees, or an empty object on failure.
     * @note Each viewport edge is sampled before reprojection to represent curved projected boundaries.
     * A caller must freeze this geometry with preview results and invalidate it after viewportChanged. */
    virtual QJsonObject viewportBoundary(QString *error = nullptr) const = 0;
    /** Loaded polygon layers: {id, title, count}; includes editable feature layers. */
    /** @brief Enumerate loaded sources that can provide polygon download boundaries.
     * @return Metadata maps with id, title and count. Editable feature layers are included;
     * unknown/mixed geometry layers are inspected feature by feature by boundaryFeatures(). */
    virtual QList<QVariantMap> polygonLayers() const = 0;
    /** Feature metadata only, paged; no data payload is downloaded by these calls. */
    /** @brief Page through polygon feature identities without copying their complete geometries.
     * @param[in] layerId Loaded GIS/feature layer identity from polygonLayers().
     * @param[in] selectedOnly Restrict entries to selected feature IDs in that layer.
     * @param[in] offset Zero-based polygon-result offset; negative values return no results.
     * @param[in] limit Requested page size, capped at 5000; nonpositive values return no results.
     * @return Owning maps with id (64-bit FID), title and selected flag; holes/multipart data is read separately.
     * @note Existing layer attribute/spatial filters apply. Reading is synchronous on the GUI thread. */
    virtual QList<QVariantMap> boundaryFeatures(const QString &layerId, bool selectedOnly = false,
                                                int offset = 0, int limit = 500) const = 0;
    /** Exact polygon/multipolygon geometry, preserving holes, in RFC 7946 longitude/latitude. */
    /** @brief Extract the exact chosen boundary in RFC 7946 longitude/latitude.
     * @param[in] layerId Loaded polygon layer identity.
     * @param[in] featureId Source FID returned by boundaryFeatures().
     * @param[out] error Optional missing-feature, wrong-geometry or CRS failure detail.
     * @return Polygon/MultiPolygon GeoJSON geometry preserving holes and parts; empty on failure.
     * @note Use an envelope only to query a remote catalogue, then intersect results with this actual geometry. */
    virtual QJsonObject boundaryPolygon(const QString &layerId, qint64 featureId,
                                        QString *error = nullptr) const = 0;
    /** Transient points/footprints in RFC 7946 GeoJSON; never persisted as model layers. */
    /** @brief Replace this extension's transient map preview of sites or product footprints.
     * @param[in] featureCollection RFC 7946 FeatureCollection containing Point/MultiPoint and Polygon/MultiPolygon geometries.
     * @param[out] error Optional transformation failure detail.
     * @return True after replacing the overlay; false retains the preceding preview.
     * @note Preview is transformed to the canvas CRS and is never saved as a model layer or download payload.
     * Optional collection-level `previewStyle: {pointLabelProperty: "field"}` draws each
     * point's string-valued `properties.field` beside its fixed-pixel marker. Missing,
     * empty or non-string values have no label. Text is plain, whitespace-normalized,
     * limited to 256 characters and starts six pixels right and sixteen pixels above
     * the marker center. Polygon geometry is unaffected. Omitting the hint preserves
     * the geometry-only presentation.
     * The host retains the display until replacement, clearPreview() or facade destruction;
     * extensions remain responsible for tracking whether displayed results are still
     * eligible for export after viewport/source changes. No review authority is conferred.
     *
     * `previewStyle: {asFeatureLayer: true, title: "Sites"}` instead creates a
     * host-owned temporary native GIS layer. It appears in Layers and the native
     * attribute table, query, selection and styling tools, but is excluded from
     * project persistence and boundary choices. Its datasource exists only in GDAL
     * virtual memory. Repeating an identical collection preserves its native identity,
     * filter, selection and style; changed input is validated before replacement.
     * Manually removing it suppresses subsequent refreshes until clearPreview()
     * explicitly resets that choice. The host clears it when its model closes or
     * this extension's project facade is destroyed. Normal native layer/revision
     * events are still published; callers must reconcile review authority separately.
     * Native previews accept at most 5000 features, 256 distinct source attributes,
     * 256 characters per nonempty attribute name, and 20 MiB of normalized GeoJSON.
     * Scalar property types use GDAL's schema inference; nested objects and arrays
     * are compact JSON strings in the native copy. A requested point label adds a
     * uniquely named `__openswmm_preview_label` display column, preserving the original
     * attribute. Native edits to presentation/filter/selection are session-only;
     * saving source records remains an explicit consumer operation. */
    virtual bool showPreview(const QJsonObject &featureCollection, QString *error = nullptr) = 0;
    /** @brief Remove this extension's preview overlay without altering model/feature layers.
     * @note Safe after model closure; the host also removes the overlay when its facade is destroyed. */
    virtual void clearPreview() = 0;
};
}
