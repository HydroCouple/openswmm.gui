/*!
 * \file   vectorexport.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Widget-free writer behind the layer tree's Export entries
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.5, R8).
 *
 * Collectors (io/swmmexport, io/meshexport) snapshot SWMM objects or mesh
 * elements into \ref ExportTable values on the main thread. This module then
 * builds them in an in-memory OGR dataset and lets one GDALVectorTranslate per
 * destination do the format, the CRS transform and the creation options — so
 * it touches no layer, engine or widget and runs on a worker. Vector and
 * feature layers skip the collector: \ref exportVectorLayer re-opens their
 * source by path, as every worker in this codebase does.
 *
 * Contract shared with io/mesh2dresultsexport: progress may cancel at any
 * point, and a cancelled or failed export deletes every file it created.
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

namespace openswmmvis::io {

/*! \enum ExportGeometry \brief The one geometry kind of an exported table. */
enum class ExportGeometry { Point, LineString, Polygon };

/*!
 * \struct ExportField
 * \brief One attribute column of an \ref ExportTable.
 */
struct ExportField
{
    QString name;    ///< Output name: an import key or a sanitised label (§8.4, Q9).
    QString label;   ///< Full readable label, e.g. "Max Depth".
    QString unit;    ///< e.g. "ft"; empty when unitless.
    openswmmvis::feature::FieldType type = openswmmvis::feature::FieldType::Text;
    /*! Stable tokens a Choice column holds; written as a value list where
     *  the format keeps one. */
    QVector<openswmmvis::feature::FieldChoice> choices;

    /*! "Label (unit)" — the description written with the column. */
    [[nodiscard]] QString description() const
    { return unit.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, unit); }
};

/*!
 * \struct ExportRow
 * \brief One feature: its coordinates and one value per field.
 */
struct ExportRow
{
    /*! Point: one point. LineString: the polyline. Polygon: the exterior
     *  ring, open or closed. */
    QVector<QPointF> points;
    QVariantList     values;   ///< Parallel to ExportTable::fields; invalid = null.
};

/*!
 * \struct ExportTable
 * \brief One output layer (one SWMM object type, one mesh element kind).
 */
struct ExportTable
{
    QString                name;      ///< Layer / file stem, e.g. "junctions".
    ExportGeometry         geometry = ExportGeometry::Point;
    QVector<ExportField>   fields;
    QVector<ExportRow>     rows;
    /*! What the collector left out, and why — logged in the report. */
    QStringList            skipped;
};

/*! \struct ExportOptions \brief Where and how to write. */
struct ExportOptions
{
    QString driver;         ///< GDAL short name (gdalcaps::vectorWriteFormats()).
    /*! A file for a multi-layer format (GeoPackage, File Geodatabase);
     *  otherwise a folder, receiving <table>.<ext> per table. */
    QString destination;
    QString sourceSrsWkt;   ///< CRS of the table coordinates; may be empty.
    /*! Reproject to this CRS; empty keeps the source CRS. GeoJSON and KML
     *  are always written in WGS 84. */
    QString targetSrsWkt;
};

/*! \struct ExportReport \brief What happened. */
struct ExportReport
{
    QStringList files;      ///< Every path created, in creation order.
    /*! (path, layer name) of every layer written — for "add to map". */
    QVector<QPair<QString, QString>> layers;
    QStringList warnings;   ///< Skipped objects, format notes.
    QString     error;      ///< Empty on success; "Cancelled" when stopped.
};

/*! Called with work done / total; return false to cancel. May be called from
 *  the worker thread. */
using ExportProgress = std::function<bool(int done, int total, const QString &what)>;

/*!
 * \brief Write \p tables to \p options.destination.
 * \details Existing outputs of the same name are replaced (the dialog asked).
 *          A Shapefile export also writes <file>_fields.csv beside each .shp,
 *          mapping every 10-character field name to its full label and unit.
 * \returns false on failure or cancel; every file created is then deleted.
 */
bool exportVectorTables(const QVector<ExportTable> &tables,
                        const ExportOptions &options,
                        const ExportProgress &progress,
                        ExportReport *report);

/*!
 * \struct VectorLayerSource
 * \brief A GIS vector or feature layer to export as it is.
 */
struct VectorLayerSource
{
    QString path;               ///< Datasource path, re-opened read-only.
    QString layerName;          ///< Layer inside it; empty = the first.
    QString outputName;         ///< Output layer / file stem.
    /*! Export only these features ("selected only"); empty = all. */
    QVector<qint64> fids;
};

/*!
 * \brief Translate \p source into \p options.destination, reprojecting when
 *        \p options.targetSrsWkt is set. Same cancel and cleanup contract.
 */
bool exportVectorLayer(const VectorLayerSource &source,
                       const ExportOptions &options,
                       const ExportProgress &progress,
                       ExportReport *report);

/*!
 * \brief Write raster \p sourcePath to GeoTIFF \p destination, warped to
 *        \p targetSrsWkt when it is set (plan Q10). Same contract.
 */
bool exportRaster(const QString &sourcePath, const QString &destination,
                  const QString &targetSrsWkt,
                  const ExportProgress &progress,
                  ExportReport *report);

/*! The file a table named \p tableName is written to under \p options. */
[[nodiscard]] QString exportPathFor(const ExportOptions &options, const QString &tableName);

/*!
 * \brief Shapefile field names: at most \p maxLen characters and unique
 *        (case-insensitively), in order. "invertElev" stays; "surchargeDepth"
 *        becomes "surchargeD", a second one "surcharg_1".
 */
[[nodiscard]] QStringList shortFieldNames(const QStringList &names, int maxLen = 10);

}   // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_VECTOREXPORT_H
