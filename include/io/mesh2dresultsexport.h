/*!
 * \file   mesh2dresultsexport.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Export a 2D results run (an IMesh2DSource — the engine's CF/UGRID `.h5`,
 * or any in-memory source) to GIS:
 *
 *   - VECTOR (ESRI Shapefile / GeoPackage) — one polygon feature per mesh
 *     CELL (triangle or quad, never the display sub-triangle fan), one
 *     LAYER/FILE PER VARIABLE, one FIELD per selected time step (`t0001`…)
 *     plus `max`.
 *   - RASTER (GeoTIFF, Float32, NoData -9999) — one file per variable with
 *     one BAND per selected time step (band description = ISO datetime),
 *     plus a separate single-band `_max.tif`. Cell values are interpolated
 *     to the pixel grid by natural neighbour, inverse distance weighting or
 *     a per-cell Green–Gauss gradient reconstruction.
 *
 * The `t0001`-style field names exist because a Shapefile's DBF caps field
 * names at 10 characters; \ref writeTimesCsv's sidecar
 * (`<base>_times.csv`) maps each name back to its frame index and datetime.
 *
 * The module is deliberately widget-free (CLAUDE.md §5.1): the dialog
 * collects a \ref Mesh2DExportOptions and this code does the work, so the
 * export is testable headless and could be driven from a script later.
 *
 * Units: every coordinate and value is written in the MODEL's display unit
 * system. The engine's 2D solver runs in SI, so the caller supplies
 * \ref Mesh2DExportInputs::unitFactor (the layer's `depthToMeshUnits()`) and
 * every x/y/z, depth, head and velocity is multiplied by it. The dry-depth
 * comparison happens in metres, before scaling.
 */
#ifndef OPENSWMMVIS_IO_MESH2DRESULTSEXPORT_H
#define OPENSWMMVIS_IO_MESH2DRESULTSEXPORT_H

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class IMesh2DSource;

namespace openswmmvis::io {

/*! \brief Output container. Vector formats share one OGR code path. */
enum class Mesh2DExportFormat
{
    Shapefile,    ///< one `.shp` set per variable ("ESRI Shapefile")
    GeoPackage,   ///< one `.gpkg`, one layer per variable ("GPKG")
    GeoTiff       ///< one `.tif` per variable, one band per step ("GTiff")
};

/*! \brief How cell-centred values are carried to raster pixels. */
enum class Mesh2DInterp
{
    NaturalNeighbour,  ///< Sibson weights over the cell-centroid Delaunay
    Idw,               ///< Shepard over the k nearest centroids
    GreenGauss         ///< containing cell + its Green–Gauss gradient (fastest)
};

/*! \brief Exportable fields, as a bitmask. */
enum Mesh2DVariable : unsigned
{
    Mesh2DDepth = 1u << 0,   ///< overland flow depth
    Mesh2DHead  = 1u << 1,   ///< water-surface elevation
    Mesh2DVx    = 1u << 2,   ///< velocity, x component
    Mesh2DVy    = 1u << 3,   ///< velocity, y component
    Mesh2DVmag  = 1u << 4    ///< speed |v|
};

/*! \brief NoData written into every raster band (dry / outside the mesh). */
constexpr float kMesh2DNoData = -9999.0f;

/*! \brief Maximum fields exist only for these variables (see \ref Mesh2DExportOptions::includeMax).
 *  A signed component's maximum is not a meaningful envelope. */
constexpr unsigned kMesh2DMaxVariables = Mesh2DDepth | Mesh2DHead | Mesh2DVmag;

/*! \brief What to export. Collected by the dialog, consumed by \ref exportMesh2DResults. */
struct Mesh2DExportOptions
{
    /*! Output directory + base name, WITHOUT extension. Each variable appends
     *  its own suffix (`_depth.shp`, `_depth.tif`, …); GeoPackage appends only
     *  `.gpkg` and puts the variables in layers. */
    QString basePath;

    Mesh2DExportFormat format = Mesh2DExportFormat::GeoTiff;

    /*! Bitwise OR of \ref Mesh2DVariable. */
    unsigned variables = Mesh2DDepth | Mesh2DHead | Mesh2DVmag;

    /*! Frame indices to export, ascending. May be empty when \ref includeMax. */
    std::vector<int> timeSteps;

    /*! Also export the whole-run maxima (depth, head, speed) — the engine's
     *  ENVELOPES datasets when the file carries them, else a scan of every
     *  frame. Independent of \ref timeSteps. */
    bool includeMax = true;

    // ── Raster only ────────────────────────────────────────────────────
    /*! Square pixel size in MODEL units. <= 0 means \ref suggestedRasterCellSize. */
    double cellSize = 0.0;
    Mesh2DInterp interp = Mesh2DInterp::NaturalNeighbour;
    /*! Neighbour count for \ref Mesh2DInterp::Idw. */
    int idwNeighbours = 8;
    /*! Shepard exponent for \ref Mesh2DInterp::Idw. */
    double idwPower = 2.0;
    /*! Write NoData wherever the interpolated depth is below the dry depth.
     *  Off writes the interpolated value everywhere inside the mesh. */
    bool maskDry = true;
};

/*! \brief The run to export plus the unit / CRS context the source cannot state. */
struct Mesh2DExportInputs
{
    IMesh2DSource *source = nullptr;   ///< not owned; must outlive the call
    /*! SI metres → model units, for coordinates AND values (the layer's
     *  `depthToMeshUnits()`; 1.0 for a metric model, 1/0.3048 for a US one). */
    double unitFactor = 1.0;
    /*! Dry threshold in METRES (the layer's `dryDepth()`). */
    double dryDepthM = 1e-4;
    /*! Model CRS as WKT; empty writes no `.prj` / projection. */
    QString srsWkt;
};

/*! \brief What was written, or why nothing was. */
struct Mesh2DExportReport
{
    QStringList files;      ///< every path created (in creation order)
    QString     error;      ///< empty on success; "Cancelled" when the caller stopped it
    QStringList warnings;   ///< non-fatal, e.g. natural neighbour fell back to IDW
};

/*! \brief Progress sink. Return false to cancel — every file written so far is
 *  then deleted, so a cancelled export leaves nothing half-written behind. */
using Mesh2DExportProgress =
    std::function<bool(int done, int total, const QString &what)>;

/*!
 * \brief Run the export described by \p options against \p inputs.
 * \param progress May be empty; called between units of work.
 * \param report   Optional; receives the files written, warnings, and the
 *                 failure reason.
 * \returns true when every requested file was written.
 */
bool exportMesh2DResults(const Mesh2DExportInputs &inputs,
                         const Mesh2DExportOptions &options,
                         const Mesh2DExportProgress &progress,
                         Mesh2DExportReport *report);

/*! \brief What a caller needs to size a raster before running the export. */
struct Mesh2DGridHint
{
    double extentWidth  = 0.0;   ///< mesh bounding box, model units
    double extentHeight = 0.0;
    /*! Half the square root of the median cell area — about two pixels across
     *  a typical cell, so the mesh resolution survives the resampling without
     *  exploding the raster. */
    double suggestedCellSize = 0.0;

    bool isValid() const
    {
        return extentWidth > 0.0 && extentHeight > 0.0 && suggestedCellSize > 0.0;
    }
};

/*!
 * \brief Mesh extent and a default pixel size for \p source, so a dialog can
 *        show the resulting raster's dimensions before anything is written.
 * \returns a default-constructed (invalid) hint when the source has no usable
 *          geometry.
 */
Mesh2DGridHint mesh2DGridHint(IMesh2DSource *source, double unitFactor);

/*! \brief Lower-case single-word name of \p variable (`"depth"`, `"vmag"`, …),
 *  used for file suffixes, layer names and the sidecar's `field` column. */
QString mesh2DVariableKey(Mesh2DVariable variable);

/*! \brief Human label for \p variable, e.g. "Water surface elevation". */
QString mesh2DVariableLabel(Mesh2DVariable variable);

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_MESH2DRESULTSEXPORT_H
