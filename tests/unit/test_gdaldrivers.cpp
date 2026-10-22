/**
 * @file      test_gdaldrivers.cpp
 * @brief     Unit tests for the runtime GDAL driver probe
 *         (openswmmvis::io::gdalcaps) added with the honest-filter work.
 *
 * Links only src/io/gdaldrivers.cpp + GDAL — no widget dependencies.
 * Register in tests/unit/CMakeLists.txt (see HANDOFF doc):
 *
 *     add_swmmvis_unit_test(test_gdaldrivers
 *         test_gdaldrivers.cpp
 *         ${PROJECT_SOURCE_DIR}/src/io/gdaldrivers.cpp)
 *     find_package(GDAL CONFIG REQUIRED)
 *     target_link_libraries(test_gdaldrivers PRIVATE GDAL::GDAL)
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <QLatin1String>
#include <QString>

#include "io/gdaldrivers.h"

using namespace openswmmvis::io;

// GDAL-core raster drivers — present in ANY build regardless of vcpkg features.
/**
 *
 * @brief Require runtime GTiff, AAIGrid and VRT drivers in the test build.
 *
 */
TEST(GdalCaps, CoreRasterDriversPresent)
{
    EXPECT_TRUE(gdalcaps::driverAvailable("GTiff"));
    EXPECT_TRUE(gdalcaps::driverAvailable("AAIGrid"));
    EXPECT_TRUE(gdalcaps::driverAvailable("VRT"));
}

// Elevation formats that are GDAL core (no extra module). The whole point of
// the "limit modules" directive is that these open out of the box.
/**
 *
 * @brief Require runtime SRTMHGT, USGSDEM and DTED elevation drivers in the test build.
 *
 */
TEST(GdalCaps, CoreDemDriversPresent)
{
    EXPECT_TRUE(gdalcaps::driverAvailable("SRTMHGT"));
    EXPECT_TRUE(gdalcaps::driverAvailable("USGSDEM"));
    EXPECT_TRUE(gdalcaps::driverAvailable("DTED"));
}

// Vector drivers confirmed present in the minimal jpeg/png/sqlite3 build.
/**
 *
 * @brief Require runtime Shapefile, GeoPackage and OpenFileGDB vector drivers.
 *
 */
TEST(GdalCaps, CoreVectorDriversPresent)
{
    EXPECT_TRUE(gdalcaps::driverAvailable("ESRI Shapefile"));
    EXPECT_TRUE(gdalcaps::driverAvailable("GPKG"));
    EXPECT_TRUE(gdalcaps::driverAvailable("OpenFileGDB")); // .gdb read
}

// The raster Open filter must advertise the core formats, end with the
// catch-all, and — critically — NOT promise NetCDF/HDF unless their optional
// driver is actually compiled in (this is the false-advertising regression fix).
/**
 *
 * @brief Verify core raster patterns/catch-all are advertised and absent netCDF/HDF5 drivers are
 * not promised.
 *
 */
TEST(GdalCaps, RasterFilterIsHonest)
{
    const QString f = gdalcaps::rasterOpenFilter();

    EXPECT_TRUE(f.contains(QLatin1String("*.tif")));
    EXPECT_TRUE(f.contains(QLatin1String("*.asc")));
    EXPECT_TRUE(f.contains(QLatin1String("*.hgt")));
    EXPECT_TRUE(f.contains(QLatin1String("All files (*)")));

    if (!gdalcaps::driverAvailable("netCDF"))
        EXPECT_FALSE(f.contains(QLatin1String("*.nc")));
    if (!gdalcaps::driverAvailable("HDF5"))
        EXPECT_FALSE(f.contains(QLatin1String("*.h5")));
}

// The vector Open filter should expose .gdb whenever OpenFileGDB is present.
/**
 *
 * @brief Require Shapefile/GeoPackage filters and conditional FileGDB advertisement when its
 * driver exists.
 *
 */
TEST(GdalCaps, VectorFilterExposesGdbWhenPresent)
{
    const QString f = gdalcaps::vectorOpenFilter();

    EXPECT_TRUE(f.contains(QLatin1String("*.shp")));
    EXPECT_TRUE(f.contains(QLatin1String("*.gpkg")));
    if (gdalcaps::driverAvailable("OpenFileGDB"))
        EXPECT_TRUE(f.contains(QLatin1String("*.gdb")));
}

// Extension enumerators should be non-empty and lower-cased.
/**
 *
 * @brief Require nonempty raster/vector extension lists and lowercase raster entries.
 * The body does not assert lowercase vector entries despite the older surrounding comment.
 *
 */
TEST(GdalCaps, ExtensionEnumeratorsPopulated)
{
    const QStringList raster = gdalcaps::availableRasterExtensions();
    const QStringList vector = gdalcaps::availableVectorExtensions();
    EXPECT_FALSE(raster.isEmpty());
    EXPECT_FALSE(vector.isEmpty());
    for (const QString &e : raster)
        EXPECT_EQ(e, e.toLower());
}
