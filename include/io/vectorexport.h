/**
 * @file      vectorexport.h
 * @brief     Widget-free writer behind the layer tree's Export entries
 *            (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.5, R8).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Widget-free writer behind the layer tree's Export entries
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.5, R8).
 *
 * Collectors (io/swmmexport, io/meshexport) snapshot SWMM objects or mesh
 * elements into \ref openswmmvis::io::ExportTable values on the main thread. This module then
 * builds them in an in-memory OGR dataset and lets one GDALVectorTranslate per
 * destination do the format, the CRS transform and the creation options — so
 * it touches no layer, engine or widget and runs on a worker. Vector and
 * feature layers skip the collector: \ref openswmmvis::io::exportVectorLayer re-opens their
 * source by path, as every worker in this codebase does.
 *
 * Contract shared with io/mesh2dresultsexport: progress may cancel at any
 * point, and a cancelled or failed export deletes every file it created.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_IO_VECTOREXPORT_H
#define OPENSWMMVIS_IO_VECTOREXPORT_H

#include "feature/featuretypes.h"

#include <QPair>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include <functional>

namespace openswmmvis::io
{

/*! \enum ExportGeometry
 * \brief The one geometry kind of an exported table. */
enum class ExportGeometry { Point, LineString, Polygon };

/*!
 * \struct ExportField
 * \brief One attribute column of an \ref ExportTable.
 */
struct ExportField {
    QString name;  ///< Output name: an import key or a sanitised label (§8.4, Q9).
    QString label; ///< Full readable label, e.g. "Max Depth".
    QString unit;  ///< e.g. "ft"; empty when unitless.
    openswmmvis::feature::FieldType type = openswmmvis::feature::FieldType::Text;
    /*! Stable tokens a Choice column holds; written as a value list where
     *  the format keeps one. */
    QVector<openswmmvis::feature::FieldChoice> choices;

    /*! "Label (unit)" — the description written with the column.
     * @brief Format the column description for GIS metadata.
     * @return label alone for empty unit, otherwise label followed by the parenthesized unit; no conversion occurs.
     */
    [[nodiscard]] QString description() const
    {
        return unit.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, unit);
    }
};

/*!
 * \struct ExportRow
 * \brief One feature: its coordinates and one value per field.
 */
struct ExportRow {
    /*! Point: one point. LineString: the polyline. Polygon: the exterior
     *  ring, open or closed. */
    QVector<QPointF> points;
    QVariantList values; ///< Parallel to ExportTable::fields; invalid = null.
};

/*!
 * \struct ExportTable
 * \brief One output layer (one SWMM object type, one mesh element kind).
 */
struct ExportTable {
    QString name; ///< Layer / file stem, e.g. "junctions".
    ExportGeometry geometry = ExportGeometry::Point;
    QVector<ExportField> fields;
    QVector<ExportRow> rows;
    /*! What the collector left out, and why — logged in the report. */
    QStringList skipped;
};

/*! \struct ExportOptions
 * \brief Where and how to write. */
struct ExportOptions {
    QString driver; ///< GDAL short name (gdalcaps::vectorWriteFormats()).
    /*! A file for a multi-layer format (GeoPackage, File Geodatabase);
     *  otherwise a folder, receiving `table.extension` per table. */
    QString destination;
    QString sourceSrsWkt; ///< CRS of the table coordinates; may be empty.
    /*! Reproject to this CRS; empty keeps the source CRS. GeoJSON and KML
     *  are always written in WGS 84. */
    QString targetSrsWkt;
};

/*! \struct ExportReport
 * \brief What happened. */
struct ExportReport {
    QStringList files; ///< Every path created, in creation order.
    /*! (path, layer name) of every layer written — for "add to map". */
    QVector<QPair<QString, QString>> layers;
    QStringList warnings; ///< Skipped objects, format notes.
    QString error;        ///< Empty on success; "Cancelled" when stopped.
};

/*! Called with work done / total; return false to cancel. May be called from
 *  the worker thread. */
using ExportProgress = std::function<bool(int done, int total, const QString &what)>;

/*!
 * \brief Write \p tables to \p options.destination.
 * \details Existing outputs of the same name are replaced (the dialog asked).
 *          A Shapefile export also writes `file_fields.csv` beside each .shp,
 *          mapping every 10-character field name to its full label and unit.
 * \returns false on failure or cancel; every file created is then deleted.
 * @param[in] tables Owning value tables with coordinates in sourceSrsWkt and values parallel to fields.
 * @param[in] options Registered curated format, destination, and optional CRS transform.
 * @param[in] progress Optional progress/cancellation callback.
 * @param[out] report Optional report reset on entry, receiving files/layers, warnings, and an error.
 * @details Callbacks run synchronously on the calling thread and may cancel by returning false. Use independent data/source paths for worker execution. Failed/cancelled output cleanup cannot restore an existing destination that was replaced.
 */
bool exportVectorTables(const QVector<ExportTable> &tables, const ExportOptions &options,
                        const ExportProgress &progress, ExportReport *report);

/*!
 * \struct VectorLayerSource
 * \brief A GIS vector or feature layer to export as it is.
 */
struct VectorLayerSource {
    QString path;       ///< Datasource path, re-opened read-only.
    QString layerName;  ///< Layer inside it; empty = the first.
    QString outputName; ///< Output layer / file stem.
    /*! Export only these features ("selected only"); empty = all. */
    QVector<qint64> fids;
};

/*!
 * \brief Translate \p source into \p options.destination, reprojecting when
 *        \p options.targetSrsWkt is set. Same cancel and cleanup contract.
 * @param[in] source Path/layer selector reopened read-only; empty FID selection exports all features.
 * @param[in] options Format, destination, and optional output CRS; source CRS is read from the opened layer.
 * @param[in] progress Optional progress/cancellation callback.
 * @param[out] report Optional report reset on entry.
 * @return True after translation; false with a diagnostic on invalid input, unavailable driver/layer, cancellation, or write failure.
 * @details Callbacks run synchronously on the calling thread and may cancel by returning false. Use independent data/source paths for worker execution. Failed/cancelled output cleanup cannot restore an existing destination that was replaced.
 */
bool exportVectorLayer(const VectorLayerSource &source, const ExportOptions &options,
                       const ExportProgress &progress, ExportReport *report);

/*!
 * \brief Write raster \p sourcePath to GeoTIFF \p destination, warped to
 *        \p targetSrsWkt when it is set (plan Q10). Same contract.
 * @param[in] sourcePath Raster source reopened independently read-only.
 * @param[in] destination GeoTIFF output file path.
 * @param[in] targetSrsWkt Target CRS, or empty to retain the source CRS without warping.
 * @param[in] progress Optional synchronous progress/cancellation callback.
 * @param[out] report Optional report reset on entry.
 * @return True on completed translation/warp; false on validation, open/write failure, or cancellation.
 * @details Callbacks run synchronously on the calling thread and may cancel by returning false. Use independent data/source paths for worker execution. Failed/cancelled output cleanup cannot restore an existing destination that was replaced.
 */
bool exportRaster(const QString &sourcePath, const QString &destination,
                  const QString &targetSrsWkt, const ExportProgress &progress,
                  ExportReport *report);

/*! The file a table named \p tableName is written to under \p options.
 * @brief Compute a table output path using the curated format metadata.
 * @param[in] options Writable curated driver and destination.
 * @param[in] tableName File stem for a single-table format.
 * @return Destination itself for multilayer formats, otherwise destination/tableName.extension; performs no IO.
 */
[[nodiscard]] QString exportPathFor(const ExportOptions &options, const QString &tableName);

/*!
 * \brief Shapefile field names: at most \p maxLen characters and unique
 *        (case-insensitively), in order. "invertElev" stays; "surchargeDepth"
 *        becomes "surchargeD", a second one "surcharg_1".
 * @param[in] names Full field names in output order.
 * @param[in] maxLen Positive name limit large enough for generated numeric collision suffixes.
 * @return One name per input, truncated with incrementing underscore suffixes to avoid case-insensitive duplicates.
 * @warning The routine does not validate an undersized maxLen; callers must choose a limit compatible with the number of collisions.
 */
[[nodiscard]] QStringList shortFieldNames(const QStringList &names, int maxLen = 10);

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_VECTOREXPORT_H
