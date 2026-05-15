/*!
 * \file   gisdatapaths.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Portable GDAL/PROJ data-directory resolution.
 *
 * Detects the vcpkg-style `gdal/` and `proj/` share trees within the running
 * application layout and points GDAL/PROJ at them via environment variables
 * and GDAL's CPL config store. This lets a zipped/folder/bundle deployment
 * of SWMMVis run without a system-wide GDAL/PROJ install or external
 * `GDAL_DATA` / `PROJ_DATA` environment.
 *
 * Search order (first match wins per subdir):
 *   1. Next to the executable — `applicationDirPath()/{gdal,proj}`
 *      (Windows / Linux standard layout; also handles non-bundle macOS dev
 *      builds.)
 *   2. macOS-only — `applicationDirPath()/../Resources/{gdal,proj}`
 *      (the standard `.app/Contents/Resources/` bundle layout).
 *
 * Call `setupBundledGisDataPaths()` once, after QCoreApplication has been
 * constructed and before any GDAL/OGR/PROJ use.
 */
#ifndef GISDATAPATHS_H
#define GISDATAPATHS_H

/*!
 * \brief Configure GDAL_DATA / PROJ_DATA from bundled data folders if they
 *        can be located relative to the running executable.
 *
 * No-op when QCoreApplication has not been constructed yet, or when no
 * candidate folder contains the expected marker files.
 */
void setupBundledGisDataPaths();

#endif // GISDATAPATHS_H
