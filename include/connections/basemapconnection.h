/*!
 * \file   basemapconnection.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Plain data structs for all supported basemap connection types.
 *
 * \details
 * `BasemapHttpHeaders` is a QMap<QString,QString> where the key "referer"
 * stores the HTTP Referer header.  Arbitrary extra headers are stored
 * alongside it.  When persisted in QSettings the key is written under a
 * `http-header/` sub-group, matching the QGIS convention so exported
 * connection XML is interoperable.
 *
 * `TileAxisOrder` is shared between `XYZConnection` (stored on the struct)
 * and `XYZTileLayer` (stored as a runtime member).
 */
#ifndef BASEMAPCONNECTION_H
#define BASEMAPCONNECTION_H

#include <QMap>
#include <QString>

// ---------------------------------------------------------------------------
// Types shared across connection structs
// ---------------------------------------------------------------------------

/*!
 * \brief Arbitrary HTTP header map for basemap requests.
 * \details Key "referer" is the HTTP Referer.  All other keys are additional
 *          headers (e.g., "User-Agent", "X-Api-Key").
 *          Written to QSettings under the `http-header/{key}` sub-group.
 */
using BasemapHttpHeaders = QMap<QString, QString>;

/*!
 * \enum TileAxisOrder
 * \brief Controls how {x}/{y} tile coordinates are ordered in the URL.
 */
enum class TileAxisOrder
{
    ZXY = 0,  /*!< Standard OSM / slippy-map: tile/{z}/{x}/{y} */
    ZYX = 1,  /*!< ArcGIS REST:               tile/{z}/{y}/{x} */
};

// ---------------------------------------------------------------------------
// Per-connection auth credentials (runtime only — never persisted in JSON)
// ---------------------------------------------------------------------------

/*!
 * \struct BasemapAuth
 * \brief Username + password pair.  Stored encrypted in QSettings by
 *        `BasemapConnectionStore`; never written to project files.
 */
struct BasemapAuth
{
    QString username;
    QString password;

    [[nodiscard]] bool isEmpty() const { return username.isEmpty() && password.isEmpty(); }
};

// ---------------------------------------------------------------------------
// XYZ tile connection
// ---------------------------------------------------------------------------

/*!
 * \struct XYZConnection
 * \brief All parameters needed to configure an XYZ (slippy-map) tile layer.
 */
struct XYZConnection
{
    QString            name;
    QString            urlTemplate;
    int                zMin           = 0;
    int                zMax           = 19;
    /*!
     * \brief DPI rendering hint.
     * 0 = Undefined (use tile dimensions as-is)
     * 1 = Standard 96 DPI (256 px tiles)
     * 2 = HiDPI 192 DPI (512 px tiles)
     * This is NOT a URL modifier.  "@2x" in a URL template is a literal
     * string the user types.
     */
    int                tilePixelRatio = 0;
    TileAxisOrder      axisOrder      = TileAxisOrder::ZXY;
    BasemapHttpHeaders httpHeaders;   /*!< Key "referer" + arbitrary extras. */
    bool               isBuiltin      = false; /*!< Built-in entries cannot be deleted. */

    /*!
     * \brief Returns the list of pre-populated, non-deletable built-in connections.
     */
    static QList<XYZConnection> builtins()
    {
        return {
            { "OpenStreetMap",
              "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
              0, 19, 0, TileAxisOrder::ZXY, {}, true },
            { "CartoDB Positron",
              "https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}@2x.png",
              0, 20, 2, TileAxisOrder::ZXY, {}, true },
            { "CartoDB Dark Matter",
              "https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}@2x.png",
              0, 20, 2, TileAxisOrder::ZXY, {}, true },
            { "Stadia Alidade Smooth",
              "https://tiles.stadiamaps.com/tiles/alidade_smooth/{z}/{x}/{y}@2x.png",
              0, 20, 2, TileAxisOrder::ZXY, {}, true },
            { "ESRI World Imagery",
              "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
              0, 23, 0, TileAxisOrder::ZXY, {}, true },
        };
    }
};

// ---------------------------------------------------------------------------
// WMS / WMTS connection  (protocol auto-detected at Connect time)
// ---------------------------------------------------------------------------

/*!
 * \struct WMSConnection
 * \brief Parameters for an OGC WMS or WMTS service.
 *
 * Protocol is auto-detected from the URL at connect time:
 * if the URL contains "SERVICE=WMTS" or "/WMTSCapabilities.xml"
 * (case-insensitive) the service is treated as WMTS; otherwise WMS.
 */
struct WMSConnection
{
    QString            name;
    QString            url;

    // --- Layer selection (populated after Connect) ---
    QString            layerName;
    QString            style;
    QString            imageFormat    = "image/png";
    QString            crs            = "EPSG:3857";
    QString            tileMatrixSet;     /*!< WMTS only; empty for WMS. */

    // --- Advanced options (mirrors QGIS WMS provider settings) ---
    /*!
     * \brief DPI mode bitmask: 0=None, 1=QGIS, 2=UMN, 4=GeoServer, 7=All (default).
     */
    int                dpiMode                 = 7;
    int                tilePixelRatio          = 0;  /*!< 0/1/2 — same semantics as XYZConnection. */
    bool               ignoreGetMapURI         = false;
    bool               ignoreAxisOrientation   = false;
    bool               invertAxisOrientation   = false;
    bool               smoothPixmapTransform   = true;

    BasemapHttpHeaders httpHeaders;
};

// ---------------------------------------------------------------------------
// WCS (Web Coverage Service) connection
// ---------------------------------------------------------------------------

/*!
 * \struct WCSConnection
 * \brief Parameters for an OGC Web Coverage Service (WCS 1.1.x / 2.0) source.
 *
 * At connect time GetCapabilities is fetched to enumerate available coverages.
 * DescribeCoverage is then fetched for the selected coverage to obtain its native
 * CRS, extent, fields, and interpolation options.
 *
 * The rendered output follows the WMS single-image pattern: one GetCoverage
 * request per canvas viewport, decoded via GDAL and painted into the canvas.
 */
struct WCSConnection
{
    QString            name;
    QString            url;                   /*!< Base service URL (no query string). */
    /*!
     * \brief Negotiated WCS version string ("1.0.0", "1.1.2", "2.0.1").
     * Populated at connect time; default targets 2.0.1.
     */
    QString            version      = QStringLiteral("2.0.1");

    // --- Coverage selection (populated after Connect + DescribeCoverage) ---
    QString            coverageId;            /*!< Selected coverage identifier. */
    QString            outputCrs    = QStringLiteral("EPSG:4326");
    QString            outputFormat = QStringLiteral("image/tiff"); /*!< GDAL-readable MIME type. */
    /*!
     * \brief Optional WCS range subset string, e.g. "band[1]".
     * Empty string = all fields (default).
     */
    QString            rangeSubset;
    QString            interpolation = QStringLiteral("nearest");

    BasemapHttpHeaders httpHeaders;
};

// ---------------------------------------------------------------------------
// ArcGIS REST connection
// ---------------------------------------------------------------------------

/*!
 * \struct ArcGISRestConnection
 * \brief Parameters for an ArcGIS REST tiled MapServer service.
 *
 * At runtime the Connect step fetches `{url}/MapServer?f=json` to discover
 * tile level range and builds an XYZConnection with
 * `urlTemplate = {url}/MapServer/tile/{z}/{x}/{y}` and `axisOrder = ZYX`.
 * The ZYX axis order swaps the {x}/{y} values so the final URL path becomes
 * `tile/{z}/{row}/{col}` — the ArcGIS REST tile endpoint convention.
 * No separate ArcGIS layer class is needed.
 */
struct ArcGISRestConnection
{
    QString            name;
    QString            url;
    QString            urlPrefix;          /*!< Optional proxy prefix. */
    /*!
     * \brief ArcGIS Enterprise Portal content endpoint.
     * Example: https://portal.example.com/arcgis/sharing/rest/content
     * Leave empty for public ArcGIS Online.
     */
    QString            contentEndpoint;
    /*!
     * \brief ArcGIS Enterprise Portal community endpoint.
     * Example: https://portal.example.com/arcgis/sharing/rest/community
     * Leave empty for public ArcGIS Online.
     */
    QString            communityEndpoint;

    BasemapHttpHeaders httpHeaders;
};

// ---------------------------------------------------------------------------
// Local raster connection
// ---------------------------------------------------------------------------

/*!
 * \struct LocalRasterConnection
 * \brief Parameters for a local raster (GeoTIFF/PNG/JPEG/...) basemap.
 *
 * Georeferencing comes from the file itself (embedded or PAM/world-file
 * sidecar) unless a world file and/or CRS override is supplied.
 * No auth or HTTP headers — local files have none.
 */
struct LocalRasterConnection
{
    QString name;
    QString filePath;
    QString worldFilePath;   /*!< Empty = rely on embedded/sidecar georef. */
    QString crsAuthCode;     /*!< e.g. "EPSG:26985". Empty = use embedded CRS. */
};

#endif // BASEMAPCONNECTION_H
