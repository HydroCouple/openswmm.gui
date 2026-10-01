/*!
 * \file   gdaldrivers.cpp
 * \author SWMMVis
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "io/gdaldrivers.h"

#include <QSet>

#include <gdal.h>

namespace openswmmvis::io::gdalcaps {

namespace {

/*! One friendly filter group: the label + extensions it offers, gated on a
 *  GDAL driver short name that must be registered for the group to appear. */
struct FormatGroup
{
    const char *driver;      ///< GDAL short name gating this group.
    const char *label;       ///< Human label for the filter entry.
    const char *extensions;  ///< Space-separated, no dots: "tif tiff".
};

// Curated raster / DEM groups. Each appears only when its driver is
// registered, so the offered list is always honest about what the running
// build can open. GeoTIFF/ASCII-grid/Imagine + the elevation formats
// (SRTMHGT/USGSDEM/DTED/EHdr/ENVI/Surfer/Terragen) are GDAL core — present
// with no extra module. NetCDF/HDF appear only if their optional module is
// compiled in.
const FormatGroup kRasterGroups[] = {
    {"GTiff",    "GeoTIFF / COG",        "tif tiff"},
    {"AAIGrid",  "Arc/Info ASCII Grid",  "asc"},
    {"HFA",      "Erdas Imagine",        "img"},
    {"SRTMHGT",  "SRTM height",          "hgt"},
    {"USGSDEM",  "USGS DEM",             "dem"},
    {"DTED",     "Military elevation",   "dt0 dt1 dt2"},
    {"EHdr",     "ESRI / ENVI binary",   "bil bsq flt"},
    {"ENVI",     "ENVI raster",          "dat raw"},
    {"GSAG",     "Surfer ASCII grid",    "grd"},
    {"GSBG",     "Surfer binary grid",   "grd"},
    {"GS7BG",    "Surfer 7 grid",        "grd"},
    {"Terragen", "Terragen terrain",     "ter"},
    {"PNG",      "PNG image",            "png"},
    {"JPEG",     "JPEG image",           "jpg jpeg"},
    {"VRT",      "GDAL virtual raster",  "vrt"},
    {"netCDF",   "NetCDF",               "nc"},
    {"HDF5",     "HDF5",                 "h5 hdf5"},
    {"HDF4",     "HDF4",                 "hdf"},
};

// Curated vector groups. OpenFileGDB (.gdb read), GeoPackage, Shapefile,
// GeoJSON and SQLite are present in the minimal build; GML/KML/MapInfo appear
// only if their optional module is compiled in.
const FormatGroup kVectorGroups[] = {
    {"ESRI Shapefile", "ESRI Shapefile",         "shp"},
    {"GPKG",           "GeoPackage",             "gpkg"},
    {"GeoJSON",        "GeoJSON",                "geojson json"},
    {"OpenFileGDB",    "Esri File Geodatabase",  "gdb"},
    {"SQLite",         "SQLite / SpatiaLite",    "sqlite db"},
    {"GML",            "GML",                    "gml"},
    {"LIBKML",         "KML",                    "kml kmz"},
    {"KML",            "KML",                    "kml"},
    {"MapInfo File",   "MapInfo",                "tab mif"},
    {"CSV",            "CSV (point data)",       "csv"},
};

QStringList extsToGlobs(const char *spaceSeparated)
{
    QStringList globs;
    const QStringList parts =
        QString::fromLatin1(spaceSeparated).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    globs.reserve(parts.size());
    for (const QString &e : parts)
        globs << QStringLiteral("*.%1").arg(e);
    return globs;
}

QString buildFilter(const FormatGroup *groups, int count)
{
    ensureRegistered();

    QStringList     perGroup;          // e.g. "GeoTIFF / COG (*.tif *.tiff)"
    QStringList     allGlobsOrdered;   // union for the "All supported" entry
    QSet<QString>   seenGlobs;

    for (int i = 0; i < count; ++i) {
        const FormatGroup &g = groups[i];
        if (!driverAvailable(g.driver))
            continue;
        const QStringList globs = extsToGlobs(g.extensions);
        perGroup << QStringLiteral("%1 (%2)")
                        .arg(QString::fromLatin1(g.label), globs.join(QLatin1Char(' ')));
        for (const QString &glob : globs) {
            if (!seenGlobs.contains(glob)) {
                seenGlobs.insert(glob);
                allGlobsOrdered << glob;
            }
        }
    }

    QStringList filter;
    if (!allGlobsOrdered.isEmpty())
        filter << QStringLiteral("All supported (%1)").arg(allGlobsOrdered.join(QLatin1Char(' ')));
    filter += perGroup;
    filter << QStringLiteral("All files (*)");
    return filter.join(QStringLiteral(";;"));
}

QStringList extensionsForCapability(const char *dcap)
{
    ensureRegistered();
    QSet<QString> exts;
    const int n = GDALGetDriverCount();
    for (int i = 0; i < n; ++i) {
        GDALDriverH drv = GDALGetDriver(i);
        if (!drv)
            continue;
        if (!GDALGetMetadataItem(drv, dcap, nullptr))
            continue;
        if (const char *e = GDALGetMetadataItem(drv, GDAL_DMD_EXTENSIONS, nullptr)) {
            const QStringList parts =
                QString::fromLatin1(e).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QString &p : parts)
                exts.insert(p.toLower());
        }
    }
    QStringList out(exts.begin(), exts.end());
    out.sort();
    return out;
}

} // namespace

void ensureRegistered()
{
    static bool done = false;
    if (!done) {
        GDALAllRegister();
        done = true;
    }
}

bool driverAvailable(const char *shortName)
{
    ensureRegistered();
    return GDALGetDriverByName(shortName) != nullptr;
}

QStringList availableRasterExtensions()
{
    return extensionsForCapability(GDAL_DCAP_RASTER);
}

QStringList availableVectorExtensions()
{
    return extensionsForCapability(GDAL_DCAP_VECTOR);
}

QString rasterOpenFilter()
{
    return buildFilter(kRasterGroups, int(sizeof(kRasterGroups) / sizeof(kRasterGroups[0])));
}

QString vectorOpenFilter()
{
    return buildFilter(kVectorGroups, int(sizeof(kVectorGroups) / sizeof(kVectorGroups[0])));
}

QVector<VectorWriteFormat> vectorWriteFormats()
{
    ensureRegistered();
    // §8.3 order: GeoPackage is the default (Q8).
    const QVector<VectorWriteFormat> curated = {
        {QStringLiteral("GPKG"), QStringLiteral("GeoPackage"), QStringLiteral("gpkg"),
         true, false, true, false,
         QStringLiteral("One file; full field names, descriptions and value lists.")},
        {QStringLiteral("ESRI Shapefile"), QStringLiteral("ESRI Shapefile"), QStringLiteral("shp"),
         false, false, false, true,
         QStringLiteral("One file set per object type; field names cut to 10 characters "
                        "(a _fields.csv maps them); no value lists; 2 GB limit.")},
        {QStringLiteral("GeoJSON"), QStringLiteral("GeoJSON"), QStringLiteral("geojson"),
         false, true, false, false,
         QStringLiteral("One file per object type, written in WGS 84 as GeoJSON requires.")},
        {QStringLiteral("FlatGeobuf"), QStringLiteral("FlatGeobuf"), QStringLiteral("fgb"),
         false, false, false, false,
         QStringLiteral("One file per object type.")},
        {QStringLiteral("KML"), QStringLiteral("KML"), QStringLiteral("kml"),
         false, true, false, false,
         QStringLiteral("One file per object type, in WGS 84; few attribute types survive.")},
        {QStringLiteral("CSV"), QStringLiteral("CSV (geometry as WKT)"), QStringLiteral("csv"),
         false, false, false, false,
         QStringLiteral("One table per object type; the geometry is a WKT column.")},
        {QStringLiteral("DXF"), QStringLiteral("AutoCAD DXF"), QStringLiteral("dxf"),
         false, false, false, false,
         QStringLiteral("One drawing per object type; attributes are not kept.")},
        {QStringLiteral("OpenFileGDB"), QStringLiteral("Esri File Geodatabase"), QStringLiteral("gdb"),
         true, false, true, false,
         QStringLiteral("One .gdb folder; full field names, descriptions and value lists.")},
    };
    QVector<VectorWriteFormat> out;
    for (const VectorWriteFormat &f : curated) {
        GDALDriverH drv = GDALGetDriverByName(f.driver.toUtf8().constData());
        if (!drv) continue;
        if (!GDALGetMetadataItem(drv, GDAL_DCAP_VECTOR, nullptr)) continue;
        if (!GDALGetMetadataItem(drv, GDAL_DCAP_CREATE, nullptr)) continue;
        out.append(f);
    }
    return out;
}

VectorWriteFormat vectorWriteFormat(const QString &driver)
{
    for (const VectorWriteFormat &f : vectorWriteFormats())
        if (f.driver == driver) return f;
    return {};
}

} // namespace openswmmvis::io::gdalcaps
