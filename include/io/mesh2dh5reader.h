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
 *   /Mesh2_face_nodes [nFace, 3]      triangle connectivity (int)
 *   /time           [nTime]           seconds since simulation start
 *   /Mesh2_face_depth [nTime, nFace]  overland flow depth (m)
 *   /Mesh2_node_head  [nTime, nNode]  reconstructed vertex head (m; engine
 *                                     pseudo-Laplacian, VertexReconstruction)
 *   /Mesh2_edge_flux  [nTime, nFace, 3] signed normal flux per edge (m^2 s^-1)
 *   /Mesh2_edge_length [nFace, 3]     edge length (m, CF.2 / new in engine 6.0+)
 *   /Mesh2_edge_nx    [nFace, 3]      edge outward unit normal x (CF.2)
 *   /Mesh2_edge_ny    [nFace, 3]      edge outward unit normal y (CF.2)
 *
 * Older files written before the engine's CF.2 step (which added the static
 * edge-geometry datasets) are tolerated: \ref readEdgeGeometry transparently
 * reconstructs length / outward normal from \c /Mesh2_node_x / \c /Mesh2_node_y
 * + \c /Mesh2_face_nodes when the cached datasets are absent. The local-edge
 * convention matches the engine's \c MeshBuilder: edge \c e is opposite
 * vertex \c e (i.e. between vertices \c v[(e+1)%3] and \c v[(e+2)%3]).
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
#include <vector>

namespace openswmmvis::io {

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

    /*! \brief Number of mesh vertices (`/Mesh2_node_x` length). */
    int vertexCount() const;
    /*! \brief Number of mesh triangles (`/Mesh2_face_nodes` row count). */
    int triangleCount() const;

    /*! \brief Read vertex coordinates into \p vx,\p vy,\p vz. Resizes output. */
    bool readMeshGeometry(std::vector<double>& vx,
                          std::vector<double>& vy,
                          std::vector<double>& vz) const;

    /*! \brief Read triangle connectivity. \p tris[i] = {v0,v1,v2}. */
    bool readTriangles(std::vector<std::array<int, 3>>& tris) const;

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
     * \brief Read one time slice of \c /Mesh2_edge_flux.
     * \param timeIdx 0-based time index (must be < timeCount()).
     * \param flux    Output, resized to \c triangleCount()*3, indexed
     *                \c [tri*3 + localEdge]. Units m² s⁻¹; sign convention
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
     * the same convention as the engine's \c MeshBuilder (edge \c e is
     * opposite vertex \c e; outward normal flipped if needed so it points
     * away from the centroid).
     *
     * \param length  Output, resized to \c triangleCount()*3.
     * \param nx,ny   Output, resized to \c triangleCount()*3.
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
    mutable int   cached_has_node_head_ = -1;  ///< -1 unknown, 0 absent, 1 present
    mutable QString last_error_;

    bool readDim_(const char* dataset, int axis, int& out) const;
    bool setError_(const QString& msg) const;
};

} // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_MESH2DH5READER_H
