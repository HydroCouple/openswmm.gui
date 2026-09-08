/*!
 * \file   mesh2dh5reader.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 *   /time           [nTime]           seconds since simulation start
 *   /Mesh2_face_depth [nTime, nFace]  overland flow depth (m), per CELL
 *   /Mesh2_node_head  [nTime, nNode]  reconstructed vertex head (m; engine
 *                                     pseudo-Laplacian, VertexReconstruction —
 *                                     SOLVER field, no longer rendered)
 *   /Mesh2_node_depth [nTime, nNode]  SIGNED vertex depth η_v − z_v (m; engine
 *                                     wet-masked render reconstruction)
 *   /Mesh2_edge_flux  [nTime, nFace, 3|4] signed normal flux per edge (m^2 s^-1)
 *   /Mesh2_edge_length [nFace, 3|4]   edge length (m, CF.2 / new in engine 6.0+)
 *   /Mesh2_edge_nx    [nFace, 3|4]    edge outward unit normal x (CF.2)
 *   /Mesh2_edge_ny    [nFace, 3|4]    edge outward unit normal y (CF.2)
 *
 * Older files written before the engine's CF.2 step (which added the static
 * edge-geometry datasets) are tolerated: \ref readEdgeGeometry transparently
 * reconstructs length / outward normal from \c /Mesh2_node_x / \c /Mesh2_node_y
 * + \c /Mesh2_face_nodes when the cached datasets are absent. The local-edge
 * convention matches the engine's \c MeshBuilder: edge \c e of an nv-gon runs
 * between vertices \c v[(e+1)%nv] and \c v[(e+2)%nv] (for a triangle: "edge
 * \c e is opposite vertex \c e").
 *
 * Mixed triangle/quad contract (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md
 * phase G0; engine plans/2D_TRI_QUAD_MESH_PLAN_2026-09-06.md §2.5):
 *   - The FILE's face index is the CELL index: every per-face dataset
 *     (depth, head, rainfall, …) and \ref cellCount / \ref triangleCount are
 *     per cell. \ref readCells returns the cells verbatim (v3 = −1 for a
 *     triangle; `_FillValue` / negative entries and /Mesh2_face_nv are
 *     honoured as padding).
 *   - \ref readTriangles returns the DISPLAY sub-triangle fan: one triangle
 *     per triangle cell, TWO per quad (split on the same Begnudelli–Sanders
 *     diagonal as the engine's storage model, via mesh::cellGeom — hence
 *     /Mesh2_node_z is consulted). \ref triangleFaceMap maps each display
 *     triangle back to its cell (the identity for an all-triangle file);
 *     \ref displayTriangleCount is its length. Consumers colour each display
 *     triangle with the CELL's value.
 *   - Per-edge arrays (\ref readEdgeFluxAt, \ref readEdgeGeometry) are ALWAYS
 *     returned with stride \ref kEdgeStride (4) — `[cell * 4 + localEdge]`,
 *     the same layout as mesh::edgeSlot — regardless of the file's own
 *     width (\ref edgeStride, 3 or 4): width-3 files are repacked on read,
 *     slot 3 of a triangle is 0. Consumers use ONE layout.
 *
 * The reader is intentionally Qt-light (QString only for the path); the
 * data interfaces use std::vector + std::array so the same wrapper can
 * be exercised from a non-Qt unit test.
 */
#ifndef OPENSWMMVIS_IO_MESH2DH5READER_H
#define OPENSWMMVIS_IO_MESH2DH5READER_H

#include <QString>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace openswmmvis::io {

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
struct CoordinateReference
{
    QString crs;                        ///< model CRS, verbatim; empty if undeclared
    double  metresPerModelUnit = 1.0;   ///< stored = model x this
    QString storedUnits;                ///< unit of the stored coords, e.g. "m"
    bool    declared = false;           ///< true when read from a `/crs` variable
};

class Mesh2DH5Reader
{
public:
    Mesh2DH5Reader();
    ~Mesh2DH5Reader();

    // Non-copyable (owns an HDF5 file handle).
    Mesh2DH5Reader(const Mesh2DH5Reader&) = delete;
    Mesh2DH5Reader& operator=(const Mesh2DH5Reader&) = delete;

    /*!
     * \brief Open the HDF5 file read-only.
     * \returns true on success. On failure, lastError() carries the message.
     *
     * The file may still be growing on disk (live mode). HDF5's SWMR is not
     * required because the engine flushes after each \c update() and our
     * \c timeCount() re-reads the time-dimension extent on every call.
     */
    bool open(const QString& path);

    /*! \brief Close the file (idempotent; called automatically on destruction). */
    void close();

    bool isOpen() const noexcept { return file_id_ >= 0; }

    /*! \brief Current path (empty if not open). */
    const QString& path() const noexcept { return path_; }

    /*! \brief Last error message from a failing open/read call. */
    const QString& lastError() const noexcept { return last_error_; }

    // ----- Mesh queries (one-shot; cached after first call) ----------------

    /*! \brief Edge slots per cell in every per-edge array this reader
     *  returns (== mesh::kEdgeStride; asserted in the .cpp). */
    static constexpr int kEdgeStride = 4;

    /*! \brief Number of mesh vertices (`/Mesh2_node_x` length). */
    int vertexCount() const;
    /*! \brief Number of mesh CELLS (`/Mesh2_face_nodes` row count — the
     *  file's face count, triangles and quads alike). Historical name;
     *  identical to \ref cellCount. Per-face datasets have this length. */
    int triangleCount() const;
    /*! \brief Number of mesh cells (== \ref triangleCount). */
    int cellCount() const { return triangleCount(); }
    /*! \brief Number of DISPLAY triangles returned by \ref readTriangles
     *  (cells + quads; == cellCount() for an all-triangle file). 0 on error. */
    int displayTriangleCount() const;
    /*! \brief Width of the file's per-edge datasets (`/Mesh2_face_nodes`
     *  dim 1): 3 for an all-triangle file, 4 once any quad exists. The
     *  arrays this reader returns are always \ref kEdgeStride wide. 0 on error. */
    int edgeStride() const;

    /*! \brief Read vertex coordinates into \p vx,\p vy,\p vz. Resizes output. */
    bool readMeshGeometry(std::vector<double>& vx,
                          std::vector<double>& vy,
                          std::vector<double>& vz) const;

    /*! \brief Read the cells verbatim. \p cells[c] = {v0,v1,v2,v3}, with
     *  v3 = −1 for a triangle (cyclic order for a quad). Indices are
     *  normalised to 0-based (`start_index`), padding is taken from
     *  `_FillValue` / negative entries / `/Mesh2_face_nv`. */
    bool readCells(std::vector<std::array<int, 4>>& cells) const;

    /*! \brief Read the DISPLAY sub-triangle fan. \p tris[i] = {v0,v1,v2};
     *  one entry per triangle cell, two per quad (mesh::cellGeom's diagonal),
     *  so `tris.size() == displayTriangleCount()`. Use \ref triangleFaceMap
     *  to look up the cell whose value display triangle i carries. For an
     *  all-triangle file this is exactly the file's connectivity. */
    bool readTriangles(std::vector<std::array<int, 3>>& tris) const;

    /*! \brief Display triangle → cell index (parallel to \ref readTriangles;
     *  the identity for an all-triangle file). Empty until \ref readTriangles
     *  / \ref displayTriangleCount / \ref readCells has run successfully. */
    const std::vector<int>& triangleFaceMap() const { return cached_face_map_; }

    /*!
     * \brief Read the `/crs` georeferencing variable (engine 6.0+).
     *
     * Never fails on a file that predates `/crs`: \p out is left with
     * \c declared == false and \c metresPerModelUnit == 1.0, and \c storedUnits
     * is still filled from the `units` attribute on `/Mesh2_node_x` when
     * present. Callers treat undeclared files with the layer-CRS fallback.
     *
     * \returns true if a `/crs` variable was found and parsed.
     */
    bool readCoordinateReference(CoordinateReference& out) const;

    // ----- Time-series queries ---------------------------------------------

    /*!
     * \brief Current number of time steps written to \c /time.
     *
     * Re-reads the dataspace dims on every call so live-tailing works: as the
     * engine appends, subsequent calls return the new count.
     */
    int timeCount() const;

    /*! \brief Read all time values. Resizes output. */
    bool readTimes(std::vector<double>& times) const;

    /*!
     * \brief Read one time slice of \c /Mesh2_face_depth.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param depths  Output, resized to triangleCount(). Values in metres.
     * \returns true on success.
     */
    bool readDepthsAt(int timeIdx, std::vector<float>& depths) const;

    /*!
     * \brief Read one time slice of any per-face \c [nTime, nFace] dataset
     *        (e.g. \c Mesh2_face_rainfall, \c Mesh2_face_rain_cum).
     * \param dataset Dataset name at the file root.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param values  Output, resized to triangleCount(); engine SI units.
     * \returns true on success; false (presence probed once per name and
     *          cached, no HDF5 error spam) when the file lacks the dataset.
     */
    bool readFaceFieldAt(const char* dataset, int timeIdx,
                         std::vector<float>& values) const;

    /*! \brief True iff \p dataset exists at the file root (probe cached). */
    bool hasFaceField(const char* dataset) const;

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
    bool readVertexHeadsAt(int timeIdx, std::vector<double>& heads) const;

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
    bool readVertexSignedDepthsAt(int timeIdx, std::vector<float>& depths) const;

    /*!
     * \brief Read one time slice of \c /Mesh2_edge_flux.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param flux    Output, resized to \c cellCount()*kEdgeStride, indexed
     *                \c [cell*kEdgeStride + localEdge] (mesh::edgeSlot)
     *                whatever the file's own width (\ref edgeStride); slot 3
     *                of a triangle is 0. Units m² s⁻¹; sign convention
     *                positive = outward through the edge's outward normal.
     * \returns true on success; false (with \c lastError set) if the file
     *          does not carry the dataset.
     */
    bool readEdgeFluxAt(int timeIdx, std::vector<float>& flux) const;

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
     */
    bool readEdgeGeometry(std::vector<float>& length,
                          std::vector<float>& nx,
                          std::vector<float>& ny) const;

private:
    QString    path_;
    int64_t    file_id_ = -1;    // hid_t under the hood (stored as int64_t
                                 // to avoid including <hdf5.h> from this header)
    mutable int   cached_n_vert_ = -1;
    mutable int   cached_n_face_ = -1;
    mutable int   cached_face_width_ = -1;     ///< /Mesh2_face_nodes dim 1 (3|4); -1 unknown
    mutable int   cached_has_node_head_ = -1;  ///< -1 unknown, 0 absent, 1 present
    mutable int   cached_has_node_depth_ = -1; ///< -1 unknown, 0 absent, 1 present
    mutable int   cached_has_edge_flux_ = -1;  ///< -1 unknown, 0 absent, 1 present (REPORT_2D_VARIABLES may drop EDGE_FLUX)
    mutable std::map<std::string, bool> cached_has_face_field_; ///< per-dataset presence
    mutable QString last_error_;
    // Connectivity, loaded once by loadCells_(): the file's cells, the
    // display sub-triangle fan and its display-triangle → cell map.
    mutable bool cached_cells_loaded_ = false;
    mutable std::vector<std::array<int, 4>> cached_cells_;
    mutable std::vector<std::array<int, 3>> cached_display_tris_;
    mutable std::vector<int>                cached_face_map_;

    bool readDim_(const char* dataset, int axis, int& out) const;
    bool setError_(const QString& msg) const;
    bool loadCells_() const;
};

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_MESH2DH5READER_H
