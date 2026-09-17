/*!
 * \file   mesh2dh5reader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "io/mesh2dh5reader.h"

#include "mesh/meshcellgeom.h"   // mesh::cellGeom (quad display split), kEdgeStride

#include <hdf5.h>

#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::io {

static_assert(Mesh2DH5Reader::kEdgeStride == mesh::kEdgeStride,
              "Mesh2DH5Reader edge layout must match mesh::edgeSlot");

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
struct AttributeGuard {
    hid_t id;
    explicit AttributeGuard(hid_t i) : id(i) {}
    ~AttributeGuard() { if (id >= 0) H5Aclose(id); }
};
struct DataTypeGuard {
    hid_t id;
    explicit DataTypeGuard(hid_t i) : id(i) {}
    ~DataTypeGuard() { if (id >= 0) H5Tclose(id); }
};

bool readStartIndexAttr(hid_t dataset, int& out)
{
    hid_t attr = -1;
    H5E_BEGIN_TRY {
        attr = H5Aopen(dataset, "start_index", H5P_DEFAULT);
    } H5E_END_TRY;
    if (attr < 0) return false;
    AttributeGuard attrGuard(attr);

    const hid_t type = H5Aget_type(attr);
    if (type < 0) return false;
    DataTypeGuard typeGuard(type);

    const H5T_class_t cls = H5Tget_class(type);
    if (cls == H5T_INTEGER) {
        int value = 0;
        if (H5Aread(attr, H5T_NATIVE_INT, &value) >= 0) {
            out = value;
            return true;
        }
        return false;
    }

    if (cls != H5T_STRING) return false;

    bool ok = false;
    QString text;
    if (H5Tis_variable_str(type) > 0) {
        char *raw = nullptr;
        if (H5Aread(attr, type, &raw) >= 0 && raw) {
            text = QString::fromLatin1(raw).trimmed();
            H5free_memory(raw);
        }
    } else {
        const size_t n = H5Tget_size(type);
        std::vector<char> buf(n + 1, '\0');
        if (H5Aread(attr, type, buf.data()) >= 0)
            text = QString::fromLatin1(buf.data()).trimmed();
    }
    const int value = text.toInt(&ok);
    if (!ok) return false;
    out = value;
    return true;
}

/*! \brief Read a string attribute off \p loc. Returns false when absent. */
bool readStringAttr(hid_t loc, const char* name, QString& out)
{
    hid_t attr = -1;
    H5E_BEGIN_TRY {
        attr = H5Aopen(loc, name, H5P_DEFAULT);
    } H5E_END_TRY;
    if (attr < 0) return false;
    AttributeGuard attrGuard(attr);

    const hid_t type = H5Aget_type(attr);
    if (type < 0) return false;
    DataTypeGuard typeGuard(type);
    if (H5Tget_class(type) != H5T_STRING) return false;

    if (H5Tis_variable_str(type) > 0) {
        char *raw = nullptr;
        if (H5Aread(attr, type, &raw) < 0 || !raw) return false;
        out = QString::fromUtf8(raw).trimmed();
        H5free_memory(raw);
        return true;
    }

    const size_t n = H5Tget_size(type);
    std::vector<char> buf(n + 1, '\0');
    if (H5Aread(attr, type, buf.data()) < 0) return false;
    out = QString::fromUtf8(buf.data()).trimmed();
    return true;
}

/*! \brief Read a floating-point attribute off \p loc. False when absent. */
bool readDoubleAttr(hid_t loc, const char* name, double& out)
{
    hid_t attr = -1;
    H5E_BEGIN_TRY {
        attr = H5Aopen(loc, name, H5P_DEFAULT);
    } H5E_END_TRY;
    if (attr < 0) return false;
    AttributeGuard attrGuard(attr);

    double value = 0.0;
    if (H5Aread(attr, H5T_NATIVE_DOUBLE, &value) < 0) return false;
    out = value;
    return true;
}

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
    cached_face_width_ = -1;
    cached_has_node_head_ = -1;
    cached_has_node_depth_ = -1;
    cached_has_edge_flux_ = -1;
    cached_cells_loaded_ = false;
    cached_cells_.clear();
    cached_display_tris_.clear();
    cached_face_map_.clear();
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

    auto readVec = [this](const char* name, double* out) -> bool {
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

bool Mesh2DH5Reader::readCoordinateReference(CoordinateReference& out) const
{
    out = CoordinateReference{};
    if (file_id_ < 0) {
        setError_(QStringLiteral("Mesh2DH5Reader: not open"));
        return false;
    }
    const hid_t fid = static_cast<hid_t>(file_id_);

    // Units of the coordinates as stored. Present since the first engine that
    // wrote this file format, so it is read whether or not /crs exists.
    {
        hid_t ds = -1;
        H5E_BEGIN_TRY {
            ds = H5Dopen2(fid, "Mesh2_node_x", H5P_DEFAULT);
        } H5E_END_TRY;
        if (ds >= 0) {
            DataSetGuard g(ds);
            readStringAttr(ds, "units", out.storedUnits);
        }
    }

    hid_t crs = -1;
    H5E_BEGIN_TRY {
        crs = H5Dopen2(fid, "crs", H5P_DEFAULT);
    } H5E_END_TRY;
    if (crs < 0)
        return false;   // pre-6.0 file: undeclared, caller falls back
    DataSetGuard crsGuard(crs);

    readStringAttr(crs, "model_crs", out.crs);

    double factor = 0.0;
    if (readDoubleAttr(crs, "metres_per_model_unit", factor)
        && std::isfinite(factor) && factor > 0.0) {
        out.metresPerModelUnit = factor;
    }

    out.declared = true;
    return true;
}

int Mesh2DH5Reader::edgeStride() const
{
    if (cached_face_width_ < 0) {
        int w = 0;
        if (readDim_("Mesh2_face_nodes", 1, w) && (w == 3 || w == 4))
            cached_face_width_ = w;
    }
    return cached_face_width_ < 0 ? 0 : cached_face_width_;
}

int Mesh2DH5Reader::displayTriangleCount() const
{
    if (!loadCells_()) return 0;
    return static_cast<int>(cached_display_tris_.size());
}

/*! Load /Mesh2_face_nodes once: the file's cells (padding resolved from
 *  `_FillValue`, negative entries and /Mesh2_face_nv), then the display
 *  sub-triangle fan and its triangle → cell map. */
bool Mesh2DH5Reader::loadCells_() const
{
    if (cached_cells_loaded_) return true;
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));

    const int n = triangleCount();
    if (n <= 0)
        return setError_(QStringLiteral("No triangles"));
    const int n_vert = vertexCount();
    if (n_vert <= 0)
        return setError_(QStringLiteral("No vertices"));
    const int w = edgeStride();
    if (w != 3 && w != 4)
        return setError_(QStringLiteral(
            "Mesh2_face_nodes must be [nFace, 3] or [nFace, 4] (UGRID mixed topology)"));

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_),
                         "Mesh2_face_nodes", H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: Mesh2_face_nodes"));
    DataSetGuard g(ds);

    // /Mesh2_face_nodes is [n, w] of native int, row-major.
    std::vector<int> raw(static_cast<size_t>(n) * w, 0);
    herr_t r = H5Dread(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL,
                       H5P_DEFAULT, raw.data());
    if (r < 0)
        return setError_(QStringLiteral("H5Dread failed: Mesh2_face_nodes"));

    // Padding: UGRID `_FillValue` (the engine writes −1) or any negative
    // entry; /Mesh2_face_nv (int8, 3|4), when present, is authoritative.
    int fill = -1;
    {
        hid_t attr = -1;
        H5E_BEGIN_TRY {
            attr = H5Aopen(ds, "_FillValue", H5P_DEFAULT);
        } H5E_END_TRY;
        if (attr >= 0) {
            AttributeGuard ag(attr);
            int v = 0;
            if (H5Aread(attr, H5T_NATIVE_INT, &v) >= 0) fill = v;
        }
    }
    std::vector<signed char> nvs;
    if (w == 4 && H5Lexists(static_cast<hid_t>(file_id_), "Mesh2_face_nv", H5P_DEFAULT) > 0) {
        hid_t nds = H5Dopen2(static_cast<hid_t>(file_id_), "Mesh2_face_nv", H5P_DEFAULT);
        if (nds >= 0) {
            DataSetGuard ng(nds);
            nvs.assign(static_cast<size_t>(n), 0);
            if (H5Dread(nds, H5T_NATIVE_SCHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                        nvs.data()) < 0)
                nvs.clear();
        }
    }

    std::vector<std::array<int, 4>> cells(static_cast<size_t>(n), {-1, -1, -1, -1});
    for (int c = 0; c < n; ++c) {
        const int* row = raw.data() + static_cast<size_t>(c) * w;
        int nv = w;
        if (!nvs.empty() && (nvs[c] == 3 || nvs[c] == 4)) nv = nvs[c];
        for (int k = 0; k < nv; ++k) {
            const int v = row[k];
            if (v == fill || v < 0) break;   // padding — a triangle in a width-4 file
            cells[c][k] = v;
        }
        if (cells[c][2] < 0)
            return setError_(QStringLiteral(
                "Mesh2_face_nodes row %1 has fewer than 3 vertices").arg(c));
    }

    int startIndex = 0;
    const bool hasStartIndex = readStartIndexAttr(ds, startIndex);
    auto minMax = [&cells](int& minIdx, int& maxIdx) {
        minIdx = std::numeric_limits<int>::max();
        maxIdx = std::numeric_limits<int>::lowest();
        for (const auto& cell : cells)
            for (int k = 0; k < 4; ++k) {
                if (cell[k] < 0 && k == 3) continue;   // triangle padding
                minIdx = std::min(minIdx, cell[k]);
                maxIdx = std::max(maxIdx, cell[k]);
            }
    };
    if (!hasStartIndex) {
        int minIdx = 0, maxIdx = 0;
        minMax(minIdx, maxIdx);
        // Be liberal for third-party UGRID files that omit start_index but
        // clearly use the common 1-based convention.
        if (minIdx == 1 && maxIdx == n_vert)
            startIndex = 1;
    }
    if (startIndex != 0) {
        for (auto& cell : cells)
            for (int k = 0; k < 4; ++k)
                if (cell[k] >= 0) cell[k] -= startIndex;
    }

    {
        int minIdx = 0, maxIdx = 0;
        minMax(minIdx, maxIdx);
        if (minIdx < 0 || maxIdx >= n_vert) {
            return setError_(QStringLiteral(
                "Mesh2_face_nodes vertex index out of range after start_index normalization "
                "(min=%1 max=%2 vertices=%3)")
                .arg(minIdx).arg(maxIdx).arg(n_vert));
        }
    }

    // Display fan: a triangle cell is its own display triangle; a quad is
    // split on mesh::cellGeom's elevation-ordered diagonal (the engine's VFR
    // storage model), which needs the vertex elevations.
    bool anyQuad = false;
    for (const auto& cell : cells) if (cell[3] >= 0) { anyQuad = true; break; }

    std::vector<std::array<int, 3>> tris;
    std::vector<int> faceMap;
    tris.reserve(static_cast<size_t>(n) + (anyQuad ? static_cast<size_t>(n) : 0));
    faceMap.reserve(tris.capacity());
    if (!anyQuad) {
        for (int c = 0; c < n; ++c) {
            tris.push_back({cells[c][0], cells[c][1], cells[c][2]});
            faceMap.push_back(c);
        }
    } else {
        std::vector<double> vx, vy, vz;
        if (!readMeshGeometry(vx, vy, vz)) return false;
        QVector<mesh::MeshVertex> verts(n_vert);
        for (int i = 0; i < n_vert; ++i) {
            verts[i].xy = QPointF(vx[i], vy[i]);
            verts[i].z  = vz[i];
        }
        for (int c = 0; c < n; ++c) {
            if (cells[c][3] < 0) {
                tris.push_back({cells[c][0], cells[c][1], cells[c][2]});
                faceMap.push_back(c);
                continue;
            }
            mesh::MeshTriangle q;
            q.v0 = cells[c][0]; q.v1 = cells[c][1]; q.v2 = cells[c][2]; q.v3 = cells[c][3];
            const mesh::CellGeom gq = mesh::cellGeom(verts, q);
            for (int s = 0; s < gq.nSub; ++s) {
                tris.push_back(gq.sub[s]);
                faceMap.push_back(c);
            }
        }
    }

    cached_cells_        = std::move(cells);
    cached_display_tris_ = std::move(tris);
    cached_face_map_     = std::move(faceMap);
    cached_cells_loaded_ = true;
    return true;
}

bool Mesh2DH5Reader::readCells(std::vector<std::array<int, 4>>& cells) const
{
    if (!loadCells_()) return false;
    cells = cached_cells_;
    return true;
}

bool Mesh2DH5Reader::readTriangles(std::vector<std::array<int, 3>>& tris) const
{
    if (!loadCells_()) return false;
    tris = cached_display_tris_;
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
    return readFaceFieldAt("Mesh2_face_depth", timeIdx, depths);
}

bool Mesh2DH5Reader::hasFaceField(const char* dataset) const
{
    if (file_id_ < 0 || !dataset) return false;
    auto it = cached_has_face_field_.find(dataset);
    if (it == cached_has_face_field_.end()) {
        const htri_t ex = H5Lexists(static_cast<hid_t>(file_id_), dataset, H5P_DEFAULT);
        it = cached_has_face_field_.emplace(dataset, ex > 0).first;
    }
    return it->second;
}

bool Mesh2DH5Reader::readFaceFieldAt(const char* dataset, int timeIdx,
                                     std::vector<float>& values) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));
    if (timeIdx < 0)
        return setError_(QStringLiteral("Negative timeIdx"));
    if (!hasFaceField(dataset))
        return setError_(QStringLiteral("%1 not in file").arg(QLatin1String(dataset)));

    const int n_face = triangleCount();
    const int n_time = timeCount();
    if (timeIdx >= n_time)
        return setError_(QStringLiteral("timeIdx %1 >= n_time %2")
                          .arg(timeIdx).arg(n_time));

    values.assign(n_face, 0.0f);
    if (n_face == 0) return true;

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_), dataset, H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: %1").arg(QLatin1String(dataset)));
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
                       values.data());
    return r >= 0 || setError_(QStringLiteral("H5Dread %1 slice failed")
                                   .arg(QLatin1String(dataset)));
}

bool Mesh2DH5Reader::readFaceEnvelope(const char* dataset,
                                      std::vector<float>& values) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));
    if (!hasFaceField(dataset))
        return setError_(QStringLiteral("%1 not in file").arg(QLatin1String(dataset)));

    const int n_face = triangleCount();
    values.assign(n_face, 0.0f);
    if (n_face == 0) return true;

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_), dataset, H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: %1").arg(QLatin1String(dataset)));
    DataSetGuard g(ds);

    hid_t fsp = H5Dget_space(ds);
    if (fsp < 0)
        return setError_(QStringLiteral("H5Dget_space failed"));
    DataSpaceGuard fg(fsp);

    // Envelopes are rank 1, [nFace]. A rank-2 [nTime, nFace] field is a
    // time series, not an envelope — reject it rather than reading its
    // first row, so a caller probing both paths cannot silently get frame 0.
    if (H5Sget_simple_extent_ndims(fsp) != 1)
        return setError_(QStringLiteral("%1 is not a rank-1 envelope")
                             .arg(QLatin1String(dataset)));

    hsize_t dims[1] = { 0 };
    if (H5Sget_simple_extent_dims(fsp, dims, nullptr) < 0)
        return setError_(QStringLiteral("H5Sget_simple_extent_dims failed"));
    if (dims[0] != static_cast<hsize_t>(n_face))
        return setError_(QStringLiteral("%1 length %2 != n_face %3")
                             .arg(QLatin1String(dataset))
                             .arg(static_cast<qulonglong>(dims[0]))
                             .arg(n_face));

    herr_t r = H5Dread(ds, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                       values.data());
    return r >= 0 || setError_(QStringLiteral("H5Dread %1 failed")
                                   .arg(QLatin1String(dataset)));
}

bool Mesh2DH5Reader::readVertexHeadsAt(int timeIdx,
                                        std::vector<double>& heads) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));
    if (timeIdx < 0)
        return setError_(QStringLiteral("Negative timeIdx"));

    // Probe-once presence cache: files written before the engine's vertex
    // reconstruction output lack /Mesh2_node_head. Without the cache every
    // scrubbed frame would re-fail H5Dopen2 and spam the HDF5 error stack.
    if (cached_has_node_head_ < 0) {
        const htri_t ex = H5Lexists(static_cast<hid_t>(file_id_),
                                    "Mesh2_node_head", H5P_DEFAULT);
        cached_has_node_head_ = (ex > 0) ? 1 : 0;
    }
    if (cached_has_node_head_ == 0)
        return setError_(QStringLiteral("Mesh2_node_head not in file"));

    const int n_vert = vertexCount();
    const int n_time = timeCount();
    if (timeIdx >= n_time)
        return setError_(QStringLiteral("timeIdx %1 >= n_time %2")
                          .arg(timeIdx).arg(n_time));

    heads.assign(n_vert, 0.0);
    if (n_vert == 0) return true;

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_),
                         "Mesh2_node_head", H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: Mesh2_node_head"));
    DataSetGuard g(ds);

    hid_t fsp = H5Dget_space(ds);
    if (fsp < 0)
        return setError_(QStringLiteral("H5Dget_space failed: Mesh2_node_head"));
    DataSpaceGuard fg(fsp);

    // Hyperslab: { offset=[timeIdx, 0], count=[1, n_vert] }
    const hsize_t offset[2] = { static_cast<hsize_t>(timeIdx), 0 };
    const hsize_t count[2]  = { 1, static_cast<hsize_t>(n_vert) };
    if (H5Sselect_hyperslab(fsp, H5S_SELECT_SET, offset, nullptr,
                              count, nullptr) < 0)
        return setError_(QStringLiteral("H5Sselect_hyperslab failed: Mesh2_node_head"));

    const hsize_t mdims[1] = { static_cast<hsize_t>(n_vert) };
    hid_t msp = H5Screate_simple(1, mdims, nullptr);
    if (msp < 0)
        return setError_(QStringLiteral("H5Screate_simple failed"));
    DataSpaceGuard mg(msp);

    // Heads stay double: they carry the elevation datum, and the head − z
    // subtraction downstream must not lose the dry-threshold signal.
    herr_t r = H5Dread(ds, H5T_NATIVE_DOUBLE, msp, fsp, H5P_DEFAULT,
                       heads.data());
    return r >= 0 || setError_(QStringLiteral("H5Dread node_head slice failed"));
}

bool Mesh2DH5Reader::readVertexSignedDepthsAt(int timeIdx,
                                               std::vector<float>& depths) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));
    if (timeIdx < 0)
        return setError_(QStringLiteral("Negative timeIdx"));

    // Probe-once presence cache, same pattern as /Mesh2_node_head: files
    // written before the engine's wet-masked render reconstruction lack
    // /Mesh2_node_depth.
    if (cached_has_node_depth_ < 0) {
        const htri_t ex = H5Lexists(static_cast<hid_t>(file_id_),
                                    "Mesh2_node_depth", H5P_DEFAULT);
        cached_has_node_depth_ = (ex > 0) ? 1 : 0;
    }
    if (cached_has_node_depth_ == 0)
        return setError_(QStringLiteral("Mesh2_node_depth not in file"));

    const int n_vert = vertexCount();
    const int n_time = timeCount();
    if (timeIdx >= n_time)
        return setError_(QStringLiteral("timeIdx %1 >= n_time %2")
                          .arg(timeIdx).arg(n_time));

    depths.assign(n_vert, 0.0f);
    if (n_vert == 0) return true;

    hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_),
                         "Mesh2_node_depth", H5P_DEFAULT);
    if (ds < 0)
        return setError_(QStringLiteral("H5Dopen2 failed: Mesh2_node_depth"));
    DataSetGuard g(ds);

    hid_t fsp = H5Dget_space(ds);
    if (fsp < 0)
        return setError_(QStringLiteral("H5Dget_space failed: Mesh2_node_depth"));
    DataSpaceGuard fg(fsp);

    // Hyperslab: { offset=[timeIdx, 0], count=[1, n_vert] }
    const hsize_t offset[2] = { static_cast<hsize_t>(timeIdx), 0 };
    const hsize_t count[2]  = { 1, static_cast<hsize_t>(n_vert) };
    if (H5Sselect_hyperslab(fsp, H5S_SELECT_SET, offset, nullptr,
                              count, nullptr) < 0)
        return setError_(QStringLiteral("H5Sselect_hyperslab failed: Mesh2_node_depth"));

    const hsize_t mdims[1] = { static_cast<hsize_t>(n_vert) };
    hid_t msp = H5Screate_simple(1, mdims, nullptr);
    if (msp < 0)
        return setError_(QStringLiteral("H5Screate_simple failed"));
    DataSpaceGuard mg(msp);

    // Signed DEPTHS (not heads) may be read as float: the datum is already
    // subtracted engine-side in double, so float precision is ample.
    herr_t r = H5Dread(ds, H5T_NATIVE_FLOAT, msp, fsp, H5P_DEFAULT,
                       depths.data());
    return r >= 0 || setError_(QStringLiteral("H5Dread node_depth slice failed"));
}

bool Mesh2DH5Reader::readEdgeFluxAt(int timeIdx, std::vector<float>& flux) const
{
    if (file_id_ < 0)
        return setError_(QStringLiteral("Mesh2DH5Reader: not open"));
    if (timeIdx < 0)
        return setError_(QStringLiteral("Negative timeIdx"));

    // Probe-once presence cache (same pattern as /Mesh2_node_head): a run
    // written with REPORT_2D_VARIABLES lacking EDGE_FLUX (e.g. MINIMAL) has
    // no /Mesh2_edge_flux; answer cleanly instead of failing H5Dopen2 and
    // spamming the HDF5 error stack on every probe/frame.
    if (cached_has_edge_flux_ < 0) {
        const htri_t ex = H5Lexists(static_cast<hid_t>(file_id_),
                                    "Mesh2_edge_flux", H5P_DEFAULT);
        cached_has_edge_flux_ = (ex > 0) ? 1 : 0;
    }
    if (cached_has_edge_flux_ == 0)
        return setError_(QStringLiteral("Mesh2_edge_flux not in file"));

    const int n_face = triangleCount();
    const int n_time = timeCount();
    if (timeIdx >= n_time)
        return setError_(QStringLiteral("timeIdx %1 >= n_time %2")
                          .arg(timeIdx).arg(n_time));

    // Output is ALWAYS [cell * kEdgeStride + e]; the file is [nTime, nFace, w]
    // with w = 3 (all-triangle) or 4 (mixed). A width-3 file is read into a
    // stride-3 scratch buffer and repacked.
    flux.assign(static_cast<size_t>(n_face) * kEdgeStride, 0.0f);
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

    hsize_t fdims[3] = { 0, 0, 0 };
    if (H5Sget_simple_extent_ndims(fsp) != 3)
        return setError_(QStringLiteral("Mesh2_edge_flux is not [nTime, nFace, 3|4]"));
    H5Sget_simple_extent_dims(fsp, fdims, nullptr);
    const int w = static_cast<int>(fdims[2]);
    if ((w != 3 && w != 4) || static_cast<int>(fdims[1]) != n_face)
        return setError_(QStringLiteral("Mesh2_edge_flux is [%1, %2, %3]; expected [nTime, %4, 3|4]")
                          .arg(static_cast<qulonglong>(fdims[0]))
                          .arg(static_cast<qulonglong>(fdims[1]))
                          .arg(static_cast<qulonglong>(fdims[2]))
                          .arg(n_face));

    // Select one time slice.
    const hsize_t offset[3] = { static_cast<hsize_t>(timeIdx), 0, 0 };
    const hsize_t count[3]  = { 1, static_cast<hsize_t>(n_face), static_cast<hsize_t>(w) };
    if (H5Sselect_hyperslab(fsp, H5S_SELECT_SET, offset, nullptr,
                              count, nullptr) < 0)
        return setError_(QStringLiteral("H5Sselect_hyperslab failed: Mesh2_edge_flux"));

    const hsize_t mdims[1] = { static_cast<hsize_t>(n_face) * w };
    hid_t msp = H5Screate_simple(1, mdims, nullptr);
    if (msp < 0)
        return setError_(QStringLiteral("H5Screate_simple failed"));
    DataSpaceGuard mg(msp);

    if (w == kEdgeStride) {
        herr_t r = H5Dread(ds, H5T_NATIVE_FLOAT, msp, fsp, H5P_DEFAULT,
                           flux.data());
        return r >= 0 || setError_(QStringLiteral("H5Dread edge_flux slice failed"));
    }
    std::vector<float> packed(static_cast<size_t>(n_face) * w, 0.0f);
    herr_t r = H5Dread(ds, H5T_NATIVE_FLOAT, msp, fsp, H5P_DEFAULT,
                       packed.data());
    if (r < 0)
        return setError_(QStringLiteral("H5Dread edge_flux slice failed"));
    for (int c = 0; c < n_face; ++c)
        for (int e = 0; e < w; ++e)
            flux[static_cast<size_t>(c) * kEdgeStride + e] =
                packed[static_cast<size_t>(c) * w + e];
    return true;
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

    // Output is ALWAYS [cell * kEdgeStride + e] (slot 3 of a triangle = 0);
    // the file's own width (3 or 4) is repacked on read.
    const size_t nSlots = static_cast<size_t>(n_face) * kEdgeStride;
    length.assign(nSlots, 0.0f);
    nx.assign(nSlots, 0.0f);
    ny.assign(nSlots, 0.0f);

    // Try the engine 6.0+ direct datasets first. If any one of them is
    // missing the file predates CF.2; fall through to vertex-derived
    // reconstruction.
    auto tryReadFloatDataset = [this, n_face](const char* name, float* out) -> bool {
        hid_t ds = H5Dopen2(static_cast<hid_t>(file_id_), name, H5P_DEFAULT);
        if (ds < 0) return false;
        DataSetGuard g(ds);
        // Sanity-check the dataset is the right shape; engine writes [nFace, 3|4].
        hid_t sp = H5Dget_space(ds);
        DataSpaceGuard sg(sp);
        hsize_t dims[2] = { 0, 0 };
        if (H5Sget_simple_extent_ndims(sp) != 2) return false;
        H5Sget_simple_extent_dims(sp, dims, nullptr);
        const int w = static_cast<int>(dims[1]);
        if (static_cast<int>(dims[0]) != n_face || (w != 3 && w != 4)) return false;
        if (w == kEdgeStride)
            return H5Dread(ds, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                            out) >= 0;
        std::vector<float> packed(static_cast<size_t>(n_face) * w, 0.0f);
        if (H5Dread(ds, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                    packed.data()) < 0)
            return false;
        for (int c = 0; c < n_face; ++c)
            for (int e = 0; e < w; ++e)
                out[static_cast<size_t>(c) * kEdgeStride + e] =
                    packed[static_cast<size_t>(c) * w + e];
        return true;
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
    std::vector<std::array<int, 4>> cells;
    if (!readCells(cells)) return false;

    // Edge e of an nv-gon joins (v[(e+1)%nv], v[(e+2)%nv]) — for a triangle
    // "edge e is opposite vertex e". Outward normal: perpendicular to the
    // edge, flipped if needed so it points away from the vertex mean (inside
    // any convex cell). Matches engine MeshBuilder.cpp.
    for (int t = 0; t < n_face; ++t)
    {
        const int nv = cells[t][3] >= 0 ? 4 : 3;
        const int* v = cells[t].data();
        double cx = 0.0, cy = 0.0;
        for (int k = 0; k < nv; ++k) { cx += vx[v[k]]; cy += vy[v[k]]; }
        cx /= nv;
        cy /= nv;
        for (int e = 0; e < nv; ++e)
        {
            const int va = v[(e + 1) % nv];
            const int vb = v[(e + 2) % nv];
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

            const size_t idx = static_cast<size_t>(t) * kEdgeStride + e;
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
