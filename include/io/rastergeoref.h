/*!
 * \file   rastergeoref.h
 * \author OpenSWMM GUI
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * World-file parsing and GDAL PAM georeferencing helpers for local raster
 * basemaps (LOCAL_RASTER_BASEMAP plan, Phase 2).
 *
 * A plain image (PNG/JPEG/BMP, or a CRS-less GeoTIFF) carries no
 * georeferencing of its own. These free functions parse the conventional
 * ESRI world-file sidecar, convert its pixel-CENTER anchor to the GDAL
 * GeoTransform's pixel-CORNER anchor, and persist SRS + GeoTransform so that
 * every subsequent *plain* GDAL open — the initial open, the pooled tile-warp
 * handles, overview builds, project restore — sees the georeferencing with no
 * changes to GISRasterLayer's open paths. Persistence goes through the
 * driver's native update path when it supports one (GTiff), or the standard
 * PAM sidecar "<image>.aux.xml" otherwise (PNG/JPEG refuse update opens).
 */
#ifndef OPENSWMMVIS_IO_RASTERGEOREF_H
#define OPENSWMMVIS_IO_RASTERGEOREF_H

#include <QString>
#include <QStringList>

namespace openswmmvis::io::rastergeoref {

/*! \brief The six lines of an ESRI world file, in file order.
 *
 *  C/F reference the CENTER of the top-left pixel — not its corner (see
 *  worldFileToGeoTransform() for the conversion). */
struct WorldFileParams
{
    double a;  //!< Line 1 — x pixel size.
    double d;  //!< Line 2 — y-axis rotation.
    double b;  //!< Line 3 — x-axis rotation.
    double e;  //!< Line 4 — y pixel size (usually negative).
    double c;  //!< Line 5 — x of the CENTER of the top-left pixel.
    double f;  //!< Line 6 — y of the CENTER of the top-left pixel.
};

/*!
 * \brief Parses a world file: six whitespace/newline-separated doubles.
 *
 * Locale-independent (world files always use '.' as the decimal separator).
 *
 * \param path  World file to read.
 * \param out   Receives the parsed parameters on success.
 * \param err   Optional; receives a message on failure (unreadable file,
 *              fewer than six values, or a non-numeric value).
 * \return true on success.
 */
bool parseWorldFile(const QString &path, WorldFileParams *out, QString *err);

/*!
 * \brief Conventional world-file sidecar paths for \a imagePath, in priority
 *        order.
 *
 * For the common formats the conventional three-letter name comes first
 * (.tif/.tiff → .tfw, .png → .pgw, .jpg/.jpeg → .jgw, .bmp → .bpw), followed
 * by the generic "first + last letter of the extension + w" form, the full
 * "extension + w" form (e.g. .tifw), and finally the format-agnostic .wld.
 *
 * Pure path arithmetic — ALL candidates are returned whether or not they
 * exist on disk; the caller checks existence.
 */
[[nodiscard]] QStringList worldFileCandidates(const QString &imagePath);

/*!
 * \brief Converts world-file parameters to a GDAL GeoTransform.
 *
 * A world file anchors C/F at the CENTER of the top-left pixel; the GDAL
 * GeoTransform anchors gt[0]/gt[3] at its top-left CORNER:
 * gt[0] = C − A/2 − B/2 and gt[3] = F − D/2 − E/2, with
 * gt[1]=A, gt[2]=B, gt[4]=D, gt[5]=E.
 */
void worldFileToGeoTransform(const WorldFileParams &wf, double gt[6]);

/*!
 * \brief Persists georeferencing on \a imagePath so every later plain GDAL
 *        open sees it.
 *
 * First tries the driver's native update path (GDALOpenEx with
 * GDAL_OF_UPDATE + SetGeoTransform/SetSpatialRef — GTiff writes internally
 * or to PAM as GDAL decides). If the driver refuses update access (PNG/JPEG
 * do), writes/merges the PAM sidecar "<imagePath>.aux.xml" directly,
 * preserving any existing non-georef PAM content (statistics, histograms).
 * Finally verifies by reopening read-only.
 *
 * \param imagePath  Raster to georeference.
 * \param gt6        Optional GeoTransform (6 doubles, CORNER-anchored — see
 *                   worldFileToGeoTransform()); nullptr = don't set.
 * \param crsWkt     CRS as WKT (see authCodeToWkt()); empty = don't set.
 * \param err        Optional; receives a message on failure.
 * \return true when the georeferencing is written AND visible on reopen.
 */
bool applyPamGeoref(const QString &imagePath, const double *gt6,
                    const QString &crsWkt, QString *err);

/*! \brief What a cheap read-only open reveals about a raster's
 *         georeferencing — for dialog prefill/validation. */
struct GeorefProbe
{
    bool    hasGeoTransform = false;  //!< GetGeoTransform() returned CE_None.
    QString crsDescription;           //!< Human name of the CRS ("NAD83 / Maryland"); empty if none.
    QString crsAuthCode;              //!< "EPSG:26985"-style code when the CRS carries one; else empty.
};

/*! \brief Probes \a imagePath read-only; never fails (an unopenable file
 *         simply yields a default-constructed probe). */
[[nodiscard]] GeorefProbe probeGeoref(const QString &imagePath);

/*!
 * \brief Resolves a CRS identifier ("EPSG:26985" or anything else
 *        OGRSpatialReference::SetFromUserInput accepts) to WKT.
 * \param authCode  CRS identifier.
 * \param err       Optional; receives a message on failure.
 * \return WKT string, or an empty string on failure.
 */
[[nodiscard]] QString authCodeToWkt(const QString &authCode, QString *err);

} // namespace openswmmvis::io::rastergeoref

#endif // OPENSWMMVIS_IO_RASTERGEOREF_H
