/*!
 * \file   mesh2dh5reader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "io/mesh2dh5reader.h"

#include <hdf5.h>

#include <cmath>

namespace openswmmvis::io {

namespace {

// Convenience RAII guards so failures inside readers don't leak handles.
struct DataSpaceGuard {
    hid_t id;
    explicit DataSpaceGuard(hid_t i) : id(i) {}
    ~DataSpaceGuard() { if (id >= 0) H5Sclose(id); }
};
struct DataSetGuard {
    hid_t id;
    explicit DataSetGuard(hid_t i) : id(i) {}
    ~DataSetGuard() { if (id >= 0) H5Dclose(id); }
};

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Mesh2DH5Reader::Mesh2DH5Reader() = default;

Mesh2DH5Reader::~Mesh2DH5Reader() { close(); }

bool Mesh2DH5Reader::open(const QString& path)
{
    close();
    path_ = path;
    cached_n_vert_ = -1;
    cached_n_face_ = -1;
    last_error_.clear();

    const QByteArray utf8 = path.toUtf8();
    hid_t fid = H5Fopen(utf8.constData(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (fid < 0) {
        return setError_(QStringLiteral("H5Fopen failed: %1").arg(path));
    }
    file_id_ = static_cast<int64_t>(fid);
    return true;
}

void Mesh2DH5Reader::close()
{
    if (file_id_ >= 0) {
        H5Fclose(static_cast<hid_t>(file_id_));
        file_id_ = -1;
    }
    path_.clear();
}

bool Mesh2DH5Reader::setError_(const QString& msg) const
{
    last_error_ = msg;
    return false;
}

// ---------------------------------------------------------------------------
// Mesh queries
// ---------------------------------------------------------------------------

bool Mesh2DH5Reader::readDim_(const char* dataset, int axis, int& out) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_), dataset, H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: %1").arg(dataset));
    DataSetGuard ds_guard(ds);

    hid_t sp = H5Dget_space(ds);
    if (sp < 0)
        return setError_(QStringLiteral("H5Dget_space failed: %1").arg(dataset));
    DataSpaceGuard sp_guard(sp);

    const int rank = H5Sget_simple_extent_ndims(sp);
    if (rank <= axis)
        return setError_(QStringLiteral("Dataset %1 has rank %2, expected > %3")
                          .arg(dataset).arg(rank).arg(axis));

    std::vector<hsize_t> dims(rank);
    H5Sget_simple_extent_dims(sp, dims.data(), nullptr);
    out = static_cast<int>(dims[axis]);
    return true;
}

int Mesh2DH5Reader::vertexCount() const
{
    if (cached_n_vert_ < 0)
        readDim_("Mesh2_node_x", 0, cached_n_vert_);
    return cached_n_vert_ < 0 ? 0 : cached_n_vert_;
}

int Mesh2DH5Reader::triangleCount() const
{
    if (cached_n_face_ < 0)
        readDim_("Mesh2_face_nodes", 0, cached_n_face_);
    return cached_n_face_ < 0 ? 0 : cached_n_face_;
}

bool Mesh2DH5Reader::readMeshGeometry(std::vector<double>& vx,
                                       std::vector<double>& vy,
                                       std::vector<double>& vz) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));

    const int n = vertexCount();
    if (n <= 0)
        return setError_(QStringLiteral("No vertices"));

    vx.assign(n, 0.0);
    vy.assign(n, 0.0);
    vz.assign(n, 0.0);

    auto readVec = [this, n](const char* name, double* out) -> bool {
        hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_), name, H5P_DEFAULT);
        if (ds < 0)
            return setError_(QStringLiteral("H5Dopen2 failed: %1").arg(name));
        DataSetGuard g(ds);
        herr_t r = H5Dread(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                            H5P_DEFAULT, out);
        return r >= 0 || setError_(QStringLiteral("H5Dread failed: %1").arg(name));
    };

    if (!readVec("Mesh2_node_x", vx.data())) return false;
    if (!readVec("Mesh2_node_y", vy.data())) return false;
    if (!readVec("Mesh2_node_z", vz.data())) return false;
    return true;
}

bool Mesh2DH5Reader::readTriangles(std::vector<std::array<int, 3>>& tris) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));

    const int n = triangleCount();
    if (n <= 0)
        return setError_(QStringLiteral("No triangles"));

    // /Mesh2_face_nodes is [n, 3] of native int, row-major
    tris.assign(n, {0, 0, 0});

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_),
                         "Mesh2_face_nodes", H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: Mesh2_face_nodes"));
    DataSetGuard g(ds);

    static_assert(sizeof(std::array<int, 3>) == 3 * sizeof(int),
                  "std::array<int,3> must be tightly packed");
    herr_t r = H5Dread(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL,
                       H5P_DEFAULT, tris.data());
    if (r < 0)
        return setError_(QStringLiteral("H5Dread failed: Mesh2_face_nodes"));
    return true;
}

// ---------------------------------------------------------------------------
// Time-series queries
// ---------------------------------------------------------------------------

int Mesh2DH5Reader::timeCount() const
{
    // Re-read every call so live-tail works as engine appends.
    int n = 0;
    readDim_("time", 0, n);
    return n;
}

bool Mesh2DH5Reader::readTimes(std::vector<double>& times) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));

    const int n = timeCount();
    times.assign(n, 0.0);
    if (n == 0) return true;

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_), "time", H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: time"));
    DataSetGuard g(ds);

    herr_t r = H5Dread(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                        H5P_DEFAULT, times.data());
    return r >= 0 || setError_(QStringLiteral("H5Dread failed: time"));
}

bool Mesh2DH5Reader::readDepthsAt(int timeIdx, std::vector<float>& depths) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));
    if (timeIdx < 0)
        return setError_(QStringLiteral("Negative timeIdx"));

    const int n_face = triangleCount();
    const int n_time = timeCount();
    if (timeIdx >= n_time)
        return setError_(QStringLiteral("timeIdx %1 >= n_time %2")
                          .arg(timeIdx).arg(n_time));

    depths.assign(n_face, 0.0f);
    if (n_face == 0) return true;

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_),
                         "Mesh2_face_depth", H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: Mesh2_face_depth"));
    DataSetGuard g(ds);

    hid_t fsp = H5Dget_space(ds);
    if (fsp < 0)
        return setError_(QStringLiteral("H5Dget_space failed"));
    DataSpaceGuard fg(fsp);

    // Hyperslab: { offset=[timeIdx, 0], count=[1, n_face] }
    const hsize_t offset[2] = { static_cast<hsize_t>(timeIdx), 0 };
    const hsize_t count[2]  = { 1, static_cast<hsize_t>(n_face) };
    if (H5Sselect_hyperslab(fsp, H5S_SELECT_SET, offset, nullptr,
                              count, nullptr) < 0)
        return setError_(QStringLiteral("H5Sselect_hyperslab failed"));

    const hsize_t mdims[1] = { static_cast<hsize_t>(n_face) };
    hid_t msp = H5Screate_simple(1, mdims, nullptr);
    if (msp < 0)
        return setError_(QStringLiteral("H5Screate_simple failed"));
    DataSpaceGuard mg(msp);

    // Read as float for downstream RGB packing — engine writes double, HDF5
    // does the type conversion automatically.
    herr_t r = H5Dread(ds, H5T_NATIVE_FLOAT, msp, fsp, H5P_DEFAULT,
                       depths.data());
    return r >= 0 || setError_(QStringLiteral("H5Dread depth slice failed"));
}

bool Mesh2DH5Reader::readEdgeFluxAt(int timeIdx, std::vector<float>& flux) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));
    if (timeIdx < 0)
        return setError_(QStringLiteral("Negative timeIdx"));

    const int n_face = triangleCount();
    const int n_time = timeCount();
    if (timeIdx >= n_time)
        return setError_(QStringLiteral("timeIdx %1 >= n_time %2")
                          .arg(timeIdx).arg(n_time));

    flux.assign(static_cast<size_t>(n_face) * 3, 0.0f);
    if (n_face == 0) return true;

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_),
                         "Mesh2_edge_flux", H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: Mesh2_edge_flux"));
    DataSetGuard g(ds);

    hid_t fsp = H5Dget_space(ds);
    if (fsp < 0)
        return setError_(QStringLiteral("H5Dget_space failed: Mesh2_edge_flux"));
    DataSpaceGuard fg(fsp);

    // Dataset shape is [nTime, nFace, 3]. Select one time slice.
    const hsize_t offset[3] = { static_cast<hsize_t>(timeIdx), 0, 0 };
    const hsize_t count[3]  = { 1, static_cast<hsize_t>(n_face), 3 };
    if (H5Sselect_hyperslab(fsp, H5S_SELECT_SET, offset, nullptr,
                              count, nullptr) < 0)
        return setError_(QStringLiteral("H5Sselect_hyperslab failed: Mesh2_edge_flux"));

    const hsize_t mdims[1] = { static_cast<hsize_t>(n_face) * 3 };
    hid_t msp = H5Screate_simple(1, mdims, nullptr);
    if (msp < 0)
        return setError_(QStringLiteral("H5Screate_simple failed"));
    DataSpaceGuard mg(msp);

    herr_t r = H5Dread(ds, H5T_NATIVE_FLOAT, msp, fsp, H5P_DEFAULT,
                       flux.data());
    return r >= 0 || setError_(QStringLiteral("H5Dread edge_flux slice failed"));
}

bool Mesh2DH5Reader::readEdgeGeometry(std::vector<float>& length,
                                       std::vector<float>& nx,
                                       std::vector<float>& ny) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));

    const int n_face = triangleCount();
    if (n_face <= 0)
        return setError_(QStringLiteral("No triangles"));

    const size_t n3 = static_cast<size_t>(n_face) * 3;
    length.assign(n3, 0.0f);
    nx.assign(n3, 0.0f);
    ny.assign(n3, 0.0f);

    // Try the engine 6.0+ direct datasets first. If any one of them is
    // missing the file predates CF.2; fall through to vertex-derived
    // reconstruction.
    auto tryReadFloatDataset = [this, n3](const char* name, float* out) -> bool {
        hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_), name, H5P_DEFAULT);
        if (ds < 0) return false;
        DataSetGuard g(ds);
        // Sanity-check the dataset is the right shape; engine writes [nFace, 3].
        hid_t sp = H5Dget_space(ds);
        DataSpaceGuard sg(sp);
        hsize_t dims[2] = { 0, 0 };
        H5Sget_simple_extent_dims(sp, dims, nullptr);
        if (dims[0] * dims[1] != n3) return false;
        return H5Dread(ds, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                        out) >= 0;
    };

    const bool haveAll =
        tryReadFloatDataset("Mesh2_edge_length", length.data()) &&
        tryReadFloatDataset("Mesh2_edge_nx",     nx.data()) &&
        tryReadFloatDataset("Mesh2_edge_ny",     ny.data());

    if (haveAll) return true;

    // ---- Fallback: derive from vertex coords + connectivity ------------
    last_error_.clear();
    std::vector<double> vx, vy, vz;
    if (!readMeshGeometry(vx, vy, vz)) return false;
    std::vector<std::array<int, 3>> tris;
    if (!readTriangles(tris)) return false;

    // Edge e is opposite vertex e: endpoints (v[(e+1)%3], v[(e+2)%3]).
    // Outward normal: perpendicular to edge, flipped if needed so it points
    // away from the centroid. Matches engine MeshBuilder.cpp.
    for (int t = 0; t < n_face; ++t)
    {
        const int v[3] = { tris[t][0], tris[t][1], tris[t][2] };
        const double cx = (vx[v[0]] + vx[v[1]] + vx[v[2]]) / 3.0;
        const double cy = (vy[v[0]] + vy[v[1]] + vy[v[2]]) / 3.0;
        for (int e = 0; e < 3; ++e)
        {
            const int va = v[(e + 1) % 3];
            const int vb = v[(e + 2) % 3];
            const double ax = vx[va], ay = vy[va];
            const double bx = vx[vb], by = vy[vb];
            const double dx = bx - ax;
            const double dy = by - ay;
            const double len = std::sqrt(dx * dx + dy * dy);

            double rnx = dy;
            double rny = -dx;
            const double mx = 0.5 * (ax + bx);
            const double my = 0.5 * (ay + by);
            if (rnx * (mx - cx) + rny * (my - cy) < 0.0) {
                rnx = -rnx;
                rny = -rny;
            }
            const double nlen = std::sqrt(rnx * rnx + rny * rny);

            const size_t idx = static_cast<size_t>(t) * 3 + e;
            length[idx] = static_cast<float>(len);
            if (nlen > 1e-15) {
                nx[idx] = static_cast<float>(rnx / nlen);
                ny[idx] = static_cast<float>(rny / nlen);
            } else {
                nx[idx] = 0.0f;
                ny[idx] = 0.0f;
            }
        }
    }
    return true;
}

} // namespace openswmmvis::io
