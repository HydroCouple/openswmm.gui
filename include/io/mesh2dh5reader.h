/**
 * @file      mesh2dh5reader.h
 * @brief     reads the CF-1.11 / UGRID-1.0 HDF5 file produced by the engine's
 *            `Default2DOutputPlugin` (openswmm.engine, src/engine/2d/output/).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice CF.MVP — reads the CF-1.11 / UGRID-1.0 HDF5 file produced by the
 * engine's `Default2DOutputPlugin` (openswmm.engine, src/engine/2d/output/).
 * The reader is used by `SWMM2DResultsLayer`'s post-run `HDF5Mesh2DSource`
 * to scrub a time slider back through inundation history.
 *
 * Layout consumed (per Default2DOutputPlugin.hpp §37-65):
 *   /Mesh2_node_x  [nNode]            vertex X coordinates
 *   /Mesh2_node_y  [nNode]            vertex Y coordinates
 *   /Mesh2_node_z  [nNode]            vertex elevations
 *   /Mesh2_face_nodes [nFace, 3|4]    cell connectivity (int); width 4 with
 *                                     attribute `_FillValue` (−1) and dataset
 *                                     /Mesh2_face_nv [nFace] (int8, 3|4) once
 *                                     the mesh holds a quadrilateral
 *   /time           [nTime]           SWMM absolute days since 1899-12-30
 *   /Mesh2_face_depth [nTime, nFace]  overland flow depth (m), per CELL
 *   /Mesh2_node_head  [nTime, nNode]  reconstructed vertex head (m; engine
 *                                     pseudo-Laplacian, VertexReconstruction —
 *                                     SOLVER field, no longer rendered)
 *   /Mesh2_node_depth [nTime, nNode]  SIGNED vertex depth η_v − z_v (m; engine
 *                                     wet-masked render reconstruction)
 *   /Mesh2_edge_flux  [nTime, nFace, 3|4] signed volumetric flux per edge (m^3 s^-1)
 *   /Mesh2_edge_length [nFace, 3|4]   edge length (m, CF.2 / new in engine 6.0+)
 *   /Mesh2_edge_nx    [nFace, 3|4]    edge outward unit normal x (CF.2)
 *   /Mesh2_edge_ny    [nFace, 3|4]    edge outward unit normal y (CF.2)
 *
 * Older files written before the engine's CF.2 step (which added the static
 * edge-geometry datasets) are tolerated: \ref openswmmvis::io::Mesh2DH5Reader::readEdgeGeometry transparently
 * reconstructs length / outward normal from \c /Mesh2_node_x / \c /Mesh2_node_y
 * + \c /Mesh2_face_nodes when the cached datasets are absent. The local-edge
 * convention matches the engine's \c MeshBuilder: edge \c e of an nv-gon runs
 * between vertices \c v[(e+1)%nv] and \c v[(e+2)%nv] (for a triangle: "edge
 * @c e is opposite vertex \c e").
 *
 * Mixed triangle/quad contract (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md
 * phase G0; engine plans/2D_TRI_QUAD_MESH_PLAN_2026-09-06.md §2.5):
 *   - The FILE's face index is the CELL index: every per-face dataset
 *     (depth, head, rainfall, …) and \ref openswmmvis::io::Mesh2DH5Reader::cellCount / \ref openswmmvis::io::Mesh2DH5Reader::triangleCount are
 *     per cell. \ref openswmmvis::io::Mesh2DH5Reader::readCells returns the cells verbatim (v3 = −1 for a
 *     triangle; `_FillValue` / negative entries and /Mesh2_face_nv are
 *     honoured as padding).
 *   - \ref openswmmvis::io::Mesh2DH5Reader::readTriangles returns the DISPLAY sub-triangle fan: one triangle
 *     per triangle cell, TWO per quad (split on the same Begnudelli–Sanders
 *     diagonal as the engine's storage model, via mesh::cellGeom — hence
 *     /Mesh2_node_z is consulted). \ref openswmmvis::io::Mesh2DH5Reader::triangleFaceMap maps each display
 *     triangle back to its cell (the identity for an all-triangle file);
 *     \ref openswmmvis::io::Mesh2DH5Reader::displayTriangleCount is its length. Consumers colour each display
 *     triangle with the CELL's value.
 *   - Per-edge arrays (\ref openswmmvis::io::Mesh2DH5Reader::readEdgeFluxAt, \ref openswmmvis::io::Mesh2DH5Reader::readEdgeGeometry) are ALWAYS
 *     returned with stride \ref openswmmvis::io::Mesh2DH5Reader::kEdgeStride (4) — `[cell * 4 + localEdge]`,
 *     the same layout as mesh::edgeSlot — regardless of the file's own
 *     width (\ref openswmmvis::io::Mesh2DH5Reader::edgeStride, 3 or 4): width-3 files are repacked on read,
 *     slot 3 of a triangle is 0. Consumers use ONE layout.
 *
 * The reader is intentionally Qt-light (QString only for the path); the
 * data interfaces use std::vector + std::array so the same wrapper can
 * be exercised from a non-Qt unit test.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_IO_MESH2DH5READER_H
#define OPENSWMMVIS_IO_MESH2DH5READER_H

#include "io/mesh2dhydraulicframe.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include "io/mesh2dresultvariable.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace openswmmvis::io
{

/*!
 * \brief How a 2D result source's XY coordinates relate to the model's CRS.
 *
 * The engine's 2D solver runs in SI, so every x/y it writes is in **metres**
 * — even when the model (and its `.2dm` mesh, and its `[OPTIONS] CRS`) is
 * authored in feet. Consumers must divide by \ref metresPerModelUnit before
 * reprojecting out of \ref crs, or the results land ~0.3048x toward the CRS
 * origin while the `.2dm`-backed mesh layer renders correctly (issue #155).
 *
 * Engine 6.0+ states this explicitly in the file's `/crs` variable. Older
 * files (and the live in-process source) declare nothing — \ref declared is
 * then false and the caller falls back to the layer CRS's own linear unit.
 */
struct CoordinateReference {
    QString crs;                     ///< model CRS, verbatim; empty if undeclared
    double metresPerModelUnit = 1.0; ///< stored = model x this
    QString storedUnits;             ///< unit of the stored coords, e.g. "m"
    bool declared = false;           ///< true when read from a `/crs` variable
};

class Mesh2DH5Reader
{
public:
    /**
     * @brief Create a closed reader without acquiring an HDF5 handle.
     * @details Each reader and its mutable caches belong to one thread. HDF5 calls across separate readers are serialized by a process-wide recursive mutex; borrowed references and cache-only queries do not provide cross-thread synchronization.
     */
    Mesh2DH5Reader();
    /**
     * @brief Close the owned file handle; cached value storage is then destroyed.
     */
    ~Mesh2DH5Reader();

    // Non-copyable (owns an HDF5 file handle).
    /**
     * @brief Prohibit copying the owned HDF5 handle and mutable read caches.
     */
    Mesh2DH5Reader(const Mesh2DH5Reader &) = delete;
    /**
     * @brief Prohibit assignment of file ownership and caches from another reader.
     */
    Mesh2DH5Reader &operator=(const Mesh2DH5Reader &) = delete;

    /*!
     * \brief Open the HDF5 file read-only.
     * \returns true on success. On failure, lastError() carries the message.
     *
     * The file may still be growing on disk (live mode). HDF5's SWMR is not
     * required because the engine flushes after each \c update() and our
     * \c timeCount() re-reads the time-dimension extent on every call.
     * @param[in] path HDF5 result file path, encoded as UTF-8 for HDF5.
     * @details Closes the previous file and clears all dimension/connectivity/presence caches and diagnostics before opening. On failure the reader is closed but path() retains the attempted path.
     */
    bool open(const QString &path);

    /*! \brief Close the file (idempotent; called automatically on destruction). */
    void close();

    /**
     * @brief Test whether an HDF5 file handle is owned.
     * @return True after a successful open() until close(); a cache-only query requiring owning-thread access.
     */
    bool isOpen() const noexcept { return file_id_ >= 0; }

    /*! \brief Current or last attempted open path (cleared by close()).
     * @return Borrowed path string, valid until the reader changes or is destroyed; a failed open retains its attempted path and close clears it.
     */
    const QString &path() const noexcept { return path_; }

    /*! \brief Last error message from a failing open/read call.
     * @return Borrowed latest stored diagnostic, valid until a subsequent error/open/destruction. Success does not generally clear a previous diagnostic; some false-returning probes do not set one.
     */
    const QString &lastError() const noexcept { return last_error_; }

    // ----- Mesh queries (one-shot; cached after first call) ----------------

    /*! \brief Edge slots per cell in every per-edge array this reader
     *  returns (== mesh::kEdgeStride; asserted in the .cpp). */
    static constexpr int kEdgeStride = 4;

    /*! \brief Number of mesh vertices (`/Mesh2_node_x` length).
     * @return Cached mesh count, or zero when the dimension cannot be read. The file must be open for uncached queries.
     */
    int vertexCount() const;
    /*! \brief Number of mesh CELLS (`/Mesh2_face_nodes` row count — the
     *  file's face count, triangles and quads alike). Historical name;
     *  identical to \ref cellCount. Per-face datasets have this length.
     * @return Cached mesh count, or zero when the dimension cannot be read. The file must be open for uncached queries.
     */
    int triangleCount() const;
    /*! \brief Number of mesh cells (== \ref triangleCount).
     * @return Cached mesh count, or zero when the dimension cannot be read. The file must be open for uncached queries.
     */
    int cellCount() const { return triangleCount(); }
    /*! \brief Number of DISPLAY triangles returned by \ref readTriangles
     *  (cells + quads; == cellCount() for an all-triangle file). 0 on error.
     * @return Cached display fan length after loading connectivity, or zero on load failure.
     */
    int displayTriangleCount() const;
    /*! \brief Width of the file's per-edge datasets (`/Mesh2_face_nodes`
     *  dim 1): 3 for an all-triangle file, 4 once any quad exists. The
     *  arrays this reader returns are always \ref kEdgeStride wide. 0 on error.
     * @return File connectivity width, or zero when it cannot be read; output arrays still use four slots per cell.
     */
    int edgeStride() const;

    /*! \brief Read vertex coordinates into \p vx,\p vy,\p vz. Resizes output.
     * @param[out] vx Vertex X coordinates in stored engine SI metres.
     * @param[out] vy Vertex Y coordinates in the same vertex order and units.
     * @param[out] vz Vertex bed elevations in metres; this dataset is required.
     * @return True after all three reads; false with a stored diagnostic on closed/missing data or IO failure. Outputs can be partially resized/filled on failure.
     */
    bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy,
                          std::vector<double> &vz) const;

    /*! \brief Read the cells verbatim. \p cells[c] = {v0,v1,v2,v3}, with
     *  v3 = −1 for a triangle (cyclic order for a quad). Indices are
     *  normalised to 0-based (`start_index`), padding is taken from
     *  `_FillValue` / negative entries / `/Mesh2_face_nv`.
     * @param[out] cells Copied zero-based connectivity, unchanged when cache loading fails.
     * @return True after validating and caching connectivity/display fan; false with a diagnostic on malformed layout or vertex indices.
     */
    bool readCells(std::vector<std::array<int, 4>> &cells) const;

    /*! \brief Read the DISPLAY sub-triangle fan. \p tris[i] = {v0,v1,v2};
     *  one entry per triangle cell, two per quad (mesh::cellGeom's diagonal),
     *  so `tris.size() == displayTriangleCount()`. Use \ref triangleFaceMap
     *  to look up the cell whose value display triangle i carries. For an
     *  all-triangle file this is exactly the file's connectivity.
     * @param[out] tris Copied display fan, unchanged when loading fails.
     * @return True on successful connectivity/geometry loading; false on malformed or unavailable mesh data.
     */
    bool readTriangles(std::vector<std::array<int, 3>> &tris) const;

    /*! \brief Display triangle → cell index (parallel to \ref readTriangles;
     *  the identity for an all-triangle file). Empty until \ref readTriangles
     *  / \ref displayTriangleCount / \ref readCells has run successfully.
     * @return Borrowed cached mapping, invalidated by open() or destruction; the method itself performs no loading or locking.
     */
    const std::vector<int> &triangleFaceMap() const { return cached_face_map_; }

    /*!
     * \brief Read the `/crs` georeferencing variable (engine 6.0+).
     *
     * Never fails on a file that predates `/crs`: \p out is left with
     * \c declared == false and \c metresPerModelUnit == 1.0, and \c storedUnits
     * is still filled from the `units` attribute on `/Mesh2_node_x` when
     * present. Callers treat undeclared files with the layer-CRS fallback.
     *
     * \returns true if a `/crs` variable was found; individual attributes may use defaults.
     * @param[out] out Reset to default on entry; then populated with available coordinate units and CRS metadata.
     * @details Presence of the crs dataset establishes declared=true even when individual attributes are absent. Invalid/nonpositive unit factors retain 1.0; callers must validate the CRS text for their operation.
     */
    bool readCoordinateReference(CoordinateReference &out) const;

    // ----- Time-series queries ---------------------------------------------

    /*!
     * \brief Current number of time steps written to \c /time.
     *
     * Re-reads the dataspace dims on every call so live-tailing works: as the
     * engine appends, subsequent calls return the new count.
     * @return Current time dimension narrowed to int, or zero on a dimension-read failure.
     */
    int timeCount() const;

    /*! \brief Read all time values. Resizes output.
     * @param[out] times Output SWMM absolute days since 1899-12-30.
     * @return False on a closed reader or failed dataset read; zero readable/failed dimension count produces an empty output and true. Outputs may be resized before failure.
     */
    bool readTimes(std::vector<double> &times) const;

    /*!
     * \brief Read one time slice of \c /Mesh2_face_depth.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param depths  Output, resized to triangleCount(). Values in metres.
     * \returns true on success.
     */
    bool readDepthsAt(int timeIdx, std::vector<float> &depths) const;

    // Sorted unique native IDs, bounded by Mesh2DHydraulicFrame::maxCells.
    // Output is compact and cleared on failure. Optional flux stays empty if absent.
    /**
     * @brief Read compact depths and optional edge flux for a bounded native-cell selection.
     * @param[in] timeIdx Zero-based available report frame.
     * @param[in] cells Strictly increasing distinct IDs below cellCount(), bounded by Mesh2DHydraulicFrame::maxCells; empty is allowed.
     * @param[out] depths Metres in selection order, cleared on failure.
     * @param[out] flux Outward-positive cubic metres per second, four slots per selected cell; empty when the dataset is absent or the read fails.
     * @param[out] stats Optional selection/layout counters reset on entry; may contain partial diagnostics on failure.
     * @return True when depth and any present flux dataset were read, false on invalid selection or read/metadata failure. Missing/unallocated values become NaN.
     */
    bool readHydraulicCellsAt(int timeIdx, const std::vector<int> &cells,
                              std::vector<float> &depths, std::vector<float> &flux,
                              Mesh2DSubsetReadStats *stats = nullptr) const;
    /**
     * @brief Read a single native-cell depth without materializing a whole frame.
     * @param[in] timeIdx Zero-based available report frame.
     * @param[in] cell Zero-based native cell index.
     * @param[out] depth Metres, initialized to NaN before validation; may remain NaN for an unavailable value even on success.
     * @return False on invalid indices or dataset/read failure; invalid-index rejection need not set lastError().
     */
    bool readDepthAt(int timeIdx, int cell, float &depth) const;

    /*!
     * \brief Read one time slice of any per-face \c [nTime, nFace] dataset
     *        (e.g. \c Mesh2_face_rainfall, \c Mesh2_face_rain_cum).
     * \param dataset Dataset name at the file root.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param values  Output, resized to triangleCount(); engine SI units.
     * \returns true on success; false when the dataset is absent, malformed, or unreadable.
     * @details Decodes numeric scale/offset and missing/fill metadata, marking nonfinite, unallocated, or float-unrepresentable values NaN. Output is empty on failure; no semantic waterless mask is applied.
     */
    bool readFaceFieldAt(const char *dataset, int timeIdx, std::vector<float> &values) const;
    /**
     * @brief Read a compact subset of a rank-two face field.
     * @param[in] dataset Non-null root dataset name; presence is cached for this subset API.
     * @param[in] timeIdx Available zero-based report frame.
     * @param[in] cells Strictly increasing bounded native-cell IDs, as required by validHydraulicCells().
     * @param[out] values Decoded stored-unit values in selection order; empty on failure, NaN for missing/unallocated values.
     * @return True on a successful read, false on invalid selection, absent data, or read failure; early rejection may leave lastError() unchanged.
     */
    bool readFaceFieldCellsAt(const char *dataset, int timeIdx, const std::vector<int> &cells,
                              std::vector<float> &values) const;

    /*! \brief True iff \p dataset exists at the file root (probe cached).
     * @param[in] dataset Root dataset/link name; null returns false.
     * @return Cached link existence, false for a closed reader; does not verify numeric type, rank, or a usable frame. Newly added datasets require reopening to invalidate an absent cache entry.
     */
    bool hasFaceField(const char *dataset) const;

    /*!
     * \brief Read a time-INVARIANT per-face \c [nFace] dataset — the engine's
     *        run envelopes (\c Mesh2_face_max_depth, \c Mesh2_face_max_velocity,
     *        \c Mesh2_face_max_continuity_err), written under the
     *        \c REPORT_2D_VARIABLES group \c ENVELOPES and overwritten in place
     *        each output step, so the values are whole-run maxima.
     * \param dataset Dataset name at the file root.
     * \param values  Output, resized to triangleCount(); engine SI units.
     * \returns true on success; false when the file lacks the dataset or when it is not rank 1 of
     *          length \c triangleCount() — \ref readFaceFieldAt handles the
     *          rank-2 \c [nTime, nFace] fields instead.
     * @details Output is empty on failure; scale/offset and missing metadata are decoded, and no water-state classification is applied.
     */
    bool readFaceEnvelope(const char *dataset, std::vector<float> &values) const;

    /*! Available per-cell scalar/species/sigma variables, with file-declared
     * identities and units. Unsupported/malformed metadata is reported through
     * warnings and never assigned a guessed species name or concentration unit.
     * @brief Discover supported root face variables from their current shapes and metadata.
     * @param[out] warnings Optional list cleared on entry, then populated with rejected metadata or unresolved-unit messages.
     * @return Owning catalog of source-local descriptors; empty for a closed/unreadable file or no supported variables. Does not read complete result histories.
     */
    QVector<Mesh2DResultVariable> faceVariables(QStringList *warnings = nullptr) const;

    /*! Read one requested variable/frame only. Resolves species by name in
     * the currently open file, so descriptors survive reordered output subsets.
     * On failure both outputs are empty; lastError explains the unavailable
     * field/frame. Static/envelope fields ignore timeIdx.
     * @brief Read and classify one current semantic variable slice.
     * @param[in] variable Descriptor resolved by key and zone against current file metadata, including species identity rather than a persisted species index.
     * @param[in] timeIdx Available report frame, ignored for static/envelope fields.
     * @param[out] values Native declared-unit cell values after scale/offset; Missing becomes NaN, other status values retain raw numbers.
     * @param[out] status One availability classification per cell; both outputs are empty on failure.
     * @return True when the slice and required masks are readable; false with lastError() when identity, frame, metadata, or a required mask cannot be resolved.
     * @details Applies available depth/water-state masks to distinguish Waterless and NotApplicable from numeric zero; unsupported metadata is never guessed.
     */
    bool readFaceVariableAt(const Mesh2DResultVariable &variable, int timeIdx,
                            std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) const;

    /*!
     * \brief Read one time slice of \c /Mesh2_node_head — the engine's
     *        pseudo-Laplacian vertex-head reconstruction.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param heads   Output, resized to vertexCount(). Values in metres,
     *                read as double — heads carry the elevation datum, and a
     *                float ulp at high z exceeds the dry-depth threshold.
     * \returns true on success; false (without HDF5 error spam — presence is
     *          probed once via H5Lexists and cached) when the file predates
     *          the dataset.
     */
    bool readVertexHeadsAt(int timeIdx, std::vector<double> &heads) const;

    /*!
     * \brief Read one time slice of \c /Mesh2_node_depth — the engine's
     *        wet-masked, depth-weighted SIGNED vertex depth (η_v − z_v; the
     *        render field: dry-cell bed elevations never contribute, negative
     *        over the dry side of partially wet cells).
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param depths  Output, resized to vertexCount(). Metres; float is ample
     *                because the datum is subtracted engine-side in double.
     * \returns true on success; false (presence probed once via H5Lexists and
     *          cached) when the file predates the dataset — callers then fall
     *          back to the GUI-side wet-only reconstruction.
     */
    bool readVertexSignedDepthsAt(int timeIdx, std::vector<float> &depths) const;

    /*!
     * \brief Read one time slice of \c /Mesh2_edge_flux.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param flux    Output, resized to \c cellCount()*kEdgeStride, indexed
     *                \c [cell*kEdgeStride + localEdge] (mesh::edgeSlot)
     *                whatever the file's own width (\ref edgeStride); slot 3
     *                of a triangle is 0. Units m³ s⁻¹; sign convention
     *                positive = outward through the edge's outward normal.
     * \returns true on success; false (with \c lastError set) if the file
     *          does not carry the dataset.
     */
    bool readEdgeFluxAt(int timeIdx, std::vector<float> &flux) const;

    /*!
     * \brief Read time-invariant edge length + outward unit normal.
     *
     * Prefers the \c /Mesh2_edge_length, \c /Mesh2_edge_nx, \c /Mesh2_edge_ny
     * datasets written by engine 6.0+ (CF.2). When any of them is absent
     * (older \c .h5 files), falls back to recomputing on the fly from
     * \c /Mesh2_node_x, \c /Mesh2_node_y, and \c /Mesh2_face_nodes using
     * the same convention as the engine's \c MeshBuilder (edge \c e joins
     * \c v[(e+1)%nv] and \c v[(e+2)%nv]; outward normal flipped if needed so
     * it points away from the centroid).
     *
     * \param length  Output, resized to \c cellCount()*kEdgeStride
     *                (\c [cell*kEdgeStride + localEdge], file width repacked).
     * \param nx,ny   Output, same layout.
     * \returns true on success.
     * @details Lengths are in metres and normals are dimensionless. Triangle padding slots are zero. Output vectors may be partially populated when an error is reported.
     */
    bool readEdgeGeometry(std::vector<float> &length, std::vector<float> &nx,
                          std::vector<float> &ny) const;

private:
    QString path_;
    int64_t file_id_ = -1; // hid_t under the hood (stored as int64_t
                           // to avoid including <hdf5.h> from this header)
    mutable int cached_n_vert_ = -1;
    mutable int cached_n_face_ = -1;
    mutable int cached_face_width_ = -1;     ///< /Mesh2_face_nodes dim 1 (3|4); -1 unknown
    mutable int cached_has_node_head_ = -1;  ///< -1 unknown, 0 absent, 1 present
    mutable int cached_has_node_depth_ = -1; ///< -1 unknown, 0 absent, 1 present
    mutable int cached_has_edge_flux_ =
        -1; ///< -1 unknown, 0 absent, 1 present (REPORT_2D_VARIABLES may drop EDGE_FLUX)
    mutable std::map<std::string, bool> cached_has_face_field_; ///< per-dataset presence
    mutable QString last_error_;
    // Connectivity, loaded once by loadCells_(): the file's cells, the
    // display sub-triangle fan and its display-triangle → cell map.
    mutable bool cached_cells_loaded_ = false;
    mutable std::vector<std::array<int, 4>> cached_cells_;
    mutable std::vector<std::array<int, 3>> cached_display_tris_;
    mutable std::vector<int> cached_face_map_;

    /**
     * @brief Read one dataset extent into the cached integer-count representation.
     * @param[in] dataset Non-null dataset path in the open file.
     * @param[in] axis Nonnegative dimension index, strictly below dataset rank.
     * @param[out] out Dimension extent narrowed to int after a successful dataspace query; unchanged on rejected input/open failure.
     * @return True on success, otherwise store a diagnostic and return false.
     * @pre Caller holds the process HDF5 mutex.
     */
    bool readDim_(const char *dataset, int axis, int &out) const;
    /**
     * @brief Store a read failure for compact error-return expressions.
     * @param[in] msg Diagnostic replacing the previous one.
     * @return Always false.
     */
    bool setError_(const QString &msg) const;
    /**
     * @brief Validate/cache native connectivity, display triangles, and the display-to-cell map.
     * @return True for an existing cache or successful load; false with a diagnostic for invalid face width/counts/indexing or required geometry failure.
     * @details Honors declared start_index and padding; absent start_index is inferred as one-based only when its observed range is exactly 1..vertexCount(). Caller holds the HDF5 mutex.
     */
    bool loadCells_() const;
};

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_MESH2DH5READER_H
