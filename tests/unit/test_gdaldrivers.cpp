/*!
 * \file   test_gdaldrivers.cpp
 * \brief  Unit tests for the runtime GDAL driver probe
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
 */
#include <gtest/gtest.h>

#include <QLatin1String>
#include <QString>

#include "io/gdaldrivers.h"

using namespace openswmmvis::io;

// GDAL-core raster drivers — present in ANY build regardless of vcpkg features.
TEST(GdalCaps, CoreRasterDriversPresent)
{
    EXPECT_TRUE(gdalcaps::driverAvailable("GTiff"));
    EXPECT_TRUE(gdalcaps::driverAvailable("AAIGrid"));
    EXPECT_TRUE(gdalcaps::driverAvailable("VRT"));
}

// Elevation formats that are GDAL core (no extra module). The whole point of
// the "limit modules" directive is that these open out of the box.
TEST(GdalCaps, CoreDemDriversPresent)
{
    EXPECT_TRUE(gdalcaps::driverAvailable("SRTMHGT"));
    EXPECT_TRUE(gdalcaps::driverAvailable("USGSDEM"));
    EXPECT_TRUE(gdalcaps::driverAvailable("DTED"));
}

// Vector drivers confirmed present in the minimal jpeg/png/sqlite3 build.
TEST(GdalCaps, CoreVectorDriversPresent)
{
    EXPECT_TRUE(gdalcaps::driverAvailable("ESRI Shapefile"));
    EXPECT_TRUE(gdalcaps::driverAvailable("GPKG"));
    EXPECT_TRUE(gdalcaps::driverAvailable("OpenFileGDB"));  // .gdb read
}

// The raster Open filter must advertise the core formats, end with the
// catch-all, and — critically — NOT promise NetCDF/HDF unless their optional
// driver is actually compiled in (this is the false-advertising regression fix).
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
TEST(GdalCaps, VectorFilterExposesGdbWhenPresent)
{
    const QString f = gdalcaps::vectorOpenFilter();

    EXPECT_TRUE(f.contains(QLatin1String("*.shp")));
    EXPECT_TRUE(f.contains(QLatin1String("*.gpkg")));
    if (gdalcaps::driverAvailable("OpenFileGDB"))
        EXPECT_TRUE(f.contains(QLatin1String("*.gdb")));
}

// Extension enumerators should be non-empty and lower-cased.
TEST(GdalCaps, ExtensionEnumeratorsPopulated)
{
    const QStringList raster = gdalcaps::availableRasterExtensions();
    const QStringList vector = gdalcaps::availableVectorExtensions();
    EXPECT_FALSE(raster.isEmpty());
    EXPECT_FALSE(vector.isEmpty());
    for (const QString &e : raster)
        EXPECT_EQ(e, e.toLower());
}
