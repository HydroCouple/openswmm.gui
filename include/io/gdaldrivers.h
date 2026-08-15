/*!
 * \file   gdaldrivers.h
 * \author OpenSWMM GUI
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Runtime GDAL/OGR driver capability probe.
 *
 * Lets the UI advertise only the formats the *compiled* GDAL actually
 * registered, instead of a hard-coded literal that can promise drivers
 * (e.g. NetCDF / HDF) a minimal build never included. This honours the
 * project directive to add as few GDAL build modules as possible: the Open
 * dialog filters simply reflect whatever driver set is present, so enabling
 * an optional vcpkg feature later widens them automatically — no code change.
 */
#ifndef OPENSWMMVIS_IO_GDALDRIVERS_H
#define OPENSWMMVIS_IO_GDALDRIVERS_H

#include <QString>
#include <QStringList>

namespace openswmmvis::io::gdalcaps {

/*! \brief Idempotently registers all GDAL/OGR drivers (safe to call often). */
void ensureRegistered();

/*!
 * \brief True when a GDAL/OGR driver with this short name is registered.
 * \param shortName  GDAL driver short name, e.g. "GTiff", "GPKG",
 *                   "OpenFileGDB", "netCDF".
 */
[[nodiscard]] bool driverAvailable(const char *shortName);

/*! \brief Lower-cased, de-duplicated file extensions ("tif", "asc", …) that
 *         registered *raster* drivers report they can open. Sorted. */
[[nodiscard]] QStringList availableRasterExtensions();

/*! \brief availableRasterExtensions() counterpart for *vector* (OGR) drivers. */
[[nodiscard]] QStringList availableVectorExtensions();

/*!
 * \brief A QFileDialog filter string for opening raster layers, built from the
 *        drivers actually present. "All supported" first, per-format groups
 *        next, "All files (*)" last. Formats whose driver is not registered
 *        never appear — so the dialog cannot promise a format the build
 *        can't open.
 */
[[nodiscard]] QString rasterOpenFilter();

/*! \brief vectorOpenFilter() counterpart for vector layers. */
[[nodiscard]] QString vectorOpenFilter();

} // namespace openswmmvis::io::gdalcaps

#endif // OPENSWMMVIS_IO_GDALDRIVERS_H
