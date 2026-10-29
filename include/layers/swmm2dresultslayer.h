/**
 * @file      swmm2dresultslayer.h
 * @brief     renders 2D surface-routing depth as a colour-mapped overlay on the canvas.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice CF.MVP — renders 2D surface-routing depth as a colour-mapped overlay
 * on the canvas. The layer is data-source agnostic: during a run the source
 * is an `EngineMesh2DSource` driven by SimulationRunner ticks; after the run
 * the source swaps to an `HDF5Mesh2DSource` reading the engine's CF/UGRID
 * HDF5 file so the user can scrub a time slider back to peak inundation.
 *
 * Rendering reuses the `SceneTri` paint pattern from `SWMM2DMeshLayer`
 * (Slice AU). The only differences:
 *   - Per-triangle colour comes from depth, not elevation, via the
 *     inundation colour ramp.
 *   - Dry cells and suppressed thin films leave the terrain visible.
 *     Partially wet cells retain their exact zero-depth shoreline.
 *   - No hillshade pass — the wet "sheen" is intentionally flat colour.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LAYERS_SWMM2DRESULTSLAYER_H
#define OPENSWMMVIS_LAYERS_SWMM2DRESULTSLAYER_H

#include "io/mesh2dscalarframe.h"
#include "render/resultscalargeometry.h"
#include "io/mesh2dlivevariables.h"
#include "io/mesh2dh5reader.h" // openswmmvis::io::CoordinateReference
#include "layers/openswmmvislayer.h"
#include "layers/meshspatialgrid.h"
#include "layers/cellwatergeometry.h"
#include "map/mapextent.h"

#include <ogr_spatialref.h> // OGRCoordinateTransformation (issue #155)

#include <QDateTime>
#include <QElapsedTimer>
#include <QFuture>
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include <array>
#include <memory>
#include <limits>
#include <vector>

class QGraphicsScene;
class QGraphicsItem;
class SWMM2DResultsGraphicsItem;
class SWMM2DVelocityArrowsItem;

namespace openswmmvis::io
{
class Mesh2DH5Reader;
}
namespace OpenSWMM::Render
{
class ResultScalarSublayer;
class IFeatureRenderer;
class RuleList; // Slice B.5b — see ruleList() override below.
}

// Slice S5.6 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md) — 2D sublayer foundation.
#include "render/iattributeprovider.h" // Slice DM.3
#include "render/isublayerhost.h"
#include "render/legendsymbolitem.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "render/sublayers/velocityvectorsublayer.h"
#include "render/sublayers/flowstreaksublayer.h"
#include "render/flowparticlelayercontroller.h"

// ---------------------------------------------------------------------------
// IMesh2DSource — data-source abstraction shared by live + replay modes
// ---------------------------------------------------------------------------

/*!
 * \brief Abstract source of mesh geometry + per-triangle depth time series.
 *
 * Two concrete implementations live in this header/.cpp pair:
 *  - `EngineMesh2DSource` — live, fed by SimulationRunner each progress tick.
 *  - `HDF5Mesh2DSource`   — post-run, reads the engine's CF/UGRID HDF5.
 */
class IMesh2DSource
{
public:
    /**
     * @brief Release a source through its polymorphic owner.
     * @note Sources are serialized on their owning (normally GUI) thread; no concurrent
     *   mutation/read guarantee is provided. Worker fits capture value inputs or open private HDF5
     *   readers.
     */
    virtual ~IMesh2DSource() = default;

    /*! \brief Geometry counts. Stable for the lifetime of the source.
     *
     *  \c triangleCount() is the number of CELLS (engine faces — triangles
     *  and, on a mixed mesh, quads; workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md).
     *  Every per-face array (\ref readDepthsAt, \ref readFaceFieldAt) is
     *  sized to it; the historical name is kept for its callers.
     * @return Source vertex slots, including any subsequently rejected invalid geometry.
     */
    virtual int vertexCount() const = 0;
    /**
     * @return Original face/cell slots (triangles plus quads); display fan size can be larger.
     */
    virtual int triangleCount() const = 0;

    /*! \brief Latest known time-step count (grows during live mode).
     * @return Current retained report/tick count; indices may change after history thinning.
     */
    virtual int timeCount() const = 0;

    /*! \brief True while this source is streaming from a running simulation
     *  (frames keep arriving). The layer may follow the newest live frame until the user takes
     *   manual
     *  playback control. A completed file source returns false.
     * @return False by default; engine source stays true until markFinished().
     */
    virtual bool isLive() const { return false; }
    // Periodic live snapshots are not a complete report history, even after
    // stopping. Whole-run scalar extrema require a complete output source.
    /**
     * @return Default follows !isLive(); engine snapshots always return false, even after
     *   completion because retention may omit reports.
     */
    virtual bool supportsWholeRunRange() const { return !isLive(); }
    /**
     * @return Empty by default; adapters can expose a source filename for independent worker
     *   readers.
     */
    virtual QString sourcePath() const { return {}; }

    /*! \brief Fetch mesh geometry as a DISPLAY triangle fan. Resizes outputs.
     *
     *  \p tris carries one triangle per triangle cell and two per quad (the
     *  engine's VFR sub-triangle split, mesh::cellGeom), in cell order — so
     *  \c tris.size() >= triangleCount(). Consumers that need per-cell values
     *  must use \ref readCells and map fan triangles back to their cell.
     * @param[out] vx Replaced vertex X coordinates in source SI metres.
     * @param[out] vy Replaced vertex Y coordinates in source SI metres, matching X count.
     * @param[out] vz Replaced bed elevations in SI metres, matching X count.
     * @param[out] tris Replaced zero-based display fan triangles; quad split follows VFR elevation
     *   diagonal.
     * @return True after all reads succeed; failure can leave coordinate/connectivity outputs
     *   partially replaced. Caller must discard them on false.
     */
    virtual bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy,
                                  std::vector<double> &vz,
                                  std::vector<std::array<int, 3>> &tris) = 0;

    /*! \brief Fetch mesh geometry as CELLS: \p cells[i] = {v0,v1,v2,v3} with
     *  v3 == -1 for a triangle (cyclic order for a quad), one entry per face,
     *  engine order (triangles first, then quads). Resizes outputs.
     *
     *  Default: an all-triangle source — \ref readMeshGeometry's triangles
     *  ARE the cells, so they are wrapped with v3 = -1. Mixed-mesh sources
     *  override.
     * @param[out] vx Replaced vertex X coordinates in source SI metres.
     * @param[out] vy Replaced vertex Y coordinates in source SI metres, matching X count.
     * @param[out] vz Replaced bed elevations in SI metres, matching X count.
     * @param[out] cells Replaced original cyclic cell connectivity; fourth slot minus one denotes a
     *   triangle.
     * @return True after all reads succeed; failure can leave coordinate/connectivity outputs
     *   partially replaced. Caller must discard them on false.
     */
    virtual bool readCells(std::vector<double> &vx, std::vector<double> &vy,
                           std::vector<double> &vz, std::vector<std::array<int, 4>> &cells)
    {
        std::vector<std::array<int, 3>> tris;
        if (!readMeshGeometry(vx, vy, vz, tris))
            return false;
        cells.resize(tris.size());
        for (size_t i = 0; i < tris.size(); ++i)
            cells[i] = {tris[i][0], tris[i][1], tris[i][2], -1};
        return true;
    }

    /*!
     * \brief How the coordinates from \ref readMeshGeometry relate to the
     *        model CRS — see openswmmvis::io::CoordinateReference.
     *
     * Every source delivers SI metres (the 2D solver's internal unit), which
     * for a foot-based model CRS is NOT the unit the coordinates must be
     * reprojected from. The default returns an undeclared reference, so a
     * source that cannot state the factor uses the layer's explicitly configured
     * fallback conversion, whose initial value1.0 leaves coordinates unchanged.
      *
     * @return Undeclared metadata by default; the layer uses its explicit fallback scale, initially
     *   1.0, rather than inferring conversion from CRS linear units.
     */
    virtual openswmmvis::io::CoordinateReference coordinateReference() const { return {}; }

    /*! \brief Fetch per-triangle depth at \p timeIdx. Resizes \p depths to triangleCount().
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] depths Per-original-cell mean water depths in metres; no display fan duplication.
     * @return True for a complete readable depth row; false for invalid index/read failure. Callers
     *   must check success and shape before use.
     */
    virtual bool readDepthsAt(int timeIdx, std::vector<float> &depths) = 0;

    // Call on the source's owning thread. Native adapters gather both arrays
    // atomically into an immutable compact snapshot. Legacy adapters can decline;
    // the layer then retains its full-domain path.
    /**
     * @return Whether compact atomic cell-subset hydraulic snapshots are supported; default false.
     */
    virtual bool supportsHydraulicCells() const { return false; }
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[in] cells Sorted unique valid original cell IDs; order is preserved in the compact
     *   arrays.
     * @return Null by default, with no reads or state changes.
     */
    virtual std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int timeIdx, const std::vector<int> &cells)
    {
        return {};
    }

    /*! \brief One cell's depth at \p timeIdx. The default copies the whole
     *  frame through \ref readDepthsAt; a source holding frames in memory
     *  overrides it in O(1) so per-cell time series (comparison plots) do not
     *  copy every frame per point.
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[in] cell Zero-based original cell index.
     * @param[out] out Mean cell depth in metres, assigned only on success.
     * @return True when the requested cell value is available; false leaves out unchanged. Default
     *   implementation copies the full frame before checking cell bounds.
     */
    virtual bool readDepthAt(int timeIdx, int cell, float &out)
    {
        std::vector<float> row;
        if (!readDepthsAt(timeIdx, row) || cell < 0 || cell >= static_cast<int>(row.size()))
            return false;
        out = row[static_cast<size_t>(cell)];
        return true;
    }

    /*! \brief Bumps whenever frames are removed or reordered (a live source
     *  thinning its history), invalidating anything cached per frame index.
     *  Appends do not bump it. */
    // Semantic result identities are source-local and never species row indexes.
    /**
     * @param[in] warnings Optional destination for discovery diagnostics. Default leaves it
     *   untouched.
     * @return Empty by default.
     */
    virtual QVector<openswmmvis::io::Mesh2DResultVariable>
    faceVariables(QStringList *warnings = nullptr) const
    {
        Q_UNUSED(warnings);
        return {};
    }
    /**
     * @param[in] variable Source-local semantic descriptor identity; key/zone must match catalog.
     * @param[in] timeIdx Zero-based retained frame index; static variables ignore requested index.
     * @param[out] values Replaced cell values in descriptor units; NaN can denote unavailable data.
     * @param[out] status Parallel validity/missing-status array, distinguishing missing chemistry
     *   from dry hydraulic zero.
     * @return False after clearing both outputs by default.
     */
    virtual bool readFaceVariableAt(const openswmmvis::io::Mesh2DResultVariable &variable,
                                    int timeIdx, std::vector<float> &values,
                                    std::vector<openswmmvis::io::Mesh2DValueStatus> &status)
    {
        values.clear();
        status.clear();
        return false;
    }

    /**
     * @return Source-local index-identity generation. Default zero; engine increments after
     *   thinning, HDF5 after every open attempt.
     */
    virtual int historyGeneration() const { return 0; }
    /*! Bumped when the depths, velocity or flux stored at \p timeIdx are replaced while
     *  the index stays the same (a live tick completed by a later push), so a
     *  cached reconstruction of that frame is recognised as stale.
     * @param[in] timeIdx Zero-based retained tick/report index.
     * @return Default zero. Engine returns tick revision for valid indices, zero otherwise;
     *   depth/flux same-tick merges increment it, auxiliary rainfall/head/vertex pushes do not.
     */
    virtual int frameContentRevision(int timeIdx) const
    {
        (void)timeIdx;
        return 0;
    }
    // Generic variable content can arrive after a hydraulic tick. This revision
    // invalidates value/catalog caches without changing pinned frame indexes.
    /**
     * @return Generic-result cache revision; default historyGeneration(), engine history generation
     *   plus successful variable-push count.
     */
    virtual int resultGeneration() const { return historyGeneration(); }

    /*! \brief Wall-clock sim time at \p timeIdx (invalid if out of range or unknown).
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @return Invalid QDateTime by default.
     */
    virtual QDateTime simTimeAt(int timeIdx) const
    {
        (void)timeIdx;
        return {};
    }

    /*!
     * \brief Fetch per-edge signed normal flux at \p timeIdx.
     * \param flux  Resized to \c mesh::edgeSlotCount(triangleCount()), indexed
     *              \c mesh::edgeSlot(cell, localEdge) (stride mesh::kEdgeStride;
     *              slot 3 of a triangle is padding).
     *              Units m³ s⁻¹; positive flows outward through the edge's
     *              outward normal.
     * \returns true on success. Default implementation returns false so callers
     *          can probe whether the source supports flux data without an error.
      *
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @return False by default with output unchanged.
     */
    virtual bool readEdgeFluxAt(int timeIdx, std::vector<float> &flux)
    {
        (void)timeIdx;
        (void)flux;
        return false;
    }

    /*!
     * \brief Fetch time-invariant edge geometry (length + outward unit normal).
     * \param length Resized to \c mesh::edgeSlotCount(triangleCount()) (m).
     * \param nx,ny  Resized likewise (dimensionless); indexed \c mesh::edgeSlot.
     * \returns true on success. Default returns false.
      *
     * @return False by default, leaving all outputs unchanged.
     */
    virtual bool readEdgeGeometry(std::vector<float> &length, std::vector<float> &nx,
                                  std::vector<float> &ny)
    {
        (void)length;
        (void)nx;
        (void)ny;
        return false;
    }

    /*!
     * \brief Fetch per-vertex SIGNED water depth (m) at \p timeIdx — the
     * engine's wet-masked, depth-weighted render reconstruction η_v − z_v
     * (\c Mesh2_node_depth / \c swmm_2d_vertex_get_render_depths_bulk).
     * NOT clamped: negative values over the dry side of partially wet cells
     * carry the sub-cell shoreline intercept; exactly 0 where no incident
     * cell is wet. Dry-cell bed elevations never contribute (unlike the
     * legacy \c Mesh2_node_head solver field, which is deliberately NOT
     * consumed here — blending dry-cell beds lifted the rendered surface up
     * adverse slopes/steps). Resized to \c vertexCount().
     * \returns true on success. Default returns false so callers can probe
     *          whether the source carries the signed field (older engines /
     *          older HDF5 files) and fall back to the GUI-side wet-only
     *          reconstruction.
      *
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] vdepths Signed vertex render depths in metres; negative dry-side values retain
     *   shoreline information.
     * @return False by default with output unchanged.
     */
    virtual bool readVertexDepthsAt(int timeIdx, std::vector<float> &vdepths)
    {
        (void)timeIdx;
        (void)vdepths;
        return false;
    }

    /*!
     * \brief Whether the source carries a named per-face \c [nTime, nFace]
     *        field beyond depth (e.g. \c "Mesh2_face_rainfall",
     *        \c "Mesh2_face_rain_cum"). Default: none — the live in-process
     *        source streams depth/flux only.
      *
     * @param[in] dataset Nullable exact engine dataset name, not a display label.
     * @return False by default.
     */
    virtual bool hasFaceField(const char *dataset) const
    {
        (void)dataset;
        return false;
    }

    /// True for velocity fields carried by the source, rather than reconstructed
    /// on demand from edge flux. Renderers prefer these authoritative values.
    virtual bool hasStoredVelocity() const
    {
        return hasFaceField("Mesh2_face_vx") && hasFaceField("Mesh2_face_vy");
    }

    /*! \brief Fetch one time slice of a named per-face field (engine SI
     *  units). Resized to \c triangleCount(). Default returns false.
     * @param[in] dataset Exact engine dataset identity; use hasFaceField() to probe capability.
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] values Cell-ordered values in dataset SI units: rainfall m/s,cumulative rain
     *   m³,head m,velocity m/s.
     * @return False by default with output unchanged.
     */
    virtual bool readFaceFieldAt(const char *dataset, int timeIdx, std::vector<float> &values)
    {
        (void)dataset;
        (void)timeIdx;
        (void)values;
        return false;
    }
    /**
     * @param[in] dataset Exact engine face dataset identity.
     * @param[in] time Zero-based report/tick index.
     * @param[in] cells Sorted unique valid original cell IDs.
     * @param[out] values Compact values in requested cell order and dataset units.
     * @return False after clearing values by default.
     */
    virtual bool readFaceFieldCellsAt(const char *dataset, int time, const std::vector<int> &cells,
                                      std::vector<float> &values)
    {
        values.clear();
        return false;
    }

    /*!
     * \brief Fetch a time-INVARIANT per-face \c [nFace] envelope written by the
     *        engine's \c ENVELOPES output group (\c "Mesh2_face_max_depth",
     *        \c "Mesh2_face_max_velocity"): the whole-run maximum per cell,
     *        engine SI units, resized to \c triangleCount().
     * \returns true on success. Default false — a source that cannot serve the
     *          envelope makes the caller scan every frame instead.
      *
     * @param[in] dataset Nullable exact envelope identity Mesh2_face_max_depth (m) or
     *   Mesh2_face_max_velocity (m/s).
     * @param[out] values Original-cell whole-run peak array, replaced only by a readable supported
     *   envelope.
     * @return False by default with output unchanged.
     */
    virtual bool readFaceEnvelope(const char *dataset, std::vector<float> &values)
    {
        (void)dataset;
        (void)values;
        return false;
    }
};

// ---------------------------------------------------------------------------
// EngineMesh2DSource — live source driven by SimulationRunner ticks
// ---------------------------------------------------------------------------

class EngineMesh2DSource : public IMesh2DSource
{
public:
    /*!
     * \brief Construct with already-queried mesh geometry.
     *
     * The runner builds this on the GUI thread immediately after the engine
     * finishes its 2D initialization, querying `swmm_2d_vertex_get_xyz_bulk`
     * / `swmm_2d_triangle_get_vertices` against the in-process engine handle.
     * The depth vector starts empty and is appended to with `pushDepths()`.
      *
     * @param[in] vx Moved source X coordinates in metres.
     * @param[in] vy Moved source Y coordinates in metres, same count as X.
     * @param[in] vz Moved source bed elevations in metres, same count as X.
     * @param[in] tris Moved zero-based connectivity in original engine order; three vertices per
     *   cell.
     * @note Creates empty live history; no coordinate/connectivity validation or engine-handle
     *   ownership. Serialize all later producers/readers on the owning thread.
     */
    EngineMesh2DSource(std::vector<double> vx, std::vector<double> vy, std::vector<double> vz,
                       std::vector<std::array<int, 3>> tris);

    /*!
     * \brief Mixed-mesh constructor: \p cells[i] = {v0,v1,v2,v3}, v3 == -1
     * for a triangle (engine cell order, `swmm_2d_cell_get_vertices`).
     * The triangle constructor above is this with every v3 = -1.
      *
     * @param[in] vx Moved source X coordinates in metres.
     * @param[in] vy Moved source Y coordinates in metres, same count as X.
     * @param[in] vz Moved source bed elevations in metres, same count as X.
     * @param[in] cells Moved zero-based connectivity in original engine order; cyclic three/four
     *   vertices; fourth minus one for triangles.
     * @note Creates empty live history; no coordinate/connectivity validation or engine-handle
     *   ownership. Serialize all later producers/readers on the owning thread.
     */
    EngineMesh2DSource(std::vector<double> vx, std::vector<double> vy, std::vector<double> vz,
                       std::vector<std::array<int, 4>> cells);

    /*!
     * \brief Append one tick's worth of per-triangle depth.
     *
     * Pushed from `SimulationRunner::twoDDepthsAvailable` via queued connection
     * — always on the GUI thread, so no synchronization is needed.
      *
     * @param[in] depths Moved cell mean depths in metres; shape/nonfinite values are not checked
     *   here.
     * @param[in] simTime Timestamp stored for a new tick; a same-time merge also replaces
     *   timestamp.
     * @param[in] elapsedSec Seconds since simulation start; compared only with newest tick using
     *   absolute tolerance 1e-6.
     * @note Owning/GUI-thread producer. Merges only when newest tick depths are empty and elapsed
     *   time matches; otherwise appends, even for equal time with existing depths. New tick
     *   enforces retention unless pinned. Same-tick merge increments hydraulic revision.
     */
    void pushDepths(std::vector<float> depths, QDateTime simTime, double elapsedSec);

    /*!
     * \brief Append one tick's worth of per-edge signed normal flux.
     *
     * Mirrors \ref pushDepths but writes to the flux slot of the most recent
     * tick. Expected size is \c mesh::edgeSlotCount(triangleCount()) (stride
     * 4, `swmm_2d_edge_stride`); a stride-3 array from an all-triangle
     * engine is re-packed to the padded layout. If called before
     * \c pushDepths for the same tick, the runner buffers the flux into the
     * pending slot and \c pushDepths will pair them. Empty flux vectors are
     * accepted (older engines without \c swmm_2d_get_edge_flux_bulk skip the
     * push entirely; see SimulationRunner CF.2.4 dlsym gating).
      *
     * @param[in] flux Moved outward-positive m³/s edge slots; triangle-only stride-three payloads
     *   are expanded to four with zero padding. Other shapes remain unchecked.
     * @param[in] simTime Timestamp stored for a new tick; a same-time merge keeps the existing
     *   timestamp.
     * @param[in] elapsedSec Seconds since simulation start; compared only with newest tick using
     *   absolute tolerance 1e-6.
     * @note Owning/GUI-thread producer. Matching newest tick is updated in place; otherwise appends
     *   a tick with empty depths. Accumulated fields from earlier ticks are never borrowed. New
     *   tick enforces retention unless pinned. Same-tick merge increments hydraulic revision.
     */
    void pushFlux(std::vector<float> flux, QDateTime simTime, double elapsedSec);

    /// Store an authoritative SI velocity pair at the matching elapsed time.
    /// Wrong-sized pairs are refused without modifying history. Either field
    /// may arrive before depth; the runner delivers the pair before its depth
    /// signal in one queued callback, so the first visible frame is consistent.
    bool pushVelocities(std::vector<float> vx, std::vector<float> vy,
                        QDateTime simTime, double elapsedSec);

    /*!
     * \brief Append one tick's worth of SIGNED per-vertex render depths
     * (\c swmm_2d_vertex_get_render_depths_bulk — the engine's wet-masked
     * η_v − z_v; datum already subtracted engine-side in double). Mirrors
     * \ref pushFlux — pairs with the tick whose elapsed time matches. Values
     * are stored as floats WITHOUT clamping: negatives carry the sub-cell
     * shoreline intercept.
      *
     * @param[in] depths Signed vertex depths in metres, narrowed to float without clamping or shape
     *   checks.
     * @param[in] simTime Timestamp stored for a new tick; a same-time merge keeps the existing
     *   timestamp.
     * @param[in] elapsedSec Seconds since simulation start; compared only with newest tick using
     *   absolute tolerance 1e-6.
     * @note Owning/GUI-thread producer. Matching newest tick is updated in place; otherwise appends
     *   a tick with empty depths. Accumulated fields from earlier ticks are never borrowed. New
     *   tick enforces retention unless pinned. Same-tick merge does not increment hydraulic
     *   revision.
     */
    void pushVertexSignedDepths(std::vector<double> depths, QDateTime simTime, double elapsedSec);

    /*!
     * \brief Append one tick's per-cell rainfall intensity (m/s) and
     * cumulative rainfall volume (m³) — the live counterparts of the HDF5
     * \c Mesh2_face_rainfall / \c Mesh2_face_rain_cum datasets, served back
     * through \ref readFaceFieldAt under those names so rainfall plots work
     * during a run exactly as they do post-run. Pairs with the tick whose
     * elapsed time matches (same convention as \ref pushFlux).
      *
     * @param[in] rainfall Moved per-cell rainfall intensity m/s.
     * @param[in] rainCum Moved per-cell cumulative rain volume m³.
     * @param[in] simTime Timestamp used only when appending a new tick.
     * @param[in] elapsedSec Seconds since start; newest-only matching tolerance 1e-6.
     * @note Owning-thread only, no shape validation. Matches newest tick or appends empty-depth
     *   tick, advertises rainfall even for empty arrays, enforces retention on append; same-time
     *   merge does not increment hydraulic revision.
     */
    void pushRainfall(std::vector<float> rainfall, std::vector<float> rainCum, QDateTime simTime,
                      double elapsedSec);

    /*!
     * \brief Append one tick's per-cell water-surface elevation (m,
     * \c swmm_2d_get_heads_bulk) — the live counterpart of the HDF5
     * \c Mesh2_face_head dataset, served back through \ref readFaceFieldAt
     * under that name so a mid-run export writes the solver's VFR head
     * rather than the depth + mean-bed approximation. Same tick pairing as
     * \ref pushRainfall.
      *
     * @param[in] heads Moved cell water-surface elevations in metres; shape unchecked.
     * @param[in] simTime Timestamp stored for a new tick; a same-time merge keeps the existing
     *   timestamp.
     * @param[in] elapsedSec Seconds since simulation start; compared only with newest tick using
     *   absolute tolerance 1e-6.
     * @note Owning/GUI-thread producer. Matching newest tick is updated in place; otherwise appends
     *   a tick with empty depths. Accumulated fields from earlier ticks are never borrowed. New
     *   tick enforces retention unless pinned. Same-tick merge does not increment hydraulic
     *   revision.
     */
    void pushHeads(std::vector<float> heads, QDateTime simTime, double elapsedSec);

    // Immutable worker-captured variables, paired with the hydraulic tick.
    // Invalid shape/identity is refused without modifying the history.
    /**
     * @param[in] frame Nonnull immutable shared cell-variable snapshot with nonempty unique
     *   semantic keys, matching cell/value/status counts and stable units/zone/temporal identity.
     * @param[in] simTime Timestamp used when appending a new tick.
     * @param[in] elapsedSec Finite elapsed simulation seconds; newest tick match tolerance 1e-6.
     * @return False without mutation if payload shape/identity is invalid; true after
     *   catalog/warnings accumulation and merge/append. Takes shared ownership, increments
     *   generic-result generation even on same-time update and enforces retention. GUI-thread only.
     */
    bool pushFaceVariables(openswmmvis::io::Mesh2DLiveVariablesPtr frame, QDateTime simTime,
                           double elapsedSec);
    /**
     * @param[in] warnings Optional destination for discovery diagnostics. Replaced with source
     *   diagnostics.
     * @return Copy of known semantic variable descriptors; temporal frameCount updated to retained
     *   time count, static count zero. Descriptor identities are source-local keys/zone, never
     *   species row offsets.
     */
    QVector<openswmmvis::io::Mesh2DResultVariable>
    faceVariables(QStringList *warnings = nullptr) const override;
    /**
     * @param[in] variable Source-local semantic descriptor identity; key/zone must match catalog.
     * @param[in] timeIdx Zero-based retained frame index; static variables ignore requested index.
     * @param[out] values Replaced cell values in descriptor units; NaN can denote unavailable data.
     * @param[out] status Parallel validity/missing-status array, distinguishing missing chemistry
     *   from dry hydraulic zero.
     * @return False after clearing outputs for unknown variable or invalid temporal index. A known
     *   variable missing in this tick succeeds with all-cell NaN/Missing; static fields search
     *   retained ticks for the first matching payload.
     */
    bool readFaceVariableAt(const openswmmvis::io::Mesh2DResultVariable &variable, int timeIdx,
                            std::vector<float> &values,
                            std::vector<openswmmvis::io::Mesh2DValueStatus> &status) override;

    /*!
     * \brief Replace the cumulative per-cell envelopes (m, m/s) read via
     * \c swmm_2d_get_stat_max_depths / \c _velocities — the live counterparts
     * of the HDF5 ENVELOPES datasets, served through \ref readFaceEnvelope.
     * Latest wins: the engine's envelope is monotone, so only the newest
     * payload is kept (nothing per frame).
      *
     * @param[in] maxDepth Moved original-cell cumulative maxima in metres.
     * @param[in] maxVel Moved original-cell maximum speed m/s.
     * @note Replaces previous payloads without size/monotonicity checks, cache generation change or
     *   notification.
     */
    void setEnvelopes(std::vector<float> maxDepth, std::vector<float> maxVel);

    /*!
     * \brief Dry threshold (m) below which a cell's reconstructed velocity is
     * zero — the engine's \c dry_depth cutoff in computeFaceVelocity. Set from
     * \c swmm_2d_get_dry_depth at twoDInitialized; default = the layer's.
      *
     * @param[in] m Raw velocity dry cutoff in metres, initially 1e-4. Copied without finite/range
     *   validation, revision bump or notification.
     */
    void setDryDepth(double m) { dry_depth_ = m; }
    /**
     * @return Raw configured velocity dry threshold in metres.
     */
    double dryDepth() const noexcept { return dry_depth_; }

    /*!
     * \brief Hold the frame numbering still: while pinned, \ref pushDepths and
     * friends still append but the history cap is not enforced (no thinning,
     * no \ref historyGeneration bump), so an index handed out before the pin
     * stays valid. Unpinning enforces the cap once. Used by the mid-run
     * export, whose modal dialog and progress loop pump the event queue that
     * delivers the ticks.
      *
     * @param[in] pinned True suspends retention/index renumbering while ticks still append; false
     *   immediately enforces caps and may invalidate frame indices.
     */
    void setHistoryPinned(bool pinned);
    /**
     * @return Whether index-renumbering retention is suspended; does not stop appends.
     */
    bool historyPinned() const noexcept { return pinned_; }

    /*!
     * \brief Install time-invariant edge geometry queried via
     * \c swmm_2d_edge_get_geometry_bulk once at twoDInitialized. Sizes are
     * \c mesh::edgeSlotCount(triangleCount()) each (a stride-3 array from an
     * all-triangle engine is re-packed). Optional — when not called, the
     * source advertises no edge geometry (readEdgeGeometry returns false).
      *
     * @param[in] length Moved padded per-cell edge lengths in metres.
     * @param[in] nx Moved outward unit-normal X components.
     * @param[in] ny Moved outward unit-normal Y components.
     * @note Triangle-only stride-three arrays expand independently to four; other shapes pass
     *   through. Does not validate positive lengths/unit normals or increment content revision.
     *   Caller must install consistent arrays before velocity reads.
     */
    void setEdgeGeometry(std::vector<float> length, std::vector<float> nx, std::vector<float> ny);

    // IMesh2DSource
    /**
     * @return Source vertex slots, including any subsequently rejected invalid geometry.
     */
    int vertexCount() const override { return static_cast<int>(vx_.size()); }
    /**
     * @return Original face/cell slots (triangles plus quads); display fan size can be larger.
     */
    int triangleCount() const override { return static_cast<int>(cells_.size()); }
    /**
     * @return Current retained report/tick count; indices may change after history thinning.
     */
    int timeCount() const override { return static_cast<int>(history_.size()); }
    /**
     * @return Default follows !isLive(); engine snapshots always return false, even after
     *   completion because retention may omit reports.
     */
    bool supportsWholeRunRange() const override { return false; }
    /**
     * @return False by default; engine source stays true until markFinished().
     */
    bool isLive() const override { return !finished_; } // streaming from the running sim

    /*! \brief Stop advertising as live once the run has ended. The retained
     *  in-memory history stays fully scrubbable; it just is not growing any
     *  more, so consumers that wait for new frames (the animation controller's
     *  wait-at-end behaviour, follow-live) must stop waiting. Called on the
     *  runner's finished signal, including the paths that keep this source
     *  instead of swapping in the .h5-backed one.
     * @note Sets finished flag only, preserving history, retention and revisions; no signals. This
     *   does not establish a complete whole-run history.
     */
    void markFinished() { finished_ = true; }
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[in] cell Zero-based original cell index.
     * @param[out] out Mean cell depth in metres, assigned only on success.
     * @return True when the requested cell value is available; false leaves out unchanged. No
     *   smoothing or visibility masking is applied.
     */
    bool readDepthAt(int timeIdx, int cell, float &out) override;
    /**
     * @return Source-local index-identity generation. Default zero; engine increments after
     *   thinning, HDF5 after every open attempt.
     */
    int historyGeneration() const override { return generation_; }
    /**
     * @param[in] timeIdx Zero-based retained tick/report index.
     * @return Default zero. Engine returns tick revision for valid indices, zero otherwise;
     *   depth/flux same-tick merges increment it, auxiliary rainfall/head/vertex pushes do not.
     */
    int frameContentRevision(int timeIdx) const override
    {
        return timeIdx >= 0 && size_t(timeIdx) < history_.size()
                   ? history_[size_t(timeIdx)].revision
                   : 0;
    }
    /**
     * @return Generic-result cache revision; default historyGeneration(), engine history generation
     *   plus successful variable-push count.
     */
    int resultGeneration() const override { return generation_ + variable_generation_; }

    /*! \brief Cap on retained frames (default 2000). Past the cap the OLDER
     *  half of the history is thinned 2:1 (frames keep their sim times, so
     *  scrubbing by time is unaffected); \ref historyGeneration bumps. A
     *  1 Hz tick on a 200 k-cell mesh is ~6 MB per frame — unbounded history
     *  paged the machine on long runs. \p n < 8 disables the cap.
     * @param[in] n Retained frame cap, initially 2000; less than 8 disables this cap. Enforces
     *   immediately unless pinned, with older-half 2:1 thinning while at least8ticks remain.
     */
    void setMaxFrames(int n)
    {
        max_frames_ = n;
        enforceCap_();
    }
    /**
     * @return Raw retained-frame cap; less than 8 disables it.
     */
    int maxFrames() const noexcept { return max_frames_; }

    /*! \brief Byte budget for the retained history (0 = unlimited), checked
     *  alongside the frame cap. The frame cap alone let a 155 k-cell mesh hold
     *  ~9 GB (a frame is ~28 B/cell + 4 B/vertex). Past the budget the same
     *  2:1 thinning repeats until the history is at or below 75 % of it, so a
     *  big mesh does not re-thin on every tick at the boundary. Preferences →
     *  Simulation → "Live 2D history budget".
     * @param[in] bytes Payload byte budget, zero unlimited. Enforces immediately unless pinned;
     *   thinning targets75% of budget but stops below 8 ticks, so this is not an absolute memory
     *   cap.
     */
    void setMaxBytes(size_t bytes)
    {
        max_bytes_ = bytes;
        enforceCap_();
    }
    /**
     * @return Raw payload byte budget; zero unlimited.
     */
    size_t maxBytes() const noexcept { return max_bytes_; }
    /*! \brief Bytes held by the retained frames' payload vectors.
     * @return Sum of retained float payload sizes and generic variable payload bytes, excluding
     *   vector spare capacity, geometry, envelopes and container/descriptor overhead.
     */
    size_t historyBytes() const;
    /**
     * @param[out] vx Replaced vertex X coordinates in source SI metres.
     * @param[out] vy Replaced vertex Y coordinates in source SI metres, matching X count.
     * @param[out] vz Replaced bed elevations in SI metres, matching X count.
     * @param[out] tris Replaced zero-based display fan triangles; quad split follows VFR elevation
     *   diagonal.
     * @return Always true; copies owned geometry and derives the display fan without validating
     *   vector lengths.
     */
    bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy, std::vector<double> &vz,
                          std::vector<std::array<int, 3>> &tris) override;
    /**
     * @param[out] vx Replaced vertex X coordinates in source SI metres.
     * @param[out] vy Replaced vertex Y coordinates in source SI metres, matching X count.
     * @param[out] vz Replaced bed elevations in SI metres, matching X count.
     * @param[out] cells Replaced original cyclic cell connectivity; fourth slot minus one denotes a
     *   triangle.
     * @return Always true; copies owned geometry and derives the display fan without validating
     *   vector lengths.
     */
    bool readCells(std::vector<double> &vx, std::vector<double> &vy, std::vector<double> &vz,
                   std::vector<std::array<int, 4>> &cells) override;
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] depths Per-original-cell mean water depths in metres; no display fan duplication.
     * @return True for a valid tick, including an empty row awaiting depth arrival. Invalid index
     *   replaces output with cell-count zeros and returns false.
     */
    bool readDepthsAt(int timeIdx, std::vector<float> &depths) override;
    /**
     * @return Whether compact atomic cell-subset hydraulic snapshots are supported; default false.
     */
    bool supportsHydraulicCells() const override { return true; }
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[in] cells Sorted unique valid original cell IDs; order is preserved in the compact
     *   arrays.
     * @return Immutable independently owned cell/depth/stride-four-flux snapshot, or null on
     *   invalid IDs/index/incomplete depth/read failure. Flux may be empty; generation/revision
     *   describe the exact retained tick. Request on the source owning thread.
     */
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int timeIdx, const std::vector<int> &cells) override;

    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @return Stored tick timestamp, or invalid for an out-of-range index; values need not be
     *   valid/monotonic unless the producer ensures this.
     */
    QDateTime simTimeAt(int timeIdx) const override;

    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] flux Replaced outward-positive edge flux in cubic metres/second, stride four per
     *   original cell, with triangle padding.
     * @return True for a nonempty tick payload without validating its shape. Invalid index/missing
     *   payload replaces output with padded zeros and returns false.
     */
    bool readEdgeFluxAt(int timeIdx, std::vector<float> &flux) override;
    /**
     * @param[out] length Padded cell-edge lengths in metres.
     * @param[out] nx Corresponding outward unit-normal X components, dimensionless.
     * @param[out] ny Corresponding outward unit-normal Y components, dimensionless.
     * @return False with outputs unchanged when no lengths were installed; otherwise copies all
     *   three arrays and returns true without shape/normal validation.
     */
    bool readEdgeGeometry(std::vector<float> &length, std::vector<float> &nx,
                          std::vector<float> &ny) override;
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] vdepths Signed vertex render depths in metres; negative dry-side values retain
     *   shoreline information.
     * @return True after copying a nonempty tick field; invalid index/missing field returns false
     *   and leaves output unchanged.
     */
    bool readVertexDepthsAt(int timeIdx, std::vector<float> &vdepths) override;
    /*! Live fields: rainfall / rain_cum, head and vx / vy. Authoritative
     *  streamed velocity is preferred. Legacy ticks without it reconstruct
     *  from edge flux using the engine's RT0 formula.
     * @param[in] dataset Nullable exact engine dataset name, not a display label.
     * @return Run-wide capability, not proof every tick contains this field:
     *   rainfall/rain_cum, head, or vx/vy when native vectors or flux geometry exist.
     *   Unknown/null names return false.
     */
    bool hasFaceField(const char *dataset) const override;
    bool hasStoredVelocity() const override { return has_velocity_; }
    /**
     * @param[in] dataset Exact engine dataset identity; use hasFaceField() to probe capability.
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] values Cell-ordered values in dataset SI units: rainfall m/s,cumulative rain
     *   m³,head m,velocity m/s.
     * @return False unchanged for unsupported dataset; invalid index/missing supported payload or
     *   failed velocity shape check writes cell-count zeros and returns false. Success copies
     *   values or reconstructs velocity with dry-depth masking.
     */
    bool readFaceFieldAt(const char *dataset, int timeIdx, std::vector<float> &values) override;
    bool readFaceFieldCellsAt(const char *dataset, int timeIdx, const std::vector<int> &cells,
                              std::vector<float> &values) override;
    /**
     * @param[in] dataset Nullable exact envelope identity Mesh2_face_max_depth (m) or
     *   Mesh2_face_max_velocity (m/s).
     * @param[out] values Original-cell whole-run peak array, replaced only by a readable supported
     *   envelope.
     * @return False for unsupported/null name or empty stored envelope, leaving output unchanged;
     *   true copies latest stored values without shape checks.
     */
    bool readFaceEnvelope(const char *dataset, std::vector<float> &values) override;

private:
    /*! Re-pack a stride-3 per-edge array (all-triangle engine) into the
     *  padded stride-4 slot layout; arrays already stride 4 pass through.
     * @param[in] a Moved edge-value payload, expected3*cellCount or4*cellCount.
     * @return Padded stride-four copy only for nonempty all-triangle stride-three input; otherwise
     *   moves input through unchanged, including malformed sizes.
     */
    std::vector<float> toEdgeSlots_(std::vector<float> a) const;

    std::vector<double> vx_, vy_, vz_;
    std::vector<std::array<int, 4>> cells_; ///< {v0,v1,v2,v3}; v3 = -1 for a triangle

    struct Tick {
        std::vector<float> depths;
        std::vector<float> flux; ///< [mesh::edgeSlot(cell, e)]; empty when source has no flux feed.
        std::vector<float> vertex_depths; ///< [vertex]; empty when engine lacks the heads API.
        std::vector<float> rainfall; ///< [tri] m/s; empty when engine lacks the rainfall bulk API.
        std::vector<float> rain_cum; ///< [tri] m³ cumulative; paired with rainfall.
        std::vector<float> heads;    ///< [tri] m water surface; empty when not pushed.
        std::vector<float> velocity_x, velocity_y; ///< optional native SI m/s pair
        openswmmvis::io::Mesh2DLiveVariablesPtr variables;
        QDateTime sim_time;
        double elapsed_sec = 0.0;
        int revision = 0; ///< see frameContentRevision
    };
    std::vector<Tick> history_;
    QVector<openswmmvis::io::Mesh2DResultVariable> live_variables_;
    QStringList live_variable_warnings_;
    bool has_rainfall_ = false;        ///< any tick carried rainfall
    bool has_heads_ = false;           ///< any tick carried heads
    bool has_flux_ = false;            ///< any tick carried flux
    bool has_velocity_ = false;        ///< any tick carried authoritative velocity
    std::vector<float> env_max_depth_; ///< see setEnvelopes (empty = none yet)
    std::vector<float> env_max_vel_;
    double dry_depth_ = 1e-4;     ///< see setDryDepth
    int max_frames_ = 2000;       ///< see setMaxFrames
    size_t max_bytes_ = 0;        ///< see setMaxBytes (0 = unlimited)
    int generation_ = 0;          ///< see historyGeneration
    int variable_generation_ = 0; ///< completed variable payloads
    bool finished_ = false;       ///< see markFinished
    bool pinned_ = false;         ///< see setHistoryPinned
    /**
     * @brief Thin the older half 2:1 repeatedly to satisfy configured retention targets, stopping
     *   below 8 ticks.
     * @note Pinned/no-exceeded-cap is a no-op. Preserves newest half and associated timestamps,
     *   increments history generation once per thinning call, renumbers retained indices and
     *   invalidates consumers that stored them.
     */
    void enforceCap_();
    /*! Both velocity components of one frame (engine SI m/s) via RT0.
     * @param[in] timeIdx Valid retained tick index; private caller must bounds-check first.
     * @param[out] vx Cell velocity X in m/s, allocated/zero-filled only after shape validation.
     * @param[out] vy Cell velocity Y in m/s before scene inversion.
     * @return False unchanged if depth/flux/length shapes mismatch; true after RT0/depth
     *   reconstruction, leaving dry/unsolvable cells zero. Requires matching valid normal arrays
     *   installed, owning-thread serialized access.
     */
    bool reconstructVelocity_(int timeIdx, std::vector<float> &vx, std::vector<float> &vy) const;

    // Time-invariant edge geometry; populated once at twoDInitialized via
    // setEdgeGeometry. Empty when the engine lacks the bulk geometry API.
    std::vector<float> edge_length_;
    std::vector<float> edge_nx_;
    std::vector<float> edge_ny_;
};

// ---------------------------------------------------------------------------
// HDF5Mesh2DSource — post-run source backed by Mesh2DH5Reader
// ---------------------------------------------------------------------------

class HDF5Mesh2DSource : public IMesh2DSource
{
public:
    /**
     * @brief Create an owned closed Mesh2DH5Reader with empty time cache; open() is required before
     *   data reads.
     */
    HDF5Mesh2DSource();
    /**
     * @brief Destroy the owned reader and release its HDF5 handles and scratch/time caches.
     */
    ~HDF5Mesh2DSource() override;

    /**
     * @param[in] warnings Optional destination for discovery diagnostics. Replaced with source
     *   diagnostics.
     * @return Reader-discovered scalar descriptors, including units/temporal support; no ownership
     *   of reader buffers escapes.
     */
    QVector<openswmmvis::io::Mesh2DResultVariable>
    faceVariables(QStringList *warnings = nullptr) const override;
    /**
     * @param[in] v Source-local semantic descriptor identity; key/zone must match catalog.
     * @param[in] t Zero-based retained frame index; static variables ignore requested index.
     * @param[out] values Replaced cell values in descriptor units; NaN can denote unavailable data.
     * @param[out] status Parallel validity/missing-status array, distinguishing missing chemistry
     *   from dry hydraulic zero.
     * @return Reader success; invalid descriptor/index/read returns false. Adapter adds no
     *   relabeling, temporal borrowing or unit conversion.
     */
    bool readFaceVariableAt(const openswmmvis::io::Mesh2DResultVariable &v, int t,
                            std::vector<float> &values,
                            std::vector<openswmmvis::io::Mesh2DValueStatus> &status) override;

    /**
     * @param[in] path File path passed to the owned reader; stored even on failure.
     * @return Reader open success. Every attempt increments index generation and clears timestamp
     *   cache; failure is not transactional restoration of the previous file. Serialize on
     *   source-owning thread.
     */
    bool open(const QString &path);
    /**
     * @return Borrowed last attempted open path, valid until next open/destruction; failed opens
     *   still change it.
     */
    const QString &path() const noexcept { return path_; }
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[in] cell Zero-based original cell index.
     * @param[out] out Mean cell depth in metres; initialized to NaN even when the read fails.
     * @return True when the requested cell value is available; false leaves out NaN. No smoothing
     *   or visibility masking is applied.
     */
    bool readDepthAt(int timeIdx, int cell, float &out) override;

    /**
     * @return Copy of most recently attempted open path, including a failed attempt; does not prove
     *   a file remains open.
     */
    QString sourcePath() const override { return path_; }

    // IMesh2DSource
    /**
     * @return Source vertex slots, including any subsequently rejected invalid geometry.
     */
    int vertexCount() const override;
    /**
     * @return Original face/cell slots (triangles plus quads); display fan size can be larger.
     */
    int triangleCount() const override;
    /**
     * @return Current retained report/tick count; indices may change after history thinning.
     */
    int timeCount() const override;
    /**
     * @return Source-local index-identity generation. Default zero; engine increments after
     *   thinning, HDF5 after every open attempt.
     */
    int historyGeneration() const override { return open_generation_; }
    /**
     * @param[out] vx Replaced vertex X coordinates in source SI metres.
     * @param[out] vy Replaced vertex Y coordinates in source SI metres, matching X count.
     * @param[out] vz Replaced bed elevations in SI metres, matching X count.
     * @param[out] tris Replaced zero-based display fan triangles; quad split follows VFR elevation
     *   diagonal.
     * @return True after all reads succeed; failure can leave coordinate/connectivity outputs
     *   partially replaced. Caller must discard them on false.
     */
    bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy, std::vector<double> &vz,
                          std::vector<std::array<int, 3>> &tris) override;
    /**
     * @param[out] vx Replaced vertex X coordinates in source SI metres.
     * @param[out] vy Replaced vertex Y coordinates in source SI metres, matching X count.
     * @param[out] vz Replaced bed elevations in SI metres, matching X count.
     * @param[out] cells Replaced original cyclic cell connectivity; fourth slot minus one denotes a
     *   triangle.
     * @return True after all reads succeed; failure can leave coordinate/connectivity outputs
     *   partially replaced. Caller must discard them on false.
     */
    bool readCells(std::vector<double> &vx, std::vector<double> &vy, std::vector<double> &vz,
                   std::vector<std::array<int, 4>> &cells) override;
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] depths Per-original-cell mean water depths in metres; no display fan duplication.
     * @return True for a complete readable depth row; false for invalid index/read failure. Callers
     *   must check success and shape before use.
     */
    bool readDepthsAt(int timeIdx, std::vector<float> &depths) override;
    /**
     * @return Whether compact atomic cell-subset hydraulic snapshots are supported; default false.
     */
    bool supportsHydraulicCells() const override { return true; }
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[in] cells Sorted unique valid original cell IDs; order is preserved in the compact
     *   arrays.
     * @return Immutable independently owned cell/depth/stride-four-flux snapshot, or null on
     *   invalid IDs/index/incomplete depth/read failure. Flux may be empty; generation/revision
     *   describe the exact retained tick. Request on the source owning thread.
     */
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCellsAt(int timeIdx, const std::vector<int> &cells) override;

    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @return Absolute SWMM DateTime converted through the canonical converter, or invalid
     *   index/time read. Time values are cached until count changes/open invalidates them;
     *   simulation-start anchor is not added.
     */
    QDateTime simTimeAt(int timeIdx) const override;

    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] flux Replaced outward-positive edge flux in cubic metres/second, stride four per
     *   original cell, with triangle padding.
     * @return Reader success; optional dataset absence/read failure returns false. Discard unusable
     *   outputs and verify the padded cell count.
     */
    bool readEdgeFluxAt(int timeIdx, std::vector<float> &flux) override;
    /**
     * @param[out] length Padded cell-edge lengths in metres.
     * @param[out] nx Corresponding outward unit-normal X components, dimensionless.
     * @param[out] ny Corresponding outward unit-normal Y components, dimensionless.
     * @return Reader success for all edge geometry arrays; false when unavailable. Consumers must
     *   check array lengths before reconstruction.
     */
    bool readEdgeGeometry(std::vector<float> &length, std::vector<float> &nx,
                          std::vector<float> &ny) override;
    /**
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] vdepths Signed vertex render depths in metres; negative dry-side values retain
     *   shoreline information.
     * @return True after reading Mesh2_node_depth into private scratch and copying it; false leaves
     *   caller output unchanged. Legacy node-head fields are deliberately ignored.
     */
    bool readVertexDepthsAt(int timeIdx, std::vector<float> &vdepths) override;
    /**
     * @param[in] dataset Nullable exact engine dataset name, not a display label.
     * @return Whether reader advertises the requested named face field; false for
     *   null/unknown/absent dataset.
     */
    bool hasFaceField(const char *dataset) const override;
    /**
     * @param[in] dataset Exact engine dataset identity; use hasFaceField() to probe capability.
     * @param[in] timeIdx Zero-based retained report/tick index.
     * @param[out] values Cell-ordered values in dataset SI units: rainfall m/s,cumulative rain
     *   m³,head m,velocity m/s.
     * @return Reader success; false for missing/invalid dataset/index/read. No visibility smoothing
     *   or unit conversion is added by this adapter.
     */
    bool readFaceFieldAt(const char *dataset, int timeIdx, std::vector<float> &values) override;
    /**
     * @param[in] dataset Exact engine face dataset identity.
     * @param[in] time Zero-based report/tick index.
     * @param[in] cells Sorted unique valid original cell IDs.
     * @param[out] values Compact values in requested cell order and dataset units.
     * @return Reader success for compact subset, false for invalid request/read. Request on owning
     *   thread; no full-domain cache is shared with workers.
     */
    bool readFaceFieldCellsAt(const char *dataset, int time, const std::vector<int> &cells,
                              std::vector<float> &values) override;
    /**
     * @param[in] dataset Nullable exact envelope identity Mesh2_face_max_depth (m) or
     *   Mesh2_face_max_velocity (m/s).
     * @param[out] values Original-cell whole-run peak array; cleared before every read, including
     *   failures.
     * @return Reader success, or false when no readable supported envelope exists.
     */
    bool readFaceEnvelope(const char *dataset, std::vector<float> &values) override;
    /**
     * @return Value copy of file CRS/unit-factor metadata; undeclared on old files or failed
     *   metadata read. Coordinates themselves remain source SI metres.
     */
    openswmmvis::io::CoordinateReference coordinateReference() const override;

    /*! \brief Store a legacy simulation-start anchor; current file times are absolute
     *  SWMM DateTime and simTimeAt() does not add this value.
     * @param[in] t Legacy start-time value stored without validation, notification or time-cache
     *   invalidation.
     * @note Current simTimeAt() interprets file time as absolute SWMM DateTime and does not use
     *   this anchor.
     */
    void setSimulationStart(QDateTime t) { sim_start_ = t; }

private:
    int open_generation_ = 0;
    mutable std::vector<double> cached_times_;
    QString path_;
    std::unique_ptr<openswmmvis::io::Mesh2DH5Reader> reader_;
    QDateTime sim_start_;
    std::vector<float> depth_buf_; ///< per-frame scratch (signed vertex depths)
};

// ---------------------------------------------------------------------------
// SWMM2DResultsLayer
// ---------------------------------------------------------------------------

class SWMM2DResultsLayer : public OpenSWMMVisLayer,
                           public OpenSWMM::Render::ISublayerHost,
                           public OpenSWMM::Render::IAttributeProvider // Slice DM.3
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Render::IAttributeProvider) // Slice DM.3
public:
    // Slice DM.3 — exposes depth / head / velocity-magnitude / velocity-
    // x / velocity-y so renderer panels can theme the heatmap and
    // contour rules by the right variable. All entries are dynamic.
    // 2D results have no SWMM category concept; we ignore the cat
    // argument and always return the mesh-scope field set.
    /**
     * @param[in] cat Ignored SWMM category; 2D fields are mesh-wide.
     * @return Owned five dynamic double descriptors:depth/head metres,vmag/vx/vy m/s. This legacy
     *   catalog is separate from resultVariables() semantic chemistry/GW discovery.
     */
    [[nodiscard]] QVector<OpenSWMM::Render::AttributeField>
    availableAttributes(OpenSWMMVis::SwmmCategory cat) const override;
    /**
     * @param[in] name Layer display name copied into base state.
     * @param[in] parent Optional workspace/QObject owner.
     * @note GUI-thread construction creates owned mesh/depth/contour/vector/particle sublayers,
     *   default Graduated placeholder renderer and a single-worker fitting pool. Contour bands
     *   default visible, smooth fill hidden; style invalidations request repaint. Source starts
     *   null, time index minus one. All layer state/cache/scene methods require serialized
     *   owning-thread access unless explicitly fitting a captured worker job.
     */
    explicit SWMM2DResultsLayer(const QString &name = QStringLiteral("2D Results"),
                                OpenSWMMVisWorkspace *parent = nullptr);
    /**
     * @brief Wait for outstanding surface fitting, invalidate queued publications and destroy the
     *   coordinate transform/source/owned children.
     * @note Scene items borrow the layer; remove them through depopulateScene() before destroying
     *   their layer owner.
     */
    ~SWMM2DResultsLayer() override;

    /*!
     * \brief Swap the data source. Triggers a one-time mesh-geometry rebuild;
     * the first live frame or newest completed frame is selected when available.
     *
     * Lifecycle: a run typically calls `setSource(EngineMesh2DSource)` once
     * at `twoDInitialized`, then `setSource(HDF5Mesh2DSource)` again at
     * `finished` to swap to the on-disk file for scrubbing.
      *
     * @param[in] source Exclusive source ownership moved into layer, releasing previous source;
     *   null detaches.
     * @note Increments source revision, cancels/waits for old fitting during geometry rebuild,
     *   clears current inputs/fit caches and emits timeRangeChanged(0,max(0,n-1)). Shows first live
     *   frame or newest completed frame, rather than preserving old cursor. Seeds colour ranges
     *   from available envelopes/bounded scans and per-cell depth maxima synchronously; can perform
     *   substantial IO. Explicit user range overrides persist across swaps.
     */
    void setSource(std::unique_ptr<IMesh2DSource> source);

    /**
     * @param[in] warnings Optional destination replaced with discovery warnings; cleared without a
     *   source.
     * @return Owned semantic descriptor catalog cached by source revision,result generation and
     *   time count. Serialized access; discovery may perform IO.
     */
    QVector<openswmmvis::io::Mesh2DResultVariable>
    resultVariables(QStringList *warnings = nullptr) const;
    /**
     * @param[in] key Exact source-local semantic descriptor key, independent of species row order.
     * @param[in] time Zero-based frame index; static descriptors may ignore it.
     * @param[in] wholeRun Whether to use complete-run extrema/distribution while retaining
     *   values/status for requested time.
     * @return Nonnull immutable shared scalar frame; inspect error for absent source/key/invalid
     *   frame/incomplete history/read failure. Live EngineMesh2DSource rejects temporal whole-run
     *   scans even after finished.
     * @note Whole-run scan reads every descriptor report synchronously, keeps exact finite Valid
     *   extrema and deterministic reservoir up to 32768 samples. Cache bounds8frames/64MiB
     *   values+samples,16range entries; generation/count change clears them. A partial range scan
     *   propagates its error, and missing statuses remain distinct from dry zeros.
     */
    std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame>
    resultFrame(const QString &key, int time, bool wholeRun = false) const;
    /**
     * @param[in] key Requested semantic attribute key; unavailable keys are retained as unresolved
     *   rather than rejected.
     * @param[in] id Optional stable ID beginning results2d.variable.; empty generates a UUID-based
     *   ID.
     * @return Borrowed new QObject child or null for invalid-prefix/duplicate ID. Appends to
     *   bottom-up order, wires catalog/style/range refresh and invalidation, emits
     *   resultSublayersChanged(),childrenChanged(),repaintRequested(). Layer owns deletion.
     */
    OpenSWMM::Render::ResultScalarSublayer *addResultSublayer(const QString &key,
                                                              const QString &id = {});
    /**
     * @param[in] id Exact stable ID of a dynamic ResultScalarSublayer.
     * @return False for missing/non-scalar sublayer. True removes/deletes it immediately and emits
     *   resultSublayersChanged(),childrenChanged(),repaintRequested(); borrowed pointers become
     *   invalid. Builtin sublayers are retained.
     */
    bool removeResultSublayer(const QString &id);
    /**
     * @param[in] j Saved host object with optional sublayers array.
     * @note Missing/non-array sublayers leaves state untouched. Creates missing dynamic result rows
     *   by prefix/attribute, ignores duplicate IDs and deletes dynamic rows absent from wanted set;
     *   ordinary add/remove notifications can fire repeatedly. This prepares identities before base
     *   JSON style/order restoration; malformed row style may yield an unresolved key.
     */
    void prepareSublayersJsonLoad(const QJsonObject &j) override;

    /**
     * @param[in] frame Shared immutable scalar frame, nullable; pointer identity is part of cache
     *   key.
     * @param[in] levels Requested contour scalar levels in descriptor units, forwarded to geometry
     *   builder.
     * @param[in] labels Whether to generate cell value-label anchors.
     * @return Nonnull immutable shared geometry; null frame returns an error object. Cache key
     *   includes source frame identity,levels,labels and geometry revision; evicts entire cache
     *   at4entries/64MiB, excludes outputs larger than 64MiB. Builds synchronously from current
     *   scene geometry; caller must keep frame geometry-compatible.
     */
    std::shared_ptr<const OpenSWMM::Render::ResultScalarGeometry>
    resultScalarGeometry(const std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> &frame,
                         const std::vector<double> &levels, bool labels) const;
    // Display-only regional fitting; analysis continues to use full-domain fields.
    // Large/unsupported views retain the full-domain path. The padded coverage
    // includes a 16-edge reconstruction halo and is reused across nearby pans.
    /**
     * @param[in] view Valid scene-map viewport after reprojection and Y inversion.
     * @param[in] scale Positive finite screen pixels per scene unit.
     * @note GUI-thread optimization only for subset-capable sources and >250000 cells unless
     *   OPENSWMM_REGIONAL_SURFACE is set. Reuses padded coverage, gathers at most
     *   50000candidate/halo cells with 16manifold-edge rings and8-ring comparison region.
     *   Unsupported/oversized/full views retain or switch to full-domain fitting. A changed region
     *   refits requested frame; no solver/analysis data are truncated. Regional comparisons reject
     *   >0.002m or material shoreline-sign discrepancies and fall back.
     */
    void setHydraulicView(const QRectF &view, double scale);
    /**
     * @return Requested regional cell count including reconstruction halo, zero for full-domain
     *   mode; can differ from currently published region.
     */
    size_t hydraulicRegionCellCount() const;
    /**
     * @return Cumulative rejected regional-fit count, not reset by source/viewport changes.
     */
    int regionalSurfaceFallbackCount() const { return regional_fallbacks_; }
    /**
     * @return Largest absolute corner-depth discrepancy in metres from last regional comparison,
     *   including rejected attempts; zero initially.
     */
    double regionalSurfaceError() const { return regional_error_; }
    /**
     * @return Whether currently displayed hydraulic buffers hold a regional publication,
     *   independently of a newer requested region.
     */
    bool regionalHydraulicsPublished() const { return bool(published_region_); }

    /**
     * @return Revision incremented by setSource()/closeSource(), not source internal updates.
     */
    quint64 sourceRevision() const noexcept { return source_revision_; }
    /** @brief Inspect source/metadata adoption before its semantic completion signal.
     * @return True while setSource()/closeSource() updates source, frame and geometry metadata. */
    bool sourceLoading() const noexcept { return source_adopting_; }
    /**
     * @return Borrowed currently owned source or null; invalidated by
     *   setSource()/closeSource()/destruction. Serialize access and never delete it.
     */
    IMesh2DSource *source() noexcept { return source_.get(); }
    /**
     * @return Borrowed const source or null; invalidated by replacement/close/destruction. Const
     *   access does not make shared source caches thread safe.
     */
    const IMesh2DSource *source() const noexcept { return source_.get(); }

    /*!
     * \brief Metres per model-CRS linear unit, for sources that declare none.
     *
     * The 2D solver runs in SI, so every result source hands this layer
     * **metres** — which for a foot-based model CRS is not the unit the
     * coordinates must be reprojected from (issue #155). Engine 6.0+ states
     * the exact factor in the file's `/crs` variable and that always wins.
     * A pre-6.0 `.2d.h5`, and the live in-process `EngineMesh2DSource`, carry
     * no such statement, so the caller supplies it here.
     *
     * Use the engine's own rule, not the layer CRS's linear unit: the engine
     * scales the mesh when `FLOW_UNITS` is US-customary **and** the mesh file
     * did not declare `;; UNITS: SI (m)` — a condition the CRS cannot report,
     * and which the engine's own `InpWriter` makes reachable on round-trip.
     *
     * \param metresPerModelUnit  0.3048 when the engine scaled, else 1.0.
     *                            Non-positive / non-finite values are ignored.
     *                            The default is 1.0, i.e. leave the
     *                            coordinates alone.
      *
     * @note Valid changed factor resets warning, waits/cancels fitting through geometry rebuild and
     *   re-applies current hydraulic arrays/repaints. Declared source unit factor takes precedence.
     *   Does not change physical SI depth/flux values or infer a new CRS.
     */
    void setFallbackCoordinateScale(double metresPerModelUnit);

    /*!
     * \brief Current time index displayed on the canvas. -1 = no frame yet.
      *
     * @return Actually published retained frame index, minus one initially/after close;
     *   asynchronous requested index may be newer.
     */
    int currentTimeIndex() const noexcept { return current_time_idx_; }
    /**
     * @param[in] t Requested retained frame index, clamped to 0..timeCount()-1; no-op without
     *   source/frames.
     * @note Does not alter followLive(). Repeated in-flight identical request is skipped; selecting
     *   displayed frame withdraws newer pending work. First fit is synchronous; later async/region
     *   requests keep previous displayed frame until coherent publication emits time/date/repaint.
     *   Re-selecting displayed frame does not re-read updated content; use refreshCurrentFrame().
     */
    void setCurrentTimeIndex(int t);

    /*!
     * \brief Re-read and re-apply the current frame without moving the cursor.
     *
     * Live 2D packets for a single simulation tick can arrive in phases
     * (depths, then flux, then reconstructed vertex heads). Those late packets
     * change velocity glyphs, smooth fills, contours, and profile samples even
     * though the time index has not advanced.
      *
     * @note Disabled live rendering makes this a no-op. Live requests coalesce per event-loop turn;
     *   completed sources clear fitted cache/published identity and re-read current/clamped first
     *   frame immediately or schedule fit. Does not itself change followLive().
     */
    void refreshCurrentFrame();

    /*!
     * \brief Re-query `source()->timeCount()` and emit `timeRangeChanged` if
     * it grew. Used by the runner's per-tick refresh during live mode.
      *
     * @note Disabled live rendering holds range/cursor while source still accumulates. Live
     *   requests coalesce and emit only changed high index, following newest only if enabled.
     *   Non-live always emits(0,max(0,n-1)) and advances to latest if behind; empty range
     *   uses(0,0).
     */
    void refreshTimeRange();

    /*!
     * \brief Follow-latest behavior for a LIVE (streaming) source. When on
     * (the default), refreshTimeRange() advances the cursor to the newest frame
     * as ticks arrive so the map animates the running simulation. Cleared when
     * the user takes manual playback control (scrub / Play) so a chosen frame
     * stays put; re-armed when they seek back to the latest frame. No effect on
     * file sources, which already follow-latest while being appended.
      *
     * @param[in] on Copied follow-newest flag; no immediate seek, signal or repaint.
     */
    void setFollowLive(bool on) noexcept { follow_live_ = on; }
    /**
     * @return Stored follow-newest flag, initially true; only live refresh selection uses it.
     */
    [[nodiscard]] bool followLive() const noexcept { return follow_live_; }

    /*!
     * \brief Master on/off gate for LIVE (streaming) rendering during a run.
     * Distinct from \ref followLive (which only chooses WHICH frame is shown)
     * and from base-layer visibility (a non-live static result keeps drawing).
     * When off, refreshTimeRange() stops ingesting/advancing streamed frames
     * and the QSG renderer stops drawing the live layer, so a running sim costs
     * no extra GPU/CPU for the 2D view. Re-enabling resumes work and re-arms follow-live so the
     *   view catches up.
     * No effect on file/static sources.
      *
     * @param[in] on Master live-refresh/display gate; equality is a no-op.
     * @note Enabling re-arms followLive and refreshes range; disabling does not stop
     *   producer/history storage or immediately cancel a dispatched fit. Both transitions update
     *   CPU items and request repaint. File source refresh is unaffected by gate.
     */
    void setLiveRenderEnabled(bool on);
    /**
     * @return Stored live-stream display/refresh gate, initially true; source packets may continue
     *   accumulating when false.
     */
    [[nodiscard]] bool liveRenderEnabled() const noexcept { return live_render_enabled_; }

    /*!
     * \brief Release the active source — closes its underlying HDF5 handle
     * (when the source is an HDF5Mesh2DSource) so the engine can truncate /
     * overwrite the file on a subsequent run. Clears all per-frame caches
     * and emits geometryChanged on both items so the canvas paints empty
     * until a new source is attached via setSource().
     *
     * Used by the dual-stream re-run handshake (Slice CF.MVP-fix.2) before
     * launching a new simulation that targets the same OUTPUT_FILE.
      *
     * @note Waits for fit completion/cancels queued publication, releases reader handle and input
     *   caches, blanks animated triangle/edge/velocity content and emits
     *   timeRangeChanged(0,0),currentTimeChanged(-1),invalid
     *   currentDateTimeChanged(),repaintRequested(). Original vertex/cell geometry and some
     *   style/envelope state remain for next source rebuild. Advances sourceRevision and emits
     *   sourceChanged after detached metadata/cursor state is coherent.
     */
    void closeSource();

    /*! \brief Model dry depth in metres; also the default thin-film threshold.
     * @return Configured physical dry cutoff in metres, initially 1e-4.
     */
    double dryDepth() const noexcept { return dry_depth_; }
    /**
     * @param[in] d Finite nonnegative physical cutoff in metres; invalid input ignored.
     * @note Always synchronizes vector-style dry cutoff even when unchanged; changed model cutoff
     *   refits visibility/velocity and invalidates historical surface envelopes. Signals can be
     *   deferred until requested async fit is published.
     */
    void setDryDepth(double d);
    /**
     * @return Effective visual thin-film threshold in metres; negative stored override inherits
     *   dryDepth().
     */
    double thinFilmDepth() const noexcept
    {
        return thin_film_depth_ < 0 ? dry_depth_ : thin_film_depth_;
    }
    /**
     * @return Whether stored visual threshold override is negative and therefore inherits current
     *   model dry depth.
     */
    bool usesModelThinFilmDepth() const noexcept { return thin_film_depth_ < 0; }
    /**
     * @param[in] metres Finite visual threshold in metres; any negative value canonicalizes to
     *   minus one/inherit dryDepth(). Nonfinite ignored.
     * @note Changed effective stored policy refits displayed/requested field and invalidates
     *   fitted-envelope identity; may defer waterDisplayPolicyChanged until async publication.
     */
    void setThinFilmDepth(double metres); // negative resets to the model threshold
    /**
     * @return Whether otherwise suppressed thin films are displayable, initially false.
     */
    bool showThinFilms() const noexcept { return show_thin_films_; }
    /**
     * @param[in] show Whether thin-film cells participate in visualization.
     * @note Equality no-op; change refreshes hydraulic display policy, not engine
     *   thresholds/volumes, and notifies with coherent refitted fields.
     */
    void setShowThinFilms(bool show);
    /**
     * @return Version1 value object containing stored thin-film override (minus one for inherited)
     *   and show flag; does not persist dryDepth().
     */
    QJsonObject waterDisplayPolicyToJson() const;
    /**
     * @param[in] json Version1 object with finite numeric thinFilmDepthMetres and boolean
     *   showThinFilms.
     * @note Invalid/unsupported schema is ignored atomically. Negative threshold becomes inherited;
     *   changed valid values are applied together with one policy refresh. Model physical dry
     *   cutoff is not restored from this object.
     */
    void restoreWaterDisplayPolicy(const QJsonObject &json);
    /**
     * @return Value snapshot of effective thin-film threshold and visibility flag; no side effects.
     */
    CellWaterGeometry::VisibilityPolicy waterVisibilityPolicy() const
    {
        return {thinFilmDepth(), showThinFilms()};
    }

    // ----- Self-description for the Layer Properties dialog ----------------
    /**
     * @return Localized no-source/live/in-memory description or HDF5 path; no file existence check.
     */
    [[nodiscard]] QString sourceDescription() const override;
    /**
     * @return Owned localized source live/count/time/dry-depth/flux descriptions; time and
     *   capability probes may read source data and mutate caches.
     */
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    // ----- VS.8 — QSG (GPU) rendering ownership ------------------------------
    //
    // When MapCanvas hosts this layer in SWMM2DResultsQSGRenderer, it sets
    // this flag and the QGraphicsItem QPainter passes return early (same
    // per-kind bypass pattern as SWMMLayerItem §QSG-1). The CPU path stays
    // intact as the fallback (mask clip active, QSG init failure).
    /**
     * @return Whether CPU graphics-item paint passes defer to Qt Quick; initially false.
     */
    [[nodiscard]] bool qsgOwnsRendering() const noexcept { return m_qsgOwnsRendering; }
    /**
     * @param[in] own Whether Qt Quick owns on-screen pixels and CPU items skip painting.
     * @note Equality no-op; changed flag updates both CPU items and emits repaintRequested(),
     *   without geometry/source mutation.
     */
    void setQsgOwnsRendering(bool own);

    /*! \brief Upper end of the colour ramp (metres). Auto-tracks the global max
     *         depth seen so far across all loaded ticks unless explicitly set.
     * @return Current colour-ramp upper depth bound in metres, initially 0.01 and normally grown by
     *   loaded reconstructed corners.
     */
    double maxDepth() const noexcept { return max_depth_; }
    /**
     * @param[in] d New ramp upper bound in metres, copied without finite/positive validation.
     * @note Equal value does not pin auto-range. Changed value permanently enables user override,
     *   updates fill item and emits repaintRequested(); no reset-to-auto method here.
     */
    void setMaxDepth(double d);

    // ----- Velocity vector overlay (CF.2) -----------------------------------
    //
    // Gap A3.1 — these knobs (and the band / isoline ones below) are now
    // facades over the sublayer model (visibility + style bags), which is
    // the single source of truth the paint passes consult. The legacy
    // fields survive only as fallbacks for the (never-hit) no-sublayer
    // case. Both the dialog (legacy setters) and the layer tree (sublayer
    // toggles) therefore drive — and report — the same state.

    /*! \brief Whether centroid arrow glyphs are drawn over the depth fill.
     * @return Authoritative velocity sublayer visibility, with stored flag fallback.
     */
    bool velocityVectorsVisible() const;
    /**
     * @param[in] v Velocity sublayer visibility.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setVelocityVectorsVisible(bool v);

    /*! \brief Overall alpha applied to all arrow glyphs, 0..1. Default 0.9.
     * @return Authoritative velocity sublayer opacity in0..1, with stored fallback initially 0.9.
     */
    qreal velocityOpacity() const;
    /**
     * @param[in] alpha Arrow sublayer opacity, clamped0..1.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setVelocityOpacity(qreal alpha);

    /*! \brief Vector-style glyph length scale in pixels per m/s;
     * the style controls its nonlinear scaling and pixel limits. The legacy
     * no-sublayer fallback instead uses a normalized logarithmic length.
     * @return Authoritative vector-style glyph-length scale in pixels per m/s; stored fallback uses
     *   normalized logarithmic length.
     */
    double velocityArrowScale() const;
    /**
     * @param[in] scale Positive glyph length scale in pixels per m/s; nonpositive ignored. Style
     *   owns its own normalization.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setVelocityArrowScale(double scale);

    /*! \brief Upper end of the velocity colour ramp (m/s). Auto-tracks the
     *  running max unless explicitly set.
     * @return Velocity colour/range upper bound in m/s, initially 1.0 until envelope/scan seeding
     *   or growth.
     */
    double maxVelocity() const noexcept { return max_velocity_; }
    /**
     * @param[in] v Positive velocity bound m/s; nonpositive rejected, nonfinite values are not
     *   explicitly rejected.
     * @note Equal value does not pin auto-range. Changed value enables explicit override, updates
     *   arrow item and requests repaint. Does not synchronize independent sublayer custom ranges.
     */
    void setMaxVelocity(double v);

    /*! \brief Whether the active source produced both edge geometry and a
     *  flux slice — i.e. whether the velocity overlay has data to render.
     *  NOTE: this reflects the CURRENTLY shown frame's reconstructed velocity
     *  magnitude; it is false on dry frames. Use \ref hasEdgeFluxData for a
     *  frame-independent "does this run carry edge flux" capability check.
     * @return Whether published frame reconstructed any positive speed; false on
     *   dry/zero-speed/missing-flux frames, independently of run capability.
     */
    bool hasVelocityData() const noexcept { return have_velocity_; }

    /*! \brief Whether the run carries per-edge flux data at all, independent of
     *  the current frame. Probes the source once (cached) and is the correct
     *  gate for whole-series edge flow / flux / velocity plotting.
     * @return Cached run capability probe of frame 0 and padded cell count. No-source/no-frames
     *   leaves result uncached; a negative first probe is retained until source replacement, though
     *   later successful frame reads can mark it positive.
     */
    bool hasEdgeFluxData() const;

    // ----- Color-ramp + contour styling (Slice CF.MVP-fix.3) ----------------

    /*! \brief How depth is mapped to colour in the per-cell heatmap pass.
     *
     *  \c Smooth   — continuous Viridis-ish gradient via the CPU inundation-colour helper.
     *               This is the default and matches behaviour prior to fix.3.a.
     *  \c Graduated — discretise the depth range into \ref colorClasses bins and
     *               sample the same gradient at each bin's midpoint. Bin colours
     *               match the ones generated for the renderer's legend.
     */
    enum class ColorRampStyle { Smooth, Graduated };
    /**
     * @return Stored legacy smooth/graduated mode; current depth fills use their sublayer style
     *   settings.
     */
    [[nodiscard]] ColorRampStyle colorRampStyle() const noexcept { return color_ramp_style_; }
    /**
     * @param[in] s Legacy heatmap mode stored without enum validation; active sublayer schemes are
     *   independent.
     * @note Equality no-op; change updates fill graphics item and requests repaint without
     *   refitting hydraulic data.
     */
    void setColorRampStyle(ColorRampStyle s);

    /**
     * @return Stored legacy class count, initially 5; current depth fills carry independent
     *   classification schemes.
     */
    [[nodiscard]] int colorClasses() const noexcept { return color_classes_; }
    /**
     * @param[in] n Legacy class count clamped2..64; independent of active sublayer classification.
     * @note Equality no-op; change updates fill graphics item and requests repaint without
     *   refitting hydraulic data.
     */
    void setColorClasses(int n);

    /*! \brief Show filled-band contour polygons over the terrain (on by
     *  default). Gap A3.1 — facades over the ContourBand sublayer
     *  (visibility / sublayer opacity / bandCount style prop).
     * @return Authoritative contour-band sublayer visibility; constructor enables it despite legacy
     *   fallback default false.
     */
    [[nodiscard]] bool filledContours() const;
    /**
     * @param[in] on Contour-band visibility.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setFilledContours(bool on);
    /**
     * @return Authoritative contour-band opacity in0..1.
     */
    [[nodiscard]] double filledContoursOpacity() const;
    /**
     * @param[in] a Contour-band sublayer opacity, clamped0..1.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setFilledContoursOpacity(double a);
    /**
     * @return Authoritative band-style count; stored fallback initially 8.
     */
    [[nodiscard]] int filledContoursLevels() const;
    /**
     * @param[in] n Number of bands, clamped2..32 before forwarding to band style.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setFilledContoursLevels(int n);

    /*! \brief Stroke iso-depth contour lines over the heatmap (off by
     *  default). Gap A3.1 — facades over the Isoline sublayer + style bag.
     * @return Authoritative depth-isoline sublayer visibility; initially off.
     */
    [[nodiscard]] bool isolines() const;
    /**
     * @param[in] on Depth-isoline sublayer visibility.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setIsolines(bool on);
    /**
     * @return Authoritative isoline style count, or stored fallback5; fixed-interval style may
     *   choose different actual levels.
     */
    [[nodiscard]] int isolinesLevels() const;
    /**
     * @param[in] n Requested contour count, clamped1..32 before forwarding to style.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setIsolinesLevels(int n);
    /**
     * @return Authoritative isoline style colour including alpha.
     */
    [[nodiscard]] QColor isolinesColor() const;
    /**
     * @param[in] c Contour line colour copied with alpha; no validity check here.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setIsolinesColor(QColor c);
    /**
     * @return Authoritative cosmetic isoline line width in screen pixels.
     */
    [[nodiscard]] double isolinesWidth() const;
    /**
     * @param[in] px Cosmetic line width in pixels, clamped0.25..10.
     * @note Stores legacy fallback and forwards to owned sublayer/style, whose change signals
     *   invalidate/repaint. Does not change source values or physical units; ordinary style
     *   equality handling controls notification.
     */
    void setIsolinesWidth(double px);

    // ----- Renderer (Slice BI Phase 8.13.6.6) -----------------------------
    // API plumbing only — the existing paint path still uses dry_depth_ /
    // max_depth_ directly.  Sub-phase 8.13.6.4 (deferred until Slice BB
    // ColorRamp lands) will swap the paint loop to consult m_renderer.

    /*!
     * \brief The IFeatureRenderer that will drive this layer's paint pass.
     * \details Constructed eagerly as a default GraduatedRenderer because
     *          this layer is fundamentally a continuous-attribute (depth)
     *          colour-mapped layer.  Owned by the layer; never null.
      *
     * @return Borrowed nonnull owned placeholder renderer, invalidated by replacement/destruction;
     *   hydraulic paint presently reads sublayer styles.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  Null pointers are silently
     *          rejected.  Emits \ref rendererChanged() when the pointer
     *          actually changes.
      *
     * @param[in] r Exclusive nonnull renderer ownership moved into layer; null ignored.
     * @note Change emits rendererChanged only, without repaint or sublayer/rule synchronization.
     *   Never wrap an already owned raw renderer in another unique_ptr.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    /*!
     * \brief Statistic on the current frame — useful for the status-bar peak readout.
     * \returns {peakDepth, peakCellIdx} of the currently displayed frame, or
     * {0, -1} if no frame is loaded.
      *
     * @return Pair of mean depth in metres and original CELL index, or(0,-1) without values.
     *   Regional mode reads full frame and ignores nonfinite/nonpositive candidates; full mode uses
     *   max_element without extra NaN filtering, preserving first maximum tie. Does not use
     *   smoothed corner maxima.
     */
    std::pair<float, int> currentPeak() const;

    /*!
     * \brief Set the current frame by nearest simulation time. Used to keep the
     * 2D layer in step with the existing AnimationController's 1D playback —
     * one slider drives both layers.
      *
     * @param[in] t Valid absolute simulation timestamp; invalid/no-source/no-frame is ignored.
     * @note Nearest timestamp with earlier index winning equal-distance ties. Binary search assumes
     *   monotonically ordered valid source times. Live follow is re-armed only when selection
     *   reaches newest frame; otherwise user takes control. Delegates to clamped/coherent frame
     *   selection, possibly asynchronous.
     */
    void setCurrentSimTime(QDateTime t);

    /*!
     * \brief Set the current frame **causally**: the latest frame whose
     * simulation time is at or before \p cursor (floor), so a coupled output
     * with a coarser report step never jumps to a frame ahead of the animation
     * cursor. Clamps to frame 0 when \p cursor precedes the first frame (hold
     * first). Peer of SWMMResultsLayer::periodIndexForDateTimeAsOf; used by the
     * animation sync path so misaligned outputs hold their last-known frame.
      *
     * @param[in] cursor Valid absolute simulation timestamp; invalid/no-source/no-frame is ignored.
     * @note Largest timestamp at/before cursor, clamped to first frame before run start. Binary
     *   search assumes monotonically ordered valid source times. Live follow is re-armed only when
     *   selection reaches newest frame; otherwise user takes control. Delegates to clamped/coherent
     *   frame selection, possibly asynchronous.
     */
    void setCurrentSimTimeAsOf(QDateTime cursor);

    // ----- Cell selection / picking (CF.3) ----------------------------------

    /*!
     * \brief Return CELL indices whose scene-space (area) centroid falls inside
     *        \p sceneRect.  Linear scan over the cells — fine for meshes
     *        up to ~100k cells; bigger meshes may want spatial indexing.
      *
     * @param[in] sceneRect Scene-map rectangle; null returns empty.
     * @return Owned original cell indices in ascending order whose area-weighted display-fan
     *   centroid is inside; centroid-only selection can omit intersected cells and include invalid
     *   cells whose default centroid is origin. No visibility/depth filter.
     */
    [[nodiscard]] QVector<int> pickCellsInRect(const QRectF &sceneRect) const;

    /*!
     * \brief Return CELL indices whose scene-space centroid falls inside
     *        \p scenePoly (odd-even fill rule). Used by lasso-select.
      *
     * @param[in] scenePoly Scene-map polygon; fewer than 3points returns empty, OddEvenFill test.
     * @return Owned original cell indices in ascending order whose area-weighted display-fan
     *   centroid is inside; centroid-only selection can omit intersected cells and include invalid
     *   cells whose default centroid is origin. No visibility/depth filter.
     */
    [[nodiscard]] QVector<int> pickCellsInPolygon(const QPolygonF &scenePoly) const;

    /*! \brief Return the CELL containing \p scenePt, or -1 (barycentric test
     *  on the display fan, mapped to the owning cell — a quad answers for
     *  both of its sub-triangles). Used by single-click cell pick and the
     *  canvas-right-click hit test.
     * @param[in] scenePt Finite scene-map point after coordinate conversion/reprojection/Y
     *   inversion.
     * @return Original cell of first containing nondegenerate display triangle, or minus one off
     *   mesh. Shared boundary ties follow spatial-candidate/fan order. Ignores water
     *   visibility/depth.
     */
    [[nodiscard]] int pickCellAt(const QPointF &scenePt) const;

    // Stored cell-mean depth (SI metres), for consumers that need V/A.
    /**
     * @param[in] scenePt Scene-map query point.
     * @return Stored mean cell depth metres, zero off mesh. Regional publication reads original
     *   full-source cell directly and returns NaN if that read fails; no
     *   thin-film/smoothing/clamping.
     */
    [[nodiscard]] float depthAtSceneNow(const QPointF &scenePt) const;
    // Convert SI result elevations/depths to the mesh's project units.
    /**
     * @return Multiplicative reciprocal of declared positive metres/model-unit factor, otherwise
     *   configured fallback reciprocal; SI depth times this value is native model linear units. May
     *   log one fallback warning, independent of canvas projection scale.
      *
     */
    [[nodiscard]] double depthToMeshUnits() const { return resolveCoordinateScale_(); }

    // Smooth VFR display depths, continuous across connected wet edges.
    /**
     * @param[in] scenePt Scene-map query point.
     * @return Nonnegative piecewise-linear fitted signed depth in metres. Outside/invalid
     *   interpolant collapses to zero via max(0,NaN); full-domain analysis may be synchronously
     *   reconstructed when regional display is active.
     */
    [[nodiscard]] float depthAtSceneInterp(const QPointF &scenePt) const;
    /**
     * @param[in] cell Known original cell ID, bounds-checked; stale/invalid gives zero.
     * @param[in] scenePt Scene-map sample assumed within that cell; quad scans its fan and falls
     *   back to first triangle if none contains it.
     * @return Nonnegative barycentric fitted depth metres; no bilinear interpolation, and
     *   out-of-cell point can extrapolate on first triangle. Regional display may trigger
     *   full-domain analysis.
     */
    [[nodiscard]] float depthAtCellInterp(int cell, const QPointF &scenePt) const;
    /**
     * @param[in] cell Original cell ID.
     * @return Whether current policy displays the cell; invalid/missing state is false.
     */
    [[nodiscard]] bool cellHasSurface(int cell) const;
    /**
     * @param[in] cell Original cell ID.
     * @return Raw/fitted visibility classification under current policy; invalid cell returns
     *   Invalid. Regional display obtains cached full-domain analysis synchronously before querying
     *   off-view cells.
     */
    [[nodiscard]] CellWaterGeometry::DisplayState cellWaterDisplayState(int cell) const;
    /**
     * @param[in] tri Display fan-triangle index, not original cell ID.
     * @param[in] p Scene-map interpolation point; containment is not enforced.
     * @return Barycentric source bed elevation metres, NaN for invalid index/vertex/degenerate
     *   basis. No vertical datum conversion; may extrapolate outside the triangle.
     */
    [[nodiscard]] double groundAtDisplayTriangle(int tri, const QPointF &p) const;
    /**
     * @param[in] tri Display fan index.
     * @param[in] p Scene-map interpolation point; may extrapolate if outside.
     * @return Barycentric signed fitted depth metres, including negative dry-side shoreline values;
     *   NaN for invalid geometry/cell/basis. Uses full-domain analysis when display is regional,
     *   which can perform synchronous IO/fitting.
     */
    [[nodiscard]] double signedDepthAtDisplayTriangle(int tri, const QPointF &p) const;
    /**
     * @param[in] tri Display fan index.
     * @param[in] p Scene-map interpolation point.
     * @param[in] field Full-domain per-cell four-corner signed depths in metres, matching current
     *   cell connectivity.
     * @return Barycentric signed metre depth, NaN for invalid index/missing cell/degenerate
     *   basis/unmapped vertex. Does not clamp or test point containment; nonfinite corners
     *   propagate.
     */
    [[nodiscard]] double
    signedDepthAtDisplayTriangle(int tri, const QPointF &p,
                                 const std::vector<CellWaterGeometry::CornerDepths> &field) const;
    /**
     * @return Owned per-cell signed corner temporal maxima in metres, computed after reconstructing
     *   each real frame, rather than smoothing independent mean maxima. NaN means no fitted corner
     *   observed; empty without source/geometry/times.
     * @note Synchronous mode fits uncached history on calling thread and can be expensive. Async
     *   mode folds only already produced full-domain fields and may return incomplete envelope
     *   until requestEnvelope()/envelopeReady(). Newest mutable tick never contaminates frozen
     *   prefix.
     */
    [[nodiscard]] std::vector<CellWaterGeometry::CornerDepths> maxSurfaceDepths() const;
    /**
     * @param[in] cell Original cell index; invalid returns default Invalid surface.
     * @param[in] meanDepth Mean nonnegative cell depth metres; reconstruction handles invalid
     *   inputs.
     * @return Volume-preserving reconstructed cell surface relative to minimum bed, using current
     *   VFR split and SI bed elevations. Does not mutate displayed state.
     */
    [[nodiscard]] CellWaterGeometry::Surface surfaceForDepth(int cell, double meanDepth) const;
    /**
     * @return Owned cell-wise maximum retained mean depth metres, zero baseline, empty without
     *   usable source/geometry/times. Synchronously reads unprocessed frames, freezes completed
     *   rows but recomputes replaceable newest row; history-generation changes invalidate cache.
     *   Stops at first failed/mismatched historical read; does not claim complete engine report
     *   history.
     */
    [[nodiscard]] QVector<float> maxDepthPerCell() const;
    /**
     * @return Publication/reconstruction revision for coherent displayed hydraulic fields;
     *   increases even on unsuccessful applyCurrentDepths_() shape checks.
     */
    [[nodiscard]] quint64 frameRevision() const noexcept { return frame_revision_; }

    /*! Fitted-surface worker. Off by default: a frame load fits the water
     *  surface synchronously on the caller's thread. When on, the fit runs on
     *  one worker thread; the previous fitted frame stays displayed until the
     *  new one is installed, and currentTimeChanged() is emitted at that
     *  point. Completed frames are kept in a byte-bounded cache.
     * @param[in] on Whether future full-domain fits use worker scheduling.
     * @note Equality no-op; transition waits for current worker, cancels pending/publication/cache
     *   state and toggles mode without immediately reloading/repainting displayed buffers. First
     *   subsequent fit still bootstraps synchronously.
     */
    void setAsyncSurface(bool on);
    /**
     * @return Whether fitting requests normally run in the single-worker pool; regional fitting
     *   also uses worker paths.
     */
    [[nodiscard]] bool asyncSurface() const noexcept { return async_surface_; }
    /*! Latest frame asked for; equals currentTimeIndex() once it is shown.
     * @return Newest requested frame index, minus one before selection/after close; can differ from
     *   displayed index while fitting.
     */
    [[nodiscard]] int requestedTimeIndex() const noexcept { return requested_time_idx_; }
    /**
     * @param[in] bytes Fitted-frame cache budget in bytes, zero disables retained cached jobs.
     * @note Evicts oldest insertion-order entries immediately until accounting fits.
     *   Published/working/envelope shared owners can keep buffers alive outside this budget; does
     *   not cancel active work or repaint.
     */
    void setSurfaceCacheBytes(size_t bytes);
    /*! Retained frame-cache and historical-envelope bytes, excluding the displayed frame.
     * @return Accounted cache plus envelope corner-vector capacity and uncached newest-envelope job
     *   storage; conservative shared-region charges can duplicate shared geometry.
     */
    [[nodiscard]] size_t surfaceAuxiliaryBytes() const;
    /*! Last settled fitter/worker-frame capacities, separate from cached history.
     * @return Published worker-workspace/spare-buffer capacity plus velocity scratch capacity; does
     *   not inspect vectors while worker owns them.
     */
    [[nodiscard]] size_t surfaceWorkingBytes() const;
    /*! Retained displayed flow-classification storage, excluding fitter/history.
     * @return Capacity bytes of unsmoothed visibility corners or compact summary depths/flags,
     *   excluding other hydraulic geometry.
     */
    [[nodiscard]] size_t waterVisibilityBytes() const;
    /*! Fits run by the async path (worker and bootstrap), for tests.
     * @return Cumulative fits dispatched/synchronously bootstrapped, not reset by source changes
     *   and not a completed-fit count.
     */
    [[nodiscard]] int surfaceFitCount() const noexcept { return surface_fit_count_; }
    /*! A fit is running or waiting on the worker.
     * @return Whether a worker fit is active or pending, including envelope work; pending/throttled
     *   display requests keep it true.
     */
    [[nodiscard]] bool surfaceBusy() const noexcept { return surface_busy_ || pending_surface_; }
    /*! Minimum spacing between worker fits started by live ticks.
     * @param[in] ms Raw milliseconds between live tick-driven worker fits; positive throttles,
     *   zero/negative disables throttle.
     * @note Default1000; copied without clamp, timer reschedule or notification. Non-live
     *   scrubs/captures/envelope work are not throttled.
     */
    void setLiveMinFitIntervalMs(int ms) noexcept { live_min_fit_interval_ms_ = ms; }
    /*! Ask for the temporal maximum; envelopeReady() follows once every
     *  frame is fitted (at once when it already is, or in sync mode).
     * @brief Request missing full-domain fitted frames for temporal corner maxima.
     * @note Emits envelopeReady synchronously if already complete (including sync/no-source/empty
     *   cases); otherwise sets demand and schedules lower-priority worker jobs after display
     *   requests. Completion is relative to current retained source history and mutable newest
     *   tick.
     */
    void requestEnvelope();
    /**
     * @return True outside async/nonempty-source mode; otherwise advances cached completed prefix
     *   and checks newest full-domain fitted identity. Can mutate envelope bookkeeping through
     *   const access but never fits/blocks itself.
     */
    [[nodiscard]] bool envelopeComplete() const;

    /*! \brief Barycentrically-interpolated scene-space velocity (m/s) at
     *  \p scenePt, from the per-vertex velocity field reconstructed in
     *  applyCurrentFlux_ (V1, Issue 5). Writes \p outVx,\p outVy and returns
     *  true when \p scenePt lies in a wet cell; returns false (and zeros)
     *  off-mesh or where the containing cell is dry. Used by the grid-sampled
     *  glyph placement so arrows are evenly spaced (independent of mesh
     *  density) and their direction varies smoothly across cell boundaries.
     * @param[in] scenePt Scene-map point within current geometry.
     * @param[out] outVx Velocity X m/s; zero on failure.
     * @param[out] outVy Velocity Y m/s with scene Y sign inversion; zero on failure.
     * @return True for a wet/visible containing cell with usable vertex field; false outside,
     *   dry/missing/nonfinite depth or missing subset vertex. Barycentric velocity remains in SI
     *   components with Y sign inversion; it is not a nonlinear CRS vector transform. Current
     *   smoothed cell visibility is checked before interpolation.
     */
    [[nodiscard]] bool velocityAtScene(const QPointF &scenePt, float &outVx, float &outVy) const;

    // Immutable velocity input for a completed contour frame. Geometry is
    // shared with the layer; callers must discard this on geometry changes.
    struct VelocityField {
        std::vector<float> depths, vx, vy;
        std::vector<int> cells, vertices; // empty for the legacy full-domain field
    };
    /**
     * @return Independent value copy of mean depths and continuous depth-weighted incident-cell
     *   vertex velocities m/s, with scene Y inversion. Invisible cells have depth zero; regional
     *   mode emits compact sorted cell/vertex maps. Geometry itself is borrowed later by
     *   velocityAtScene, so capture cannot survive geometry changes without caller coordination.
     */
    [[nodiscard]] VelocityField captureVelocityField() const;
    /**
     * @param[in] scenePt Scene-map point within current geometry.
     * @param[out] outVx Velocity X m/s; zero on failure.
     * @param[out] outVy Velocity Y m/s with scene Y sign inversion; zero on failure.
     * @param[in] field Stable captured field matching current geometry; compact sorted cell/vertex
     *   IDs map subset arrays.
     * @return True for a wet/visible containing cell with usable vertex field; false outside,
     *   dry/missing/nonfinite depth or missing subset vertex. Barycentric velocity remains in SI
     *   components with Y sign inversion; it is not a nonlinear CRS vector transform. Captured
     *   field supplies its own visibility-masked depth; current thin-film policy is not reapplied.
     */
    [[nodiscard]] bool velocityAtScene(const QPointF &scenePt, float &outVx, float &outVy,
                                       const VelocityField &field) const;
    /**
     * @param[in] includeDry True includes finite zero mean depths; false includes finite strictly
     *   positive depths, independently of model dry threshold.
     * @return Owned full-domain mean-depth samples metres in cell order; regional mode reads the
     *   entire published source frame synchronously. Invalid/negative values excluded; read failure
     *   can yield empty list.
     */
    [[nodiscard]] QVector<double> currentDepthSamples(bool includeDry = false) const;
    // Fixed classifications use one temporal peak mean-depth sample per cell.
    /**
     * @param[in] scheme Classification method/range policy to supply statistics for.
     * @return Finite positive mean depths metres for quantile/natural-break/stddev methods only.
     *   Per-frame uses current values; other modes use one retained temporal maximum per cell, not
     *   the all-frame distribution. Other bin methods return empty.
     */
    [[nodiscard]] QVector<double>
    depthClassificationSamples(const OpenSWMM::Render::ClassificationScheme &scheme) const;

    // Compatibility vertex summary of the smooth temporal envelope.
    // Separate values are retained internally for disconnected vertex fans.
    /**
     * @return Compatibility envelope formed by maximum nonnegative corner depth over incident
     *   cells, one float per source vertex; zero baseline, empty if no surface envelope. In async
     *   mode may be incomplete; cell-corner discontinuities are collapsed.
     */
    [[nodiscard]] QVector<float> maxDepthPerVertex() const;
    // Compatibility query; vertMax is ignored in favor of the fan-aware envelope.
    /**
     * @param[in] scenePt Scene-map query point.
     * @param[in] vertMax Ignored legacy vertex-envelope argument; retained for compatibility.
     * @return Nonnegative interpolation of actual per-cell corner temporal envelope metres, zero
     *   outside/invalid. Recomputes/queries maxSurfaceDepths(), which can synchronously scan
     *   history outside async mode.
     */
    [[nodiscard]] float maxDepthAtSceneInterp(const QPointF &scenePt,
                                              const QVector<float> &vertMax) const;

    /*! \brief Replace the highlight set. Triggers an Overlay repaint via the
     *  layer's existing invalidate path.
     * @param[in] triIdxSet Set copied verbatim without validating IDs; CPU painter interprets
     *   members as DISPLAY fan triangle indices, despite historical cell naming.
     * @note Equality no-op; change updates fill item and emits
     *   repaintRequested()/highlightedCellsChanged(). Original cell IDs from
     *   pickCellsInRect/Polygon require fan mapping for mixed-mesh CPU highlighting.
     */
    void highlightCells(const QSet<int> &triIdxSet);

    /*! \brief Add to / clear the highlight set.
     * @brief Clear nonempty selection highlights, update fill item and emit
     *   repaintRequested()/highlightedCellsChanged(); empty selection is a no-op.
     */
    void clearHighlights();

    /*! \brief Current highlight set (read-only).
     * @return Borrowed stored highlight-index set, possibly containing unchecked/out-of-range
     *   indices. Element identity follows highlightCells() implementation; invalidated by
     *   mutation/destruction.
     */
    [[nodiscard]] const QSet<int> &highlightedCells() const noexcept { return m_highlighted; }

    // ----- OpenSWMMVisLayer interface ----------------------------------------

    /**
     * @param[in] scene Borrowed target scene, null is a no-op.
     * @param[in] canvasExtent Unused extent; full cached mesh supplies item bounds.
     * @param[in] canvasSRS Unused here; onCanvasCRSChanged prepares geometry separately.
     * @note Deletes/replaces existing fill/arrow items and adds two new scene-owned items borrowing
     *   this layer. GUI thread only.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] scene Optional scene to remove/invalidate vacated region; items are deleted even
     *   when null.
     * @note Deletes both graphics items and invalidates their former bounds (whole scene when
     *   empty), preserving source/geometry. GUI thread only.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @param[in] scene Borrowed target scene; used only if fill item must be created.
     * @param[in] canvasExtent Forwarded on lazy population, otherwise unused.
     * @param[in] canvasSRS Forwarded on lazy population, otherwise unused.
     * @note Creates missing items or updates their geometry/repaint state; does not re-read source
     *   or rebuild CRS. GUI thread only.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] newCanvasSRS Borrowed destination CRS, nullable; valid differing declared
     *   source/destination CRS builds an owned OGR transform.
     * @note Undeclared source with no transform is a no-op. Destroys prior transform, rebuilds
     *   scene geometry/index/revision and reapplies/refits displayed field; invalid transform
     *   creation falls through to identity reprojection. Source SI coordinates first convert to
     *   model units, then CRS transform, then Y inversion. No independent repaint signal here.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- Scene caches (public for the graphics item) -----------------------

    /*! Per-triangle scene-space vertices + per-tri animated state. */
    struct SceneTri {
        QPointF a, b, c;
        QPointF centroid;   ///< Scene-space centroid; cached at rebuildSceneGeometry_.
        float depth = 0.0f; ///< Cell-centre depth (m) — drives the heatmap fill.
        // Signed smooth VFR depths, shared on connected wet edges. Negative
        // values retain dry-side geometry; NaN means no water/invalid data.
        float dv0 = std::numeric_limits<float>::quiet_NaN();
        float dv1 = std::numeric_limits<float>::quiet_NaN();
        float dv2 = std::numeric_limits<float>::quiet_NaN();
        float vx = 0.0f;   ///< Scene-space velocity x (m/s; sign flipped to match scene Y).
        float vy = 0.0f;   ///< Scene-space velocity y (m/s).
        float vmag = 0.0f; ///< |v| in m/s, computed in model coords.
    };

    QRectF m_sceneBBox;
    QVector<SceneTri> m_sceneTris;
    /*! Point-location index over m_sceneTris bboxes (parallel indices). Built in
     *  rebuildSceneGeometry_; accelerates pickCellAt from O(n) to O(cell). */
    MeshSpatialGrid m_triGrid;

    /*! Deduplicated mesh edges in scene space (Issue 3). Each undirected edge is
     *  stored exactly once — mirrors SWMM2DMeshLayer's pushEdge — so the
     *  wireframe overlay no longer strokes shared edges twice. Double-stroking
     *  the translucent edge colour composited it to a dark wash ("dark
     *  artifact"). `slope` (|Δz|/length of the edge's bed) lets the renderer
     *  honour MeshEdgeStyle's slope-driven thin/wide width. Built in
     *  rebuildSceneGeometry_. */
    struct SceneEdge {
        QPointF a, b;
        float slope = 0.0f;
    };
    QVector<SceneEdge> m_sceneEdges;

    /*! QSG-2D-1M — shared scene-space vertex positions (parallel to the
     *  source vertex arrays). Built in rebuildSceneGeometry_; lets the QSG
     *  renderer draw one marker per UNIQUE mesh vertex (the per-corner
     *  emission repeated shared vertices ~6×) and feed the static indexed
     *  geometry buffers without expanding corners. */
    QVector<QPointF> m_sceneVerts;

    /*! QSG-2D-1M — display triangle → vertex-id triples backing m_sceneTris
     *  (indices into m_sceneVerts). Read-only view for the renderer's
     *  static indexed-geometry path. On a mixed mesh this is the sub-triangle
     *  FAN (one entry per triangle cell, two per quad, cell order); see
     *  \ref triCellMap / \ref cellTriRange for the cell mapping.
     * @return Borrowed zero-based display-fan triplets, one per triangle cell/two per quad, invalid
     *   slots {-1,-1,-1}. Views/references are invalidated by rebuild/source
     *   replacement/destruction; copy for workers.
     */
    [[nodiscard]] const std::vector<std::array<int, 3>> &triVertexIndices() const noexcept
    {
        return tris_;
    }

    /*! Cells (engine faces): {v0,v1,v2,v3}, v3 == -1 for a triangle. Indexed
     *  by CELL — the index every public pick/highlight/plot API uses.
     * @return Borrowed original cyclic four-slot cells, fourth minus one for triangles; invalid
     *   input can have first three slots minus one. Views/references are invalidated by
     *   rebuild/source replacement/destruction; copy for workers.
     */
    [[nodiscard]] const std::vector<std::array<int, 4>> &cellVertexIndices() const noexcept
    {
        return cells_;
    }

    /*! Display triangle → owning cell (parallel to m_sceneTris / triVertexIndices).
     * @return Borrowed display-fan-to-original-cell IDs, parallel to triVertexIndices().
     *   Views/references are invalidated by rebuild/source replacement/destruction; copy for
     *   workers.
     */
    [[nodiscard]] const std::vector<int> &triCellMap() const noexcept { return triCell_; }

    /*! CSR cell → display-triangle range: the fan triangles of cell c are
     *  \c [cellTriRange()[c], cellTriRange()[c+1]). Size cellCount()+1.
     * @return Borrowed cell-to-display CSR offsets, size cellCount()+1 after valid rebuild,
     *   possibly empty otherwise. Views/references are invalidated by rebuild/source
     *   replacement/destruction; copy for workers.
     */
    [[nodiscard]] const std::vector<int> &cellTriRange() const noexcept { return cellTri0_; }

    /*! Number of cells (faces) in the current geometry.
     * @return Original cell slot count, distinct from display fan count. Views/references are
     *   invalidated by rebuild/source replacement/destruction; copy for workers.
     */
    [[nodiscard]] int cellCount() const noexcept { return static_cast<int>(cells_.size()); }

    /*! QSG-2D-1M — bumped every rebuildSceneGeometry_. Lets the QSG
     *  renderer classify an ambiguous repaint into "geometry changed"
     *  vs "style changed" and key its static buffers / chunk index.
     *  Mirrors SWMM2DMeshLayer::geomRevision().
     * @return Revision incremented for every geometry rebuild, including empty/failed rebuild; not
     *   every frame/style edit. Views/references are invalidated by rebuild/source
     *   replacement/destruction; copy for workers.
     */
    [[nodiscard]] quint64 geomRevision() const noexcept { return m_geomRevision; }

    /**
 * @brief A changed thin-film policy is coherent with current displayed field; may be deferred until
 *   refit publication.
 */
signals:
    /** @brief A source replacement or detachment completed its metadata adoption.
     * @details Query sourceRevision/source/currentTimeIndex on the GUI thread. An adopted
     * source can have zero periods and no displayed frame; this does not imply a run finished. */
    void sourceChanged();
    void waterDisplayPolicyChanged();
    /**
     * @brief Dynamic scalar sublayer membership changed; childrenChanged/repaintRequested also
     *   follow add/remove.
     */
    void resultSublayersChanged();
    /*! Emitted when `source()->timeCount()` changes (either via setSource or refreshTimeRange).
     * @brief Announce available retained time range after source/refresh changes.
     * @param[in] lo,hi Inclusive available frame-index range; empty/reset state uses(0,0), which is
     *   not proof frame 0exists.
     */
    void timeRangeChanged(int lo, int hi);

    /*! Emitted whenever the current frame changes (setCurrentTimeIndex).
     * @brief Announce coherent displayed hydraulic frame, not merely requested worker index.
     * @param[in] t Actually published retained frame index; minus one after close.
     */
    void currentTimeChanged(int t);

    /*!
     * \brief Emitted alongside currentTimeChanged(int) with the QDateTime of
     * the frame (or invalid if the source has no time anchor). Mirrors
     * SWMMResultsLayer::currentDateTimeChanged so AnimationController can
     * drive the 2D layer as a fallback when no 1D primary is loaded.
      *
     * @brief Announce timestamp alongside displayed-index publication.
     * @param[in] dt Published source timestamp; invalid when closing or when source has no valid
     *   time.
     */
    void currentDateTimeChanged(const QDateTime &dt);

    /*! The maximum asked for through requestEnvelope() is complete.
     * @brief Requested retained-history corner envelope is complete at notification time; later
     *   ticks can make it incomplete.
     */
    void envelopeReady();

    /*! CF.3 — emitted on plot↔canvas hover sync.
     * @brief Expose hover notifications for externally driven item/tool integration; this layer
     *   implementation does not emit it itself.
     * @param[in] triIdx Historical hover index payload; producer must establish cell versus
     *   display-fan identity and use minus one for no hit.
     */
    void cellHovered(int triIdx);

    /*! CF.3 — emitted when the highlight set changes.
     * @brief Stored highlight set changed after fill-item update/repaint request.
     */
    void highlightedCellsChanged();

    /*! \brief Emitted when setRenderer() swaps the renderer pointer.
     * @brief Placeholder owned feature-renderer pointer changed; this signal alone does not request
     *   repaint.
     */
    void rendererChanged();

private:
    /**
     * @param[in] t Requested retained frame, clamped when source has frames.
     * @return False without source/frames, true after accepting a cached/synchronous/pending
     *   request even if input values were masked NaN.
     * @note Advances request sequence. Bootstrap fits synchronously; subsequent async/region work
     *   keeps previous publication. Publishes time/date/repaint only after a coherent fit, and can
     *   fall back/recurse to full-domain mode for rejected regional comparison. GUI-thread only.
     */
    bool loadFrame_(int t); ///< Re-read frame \p t and emit frame/repaint signals.
    /**
     * @brief Wait/cancel fits, discard display/regional/envelope geometry caches and rebuild
     *   validated cells/fans/index/adjacency/edge geometry.
     * @note Always increments geometry revision, including failure/empty exits. Validates array
     *   shapes, normalizes unambiguous1-based connectivity, collapses invalid/degenerate fan slots
     *   while preserving cell mapping. SI XY convert by source/fallback scale, optional CRS
     *   transform and Y flip; extent stays model CRS units. Does not emit repaint or reset original
     *   source counters.
     */
    void
    rebuildSceneGeometry_(); ///< Recompute scene-space triangle vertices + centroids; refresh cached edge geometry.
    /**
     * @param[in] scenePt Scene-map point located against current display geometry.
     * @param[out] outVx SI interpolated velocity X, zero initialized.
     * @param[out] outVy SI interpolated velocity Y with scene sign, zero initialized.
     * @param[in] depths Full-domain cell mean depths metres, current geometry indexing.
     * @param[in] vx Full-domain per-vertex velocity X m/s; missing vertex slots contribute zero.
     * @param[in] vy Full-domain per-vertex scene-sign velocity Y m/s.
     * @return False for empty field/outside/invalid/dry containing cell; true after barycentric
     *   interpolation, even if individual vertex slots contributed zero. Degenerate basis defaults
     *   to first corner. No thin-film visibility check in this helper; public caller/captured
     *   depths supply it.
     */
    bool velocityAtScene_(const QPointF &scenePt, float &outVx, float &outVy,
                          const std::vector<float> &depths, const std::vector<float> &vx,
                          const std::vector<float> &vy) const;
    /**
     * @brief Increment frame revision and fit VFR/flow/smoothed corner fields from current
     *   mean-depth/flux inputs.
     * @note Shape mismatch exits after revision increment without replacing prior fields. Valid
     *   input refreshes scene corners and automatic depth bound, clears compact visibility
     *   summaries in favor of raw fitted corners. Caller handles item updates/signals.
     */
    void applyCurrentDepths_(); ///< Copy `current_depths_` into the SceneTri buffer.
    /**
     * @brief Bump policy revision and invalidate historical corner-envelope identity, then
     *   recompute requested visibility/velocity.
     * @note With published async/region field, refit requested index and defer policy notification
     *   to coherent publication; otherwise apply synchronously, notify policy and repaint. Does not
     *   mutate source solver values.
     */
    void refreshWaterDisplayPolicy_();
    /**
     * @brief Reconstruct current cell RT0/depth velocities in SI, mirror scene Y sign and
     *   depth-weight-average wet incident cells into vertex velocities.
     * @note Missing/wrong-sized depth/flux/length inputs wipe old triangle/vertex velocities and
     *   capability flag. Applies physical dry cutoff and current display visibility; quad vertex
     *   contribution uses0.75 depth weight. Grows auto velocity bound and reuses GUI scratch
     *   storage. No source writes, coordinate-vector reprojection or direct signals.
     */
    void applyCurrentFlux_(const std::vector<float> *savedVx = nullptr,
                           const std::vector<float> *savedVy = nullptr);

    /*!
     * \brief Metres-to-model-CRS-unit divisor for the source coordinates.
     *
     * Resolved in rebuildSceneGeometry_() from the source's
     * CoordinateReference, falling back to setFallbackCoordinateScale()'s
     * value for sources that declare nothing. 1.0 for a metric model, and the
     * default when nobody supplied a fallback. Issue #155.
      *
     * @return Reciprocal of declared finite positive metres-per-model-unit factor, otherwise
     *   configured fallback reciprocal. SI coordinates/depth multiplied by it become model linear
     *   units; canvas CRS scaling is separate.
     * @note Undeclared nonunity fallback logs once; const access mutates warning guard, so
     *   serialize.
     */
    [[nodiscard]] double resolveCoordinateScale_() const;

    /*! QSG-2D-1M — see geomRevision(). */
    quint64 m_geomRevision = 0;

    /*! Layer-CRS → canvas-CRS transform, rebuilt in onCanvasCRSChanged().
     *  nullptr means "no reprojection needed" — CRSReproject treats a null
     *  transform as a documented pass-through. Mirrors SWMM2DMeshLayer. */
    OGRCoordinateTransformation *m_transform = nullptr;

    /*! One-shot guard: the undeclared-CRS fallback warns once, not on every
     *  geometry rebuild. Reset by setFallbackCoordinateScale(). */
    mutable bool m_warnedUndeclaredCrs = false;

    /*! Metres per model-CRS linear unit for sources that declare no `/crs`
     *  variable — see setFallbackCoordinateScale(). 1.0 = leave coordinates
     *  alone, the pre-#155 behaviour and the safe default. */
    double m_fallbackMetresPerModelUnit = 1.0;

    /*! Display triangle containing \p scenePt (index into m_sceneTris /
     *  tris_), or -1. \ref pickCellAt is this mapped through triCell_.
     * @param[in] scenePt Finite scene-map query point.
     * @return First containing display fan triangle, minus one outside/invalid/degenerate. Uses
     *   read-only grid point candidates when present, linear fallback otherwise; shared boundary
     *   ties follow stored order.
     */
    [[nodiscard]] int pickDisplayTriAt_(const QPointF &scenePt) const;
    /*! Barycentric current-frame depth on display triangle \p triIdx (the
     *  per-sub-triangle body of \ref depthAtCellInterp).
     * @param[in] triIdx Display fan index, bounds checked by signed-depth helper.
     * @param[in] scenePt Scene-map sample point.
     * @return max(0,signed fitted depth) metres; NaN/invalid collapses to zero by argument order.
     *   May trigger full-domain analysis in regional mode and extrapolate outside specified
     *   triangle.
     */
    [[nodiscard]] float depthAtDisplayTriInterp_(int triIdx, const QPointF &scenePt) const;

    std::unique_ptr<IMesh2DSource> source_;
    std::vector<double> vx_, vy_, vz_;
    // Mixed-mesh split (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md): the
    // CELLS carry the per-face values; the display FAN (one sub-triangle per
    // triangle cell, two per quad on the engine's VFR diagonal) is what the
    // scene, hit-testing, contours and interpolation run on. Every per-face
    // lookup for a display triangle goes through triCell_.
    std::vector<std::array<int, 4>> cells_; ///< per CELL: {v0,v1,v2,v3}, v3 = -1 for a triangle
    std::vector<std::array<int, 3>> tris_;  ///< display fan, parallel to m_sceneTris
    std::vector<int> triCell_;              ///< display triangle → cell
    std::vector<int> cellTri0_;             ///< CSR: cell → first display triangle (size nCells+1)
    std::vector<VertexDepthReconstruct::CellSplit>
        cellSplit_;                          ///< per CELL, feeds the vertex reconstruction
    std::vector<QPointF> cellCentroidScene_; ///< per CELL area centroid, scene space
    std::vector<float> current_depths_;      ///< per CELL

    std::vector<CellWaterGeometry::Surface> cellSurfaces_;
    std::vector<CellWaterGeometry::CornerDepths> cellFlowDepths_; // before shared-corner projection
    // Async display queries need only these policy-independent summaries;
    // raw corners remain in the worker/cache for fitting and analysis.
    std::vector<double> cellFlowMaxDepths_;
    std::vector<unsigned char> cellFlowFlags_;
    CellWaterGeometry::SmoothTopology surfaceTopology_;
    std::vector<CellWaterGeometry::CornerDepths> surfaceDepths_;
    mutable std::vector<CellWaterGeometry::CornerDepths> surfaceMaxCache_;
    mutable int surfaceMaxFramesDone_ = 0;
    mutable const IMesh2DSource *surfaceMaxSource_ = nullptr;
    mutable int surfaceMaxGeneration_ = -1;
    quint64 frame_revision_ = 0;

    // Fitted-surface worker (async mode): one worker thread, one pending
    // request, one publish point on the GUI thread. The worker reads only the
    // per-geometry members above and its own copies of the frame inputs.
    struct SurfaceJob;
    struct SurfaceRegion {
        static constexpr size_t maxCells = 50000;
        QRectF coverage;
        std::vector<int> cells, vertices, coreCells;
        std::shared_ptr<const SurfaceRegion> checkRegion;
        std::vector<VertexDepthReconstruct::CellSplit> splits;
        CellWaterGeometry::SmoothTopology topology;
    };
    std::vector<std::array<int, 4>> surfaceNeighbors_; // globally validated manifold neighbors
    std::shared_ptr<const SurfaceRegion> surface_region_, published_region_;
    quint64 surface_region_revision_ = 0;
    QRectF hydraulic_view_;
    int regional_fallbacks_ = 0;
    double regional_error_ = 0;
    /**
     * @param[in] job Completed accepted regional job with matching sorted cell/vertex maps and
     *   fitted array shapes.
     * @note GUI-thread publication retains globally indexed buffers, clears previous covered cells
     *   to NaN/zero and installs new region only. Copies mean/flux/corner fields, derives visible
     *   cell RT0 velocities and depth-weighted vertex field, grows auto bounds and retains shared
     *   region owner. Caller owns revision/sequence/time/item/repaint notifications. Requires
     *   nonnull job.region.
     */
    void publishRegionalSurface_(const SurfaceJob &job);
    /**
     * @param[in] job GUI-owned mutable request initialized with source/frame/key/policy and
     *   optional region; outputs reference/inputs/file path.
     * @note Requires nonnull source and matching geometry. Validates/refreshes edge arrays,
     *   captures full-domain reference corners for regional error check, stores private-reader path
     *   for HDF5 workers; other sources copy coherent frame/subset on GUI thread and mask
     *   invalid/negative depths NaN. Mismatched subset identity is not borrowed from another tick.
     */
    void readSurfaceJob_(SurfaceJob &job);
    /**
     * @return Shared immutable full-domain fit for actually published regional frame, cached by
     *   source/history/geometry/policy/tick identity; null outside regional/source/valid-frame
     *   mode.
     * @note Synchronously reads/fits the entire domain through const GUI access when cache misses;
     *   analysis intentionally exceeds display-only viewport region and can be expensive.
     */
    std::shared_ptr<const SurfaceJob> analysisSurface_() const;
    mutable std::shared_ptr<const SurfaceJob> analysis_surface_;

    struct SurfaceKey {
        quint64 sourceRev = 0, geomRev = 0, policyRev = 0, regionRev = 0;
        int generation = 0, frame = -1, tickRev = 0;
        /**
         * @brief Compare all source/geometry/policy/region/history/frame/tick identity fields; a
         *   matching key permits fitted-field reuse.
         */
        bool operator==(const SurfaceKey &) const = default;
    };
    struct SurfaceJob {
        std::shared_ptr<const SurfaceRegion> region;
        QString inputPath; // worker-owned HDF5 reader; live inputs are captured on the GUI thread
        std::vector<CellWaterGeometry::CornerDepths> reference;
        bool acceptable = true;
        double regionalError = 0;
        SurfaceKey key;
        quint64 seq = 0, epoch = 0; // request order; cancellation epoch
        bool forEnvelope = false, live = false, buffersTransferred = false;
        size_t workspaceBytes = 0;
        CellWaterGeometry::VisibilityPolicy policy;
        std::vector<CellWaterGeometry::Surface> surfaces;
        std::vector<float> depths, flux, velocityX, velocityY;     // inputs
        std::vector<CellWaterGeometry::CornerDepths> flow, smooth; // outputs
    };
    /**
     * @param[in] frame Retained source frame index; invalid index still creates a key with source
     *   revision fallback0.
     * @return Value identity combining source,geometry,policy,region,history generation,frame and
     *   same-tick hydraulic revision. No reads beyond source revision accessors.
     */
    [[nodiscard]] SurfaceKey surfaceKey_(int frame) const;
    /**
     * @param[in] frame Source frame index; source must be nonnull.
     * @param[out] depths Full cell means metres, replaced with all NaN on failed/wrong-size read
     *   and individual NaN for negative/nonfinite values.
     * @param[out] flux Outward-positive stride-four edge flux m³/s; cleared on missing read.
     * @note GUI source access refreshes/validates optional edge geometry, clears invalid arrays,
     *   marks positive flux capability on successful read; debug environment can log invalid count.
     *   Does not itself validate successful flux shape or publish.
     */
    void readSurfaceInputs_(int frame, std::vector<float> &depths, std::vector<float> &flux);
    /**
     * @param[in] depths Full-domain mean depths metres, exactly cell count; invalid values handled
     *   by reconstruction.
     * @param[in] flux Optional stride-four outward edge flux m³/s.
     * @param[in] policy Captured display visibility policy.
     * @param[out] surfaces Replaced per-cell VFR storage surfaces.
     * @param[out] flow Reused/replaced flowing per-cell corner depths metres before shared-corner
     *   projection.
     * @param[out] smooth Replaced smoothed per-cell signed corner depths metres.
     * @param[in] workspace Optional exclusively owned reusable fit scratch; null uses helper-local
     *   storage.
     * @note Reads stable geometry/topology only; safe on serial worker while GUI prevents geometry
     *   rebuild through cancel/wait. This is shared by synchronous,worker and envelope paths; no
     *   live source or GUI signals are accessed.
     */
    void
    fitSurfaceFields_(const std::vector<float> &depths, const std::vector<float> &flux,
                      CellWaterGeometry::VisibilityPolicy policy,
                      std::vector<CellWaterGeometry::Surface> &surfaces,
                      std::vector<CellWaterGeometry::CornerDepths> &flow,
                      std::vector<CellWaterGeometry::CornerDepths> &smooth,
                      CellWaterGeometry::NeighborhoodFit::Workspace *workspace = nullptr) const;
    /**
     * @param[in] job Exclusively owned mutable fit request; file inputs/fit
     *   outputs/acceptance/error are replaced.
     * @param[in] workspace Optional exclusive reusable scratch, never shared with concurrent fit.
     * @note Reads stable geometry plus captured live inputs, or opens worker-private HDF5 handle
     *   and masks failed rowsNaN. Regional jobs reconstruct local fields and compare core8/16-ring
     *   results/reference; >0.002m or material sign mismatch marks unacceptable. Does not
     *   publish/emit; geometry must remain unchanged until completion.
     */
    void fitSurface_(SurfaceJob &job,
                     CellWaterGeometry::NeighborhoodFit::Workspace *workspace = nullptr) const;
    /**
     * @param[in] job Completed job, possibly holding old display vectors after oversized-frame
     *   transfer.
     * @note No-op unless buffersTransferred. Resizes/reclaims its arrays as second-frame reusable
     *   spare and publishes capacity accounting on GUI thread, avoiding retaining an additional
     *   oversized cached frame.
     */
    void retainSurfaceBuffers_(const std::shared_ptr<SurfaceJob> &job);
    /**
     * @brief Copy current per-cell mean/fitted corner depths into display-fan triangle slots and
     *   grow automatic depth bound from finite corners.
     * @note Requires compatible cell/fan/depth/surface arrays. Invalid vertex mapping yields corner
     *   NaN; does not alter revisions,source data or issue signals.
     */
    void fillSceneCorners_();
    /**
     * @param[in] job Completed accepted fit matching current source/geometry and request identity;
     *   mutable buffers may transfer to display.
     * @param[in] seq Request sequence used to reject/reconcile obsolete publication ordering.
     * @note GUI-thread coherent install increments frame revision, clears full-analysis cache,
     *   updates mean/flux/visibility/fitted/velocity buffers and shared region identity. Oversized
     *   uncached non-envelope jobs swap buffers rather than duplicate them. Emits deferred policy
     *   then time/date/repaint notifications only after state is coherent.
     */
    void publishSurface_(SurfaceJob &job, quint64 seq);
    /**
     * @brief Dispatch one pending displayed request or next envelope job to single-worker pool when
     *   idle/source exists.
     * @note Latest displayed request wins over envelope. Live requests honor configured positive
     *   spacing using owner-bound timer; spares transfer capacity before launch. Increments fit
     *   count and posts GUI completion; no worker touches live source/QObject UI state.
     */
    void scheduleFit_();
    /**
     * @param[in] job Shared completed worker job delivered on GUI thread, possibly manually
     *   consumed by synchronous capture.
     * @note Ignores wrong epoch/already-consumed identity; validates source/key/content before
     *   caching/publication. Failed regional comparison falls back to full-domain worker request.
     *   Publishes only sequence newer than displayed, folds full-domain envelope prefix/newest and
     *   schedules next work; stale same-latest request is reloaded.
     */
    void onSurfaceFinished_(const std::shared_ptr<SurfaceJob> &job);
    /**
     * @brief Withdraw pending/envelope work, wait for active future, release cache/spare/workspace
     *   and advance cancellation epoch.
     * @note GUI-thread blocking call; already queued callbacks are ignored. Invalidates published
     *   key/sequence bookkeeping but does not blank currently displayed hydraulic arrays or emit
     *   signals.
     */
    void cancelSurfaceJobs_();
    /**
     * @param[in] key Exact source/frame/geometry/policy/region/tick identity.
     * @return Shared newest matching cached job or null; no recency promotion/eviction or worker
     *   wait. Returned buffers must be treated immutable except controlled GUI publication
     *   transfer.
     */
    [[nodiscard]] std::shared_ptr<SurfaceJob> cachedSurface_(const SurfaceKey &key) const;
    /**
     * @param[in] job Shared completed job with exact immutable identity and measured vector
     *   capacities.
     * @note Existing equal key is a no-op; otherwise appends, charges conservative job bytes and
     *   evicts oldest to configured budget. External display/envelope/shared owners can keep
     *   removed storage alive.
     */
    void cacheSurface_(const std::shared_ptr<SurfaceJob> &job);
    /**
     * @return Shared next missing full-domain job or null when no demand/source/geometry/work;
     *   finished demand emits envelopeReady and clears flag.
     * @note GUI-thread request captures inputs for oldest missing prefix/newest frame, forces
     *   region revision 0 and does not alter displayed request sequence.
     */
    [[nodiscard]] std::shared_ptr<SurfaceJob> nextEnvelopeJob_();
    /**
     * @brief Initialize all NaN full-domain corner maxima and reset frozen-prefix count when
     *   source/history generation/shape/count regresses.
     * @note Source must be nonnull; policy changes invalidate source marker before calling. Mutates
     *   const cache only, performs no fit/IO itself.
     */
    void resetEnvelopeIfStale_() const;
    /**
     * @brief Fold sequential cached completed full-domain fields into frozen corner maxima,
     *   stopping at first cache miss before mutable newest frame.
     * @note Nonnull source and initialized envelope required; cached keys include current
     *   geometry/policy/history/tick revision. No source value read/worker dispatch.
     */
    void advanceEnvelope_() const;
    /**
     * @param[in] frame Requested newest/full-domain frame identity.
     * @return Borrowed corner array from matching displayed full field,newest-envelope owner or
     *   cache; null if unavailable. Reference is valid only until subsequent
     *   cache/publication/cancel operations; no ownership transfer.
     */
    [[nodiscard]] const std::vector<CellWaterGeometry::CornerDepths> *
    newestSurface_(int frame) const;

    std::vector<std::shared_ptr<SurfaceJob>> surface_cache_; // oldest first
    size_t surface_cache_bytes_ = 0, max_surface_cache_bytes_ = size_t(128) << 20;
    CellWaterGeometry::NeighborhoodFit::Workspace surface_workspace_;
    size_t surface_workspace_bytes_ = 0,
           surface_buffer_bytes_ =
               0; // published capacity; never inspect worker-owned vectors from the GUI
    std::shared_ptr<SurfaceJob>
        spare_surface_; // uncached second-frame buffers, reused after coherent swaps
    std::shared_ptr<SurfaceJob> active_surface_;
    std::shared_ptr<SurfaceJob> pending_surface_;       // latest request not yet fitted
    std::shared_ptr<const SurfaceJob> envelope_newest_; // newest frame; never frozen
    SurfaceKey published_key_, requested_key_;
    quint64 policy_revision_ = 0, request_seq_ = 0, published_seq_ = 0, surface_epoch_ = 0;
    int requested_time_idx_ = -1, surface_fit_count_ = 0, live_min_fit_interval_ms_ = 1000;
    bool async_surface_ = false, surface_busy_ = false, envelope_wanted_ = false;
    bool policy_signal_pending_ = false, loading_live_ = false, live_fit_timer_armed_ = false;
    QElapsedTimer live_fit_clock_;
    QThreadPool surface_pool_;
    QFuture<void> surface_future_;

    // V1 (Issue 5) — per-vertex velocity field, reconstructed each frame in
    // applyCurrentFlux_ as the depth-weighted average of incident cell vectors.
    // A continuous (C0) field so glyphs sampled across cell boundaries vary
    // smoothly instead of stepping at each cell; the peer of vdepth_ for flow.
    std::vector<float> vvx_; ///< per-vertex velocity x (scene units)
    std::vector<float> vvy_; ///< per-vertex velocity y (scene units)
    // GUI-owned cell x/y/magnitude and vertex weights. Reinitialize each
    // publication; retain storage rather than allocating a full mesh per tick.
    std::array<std::vector<float>, 4> velocity_scratch_;

    // CF.2 — per-tick flux + time-invariant edge geometry pulled once from the source.
    std::vector<float> current_flux_; ///< [mesh::edgeSlot(cell, e)], m^3/s.
    std::vector<float> edge_length_;  ///< [mesh::edgeSlot(cell, e)], m.
    std::vector<float> edge_nx_;      ///< [mesh::edgeSlot(cell, e)], dimensionless.
    std::vector<float> edge_ny_;      ///< [mesh::edgeSlot(cell, e)], dimensionless.
    bool have_edge_geom_ = false;
    bool have_velocity_ = false;
    /*! Tri-state cache for hasEdgeFluxData(): 0 = not yet determined,
     *  +1 = edge flux present, -1 = absent. Reset on setSource. */
    mutable int edge_flux_probe_ = 0;

    int current_time_idx_ = -1;
    bool follow_live_ = true; // live source: auto-advance to newest frame until the user scrubs
    bool live_render_enabled_ =
        true; // live source: master gate for streaming/render work (Issue 2 toggle)

    // Live-tick coalescing. A tick arrives as up to four queued pushes
    // (depths, flux, vertex depths, rainfall), each followed by a refresh
    // request; the requests are folded into ONE range emission + ONE frame
    // load per event-loop turn (scheduleLiveSync_ / liveSync_).
    bool live_sync_pending_ = false;
    bool live_range_dirty_ = false;
    bool live_frame_dirty_ = false;
    quint64 source_revision_ = 0;
    bool source_adopting_ = false; ///< Internal adoption bracket, independent of displayed frames.
    int last_range_hi_ = -1; ///< last timeRangeChanged hi emitted
    /**
     * @brief Post at most one owner-bound zero-delay sync per GUI event-loop turn, coalescing
     *   phased live packet refreshes.
     * @note Does not wait for later packets not yet queued or suppress producer pushes.
     */
    void scheduleLiveSync_();
    /**
     * @brief Consume pending live range/frame flags, announce changed range and select/reload
     *   first/newest/user-controlled frame once.
     * @note No-op after clearing flags if source is no longer live or gate disabled. Marks
     *   dispatched fits live for throttling, follows requested async index rather than older
     *   displayed one; no source writes.
     */
    void liveSync_();

    // Incremental cell-storage maxima, keyed on source/history generation.
    // Only completed frames are frozen; the replaceable newest frame is
    // folded into a copy so a same-time update may lower the envelope.
    mutable std::vector<float> cellMaxCache_;
    mutable int cellMaxFramesDone_ = 0;
    mutable const IMesh2DSource *cellMaxSource_ = nullptr;
    mutable int cellMaxGeneration_ = -1;
    double dry_depth_ = 1e-4;       // fixed fallback; populated from model
    double thin_film_depth_ = -1.0; // inherit model threshold
    bool show_thin_films_ = false;
    double max_depth_ = 0.01; // 10 mm — auto-grows from data each tick
    bool max_depth_user_set_ = false;

    // Velocity overlay state.
    // CF.2 transitional default: arrows ON so the overlay is eyeball-able
    // before the layer-panel UI lands. Flip back to false once a UI toggle
    // exists.
    bool velocity_visible_ = true;
    qreal velocity_opacity_ = 0.9;
    double velocity_arrow_scale_ = 30.0; // px per log1p(vmag/vRef)
    double max_velocity_ = 1.0;          // m/s, auto-grown
    bool max_velocity_user_set_ = false;

    struct ScalarGeometryCache {
        std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> frame;
        std::vector<double> levels;
        bool labels = false;
        quint64 geometry = 0;
        std::shared_ptr<const OpenSWMM::Render::ResultScalarGeometry> output;
    };
    mutable QVector<ScalarGeometryCache> m_scalarGeometryCache;
    SWMM2DResultsGraphicsItem *graphics_item_ = nullptr;
    SWMM2DVelocityArrowsItem *arrows_item_ = nullptr;

    // VS.8 — true while SWMM2DResultsQSGRenderer owns the on-screen pixels.
    bool m_qsgOwnsRendering = false;

    // CF.3 — selection / highlight state.
    QSet<int> m_highlighted;

    // Slice BI Phase 8.13.6.6 — renderer plumbing.  Initialised eagerly in
    // the ctor (default GraduatedRenderer) so renderer() never returns
    // null.  Paint refactor deferred until Slice BB ColorRamp ships.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;

    // Slice CF.MVP-fix.3 — graduated colour + contour styling. All default
    // values preserve the pre-fix.3 paint output byte-identically.
    ColorRampStyle color_ramp_style_ = ColorRampStyle::Smooth;
    int color_classes_ = 5;
    bool filled_contours_ = false;
    double filled_contours_opacity_ = 0.60;
    int filled_contours_levels_ = 8;
    bool isolines_ = false;
    int isolines_levels_ = 5;
    QColor isolines_color_ = QColor(20, 20, 20, 230);
    double isolines_width_ = 1.0; // pixels

    // Slice S5.6 — sublayer foundation (dormant pointers, populated in
    // ctor). The existing paint pipeline does not yet consume these; the
    // final paint-replacement slice will replace the current SceneTri
    // pipeline with sublayer-driven QSG geometry.
    OpenSWMM::Render::MeshFillSublayer *m_meshFillSublayer = nullptr;
    OpenSWMM::Render::MeshEdgeSublayer *m_meshEdgeSublayer = nullptr;
    OpenSWMM::Render::MeshNodeSublayer *m_meshNodeSublayer = nullptr;
    OpenSWMM::Render::ContourBandSublayer *m_contourBandSublayer = nullptr;
    OpenSWMM::Render::CellDepthFillSublayer *m_cellDepthFillSublayer = nullptr;
    OpenSWMM::Render::SmoothDepthFillSublayer *m_smoothDepthFillSublayer = nullptr;
    OpenSWMM::Render::IsolineSublayer *m_isolineSublayer = nullptr;
    OpenSWMM::Render::VelocityVectorSublayer *m_velocityVectorSublayer = nullptr;
    bool m_flowOnlyRepaint = false;
    OpenSWMM::Render::FlowStreakSublayer *m_flowStreakSublayer = nullptr;
    friend class OpenSWMM::Render::FlowParticleLayerController;
    OpenSWMM::Render::FlowParticleLayerController *m_particleController = nullptr;
    double m_velocityFloorSI = 0;
    bool m_edgeFluxAvailable = false;

    // User-customisable paint order (Slice GUI-2026-05-30 §2).  Lazy-seeded
    // from the default order in sublayers(); reordered via moveSublayer();
    // round-tripped through ISublayerHost::save/loadSublayersFromJson.
    mutable quint64 m_resultCatalogRevision = 0;
    mutable int m_resultCatalogGeneration = -1, m_resultCatalogTimes = -1;
    mutable QVector<openswmmvis::io::Mesh2DResultVariable> m_resultCatalog;
    mutable QStringList m_resultCatalogWarnings;
    mutable quint64 m_resultCacheRevision = 0;
    mutable int m_resultCacheGeneration = -1, m_resultCacheTimes = -1;
    mutable size_t m_resultCacheBytes = 0;
    mutable QHash<QString, std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame>>
        m_resultFrames;
    mutable QHash<QString, std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame>>
        m_resultRanges;
    mutable QList<OpenSWMM::Render::ISublayer *> m_sublayerOrder;

public:
    // ----- ISublayerHost interface (Slice S5.6) -----------------------------
    //
    // Default sublayer mix (2026-06-21; depth color ramp + flow arrows removed):
    //   [0] MeshFillSublayer         (static — terrain hillshade base)
    //   [1] ContourBandSublayer      (dynamic — filled bands; the depth fill, default on)
    //   [2] MeshEdgeSublayer         (static — wireframe over results)
    //   [3] IsolineSublayer          (dynamic — marching-squares isolines; default off)
    //   [4] MeshNodeSublayer         (static — coupled-vertex markers)
    //   [5] VelocityVectorSublayer   (dynamic — RT0 arrow glyphs; also show direction)
    /**
     * @return List copy of borrowed layer-owned sublayers in bottom-up custom order, lazily seeded
     *   with nine builtin rows including particle strokes; dynamic scalar rows append later.
     *   Mutable const lazy initialization requires GUI-thread serialization.
     */
    [[nodiscard]] QList<OpenSWMM::Render::ISublayer *> sublayers() const override;

    /*! Reorder sublayers in paint order (bottom-up).  Emits
     *  repaintRequested() on success.  Returns false on out-of-range indices.
     * @param[in] from,to Zero-based positions in current bottom-up list.
     * @return False for invalid/equal positions, true after move and repaintRequested(); does not
     *   rebuild geometry or rule-list ordering.
     */
    bool moveSublayer(int from, int to) override;

    /*! Graduated ramp swatches for the legend dock. Mirrors
     *  SWMMResultsLayer::sublayerLegendItems for the 2D layer: emits
     *  one row per colour-ramp bin (depth), one per filled-contour band
     *  when enabled, plus single rows for isolines and velocity arrows.
     *  Each row carries the originating sublayerId so the legend's
     *  right-click → "Edit Sublayer Style…" path still works.
     * @return Owned visible depth-fill/band/isoline/particle/available-velocity and dynamic-scalar
     *   legend rows with stable sublayer IDs. Dynamic scalar statistics can synchronously read
     *   whole history; QSG-presented frames are preferred for matching pixels. Serialized GUI
     *   access.
     */
    [[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem> sublayerLegendItems() const;

    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::MeshFillSublayer *meshFillSublayer() const
    {
        return m_meshFillSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::MeshEdgeSublayer *meshEdgeSublayer() const
    {
        return m_meshEdgeSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::MeshNodeSublayer *meshNodeSublayer() const
    {
        return m_meshNodeSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::ContourBandSublayer *contourBandSublayer() const
    {
        return m_contourBandSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::CellDepthFillSublayer *cellDepthFillSublayer() const
    {
        return m_cellDepthFillSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::SmoothDepthFillSublayer *smoothDepthFillSublayer() const
    {
        return m_smoothDepthFillSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::IsolineSublayer *isolineSublayer() const
    {
        return m_isolineSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    [[nodiscard]] OpenSWMM::Render::VelocityVectorSublayer *velocityVectorSublayer() const
    {
        return m_velocityVectorSublayer;
    }
    /**
     * @return Borrowed QObject child created with layer; mutable style/visibility invalidates
     *   repaint, no ownership transfer. Do not delete or outlive layer.
     */
    OpenSWMM::Render::FlowStreakSublayer *flowStreakSublayer() const
    {
        return m_flowStreakSublayer;
    }
    /**
     * @param[in] view Finite valid scene viewport intersecting mesh bounds.
     * @param[in] pixelsPerSceneUnit Positive finite pixels per scene unit.
     * @return Shared immutable coherent native hydraulic capture, possibly cached, or null for
     *   disabled/invalid/incoherent/read/geometry/memory constraints. GUI-thread capture may read
     *   region/velocity synchronously and updates counters; no particle dispatch by capture alone.
     */
    std::shared_ptr<const OpenSWMM::Render::FlowParticleNativeInput>
    captureFlowParticleInput(const QRectF &view, double pixelsPerSceneUnit);
    /**
     * @param[in] view Finite valid scene viewport, remembered for future animation.
     * @param[in] pixelsPerSceneUnit Positive finite pixels per scene unit.
     * @param[in] displayedRevision Requested water frame revision; new capture occurs only when
     *   equal current layer revision.
     * @param[in] presented True declares revision presented; false prepares without presentation
     *   handoff.
     * @return Shared exact-revision particle frame or null while unavailable/disabled/invalid. May
     *   capture regional inputs and dispatch serial integration; caller must still ensure
     *   geometry/revision compatibility. GUI-thread only.
     */
    std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame>
    flowParticles(const QRectF &view, double pixelsPerSceneUnit, quint64 displayedRevision,
                  bool presented = true);
    /**
     * @param[in] input Shared candidate native capture moved to controller; null/wrong geometry
     *   needs no wait.
     * @return True for no compatible candidate or already matching retained particle frame; false
     *   while serial preparation is pending. Readiness/dispatch result, not general input
     *   validation.
     */
    bool prepareFlowParticlesForPresentation(
        std::shared_ptr<const OpenSWMM::Render::FlowParticleNativeInput> input)
    {
        return m_particleController->prepareForPresentation(std::move(input));
    }
    /**
     * @param[in] frame Optional immutable interval snapshot; null queries latest worker frame.
     * @return Clamped presentation fraction0..1, zero for absent/degenerate interval. GUI-thread
     *   delegation, no field fitting.
     */
    double flowParticleInterpolation(
        const std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> &frame = {}) const
    {
        return m_particleController->interpolation(frame);
    }
    /**
     * @return Initial reveal cubic fade multiplier0..1, or1when reveal disabled.
     */
    double flowParticleOpacity() const { return m_particleController->opacity(); }
    /**
     * @return Latest completed worker measurement copy, excluding separate layer native-capture
     *   counters.
     */
    OpenSWMM::Render::FlowParticleDiagnostics flowParticleDiagnostics() const
    {
        return m_particleController->diagnostics();
    }
    /**
     * @return Conservative native-field high-water byte estimate, initially 0; includes
     *   retained/replacement inputs/field/scratch, not actual RSS/GPU/particle total.
     */
    size_t flowParticleFieldBytes() const { return m_particleController->fieldBytes(); }
    /**
     * @return Cumulative successful new native input captures, excluding cached returns/failures.
     */
    int flowParticleFieldCaptures() const { return m_particleController->captures(); }
    /**
     * @return Cumulative compact hydraulic read attempts, including failures; reused published
     *   arrays/separate face velocity reads excluded.
     */
    int flowParticleFieldReads() const { return m_particleController->reads(); }
    /**
     * @param[in] view Scene-map visible coverage; invalid falls back full mesh bbox, then painter
     *   clip intersects it.
     * @note Required nonnull QPainter first argument, active stable world transform with positive
     *   scale. Fetches/schedules particle frame then paints interpolated gradient ribbons with
     *   screen-fixed width, trail/lifetime/reveal fades and optional dark outline. Saves/restores
     *   painter; GUI thread, no hydraulic invalidation.
      *
     * @param[in] p Required nonnull active GUI painter, borrowed for this call; world transform
     *   must have positive finite scale.
     */
    void paintFlowStreaks(QPainter *p, const QRectF &view);
    /**
     * @param[in] view Valid scene-map capture rectangle.
     * @param[in] pixelsPerSceneUnit Positive finite pixels per scene unit.
     * @return False if display fit remains pending after 3 settlement attempts or enabled
     *   intersecting particle frame still unavailable; true when disabled/outside or ready.
     * @note GUI-thread blocking capture barrier: requests regional coverage, waits worker fits
     *   without pumping application events, consumes completion coherently, requests/waits particle
     *   work. Individual waits have no timeout; counters/ranges/pixels may update through ordinary
     *   publication.
     */
    bool prepareFlowStreaksForCapture(const QRectF &view, double pixelsPerSceneUnit);
    /**
     * @param[in] seconds Positive finite GUI elapsed seconds; invalid/nonpositive or
     *   disabled/nonanimated/motionless state ignored.
     * @note May refresh remembered-view native support before forwarding time to serial worker.
     *   Does not advance simulation/report cursor or hydraulic revision. GUI thread only.
     */
    void advanceFlowStreaks(double seconds);
    /**
     * @return Serial particle worker active/pending flag; no off-thread synchronization guarantee.
     */
    bool flowStreakBusy() const { return m_particleController->busy(); }
    /**
     * @return Availability text when gated; otherwise optional migrated-motion note plus controller
     *   regional/worker/capture status.
     */
    QString flowStreakStatus() const;
    /**
     * @return Localized no-source/live-disabled/hidden-streak gate text, otherwise controller
     *   limits/preparation text; empty indicates no known blocking message, not guaranteed frame
     *   readiness.
     */
    QString flowStreakAvailability() const;
    /**
     * @return Whether layer/sublayer visible with positive opacities, frame+geometry+source present
     *   and live gate enabled. Does not require nonzero velocity or already published particle
     *   frame.
     */
    bool flowStreaksEnabled() const;
    /*! True only during the synchronous repaint for a particle-style edit.
     * @return True only during synchronous particle-style invalidation repaint emission, allowing
     *   hosts to preserve hydraulic geometry caches.
     */
    bool flowOnlyRepaint() const noexcept { return m_flowOnlyRepaint; }
    /**
 * @brief Particle worker publication became available; does not invalidate hydraulic geometry/frame
 *   revision.
 */
Q_SIGNALS:
    void flowStreaksChanged(); // particle publication only; never hydraulic invalidation
public:
    /*! Slice U-6 — surface the 5 sublayer style bags as styleable subjects
     *  for the unified LayerStyleDialog.
     * @return Owned editor wrappers borrowing every builtin/dynamic scalar sublayer style. Wrappers
     *   must not outlive the layer/removed sublayer; no source/frame ownership transfers.
     */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
    styleSubjects() override;

    // ----- Rule Model (Slice B.5b, Phase B) -------------------------------
    //
    // Five seed Rules covering the result-layer decoration archetypes
    // (Depth color ramp / Velocity vectors / Contour / Mesh edges /
    // Mesh nodes). Same lazy-build pattern as SWMM2DMeshLayer.
    /**
     * @return Borrowed owned seven-rule decoration model, lazily created on GUI thread even via
     *   const access. Seed specs are defaults rather than current style snapshots; renderer
     *   replacement forwards supported fields into legacy sublayers.
     */
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    /**
     * @return Borrowed owned seven-rule decoration model, lazily created on GUI thread even via
     *   const access. Seed specs are defaults rather than current style snapshots; renderer
     *   replacement forwards supported fields into legacy sublayers.
     */
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

private:
    /**
     * @brief Construct default decoration rules and connect first-SingleSymbol-layer replacement
     *   propagation to supported sublayer style fields.
     * @note Seven seeds include inert retired depth-ramp rule plus
     *   hillshade/bands/lines/edges/nodes/velocity. Wrong renderer/kind/empty layer is ignored.
     *   Ordinary sublayer changes do not reverse-sync seed specs; GUI-thread only.
     */
    void buildRuleListLazy() const;
    mutable std::unique_ptr<OpenSWMM::Render::RuleList> m_ruleList;
};

#endif // OPENSWMMVIS_LAYERS_SWMM2DRESULTSLAYER_H
