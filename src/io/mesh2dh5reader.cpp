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
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace openswmmvis::io {

static_assert(Mesh2DH5Reader::kEdgeStride == mesh::kEdgeStride,
              "Mesh2DH5Reader edge layout must match mesh::edgeSlot");

namespace {

// The bundled HDF5 is not thread-safe (H5_HAVE_THREADSAFE is undefined) and
// this reader is its only user in the GUI process, so every entry point below
// holds one process-wide lock. A worker-owned reader can then extract plot
// series while the map reads frames through another reader; each call (one
// frame) holds the lock, so neither side waits longer than one read.
// Recursive: entry points call each other (readDepthsAt → readFaceFieldAt).
using Hdf5Lock = std::lock_guard<std::recursive_mutex>;
std::recursive_mutex& hdf5Mutex()
{
    static std::recursive_mutex m;
    return m;
}

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
struct PropertyGuard {
    hid_t id;
    explicit PropertyGuard(hid_t i) : id(i) {}
    ~PropertyGuard() { if (id >= 0) H5Pclose(id); }
};

bool scalarAttribute(hid_t attribute)
{
    DataSpaceGuard space(H5Aget_space(attribute));
    return space.id >= 0 && H5Sget_simple_extent_npoints(space.id) == 1;
}

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
    if (!scalarAttribute(attr)) return false;

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
    if (n > 1024 * 1024) return false;
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
    if (!scalarAttribute(attr)) return false;

    double value = 0.0;
    if (H5Aread(attr, H5T_NATIVE_DOUBLE, &value) < 0) return false;
    out = value;
    return true;
}


bool datasetShape(hid_t data, std::vector<hsize_t>& dims)
{
    DataSpaceGuard space(H5Dget_space(data));
    if (space.id < 0) return false;
    const int rank = H5Sget_simple_extent_ndims(space.id);
    if (rank < 1 || rank > 3) return false;
    dims.resize(rank);
    if (H5Sget_simple_extent_dims(space.id, dims.data(), nullptr) < 0) return false;
    for (hsize_t dim : dims)
        if (dim > hsize_t(std::numeric_limits<int>::max())) return false;
    DataTypeGuard type(H5Dget_type(data));
    return type.id >= 0 && (H5Tget_class(type.id) == H5T_FLOAT || H5Tget_class(type.id) == H5T_INTEGER);
}

QVector<Mesh2DResultVariable> describeFaceDataset(hid_t file, const QString& name,
                                                int cells, QStringList* warnings)
{
    using V = Mesh2DResultVariable;
    QVector<V> variables;
    const auto warn = [&](const QString& reason) {
        if (warnings) warnings->append(name + QStringLiteral(": ") + reason);
    };
    const QByteArray encoded = name.toUtf8();
    hid_t data = -1;
    H5E_BEGIN_TRY { data = H5Dopen2(file, encoded.constData(), H5P_DEFAULT); } H5E_END_TRY;
    DataSetGuard guard(data);
    if (data < 0) return variables;
    std::vector<hsize_t> dims;
    if (!datasetShape(data, dims) || cells <= 0 || dims.back() != hsize_t(cells)) {
        warn(QStringLiteral("unsupported rank/type or cell count"));
        return variables;
    }
    const bool speciesDataset = name == "Mesh2_face_species_conc" || name == "Mesh2_face_buildup"
        || name == "Mesh2_face_gw_sat_conc" || name == "Mesh2_face_gw_unsat_conc";
    if ((speciesDataset || name == "Mesh2_face_gw_theta_sigma") && dims.size() != 3) {
        warn(QStringLiteral("species/layer output must have rank three"));
        return variables;
    }
    V base;
    base.dataset = name;
    base.domain = name.startsWith(QStringLiteral("Mesh2_face_gw_")) ? V::Domain::Groundwater : V::Domain::Surface;
    if (!readStringAttr(data, "long_name", base.label)) {
        base.label = name.mid(QStringLiteral("Mesh2_face_").size());
        base.label.replace('_', ' ');
    }
    const QString domainLabel = base.domain == V::Domain::Groundwater
        ? QStringLiteral("Groundwater — ") : QStringLiteral("Surface — ");
    base.label.prepend(domainLabel);
    readStringAttr(data, "units", base.units);
    base.unitsKnown = !base.units.isEmpty();
    if (dims.size() == 1) {
        QString methods;
        readStringAttr(data, "cell_methods", methods);
        base.temporal = name.startsWith(QStringLiteral("Mesh2_face_max_")) || methods.contains("time: maximum")
            ? V::Temporal::Envelope : V::Temporal::Static;
        variables.append(base);
        return variables;
    }
    base.frameCount = int(dims[0]);
    if (dims.size() == 2) {
        static const QSet<QString> held{
            "Mesh2_face_gw_recharge", "Mesh2_face_gw_lateral", "Mesh2_face_gw_node_exchange",
            "Mesh2_face_gw_deep", "Mesh2_face_gw_et", "Mesh2_face_gw_dunne",
            "Mesh2_face_gw_infil_in", "Mesh2_face_gw_link_seepage", "Mesh2_face_gw_infil_capacity", "Mesh2_face_gw_reject", "Mesh2_face_infil_rate"};
        if (held.contains(name)) base.temporal = V::Temporal::Held;
        variables.append(base);
        return variables;
    }
    if (dims[1] == 0 || dims[1] > 65536) {
        warn(QStringLiteral("invalid or excessive species/layer count"));
        return variables;
    }
    if (name == QStringLiteral("Mesh2_face_gw_theta_sigma")) {
        QString layout;
        readStringAttr(data, "layout", layout);
        hid_t closure = -1;
        H5E_BEGIN_TRY { closure = H5Dopen2(file, "Mesh2_face_gw_closure", H5P_DEFAULT); } H5E_END_TRY;
        DataSetGuard closureGuard(closure);
        std::vector<hsize_t> closureDims;
        if (base.units != "1" || !layout.startsWith("[time, layer, face]")
            || closure < 0 || !datasetShape(closure, closureDims)
            || closureDims.size() != 1 || closureDims[0] != hsize_t(cells)) {
            warn(QStringLiteral("sigma output needs explicit layer layout, dimensionless units and closure mask"));
            return variables;
        }
        base.zone = V::Zone::Sigma;
        for (int layer = 0; layer < int(dims[1]); ++layer) {
            auto variable = base; variable.layer = layer;
            variable.label = QStringLiteral("Groundwater — water content, sigma layer %1%2")
                .arg(layer).arg(layer == 0 ? QStringLiteral(" (surface)") : QString());
            variables.append(variable);
        }
        return variables;
    }
    const bool surface = name == "Mesh2_face_species_conc";
    const bool buildup = name == "Mesh2_face_buildup";
    const bool sat = name == "Mesh2_face_gw_sat_conc";
    const bool unsat = name == "Mesh2_face_gw_unsat_conc";
    if (!surface && !buildup && !sat && !unsat) {
        warn(QStringLiteral("unsupported rank-three axis metadata"));
        return variables;
    }
    QString nameText;
    if (!readStringAttr(data, "species_names", nameText)) {
        warn(QStringLiteral("species identities are missing or malformed"));
        return variables;
    }
    QStringList names = nameText.split(',', Qt::KeepEmptyParts);
    QSet<QString> unique;
    for (QString& species : names) {
        species = species.trimmed();
        if (species.isEmpty() || unique.contains(species.toCaseFolded())) {
            warn(QStringLiteral("species identities are empty or ambiguous"));
            return variables;
        }
        unique.insert(species.toCaseFolded());
    }
    if (names.size() != int(dims[1])) {
        warn(QStringLiteral("species identity count does not match the stored rows"));
        return variables;
    }
    QString unitText;
    QStringList units;
    if (readStringAttr(data, "species_units", unitText)) units = unitText.split(',', Qt::KeepEmptyParts);
    const bool unitsMatch = units.size() == names.size();
    if (!buildup && !unitsMatch) warn(QStringLiteral("per-species units unresolved; generic dataset units are not concentration units"));
    if ((surface && H5Lexists(file, "Mesh2_face_depth", H5P_DEFAULT) <= 0)
        || (sat && H5Lexists(file, "Mesh2_face_gw_hg", H5P_DEFAULT) <= 0)
        || (unsat && (H5Lexists(file, "Mesh2_face_gw_hu", H5P_DEFAULT) <= 0
                       || H5Lexists(file, "Mesh2_face_gw_closure", H5P_DEFAULT) <= 0
                       || H5Lexists(file, "Mesh2_face_gw_theta_sigma", H5P_DEFAULT) <= 0)))
        warn(QStringLiteral("water-state metadata incomplete; raw zero alone does not establish a waterless zone"));
    if (sat) base.zone = V::Zone::Saturated;
    if (unsat) base.zone = V::Zone::Unsaturated;
    for (int row = 0; row < names.size(); ++row) {
        auto variable = base;
        variable.species = names[row];
        if (!buildup) {
            variable.units = unitsMatch ? units[row].trimmed() : QString();
            variable.unitsKnown = !variable.units.isEmpty();
        }
        variable.label = domainLabel + names[row]
            + (buildup ? QStringLiteral(" — buildup")
               : sat ? QStringLiteral(" — saturated concentration")
               : unsat ? QStringLiteral(" — unsaturated concentration")
                       : QStringLiteral(" — concentration"));
        variables.append(variable);
    }
    return variables;
}

// One cell slice, never the whole time/species cube. Both outputs are atomic
// on failure; raw missing values are normalised before optional water masks.
bool readCellSlice(hid_t file, const char* name, int cells, int rank,
                   int time, int row, std::vector<float>& values,
                   std::vector<Mesh2DValueStatus>& status, QString& error)
{
    values.clear(); status.clear();
    const auto fail = [&](const QString& why) {
        error = QString::fromUtf8(name) + QStringLiteral(": ") + why;
        return false;
    };
    hid_t data = -1;
    H5E_BEGIN_TRY { data = H5Dopen2(file, name, H5P_DEFAULT); } H5E_END_TRY;
    DataSetGuard guard(data);
    if (data < 0) return fail(QStringLiteral("dataset unavailable"));
    std::vector<hsize_t> dims;
    if (!datasetShape(data, dims) || int(dims.size()) != rank || cells <= 0
        || dims.back() != hsize_t(cells)) return fail(QStringLiteral("rank or cell count mismatch"));
    if (rank > 1 && (time < 0 || hsize_t(time) >= dims[0])) return fail(QStringLiteral("frame unavailable"));
    if (rank == 3 && (row < 0 || hsize_t(row) >= dims[1])) return fail(QStringLiteral("species/layer unavailable"));
    DataSpaceGuard fileSpace(H5Dget_space(data));
    std::vector<hsize_t> offset(rank, 0), count(rank, 1);
    count.back() = hsize_t(cells);
    if (rank > 1) offset[0] = hsize_t(time);
    if (rank == 3) offset[1] = hsize_t(row);
    if (H5Sselect_hyperslab(fileSpace.id, H5S_SELECT_SET, offset.data(), nullptr, count.data(), nullptr) < 0)
        return fail(QStringLiteral("cannot select requested slice"));
    const hsize_t cellCount = hsize_t(cells);
    DataSpaceGuard memory(H5Screate_simple(1, &cellCount, nullptr));
    std::vector<double> raw(std::size_t(cells), 0.);
    if (memory.id < 0 || H5Dread(data, H5T_NATIVE_DOUBLE, memory.id, fileSpace.id, H5P_DEFAULT, raw.data()) < 0)
        return fail(QStringLiteral("cannot read requested slice"));
    double fill = 0., missing = 0., scale = 1., add = 0.;
    const bool hasFill = readDoubleAttr(data, "_FillValue", fill);
    const bool hasMissing = readDoubleAttr(data, "missing_value", missing);
    const bool hasScale = readDoubleAttr(data, "scale_factor", scale);
    const bool hasOffset = readDoubleAttr(data, "add_offset", add);
    if ((H5Aexists(data, "_FillValue") > 0 && !hasFill)
        || (H5Aexists(data, "missing_value") > 0 && !hasMissing)
        || (H5Aexists(data, "scale_factor") > 0 && !hasScale)
        || (H5Aexists(data, "add_offset") > 0 && !hasOffset))
        return fail(QStringLiteral("nonnumeric or nonscalar missing/scale metadata"));
    if (!std::isfinite(scale) || !std::isfinite(add)) return fail(QStringLiteral("invalid scale/offset metadata"));
    std::vector<float> out(std::size_t(cells), std::numeric_limits<float>::quiet_NaN());
    std::vector<Mesh2DValueStatus> flags(std::size_t(cells), Mesh2DValueStatus::Valid);
    for (int cell = 0; cell < cells; ++cell) {
        const double v = raw[std::size_t(cell)];
        const double decoded = v * scale + add;
        if (!std::isfinite(v) || !std::isfinite(decoded)
            || std::abs(decoded) > std::numeric_limits<float>::max()
            || (hasFill && v == fill) || (hasMissing && v == missing))
            flags[std::size_t(cell)] = Mesh2DValueStatus::Missing;
        else out[std::size_t(cell)] = float(decoded);
    }
    // Lazy engine variables use zero default fill. A wholly unallocated chunk
    // is unreported data, even though HDF5 returns zeros for that chunk.
    PropertyGuard properties(H5Dget_create_plist(data));
    if (properties.id >= 0 && H5Pget_layout(properties.id) == H5D_CHUNKED) {
        std::vector<hsize_t> chunks(rank);
        if (H5Pget_chunk(properties.id, rank, chunks.data()) != rank || chunks.back() == 0)
            return fail(QStringLiteral("invalid chunk metadata"));
        for (hsize_t size : chunks)
            if (size == 0) return fail(QStringLiteral("invalid chunk metadata"));
        for (hsize_t cell = 0; cell < hsize_t(cells);) {
            auto coordinate = offset;
            coordinate.back() = cell;
            for (int axis = 0; axis < rank; ++axis) coordinate[axis] -= coordinate[axis] % chunks[axis];
            unsigned filterMask = 0; haddr_t address = HADDR_UNDEF; hsize_t bytes = 0;
            if (H5Dget_chunk_info_by_coord(data, coordinate.data(), &filterMask, &address, &bytes) < 0)
                return fail(QStringLiteral("cannot inspect reported chunk coverage"));
            const hsize_t end = std::min(hsize_t(cells), coordinate.back() + chunks.back());
            if (address == HADDR_UNDEF || bytes == 0)
                for (hsize_t index = cell; index < end; ++index) {
                    flags[std::size_t(index)] = Mesh2DValueStatus::Missing;
                    out[std::size_t(index)] = std::numeric_limits<float>::quiet_NaN();
                }
            cell = end;
        }
    }
    values.swap(out); status.swap(flags);
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
    const Hdf5Lock lock(hdf5Mutex());
    close();
    path_ = path;
    cached_n_vert_ = -1;
    cached_n_face_ = -1;
    cached_face_width_ = -1;
    cached_has_node_head_ = -1;
    cached_has_node_depth_ = -1;
    cached_has_edge_flux_ = -1;
    cached_has_face_field_.clear();
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
    const Hdf5Lock lock(hdf5Mutex());
    if (file_id_ >= 0) {
        H5Fclose(static_cast<hid_t>(file_id_));
        file_id_ = -1;
    }
    path_.clear();
    cached_has_face_field_.clear();
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
    // Cached lookups take no lock: a reader is used by one thread, so only
    // a miss (an HDF5 call) needs to wait for another thread's read.
    if (cached_n_vert_ < 0) {
        const Hdf5Lock lock(hdf5Mutex());
        readDim_("Mesh2_node_x", 0, cached_n_vert_);
    }
    return cached_n_vert_ < 0 ? 0 : cached_n_vert_;
}

int Mesh2DH5Reader::triangleCount() const
{
    if (cached_n_face_ < 0) {
        const Hdf5Lock lock(hdf5Mutex());
        readDim_("Mesh2_face_nodes", 0, cached_n_face_);
    }
    return cached_n_face_ < 0 ? 0 : cached_n_face_;
}

bool Mesh2DH5Reader::readMeshGeometry(std::vector<double>& vx,
                                       std::vector<double>& vy,
                                       std::vector<double>& vz) const
{
    const Hdf5Lock lock(hdf5Mutex());
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
    const Hdf5Lock lock(hdf5Mutex());
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
        const Hdf5Lock lock(hdf5Mutex());
        int w = 0;
        if (readDim_("Mesh2_face_nodes", 1, w) && (w == 3 || w == 4))
            cached_face_width_ = w;
    }
    return cached_face_width_ < 0 ? 0 : cached_face_width_;
}

int Mesh2DH5Reader::displayTriangleCount() const
{
    const Hdf5Lock lock(hdf5Mutex());
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
    const Hdf5Lock lock(hdf5Mutex());
    if (!loadCells_()) return false;
    cells = cached_cells_;
    return true;
}

bool Mesh2DH5Reader::readTriangles(std::vector<std::array<int, 3>>& tris) const
{
    const Hdf5Lock lock(hdf5Mutex());
    if (!loadCells_()) return false;
    tris = cached_display_tris_;
    return true;
}

// ---------------------------------------------------------------------------
// Time-series queries
// ---------------------------------------------------------------------------

int Mesh2DH5Reader::timeCount() const
{
    const Hdf5Lock lock(hdf5Mutex());
    // Re-read every call so live-tail works as engine appends.
    int n = 0;
    readDim_("time", 0, n);
    return n;
}

bool Mesh2DH5Reader::readTimes(std::vector<double>& times) const
{
    const Hdf5Lock lock(hdf5Mutex());
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
    const Hdf5Lock lock(hdf5Mutex());
    return readFaceFieldAt("Mesh2_face_depth", timeIdx, depths);
}

QVector<Mesh2DResultVariable> Mesh2DH5Reader::faceVariables(QStringList* warnings) const
{
    const Hdf5Lock lock(hdf5Mutex());
    if (warnings) warnings->clear();
    QVector<Mesh2DResultVariable> variables;
    if (file_id_ < 0) {
        if (warnings) warnings->append(QStringLiteral("No result file is open."));
        return variables;
    }
    const hid_t file = static_cast<hid_t>(file_id_);
    H5G_info_t info{};
    if (H5Gget_info(file, &info) < 0) return variables;
    const int cells = triangleCount();
    for (hsize_t index = 0; index < info.nlinks; ++index) {
        const ssize_t size = H5Lget_name_by_idx(file, ".", H5_INDEX_NAME, H5_ITER_INC,
                                               index, nullptr, 0, H5P_DEFAULT);
        if (size <= 0 || size > 4096) continue;
        std::vector<char> bytes(std::size_t(size) + 1, '\0');
        if (H5Lget_name_by_idx(file, ".", H5_INDEX_NAME, H5_ITER_INC, index,
                             bytes.data(), bytes.size(), H5P_DEFAULT) < 0) continue;
        const QString name = QString::fromUtf8(bytes.data());
        if (!name.startsWith("Mesh2_face_") || name == "Mesh2_face_nodes" || name == "Mesh2_face_nv") continue;
        variables += describeFaceDataset(file, name, cells, warnings);
    }
    return variables;
}

bool Mesh2DH5Reader::readFaceVariableAt(const Mesh2DResultVariable& variable, int timeIdx,
                                      std::vector<float>& values,
                                      std::vector<Mesh2DValueStatus>& status) const
{
    const Hdf5Lock lock(hdf5Mutex());
    using V = Mesh2DResultVariable;
    using S = Mesh2DValueStatus;
    values.clear(); status.clear();
    if (file_id_ < 0) return setError_(QStringLiteral("No result file is open."));
    const hid_t file = static_cast<hid_t>(file_id_);
    const int cells = triangleCount();
    const auto candidates = describeFaceDataset(file, variable.dataset, cells, nullptr);
    int row = -1;
    V resolved;
    for (int i = 0; i < candidates.size(); ++i)
        if (candidates[i].key() == variable.key() && candidates[i].zone == variable.zone) {
            row = i; resolved = candidates[i]; break;
        }
    if (row < 0) return setError_(QStringLiteral("Requested result identity is unavailable: %1").arg(variable.key()));
    const bool independent = resolved.temporal == V::Temporal::Static || resolved.temporal == V::Temporal::Envelope;
    if (!independent && (timeIdx < 0 || timeIdx >= timeCount()))
        return setError_(QStringLiteral("Requested report time is unavailable."));
    const int rank = independent ? 1 : (!resolved.species.isEmpty() || resolved.layer >= 0) ? 3 : 2;
    std::vector<float> out; std::vector<S> flags;
    QString error;
    const QByteArray dataset = resolved.dataset.toUtf8();
    if (!readCellSlice(file, dataset.constData(), cells, rank, timeIdx, row, out, flags, error))
        return setError_(error);
    const auto readMask = [&](const char* name, int maskRank, int layer,
                              std::vector<float>& data, std::vector<S>& valid) {
        return readCellSlice(file, name, cells, maskRank, timeIdx, layer, data, valid, error);
    };
    const auto invalidate = [&](int cell, S reason) {
        if (flags[std::size_t(cell)] == S::Missing) return;
        flags[std::size_t(cell)] = reason;
        if (reason == S::Missing) out[std::size_t(cell)] = std::numeric_limits<float>::quiet_NaN();
    };
    std::vector<float> mask; std::vector<S> maskStatus;
    if (resolved.zone == V::Zone::Sigma) {
        if (!readMask("Mesh2_face_gw_closure", 1, -1, mask, maskStatus)) return setError_(error);
        for (int cell = 0; cell < cells; ++cell) {
            if (maskStatus[std::size_t(cell)] != S::Valid) invalidate(cell, S::Missing);
            else if (mask[std::size_t(cell)] != 2) invalidate(cell, S::NotApplicable);
        }
    } else if (resolved.zone == V::Zone::Saturated || resolved.dataset == "Mesh2_face_species_conc") {
        const char* name = resolved.zone == V::Zone::Saturated ? "Mesh2_face_gw_hg" : "Mesh2_face_depth";
        if (hasFaceField(name)) {
            if (!readMask(name, 2, -1, mask, maskStatus)) return setError_(error);
            for (int cell = 0; cell < cells; ++cell) {
                if (maskStatus[std::size_t(cell)] != S::Valid || mask[std::size_t(cell)] < 0) invalidate(cell, S::Missing);
                else if (mask[std::size_t(cell)] == 0) invalidate(cell, S::Waterless);
            }
        }
    } else if (resolved.zone == V::Zone::Unsaturated
               && hasFaceField("Mesh2_face_gw_closure") && hasFaceField("Mesh2_face_gw_hu")) {
        std::vector<float> closure; std::vector<S> closureStatus;
        if (!readMask("Mesh2_face_gw_closure", 1, -1, closure, closureStatus)
            || !readMask("Mesh2_face_gw_hu", 2, -1, mask, maskStatus)) return setError_(error);
        bool needsSigma = false;
        for (int cell = 0; cell < cells; ++cell) {
            const std::size_t c = std::size_t(cell);
            if (closureStatus[c] != S::Valid) invalidate(cell, S::Missing);
            else if (closure[c] == 2) needsSigma = true;
            else if (closure[c] != 0 && closure[c] != 1) invalidate(cell, S::Missing);
            else if (maskStatus[c] != S::Valid || mask[c] < 0) invalidate(cell, S::Missing);
            else if (mask[c] == 0) invalidate(cell, S::Waterless);
        }
        if (needsSigma && hasFaceField("Mesh2_face_gw_theta_sigma")) {
            const auto layers = describeFaceDataset(file, "Mesh2_face_gw_theta_sigma", cells, nullptr);
            if (layers.isEmpty()) return setError_(QStringLiteral("Cannot resolve unsaturated-zone sigma water content."));
            std::vector<bool> wet(std::size_t(cells), false), known(std::size_t(cells), true);
            for (int layer = 0; layer < layers.size(); ++layer) {
                if (!readMask("Mesh2_face_gw_theta_sigma", 3, layer, mask, maskStatus)) return setError_(error);
                for (int cell = 0; cell < cells; ++cell) {
                    const std::size_t c = std::size_t(cell);
                    if (maskStatus[c] != S::Valid || mask[c] < 0) known[c] = false;
                    else if (mask[c] > 0) wet[c] = true;
                }
            }
            for (int cell = 0; cell < cells; ++cell) {
                const std::size_t c = std::size_t(cell);
                if (closure[c] != 2 || closureStatus[c] != S::Valid) continue;
                if (!known[c]) invalidate(cell, S::Missing);
                else if (!wet[c]) invalidate(cell, S::Waterless);
            }
        }
    }
    values.swap(out); status.swap(flags);
    return true;
}

bool Mesh2DH5Reader::hasFaceField(const char* dataset) const
{
    if (file_id_ < 0 || !dataset) return false;
    auto it = cached_has_face_field_.find(dataset);
    if (it == cached_has_face_field_.end()) {
        const Hdf5Lock lock(hdf5Mutex());
        const htri_t ex = H5Lexists(static_cast<hid_t>(file_id_), dataset, H5P_DEFAULT);
        it = cached_has_face_field_.emplace(dataset, ex > 0).first;
    }
    return it->second;
}

bool Mesh2DH5Reader::readFaceFieldAt(const char* dataset, int timeIdx,
                                     std::vector<float>& values) const
{
    const Hdf5Lock lock(hdf5Mutex());
    values.clear();
    if (file_id_ < 0 || !dataset) return setError_(QStringLiteral("Result file or dataset is unavailable."));
    if (timeIdx < 0 || timeIdx >= timeCount()) return setError_(QStringLiteral("Requested report time is unavailable."));
    std::vector<Mesh2DValueStatus> status;
    QString error;
    return readCellSlice(static_cast<hid_t>(file_id_), dataset, triangleCount(), 2,
                         timeIdx, -1, values, status, error) || setError_(error);
}

bool Mesh2DH5Reader::readFaceEnvelope(const char* dataset, std::vector<float>& values) const
{
    const Hdf5Lock lock(hdf5Mutex());
    values.clear();
    if (file_id_ < 0 || !dataset) return setError_(QStringLiteral("Result file or dataset is unavailable."));
    std::vector<Mesh2DValueStatus> status;
    QString error;
    return readCellSlice(static_cast<hid_t>(file_id_), dataset, triangleCount(), 1,
                         0, -1, values, status, error) || setError_(error);
}

bool Mesh2DH5Reader::readVertexHeadsAt(int timeIdx,
                                        std::vector<double>& heads) const
{
    const Hdf5Lock lock(hdf5Mutex());
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
    const Hdf5Lock lock(hdf5Mutex());
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
    const Hdf5Lock lock(hdf5Mutex());
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
    const Hdf5Lock lock(hdf5Mutex());
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
