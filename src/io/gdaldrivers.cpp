/*!
 * \file   gdaldrivers.cpp
 * \author OpenSWMM GUI
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

} // namespace openswmmvis::io::gdalcaps
