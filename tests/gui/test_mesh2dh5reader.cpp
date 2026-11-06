/**
 * @file      test_mesh2dh5reader.cpp
 * @brief     Tests for mesh2dh5reader.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice CF.MVP — unit test for openswmmvis::io::Mesh2DH5Reader. The test
 * fabricates a tiny CF/UGRID-1.0 HDF5 file (2 triangles, 4 vertices, 3 time
 * steps) that mirrors what Default2DOutputPlugin would write, then exercises
 * the reader against it.
 *
 * Keeping the fixture self-generated means we don't need to run the engine
 * (which requires the full vcpkg HDF5 + SUNDIALS stack to be installed) to
 * exercise the GUI side of the loop.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest/QtTest>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <hdf5.h>

#include "io/mesh2dh5reader.h"
#include "mesh/meshcellgeom.h"

#include <cmath>
#include <cstring>
#include <limits>

using openswmmvis::io::Mesh2DH5Reader;

namespace
{

// Test-only filter after decompression counts actual chunk decodes. It leaves
// bytes untouched; generated diagnostic files are read by this registered test.
size_t decodedChunks = 0, decodedBytes = 0;
/**
 * @brief Count reverse-filter chunk decode calls without changing their buffer contents.
 * @param[in] flags HDF5 filter flags; only H5Z_FLAG_REVERSE updates global counters.
 * @param[in] bytes Input byte count returned unchanged, added to decodedBytes for decompression.
 * @return bytes, denoting unchanged successful filter payload; all unnamed filter
 * parameters/client data/ buffer pointers ignored, no buffer allocation or ownership transfer.
 * Mutates test-global decodedChunks/ decodedBytes without synchronization, use only serialized
 * HDF5 diagnostic reads.
 */
size_t countDecodedChunks(unsigned flags, size_t, const unsigned *, size_t bytes, size_t *, void **)
{
    if (flags & H5Z_FLAG_REVERSE) {
        ++decodedChunks;
        decodedBytes += bytes;
    }
    return bytes;
}

// VS-vertex — fixture node heads (z + depth at the vertex), [n_time, n_vert].
// t=0 dry (head == ground), t=1 shallow, t=2 peak. Vertex z = {10,10,11,11}.
constexpr double kNodeHeads[3 * 4] = {
    10.00, 10.00, 11.00, 11.00, // t=0 — dry: head pinned at ground
    10.05, 10.05, 11.00, 11.02, // t=1
    10.20, 10.25, 11.10, 11.30, // t=2 (peak)
};

// SIGNED render depths (η_v − z_v) matching kNodeHeads, with v2 carrying the
// sub-cell-shoreline NEGATIVE at t=1/t=2 that the reader must NOT clamp.
constexpr double kNodeSignedDepths[3 * 4] = {
    0.00, 0.00, 0.00,  0.00, // t=0 — all dry
    0.05, 0.05, -0.40, 0.02, // t=1
    0.20, 0.25, -0.10, 0.30, // t=2 (peak)
};

/**
 * @brief Create a fixed-length null-terminated scalar string attribute on a borrowed HDF5
 * object.
 * @param[in] obj Valid writable object handle, ownership retained by caller.
 * @param[in] name Required nonnull new attribute name, must not already exist for this
 * create-only helper.
 * @param[in] value Required nonnull terminated C string, copied including null terminator.
 * @note Creates/closes local datatype/dataspace/attribute handles, does not check HDF5 errors or
 * return status; caller fixture setup must ensure valid resources. May leave missing/partial
 * metadata on IO failure.
 */
void writeStringAttr(hid_t obj, const char *name, const char *value)
{
    hid_t type = H5Tcopy(H5T_C_S1);
    H5Tset_size(type, std::strlen(value) + 1);
    H5Tset_strpad(type, H5T_STR_NULLTERM);
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t attr = H5Acreate2(obj, name, type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, type, value);
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(type);
}

/**
 * @brief Write a persistent four-vertex/two-triangle/three-frame HDF5 result fixture.
 * @param[in] withNodeHead True includes vertex heads and signed rendering depths, default false.
 * @param[in] startIndex Connectivity index origin, callers use 0 or 1; written as start_index
 * attribute.
 * @return Owned generated path, empty if directory creation fails; HDF5 creation asserted but
 * later writes not comprehensively checked. Writes base_serial_heads_bool_start_index.h5 under
 * configured results review root, truncating that serial name; static serial requires serialized
 * test use. Coordinates/elevations in metres, times 0/60/120 seconds, width 3 edgeflux; closes
 * all fixture handles.
 */
QString writeFixture(bool withNodeHead = false, int startIndex = 0)
{
    static int serial = 0;
    const QString directory = qEnvironmentVariable(
        "SWMMVIS_RESULTS_TEST_OUTPUT",
        QFileInfo(QString::fromUtf8(__FILE__))
            .absoluteDir()
            .absoluteFilePath("../../workplans/artifacts/phase_34_themes_results/reader"));
    if (!QDir().mkpath(directory))
        return {};
    const QString path = QDir(directory).filePath(QStringLiteral("base_%1_heads_%2_start_%3.h5")
                                                      .arg(serial++)
                                                      .arg(withNodeHead)
                                                      .arg(startIndex));

    hid_t fid = H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    Q_ASSERT(fid >= 0);

    constexpr int n_vert = 4;
    constexpr int n_face = 2;
    constexpr int n_time = 3;

    // Mesh2_node_x / _y / _z
    {
        const double xs[n_vert] = {0.0, 1.0, 0.0, 1.0};
        const double ys[n_vert] = {0.0, 0.0, 1.0, 1.0};
        const double zs[n_vert] = {10.0, 10.0, 11.0, 11.0};
        hsize_t d = n_vert;
        hid_t sp = H5Screate_simple(1, &d, nullptr);
        auto writeD = [&](const char *name, const double *data) {
            hid_t ds =
                H5Dcreate2(fid, name, H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
            H5Dclose(ds);
        };
        writeD("Mesh2_node_x", xs);
        writeD("Mesh2_node_y", ys);
        writeD("Mesh2_node_z", zs);
        H5Sclose(sp);
    }

    // Mesh2_face_nodes [n_face, 3]
    {
        const int conn[n_face * 3] = {
            0 + startIndex, 1 + startIndex, 3 + startIndex, // T0
            0 + startIndex, 3 + startIndex, 2 + startIndex, // T1
        };
        hsize_t dims[2] = {n_face, 3};
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_nodes", H5T_NATIVE_INT, sp, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, conn);
        const QByteArray start = QByteArray::number(startIndex);
        writeStringAttr(ds, "start_index", start.constData());
        H5Dclose(ds);
        H5Sclose(sp);
    }

    // /time
    {
        const double times[n_time] = {0.0, 60.0, 120.0};
        hsize_t d = n_time;
        hid_t sp = H5Screate_simple(1, &d, nullptr);
        hid_t ds =
            H5Dcreate2(fid, "time", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, times);
        H5Dclose(ds);
        H5Sclose(sp);
    }

    // /Mesh2_face_depth [n_time, n_face]
    {
        const double depths[n_time * n_face] = {
            0.00, 0.00, // t=0
            0.05, 0.10, // t=1
            0.20, 0.30, // t=2 (peak)
        };
        hsize_t dims[2] = {n_time, n_face};
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_depth", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, depths);
        H5Dclose(ds);
        H5Sclose(sp);
    }

    // /Mesh2_edge_flux [n_time, n_face, 3] — the historical all-triangle
    // width; the reader must repack it to stride 4.
    {
        double flux[n_time * n_face * 3];
        for (int i = 0; i < n_time * n_face * 3; ++i)
            flux[i] = 0.01 * i;
        hsize_t dims[3] = {n_time, n_face, 3};
        hid_t sp = H5Screate_simple(3, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_edge_flux", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, flux);
        H5Dclose(ds);
        H5Sclose(sp);
    }

    // /Mesh2_node_head + /Mesh2_node_depth [n_time, n_vert] — only on the
    // "new engine" fixture; the plain fixture doubles as the older-file
    // probe case for both datasets.
    if (withNodeHead) {
        hsize_t dims[2] = {n_time, n_vert};
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_node_head", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, kNodeHeads);
        H5Dclose(ds);
        hid_t ds2 = H5Dcreate2(fid, "Mesh2_node_depth", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds2, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, kNodeSignedDepths);
        H5Dclose(ds2);
        H5Sclose(sp);
    }

    H5Fclose(fid);
    return path;
}

/*!
 * \brief Add the engine 6.0+ `/crs` variable to an existing fixture.
 *
 * Issue #155 — the coordinates in a `.2d.h5` are always SI metres, so a
 * foot-CRS model needs the file to say so. Written as a separate step so the
 * base fixture keeps standing in for a pre-6.0 file.
 */
/**
 * @brief Add a scalar CRS metadata dataset to an existing owned fixture without rescaling
 * coordinates.
 * @param[in] path Writable existing HDF5 fixture without crs dataset; file opened read/write.
 * @param[in] modelCrs Optional null-terminated model CRS token, null omits model_crs attribute.
 * @param[in] metresPerModelUnit Scalar metre conversion metadata copied verbatim, no validation.
 * @note Writes units m, factor, optional CRS and node-x units attribute; raw stored SI geometry
 * unchanged. Closes all locally opened/created handles, Q_ASSERT checks file open, subsequent
 * failures unchecked.
 */
void appendCrsVariable(const QString &path, const char *modelCrs, double metresPerModelUnit)
{
    hid_t fid = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
    Q_ASSERT(fid >= 0);

    hid_t sp = H5Screate(H5S_SCALAR);
    hid_t ds = H5Dcreate2(fid, "crs", H5T_NATIVE_INT, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    writeStringAttr(ds, "units", "m");
    if (modelCrs)
        writeStringAttr(ds, "model_crs", modelCrs);
    {
        hid_t asp = H5Screate(H5S_SCALAR);
        hid_t attr = H5Acreate2(ds, "metres_per_model_unit", H5T_NATIVE_DOUBLE, asp, H5P_DEFAULT,
                                H5P_DEFAULT);
        H5Awrite(attr, H5T_NATIVE_DOUBLE, &metresPerModelUnit);
        H5Aclose(attr);
        H5Sclose(asp);
    }
    int dummy = 0;
    H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, &dummy);
    H5Dclose(ds);
    H5Sclose(sp);

    // The engine also tags the coordinate variables' storage unit.
    hid_t nx = H5Dopen2(fid, "Mesh2_node_x", H5P_DEFAULT);
    if (nx >= 0) {
        writeStringAttr(nx, "units", "m");
        H5Dclose(nx);
    }

    H5Fclose(fid);
}

/*!
 * \brief Engine 2026-09-06 mixed triangle/quad fixture (UGRID mixed topology):
 * a 2x1 strip — the left unit square as two triangles, the right one as a
 * quad — so `Mesh2_face_nodes` is [3, 4] with `_FillValue = -1` and
 * `Mesh2_face_nv` = {3, 3, 4}; the edge datasets are [3, 4] / [1, 3, 4].
 * Vertex z makes the quad's elevation-ordered (cellGeom) diagonal
 * unambiguous. Kept on disk for review (transparent-IO).
 */
constexpr int kMixNVert = 6;
constexpr int kMixNFace = 3;
constexpr double kMixZ[kMixNVert] = {1.0, 1.1, 1.2, 1.3, 1.4, 1.5};
constexpr int kMixConn[kMixNFace * 4] = {
    0, 1, 4, -1, // T0
    0, 4, 3, -1, // T1
    1, 2, 5, 4,  // Q0 (cell 2)
};

/**
 * @brief Write a persistent UGRID fixture containing two triangles and one quad with stride-four
 * edge data.
 * @return Current-directory/test_artifacts/mesh2d_fixture_mixed.h5 path, existing file
 * truncated. Six vertices, three faces/one frame, face_nv3/3/4, fill-1 and known z choose
 * deterministic display split; edge values 10*cell+edge with triangle padding 0. Creates
 * directory best-effort, HDF5 file asserted, other failures unchecked; closes all local handles,
 * no source engine required.
 */
QString writeMixedFixture()
{
    QDir out(QDir::currentPath() + QStringLiteral("/test_artifacts"));
    if (!out.exists())
        QDir().mkpath(out.absolutePath());
    const QString path = out.filePath(QStringLiteral("mesh2d_fixture_mixed.h5"));

    hid_t fid = H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    Q_ASSERT(fid >= 0);

    {
        const double xs[kMixNVert] = {0.0, 1.0, 2.0, 0.0, 1.0, 2.0};
        const double ys[kMixNVert] = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
        hsize_t d = kMixNVert;
        hid_t sp = H5Screate_simple(1, &d, nullptr);
        auto writeD = [&](const char *name, const double *data) {
            hid_t ds =
                H5Dcreate2(fid, name, H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
            H5Dclose(ds);
        };
        writeD("Mesh2_node_x", xs);
        writeD("Mesh2_node_y", ys);
        writeD("Mesh2_node_z", kMixZ);
        H5Sclose(sp);
    }
    {
        hsize_t dims[2] = {kMixNFace, 4};
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_nodes", H5T_NATIVE_INT, sp, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, kMixConn);
        writeStringAttr(ds, "start_index", "0");
        {
            const int fill = -1;
            hid_t asp = H5Screate(H5S_SCALAR);
            hid_t attr =
                H5Acreate2(ds, "_FillValue", H5T_NATIVE_INT, asp, H5P_DEFAULT, H5P_DEFAULT);
            H5Awrite(attr, H5T_NATIVE_INT, &fill);
            H5Aclose(attr);
            H5Sclose(asp);
        }
        H5Dclose(ds);
        H5Sclose(sp);

        const signed char nv[kMixNFace] = {3, 3, 4};
        hsize_t d = kMixNFace;
        hid_t sp1 = H5Screate_simple(1, &d, nullptr);
        hid_t dn = H5Dcreate2(fid, "Mesh2_face_nv", H5T_NATIVE_SCHAR, sp1, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT);
        H5Dwrite(dn, H5T_NATIVE_SCHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, nv);
        H5Dclose(dn);
        H5Sclose(sp1);
    }
    {
        const double times[1] = {0.0};
        hsize_t d = 1;
        hid_t sp = H5Screate_simple(1, &d, nullptr);
        hid_t ds =
            H5Dcreate2(fid, "time", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, times);
        H5Dclose(ds);
        H5Sclose(sp);
    }
    {
        const double depths[kMixNFace] = {0.1, 0.2, 0.3};
        hsize_t dims[2] = {1, kMixNFace};
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_depth", H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT,
                              H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, depths);
        H5Dclose(ds);
        H5Sclose(sp);
    }
    {
        // Edge datasets at the file's width 4: value = cell*10 + edge,
        // padding slot of a triangle = 0.
        double v[kMixNFace * 4];
        for (int c = 0; c < kMixNFace; ++c)
            for (int e = 0; e < 4; ++e)
                v[c * 4 + e] = (c < 2 && e == 3) ? 0.0 : c * 10.0 + e;
        hsize_t dims[2] = {kMixNFace, 4};
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        for (const char *name : {"Mesh2_edge_length", "Mesh2_edge_nx", "Mesh2_edge_ny"}) {
            hid_t ds =
                H5Dcreate2(fid, name, H5T_NATIVE_DOUBLE, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, v);
            H5Dclose(ds);
        }
        H5Sclose(sp);
        hsize_t fdims[3] = {1, kMixNFace, 4};
        hid_t fsp = H5Screate_simple(3, fdims, nullptr);
        hid_t fds = H5Dcreate2(fid, "Mesh2_edge_flux", H5T_NATIVE_DOUBLE, fsp, H5P_DEFAULT,
                               H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(fds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, v);
        H5Dclose(fds);
        H5Sclose(fsp);
    }

    H5Fclose(fid);
    return path;
}

} // namespace

namespace
{
/**
 * @brief Copy a base HDF5 fixture to a named persistent catalog scenario.
 * @param[in] base Existing read-only source path, preserved.
 * @param[in] name Safe test-owned basename, output name.h5 under configured reader review root.
 * @return Copied path or empty if directory creation/copy fails; existing named output removed
 * first, so failure may leave it absent. No broad cleanup, open handle or native engine
 * ownership.
 */
QString catalogCopy(const QString &base, const QString &name)
{
    const QString directory = qEnvironmentVariable(
        "SWMMVIS_RESULTS_TEST_OUTPUT",
        QFileInfo(QString::fromUtf8(__FILE__))
            .absoluteDir()
            .absoluteFilePath("../../workplans/artifacts/phase_34_themes_results/reader"));
    if (!QDir().mkpath(directory))
        return {};
    const QString path = QDir(directory).filePath(name + ".h5");
    QFile::remove(path);
    return QFile::copy(base, path) ? path : QString();
}
/**
 * @brief Replace one HDF5 dataset and attach face-variable catalog metadata in a test-owned
 * file.
 * @param[in] path Existing writable fixture, opened read/write.
 * @param[in] name Required nonnull dataset path; existing link deleted before replacement.
 * @param[in] dims Nonempty shape fitting HDF5 rank/int and allocation product, trusted fixture
 * values.
 * @param[in] values Contiguous doubles with at least product(dims) entries, borrowed during
 * H5Dwrite.
 * @param[in] units Optional null-terminated unit attribute, null omits it.
 * @param[in] names Optional comma-delimited species labels, empty omits metadata.
 * @param[in] speciesUnits Optional comma-delimited per-species native units, empty omits
 * metadata.
 * @param[in] layout Optional null-terminated axis-description attribute, null omits it.
 * @param[in] fill True adds scalar _FillValue=-9999, default false.
 * @note Sets face/Mesh2 location metadata, asserts file/dataset/write success, closes local
 * handles. No rollback on failed writes, no shape validation, does not publish/modify source
 * base fixture.
 */
void appendCatalogField(const QString &path, const char *name, const std::vector<hsize_t> &dims,
                        const std::vector<double> &values, const char *units,
                        const QString &names = {}, const QString &speciesUnits = {},
                        const char *layout = nullptr, bool fill = false)
{
    const hid_t file = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
    Q_ASSERT(file >= 0);
    if (H5Lexists(file, name, H5P_DEFAULT) > 0)
        H5Ldelete(file, name, H5P_DEFAULT);
    const hid_t space = H5Screate_simple(int(dims.size()), dims.data(), nullptr);
    const hid_t data =
        H5Dcreate2(file, name, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    Q_ASSERT(data >= 0);
    const herr_t written =
        H5Dwrite(data, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data());
    Q_ASSERT(written >= 0);
    Q_UNUSED(written);
    writeStringAttr(data, "location", "face");
    writeStringAttr(data, "mesh", "Mesh2");
    if (units)
        writeStringAttr(data, "units", units);
    if (!names.isEmpty())
        writeStringAttr(data, "species_names", names.toUtf8().constData());
    if (!speciesUnits.isEmpty())
        writeStringAttr(data, "species_units", speciesUnits.toUtf8().constData());
    if (layout)
        writeStringAttr(data, "layout", layout);
    if (fill) {
        const hid_t scalar = H5Screate(H5S_SCALAR);
        const hid_t attr =
            H5Acreate2(data, "_FillValue", H5T_NATIVE_DOUBLE, scalar, H5P_DEFAULT, H5P_DEFAULT);
        const double missing = -9999.;
        H5Awrite(attr, H5T_NATIVE_DOUBLE, &missing);
        H5Aclose(attr);
        H5Sclose(scalar);
    }
    H5Dclose(data);
    H5Sclose(space);
    H5Fclose(file);
}
/**
 * @brief Copy a base fixture and populate surface/groundwater scalar/species/layer catalog
 * cases.
 * @param[in] base Read-only base path with two cells/three times, preserved.
 * @param[in] tag Safe output basename passed to catalogCopy, prior matching scenario replaced.
 * @param[in] names Ordered selected species names, defaults TSS/NO3/MSX:X/__WATER_AGE__;
 * duplicates allowed for malformed-metadata tests, unknown labels use fallback unit mg/L and
 * identity index -1.
 * @param[in] speciesUnits True declares surface per-species units, false leaves them unresolved.
 * @return Owned generated path, catalogCopy failure subsequently reaches asserted append
 * helpers. Writes rank 1/2/3 datasets, units, static fields/closure/status fixtures; no source
 * model or engine run.
 */
QString resultCatalogFixture(const QString &base, const QString &tag,
                             QStringList names = {"TSS", "NO3", "MSX:X", "__WATER_AGE__"},
                             bool speciesUnits = true)
{
    const QString path = catalogCopy(base, tag);
    const QStringList allNames{"TSS", "NO3", "MSX:X", "__WATER_AGE__"};
    const QStringList allUnits{"mg/L", "ug/L", "mmol/L", "h"};
    QStringList units;
    std::vector<double> conc;
    for (int t = 0; t < 3; ++t)
        for (const QString &name : names)
            for (int cell = 0; cell < 2; ++cell) {
                const int identity = allNames.indexOf(name);
                conc.push_back(t == 1 && name == "TSS" && cell == 0 ? 0.
                               : t == 1 && name == "NO3" && cell == 1
                                   ? -9999.
                                   : 100. * t + 10. * identity + cell);
            }
    for (const QString &name : names)
        units << allUnits.value(allNames.indexOf(name), "mg/L");
    const std::vector<hsize_t> dims{3, hsize_t(names.size()), 2};
    appendCatalogField(path, "Mesh2_face_species_conc", dims, conc, "1", names.join(','),
                       speciesUnits ? units.join(',') : QString(), "[time, species, face]", true);
    auto sat = conc, unsat = conc;
    for (std::size_t i = 0; i < conc.size(); ++i) {
        if (i % 2)
            sat[i] = 0.;
        else
            unsat[i] = 0.;
    }
    appendCatalogField(path, "Mesh2_face_gw_sat_conc", dims, sat, "1", names.join(','));
    appendCatalogField(path, "Mesh2_face_gw_unsat_conc", dims, unsat, "1", names.join(','));
    appendCatalogField(path, "Mesh2_face_gw_hg", {3, 2}, {2, 0, 2, 0, 2, 0}, "m");
    appendCatalogField(path, "Mesh2_face_gw_hu", {3, 2}, {0, 0, 0, 0, 0, 0}, "m");
    appendCatalogField(path, "Mesh2_face_gw_recharge", {3, 2}, {0, 0, 0, 1e-6, 0, 1e-6}, "m s-1");
    appendCatalogField(path, "Mesh2_face_gw_bed_elev", {2}, {10, 11}, "m");
    appendCatalogField(path, "Mesh2_face_gw_closure", {2}, {0, 2}, "1");
    appendCatalogField(path, "Mesh2_face_gw_theta_sigma", {3, 2, 2},
                       {0, .2, 0, .3, 0, .2, 0, .3, 0, .2, 0, .3}, "1", {}, {},
                       "[time, layer, face]; layer 0 at the ground surface");
    return path;
}
/**
 * @brief Find the first catalog descriptor with exact dataset/species/layer identity.
 * @param[in] reader Open stable reader, faceVariables may populate its logical internal metadata
 * cache.
 * @param[in] dataset Exact dataset name, preserved.
 * @param[in] species Exact species identity, default empty for nonspecies fields.
 * @param[in] layer Zero-based layer identity, default -1 for unlayered fields.
 * @return Owned descriptor copy on match, default invalid descriptor if absent; no explicit
 * diagnostics or file writes, reader lifetime remains caller-owned.
 */
openswmmvis::io::Mesh2DResultVariable catalogVariable(const Mesh2DH5Reader &reader,
                                                      const QString &dataset,
                                                      const QString &species = {}, int layer = -1)
{
    for (const auto &variable : reader.faceVariables())
        if (variable.dataset == dataset && variable.species == species && variable.layer == layer)
            return variable;
    return {};
}
} // namespace

class TestMesh2DH5Reader : public QObject
{
    Q_OBJECT

private slots:
    /**
     * @brief Verify rank-four groundwater slices distinguish every species/layer key and closure
     * applicability.
     * @note Writes 3 x 2 x 2 x 2 dataset with encoded t/species/layer/cell values, four unique
     * Sigma descriptors, known native units, frame 2 cell 0 correct and non-Sigma cell 1
     * NotApplicable. Own HDF5 fixture only.
     */
    void groundwaterLayerSlicesKeepSpeciesAndLayerCoordinatesDistinct()
    {
        using V = openswmmvis::io::Mesh2DResultVariable;
        const QString path = writeFixture();
        hid_t file = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
        QVERIFY(file >= 0);
        const auto write = [&](const char *name, std::vector<hsize_t> dims,
                               std::vector<double> values) {
            hid_t space = H5Screate_simple(int(dims.size()), dims.data(), nullptr);
            hid_t ds = H5Dcreate2(file, name, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT,
                                  H5P_DEFAULT);
            const bool ok = ds >= 0 && H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL,
                                                H5P_DEFAULT, values.data()) >= 0;
            if (ds >= 0 && dims.size() == 4) {
                writeStringAttr(ds, "species_names", "A,B");
                writeStringAttr(ds, "species_units", "mg/L,ug/L");
                writeStringAttr(ds, "layout", "[time,species,layer,face]; zero-based SIGMA layer");
            }
            if (ds >= 0)
                H5Dclose(ds);
            H5Sclose(space);
            return ok;
        };
        QVERIFY(write("Mesh2_face_gw_closure", {2}, {2, 0}));
        std::vector<double> values(3 * 2 * 2 * 2);
        for (int t = 0; t < 3; ++t)
            for (int species = 0; species < 2; ++species)
                for (int layer = 0; layer < 2; ++layer)
                    for (int cell = 0; cell < 2; ++cell)
                        values[((t * 2 + species) * 2 + layer) * 2 + cell] =
                            100 * t + 10 * species + 2 * layer + cell;
        QVERIFY(write("Mesh2_face_gw_layer_conc", {3, 2, 2, 2}, values));
        H5Fclose(file);
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        QVector<V> layers;
        for (const auto &v : reader.faceVariables())
            if (v.dataset == "Mesh2_face_gw_layer_conc")
                layers.append(v);
        QCOMPARE(layers.size(), 4);
        QSet<QString> keys;
        for (const auto &v : layers) {
            keys.insert(v.key());
            QCOMPARE(v.zone, V::Zone::Sigma);
            QVERIFY(v.unitsKnown);
            std::vector<float> out;
            std::vector<openswmmvis::io::Mesh2DValueStatus> status;
            QVERIFY2(reader.readFaceVariableAt(v, 2, out, status), qPrintable(reader.lastError()));
            QCOMPARE(out[0], float(200 + (v.species == "B" ? 10 : 0) + 2 * v.layer));
            QCOMPARE(status[1], openswmmvis::io::Mesh2DValueStatus::NotApplicable);
        }
        QCOMPARE(keys.size(), 4);
    }

    /**
     * @brief Verify decoded scalar rows remain exact across internal blocks, chunk gaps, scaling
     * and invalid raw values.
     * @note Writes 196625 cells with 1000 cellchunks/ranks 1..3, missing chunks 65/193,
     * fill/missing/NaN/inf/ float-overflow values; scale.125+offset 2.3 applied before float
     * conversion. Frames 0/1/0 compare every value/status, dynamic out-of-range clears outputs;
     * optional READER_DECODE_PARITY writes raw arrays. Creates large_decoded_rows.h5 in review
     * directory, no source engine, substantial local IO/RAM.
     */
    void decodedRowsPreserveBlockBoundariesAndMissingChunks()
    {
        using S = openswmmvis::io::Mesh2DValueStatus;
        constexpr size_t cells = 3 * 65536 + 17, chunkSize = 1000;
        const QString path =
            QFileInfo(fixturePath_).absoluteDir().filePath("large_decoded_rows.h5");
        struct Handle {
            hid_t id;
            herr_t (*close)(hid_t);
            /**
             * @brief Close the owned HDF5 resource when id is nonnegative.
             * @note close must be a valid matching HDF5 closer; result ignored, id=-1 disables
             * cleanup after explicit close. Scope-local ownership, no copying/ownership transfer
             * intended.
             */
            ~Handle()
            {
                if (id >= 0)
                    close(id);
            }
        };
        Handle file{H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT),
                    H5Fclose};
        QVERIFY(file.id >= 0);
        const auto plain = [&](const char *name, std::vector<hsize_t> dims, const double *values) {
            Handle space{H5Screate_simple(int(dims.size()), dims.data(), nullptr), H5Sclose};
            Handle data{H5Dcreate2(file.id, name, H5T_NATIVE_DOUBLE, space.id, H5P_DEFAULT,
                                   H5P_DEFAULT, H5P_DEFAULT),
                        H5Dclose};
            return data.id >= 0 && (!values || H5Dwrite(data.id, H5T_NATIVE_DOUBLE, H5S_ALL,
                                                        H5S_ALL, H5P_DEFAULT, values) >= 0);
        };
        const double times[] = {0, 60};
        QVERIFY(plain("time", {2}, times));
        QVERIFY(plain("Mesh2_face_nodes", {cells, 3}, nullptr));
        const std::vector<double> water(2 * cells, 1);
        QVERIFY(plain("Mesh2_face_depth", {2, cells}, water.data()));
        const auto raw = [](size_t i, int frame) {
            if (i % 257 == 0)
                return -9999.;
            if (i % 263 == 0)
                return -777.;
            if (i % 269 == 0)
                return std::numeric_limits<double>::quiet_NaN();
            if (i % 271 == 0)
                return std::numeric_limits<double>::infinity();
            if (i % 277 == 0)
                return 1e300;
            return i % 37 == 0 ? 0. : (double(i % 79) - 39) * .123456789012345 + frame * .3125;
        };
        const char *names[] = {"Mesh2_face_max_depth", "Mesh2_face_decode_probe",
                               "Mesh2_face_species_conc"};
        for (int rank = 1; rank <= 3; ++rank) {
            std::vector<hsize_t> dims(size_t(rank), 1), chunks(size_t(rank), 1);
            dims.back() = cells;
            chunks.back() = chunkSize;
            if (rank > 1)
                dims[0] = 2;
            Handle space{H5Screate_simple(rank, dims.data(), nullptr), H5Sclose};
            Handle props{H5Pcreate(H5P_DATASET_CREATE), H5Pclose};
            QVERIFY(H5Pset_chunk(props.id, rank, chunks.data()) >= 0);
            Handle data{H5Dcreate2(file.id, names[rank - 1], H5T_NATIVE_DOUBLE, space.id,
                                   H5P_DEFAULT, props.id, H5P_DEFAULT),
                        H5Dclose};
            QVERIFY(data.id >= 0);
            writeStringAttr(data.id, "units", "m");
            if (rank == 3) {
                writeStringAttr(data.id, "species_names", "Tracer");
                writeStringAttr(data.id, "species_units", "mg/L");
            }
            Handle scalar{H5Screate(H5S_SCALAR), H5Sclose};
            for (const auto &attribute :
                 std::vector<std::pair<const char *, double>>{{"_FillValue", -9999},
                                                              {"missing_value", -777},
                                                              {"scale_factor", .125},
                                                              {"add_offset", 2.3}}) {
                Handle a{H5Acreate2(data.id, attribute.first, H5T_NATIVE_DOUBLE, scalar.id,
                                    H5P_DEFAULT, H5P_DEFAULT),
                         H5Aclose};
                QVERIFY(a.id >= 0);
                QVERIFY(H5Awrite(a.id, H5T_NATIVE_DOUBLE, &attribute.second) >= 0);
            }
            for (int t = 0; t < (rank == 1 ? 1 : 2); ++t)
                for (size_t first = 0; first < cells; first += chunkSize) {
                    if (first / chunkSize == 65 || first / chunkSize == 193)
                        continue;
                    const hsize_t count = std::min(chunkSize, cells - first);
                    std::vector<double> values(size_t(count), 0);
                    for (size_t i = 0; i < values.size(); ++i)
                        values[i] = raw(first + i, t);
                    auto offset = std::vector<hsize_t>(size_t(rank), 0),
                         extent = std::vector<hsize_t>(size_t(rank), 1);
                    if (rank > 1)
                        offset[0] = hsize_t(t);
                    offset.back() = first;
                    extent.back() = count;
                    QVERIFY(H5Sselect_hyperslab(space.id, H5S_SELECT_SET, offset.data(), nullptr,
                                                extent.data(), nullptr) >= 0);
                    Handle memory{H5Screate_simple(1, &count, nullptr), H5Sclose};
                    QVERIFY(H5Dwrite(data.id, H5T_NATIVE_DOUBLE, memory.id, space.id, H5P_DEFAULT,
                                     values.data()) >= 0);
                }
        }
        QCOMPARE(H5Fclose(file.id), herr_t(0));
        file.id = -1;
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        QFile parity(qEnvironmentVariable("SWMMVIS_READER_DECODE_PARITY"));
        if (!parity.fileName().isEmpty())
            QVERIFY(parity.open(QIODevice::WriteOnly));
        std::vector<float> values;
        std::vector<S> status;
        for (int rank = 1; rank <= 3; ++rank) {
            const auto variable =
                catalogVariable(reader, names[rank - 1], rank == 3 ? "Tracer" : "");
            QVERIFY(!variable.key().isEmpty());
            for (int frame : {0, 1, 0}) {
                QVERIFY(reader.readFaceVariableAt(variable, frame, values, status));
                QCOMPARE(values.size(), cells);
                QCOMPARE(status.size(), cells);
                for (size_t i = 0; i < cells; ++i) {
                    const double v = raw(i, rank == 1 ? 0 : frame), decoded = v * .125 + 2.3;
                    const bool missing = i / chunkSize == 65 || i / chunkSize == 193 ||
                                         !std::isfinite(v) || !std::isfinite(decoded) ||
                                         std::abs(decoded) > std::numeric_limits<float>::max() ||
                                         v == -9999 || v == -777;
                    QCOMPARE(status[i], missing ? S::Missing : S::Valid);
                    if (missing)
                        QVERIFY(std::isnan(values[i]));
                    else
                        QCOMPARE(values[i], float(decoded));
                }
                if (parity.isOpen()) {
                    QCOMPARE(parity.write(reinterpret_cast<const char *>(values.data()),
                                          qint64(values.size() * sizeof(float))),
                             qint64(values.size() * sizeof(float)));
                    QCOMPARE(parity.write(reinterpret_cast<const char *>(status.data()),
                                          qint64(status.size() * sizeof(S))),
                             qint64(status.size() * sizeof(S)));
                }
            }
            if (rank > 1) {
                QVERIFY(!reader.readFaceVariableAt(variable, 99, values, status));
                QVERIFY(values.empty());
                QVERIFY(status.empty());
            }
        }
    }

    /**
     * @brief Verify domain/zone identities, native units and static/held temporal
     * classifications remain independent.
     * @note SurfaceNO 3 ug/Lknown, saturatedNO 3 units unresolved/keydistinct,
     * unsaturatedkeydistinct, recharge held m/s, aquifer base static/frameCount 0. Copies/writes
     * owned catalog fixture only.
     */
    void resultCatalogSeparatesIdentityUnitsAndTime()
    {
        using V = openswmmvis::io::Mesh2DResultVariable;
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(resultCatalogFixture(fixturePath_, "catalog")));
        const auto surface = catalogVariable(reader, "Mesh2_face_species_conc", "NO3");
        QVERIFY(!surface.key().isEmpty());
        QCOMPARE(surface.domain, V::Domain::Surface);
        QCOMPARE(surface.units, QString("ug/L"));
        QVERIFY(surface.unitsKnown);
        QCOMPARE(surface.frameCount, 3);
        const auto sat = catalogVariable(reader, "Mesh2_face_gw_sat_conc", "NO3");
        QCOMPARE(sat.zone, V::Zone::Saturated);
        QVERIFY(!sat.unitsKnown);
        QVERIFY(sat.units.isEmpty());
        QVERIFY(sat.key() != surface.key());
        const auto unsat = catalogVariable(reader, "Mesh2_face_gw_unsat_conc", "NO3");
        QCOMPARE(unsat.zone, V::Zone::Unsaturated);
        QVERIFY(unsat.key() != sat.key());
        const auto recharge = catalogVariable(reader, "Mesh2_face_gw_recharge");
        QCOMPARE(recharge.temporal, V::Temporal::Held);
        QCOMPARE(recharge.units, QString("m s-1"));
        const auto base = catalogVariable(reader, "Mesh2_face_gw_bed_elev");
        QCOMPARE(base.temporal, V::Temporal::Static);
        QCOMPARE(base.frameCount, 0);
    }

    /**
     * @brief Verify descriptors resolve by species name after file order changes and refuse
     * missing species cleanly.
     * @note NO 3 key stable across reorder, frame 2 values 210/211; subset withoutNO 3
     * returnsfalse,cleared outputs and error. Writes three scenario copies, source base
     * unchanged.
     */
    void speciesResolveByNameAfterReorderAndSubset()
    {
        using S = openswmmvis::io::Mesh2DValueStatus;
        Mesh2DH5Reader first, reordered, subset;
        QVERIFY(first.open(resultCatalogFixture(fixturePath_, "species-original")));
        const auto nitrate = catalogVariable(first, "Mesh2_face_species_conc", "NO3");
        QVERIFY(!nitrate.key().isEmpty());
        QVERIFY(reordered.open(resultCatalogFixture(fixturePath_, "species-reordered",
                                                    {"MSX:X", "__WATER_AGE__", "NO3", "TSS"})));
        QCOMPARE(catalogVariable(reordered, nitrate.dataset, nitrate.species).key(), nitrate.key());
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(reordered.readFaceVariableAt(nitrate, 2, values, status));
        QCOMPARE(values, std::vector<float>({210, 211}));
        QVERIFY(
            subset.open(resultCatalogFixture(fixturePath_, "species-subset", {"TSS", "MSX:X"})));
        QVERIFY(!subset.readFaceVariableAt(nitrate, 2, values, status));
        QVERIFY(values.empty());
        QVERIFY(status.empty());
        QVERIFY(!subset.lastError().isEmpty());
    }

    /**
     * @brief Verify explicit zero, fill-missing, waterless and closure-inapplicable scalar
     * states remain distinct.
     * @note SurfaceTSSzeroValid,NO 3 fillNaNMissing,saturated/unsaturated water-presence masks
     * complementary, Sigma layer applies only to closure 2 cell with theta.3. Local copied HDF5
     * catalog, no simulation.
     */
    void resultValidityDistinguishesZeroFillAndWaterless()
    {
        using S = openswmmvis::io::Mesh2DValueStatus;
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(resultCatalogFixture(fixturePath_, "validity")));
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(reader.readFaceVariableAt(catalogVariable(reader, "Mesh2_face_species_conc", "TSS"),
                                          1, values, status));
        QCOMPARE(values[0], 0.f);
        QCOMPARE(status[0], S::Valid);
        QVERIFY(reader.readFaceVariableAt(catalogVariable(reader, "Mesh2_face_species_conc", "NO3"),
                                          1, values, status));
        QVERIFY(std::isnan(values[1]));
        QCOMPARE(status[1], S::Missing);
        QVERIFY(reader.readFaceVariableAt(catalogVariable(reader, "Mesh2_face_gw_sat_conc", "TSS"),
                                          1, values, status));
        QCOMPARE(status[0], S::Valid);
        QCOMPARE(status[1], S::Waterless);
        QVERIFY(reader.readFaceVariableAt(
            catalogVariable(reader, "Mesh2_face_gw_unsat_conc", "TSS"), 1, values, status));
        QCOMPARE(status[0], S::Waterless);
        QCOMPARE(status[1], S::Valid);
        QVERIFY(reader.readFaceVariableAt(
            catalogVariable(reader, "Mesh2_face_gw_theta_sigma", {}, 1), 1, values, status));
        QCOMPARE(status[0], S::NotApplicable);
        QCOMPARE(status[1], S::Valid);
        QCOMPARE(values[1], .3f);
    }

    /**
     * @brief Verify duplicate species metadata warns/skips ambiguous species and absent unit
     * metadata stays unknown.
     * @note DuplicateTSSdoes not create named descriptors; water-age label alone cannot imply
     * hours units. Two owned fixture copies, base unchanged.
     */
    void malformedSpeciesMetadataIsNotInvented()
    {
        Mesh2DH5Reader reader;
        QVERIFY(
            reader.open(resultCatalogFixture(fixturePath_, "duplicate-species", {"TSS", "TSS"})));
        QStringList warnings;
        const auto vars = reader.faceVariables(&warnings);
        QVERIFY(!warnings.isEmpty());
        for (const auto &variable : vars)
            QVERIFY(variable.species.isEmpty());
        QVERIFY(reader.open(resultCatalogFixture(fixturePath_, "unknown-species-units",
                                                 {"TSS", "__WATER_AGE__"}, false)));
        const auto age = catalogVariable(reader, "Mesh2_face_species_conc", "__WATER_AGE__");
        QVERIFY(!age.key().isEmpty());
        QVERIFY(!age.unitsKnown);
        QVERIFY(age.units.isEmpty());
    }

    /**
     * @brief Verify a dataset whose face extent disagrees with geometry refuses and clears prior
     * output.
     * @note Shape 3 x 3 in two-cellfixture, readfalse/emptyvalues/nonemptyerror; writes
     * wrong-face-size.h5 only.
     */
    void malformedFaceShapeFailsWithoutPartialValues()
    {
        const QString path = catalogCopy(fixturePath_, "wrong-face-size");
        appendCatalogField(path, "Mesh2_face_wrong", {3, 3}, {1, 2, 3, 4, 5, 6, 7, 8, 9}, "m");
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        std::vector<float> values{42};
        QVERIFY(!reader.readFaceFieldAt("Mesh2_face_wrong", 1, values));
        QVERIFY(values.empty());
        QVERIFY(!reader.lastError().isEmpty());
    }

    /**
     * @brief Verify infiltration capacity/remaining diagnostics preserve held native units and
     * explicit zero versus fill.
     * @note Capacity m/s held, remaining m3; frame 1 zeroValid and -9999 NaNMissing. Owned
     * catalog fixture only.
     */
    void aquiferReceivingResultsDistinguishMissingAndZero()
    {
        using S = openswmmvis::io::Mesh2DValueStatus;
        using V = openswmmvis::io::Mesh2DResultVariable;
        const QString path = catalogCopy(fixturePath_, "aquifer-receiving");
        appendCatalogField(path, "Mesh2_face_gw_infil_capacity", {3, 2}, {0, 0, 0, -9999, 1, 2},
                           "m s-1", {}, {}, nullptr, true);
        appendCatalogField(path, "Mesh2_face_gw_infil_remaining", {3, 2}, {0, 0, 0, -9999, 1, 2},
                           "m3", {}, {}, nullptr, true);
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        const auto capacity = catalogVariable(reader, "Mesh2_face_gw_infil_capacity");
        QCOMPARE(capacity.units, QString("m s-1"));
        QCOMPARE(capacity.temporal, V::Temporal::Held);
        const auto remaining = catalogVariable(reader, "Mesh2_face_gw_infil_remaining");
        QCOMPARE(remaining.units, QString("m3"));
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(reader.readFaceVariableAt(capacity, 1, values, status));
        QCOMPARE(values[0], 0.f);
        QCOMPARE(status[0], S::Valid);
        QVERIFY(std::isnan(values[1]));
        QCOMPARE(status[1], S::Missing);
    }

    /**
     * @brief Verify shipped dry/ponded ET results expose diagnostics and close the shared
     * evaporation budget.
     * @note Reads R3_ENGINE_OUTPUT or bundled surface_r3_out/results, potential/stress held with
     * m/s/dimensionless units; final potential=surface+soil+unused+pending within 1e-8 and
     * used<=potential. Ponded uses surface only, dry uses soil only. Read-only existing HDF5
     * files, no generated engine run/output.
     */
    void actualEtResultsExposeTheSharedBudget()
    {
        const QString base = qEnvironmentVariable(
            "SWMMVIS_R3_ENGINE_OUTPUT", QFileInfo(QString::fromUtf8(__FILE__))
                                            .absoluteDir()
                                            .absoluteFilePath("data/surface_r3_out/results"));
        using V = openswmmvis::io::Mesh2DResultVariable;
        for (const char *suffix : {"dry", "ponded"}) {
            Mesh2DH5Reader reader;
            QVERIFY(reader.open(QDir(base).filePath(QString("et_SIGMA_%1.2d.h5").arg(suffix))));
            for (const char *name : {"Mesh2_face_et_pending", "Mesh2_face_et_potential_cum",
                                     "Mesh2_face_et_surface_cum", "Mesh2_face_et_soil_cum",
                                     "Mesh2_face_et_unused_cum", "Mesh2_face_et_refresh"})
                QVERIFY(!catalogVariable(reader, name).key().isEmpty());
            const auto potential = catalogVariable(reader, "Mesh2_face_et_potential");
            QCOMPARE(potential.units, QString("m s-1"));
            QCOMPARE(potential.temporal, V::Temporal::Held);
            const auto stress = catalogVariable(reader, "Mesh2_face_et_stress");
            QCOMPARE(stress.units, QString("1"));
            QCOMPARE(stress.temporal, V::Temporal::Held);
            std::vector<float> p, s, g, u, b;
            std::vector<double> times;
            QVERIFY(reader.readTimes(times));
            QVERIFY(!times.empty());
            const auto last = times.size() - 1;
            QVERIFY(reader.readFaceFieldAt("Mesh2_face_et_potential_cum", last, p));
            QVERIFY(reader.readFaceFieldAt("Mesh2_face_et_surface_cum", last, s));
            QVERIFY(reader.readFaceFieldAt("Mesh2_face_et_soil_cum", last, g));
            QVERIFY(reader.readFaceFieldAt("Mesh2_face_et_unused_cum", last, u));
            QVERIFY(reader.readFaceFieldAt("Mesh2_face_et_pending", last, b));
            QVERIFY(std::abs(p[0] - s[0] - g[0] - u[0] - b[0]) < 1e-8);
            QVERIFY(s[0] + g[0] <= p[0] + 1e-8);
            if (QString(suffix) == "ponded") {
                QCOMPARE(g[0], 0.f);
                QVERIFY(s[0] > 0);
            } else {
                QCOMPARE(s[0], 0.f);
                QVERIFY(g[0] > 0);
            }
        }
    }

    /**
     * @brief Verify generic face-field reading maps declared scalar fill to NaN while retaining
     * explicit zero.
     * @note Adds rainfall with fill-9999, frame 1 returns 0 andNaN; owned scalar-fill.h5 copy
     * only.
     */
    void scalarFillValuesAreMissing()
    {
        const QString path = catalogCopy(fixturePath_, "scalar-fill");
        appendCatalogField(path, "Mesh2_face_rainfall", {3, 2}, {0, 0, 0, -9999, 1, 2}, "m s-1", {},
                           {}, nullptr, true);
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        std::vector<float> values;
        QVERIFY(reader.readFaceFieldAt("Mesh2_face_rainfall", 1, values));
        QCOMPARE(values[0], 0.f);
        QVERIFY(std::isnan(values[1]));
    }

    /**
     * @brief Verify opening different files resets cached dataset presence in both directions.
     * @note Same reader opens base/catalog/base, gw_hg absent/present/absent; source files
     * unchanged.
     */
    void reopenedReaderDoesNotReusePresenceCache()
    {
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(fixturePath_));
        QVERIFY(!reader.hasFaceField("Mesh2_face_gw_hg"));
        QVERIFY(reader.open(resultCatalogFixture(fixturePath_, "reopened-presence")));
        QVERIFY(reader.hasFaceField("Mesh2_face_gw_hg"));
        QVERIFY(reader.open(fixturePath_));
        QVERIFY(!reader.hasFaceField("Mesh2_face_gw_hg"));
    }

    /**
     * @brief Verify unwritten chunk regions are Missing despite HDF5 default zero while
     * explicitly stored zero stays Valid.
     * @note Three-frame rainfall writes only middle row; frame 0 NaNMissing/frame 1
     * zeroValid,frame 3 refuses and clears value/status arrays. Writes owned chunk fixture,
     * closes all HDF5 handles.
     */
    void unwrittenChunksAreMissingButWrittenZeroIsValid()
    {
        using S = openswmmvis::io::Mesh2DValueStatus;
        const QString path = catalogCopy(fixturePath_, "unreported-chunks");
        const hid_t file = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
        QVERIFY(file >= 0);
        const hsize_t dims[]{3, 2}, chunk[]{1, 2}, offset[]{1, 0}, count[]{1, 2};
        const hid_t space = H5Screate_simple(2, dims, nullptr);
        const hid_t props = H5Pcreate(H5P_DATASET_CREATE);
        QVERIFY(H5Pset_chunk(props, 2, chunk) >= 0);
        const hid_t data = H5Dcreate2(file, "Mesh2_face_rainfall", H5T_NATIVE_DOUBLE, space,
                                      H5P_DEFAULT, props, H5P_DEFAULT);
        QVERIFY(data >= 0);
        QVERIFY(H5Sselect_hyperslab(space, H5S_SELECT_SET, offset, nullptr, count, nullptr) >= 0);
        const hid_t memory = H5Screate_simple(2, count, nullptr);
        const double values[]{0, 2};
        QVERIFY(H5Dwrite(data, H5T_NATIVE_DOUBLE, memory, space, H5P_DEFAULT, values) >= 0);
        writeStringAttr(data, "units", "m s-1");
        H5Sclose(memory);
        H5Dclose(data);
        H5Pclose(props);
        H5Sclose(space);
        H5Fclose(file);
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        const auto variable = catalogVariable(reader, "Mesh2_face_rainfall");
        std::vector<float> out;
        std::vector<S> status;
        QVERIFY(reader.readFaceVariableAt(variable, 0, out, status));
        QCOMPARE(status[0], S::Missing);
        QVERIFY(std::isnan(out[0]));
        QVERIFY(reader.readFaceVariableAt(variable, 1, out, status));
        QCOMPARE(status[0], S::Valid);
        QCOMPARE(out[0], 0.f);
        QVERIFY(!reader.readFaceVariableAt(variable, 3, out, status));
        QVERIFY(out.empty());
        QVERIFY(status.empty());
    }

    /**
     * @brief Verify array-valued species-name attributes cannot masquerade as scalar catalog
     * metadata.
     * @note Replaces species_names with two-element fixed-string array, open succeeds but
     * warnings present and affected species dataset omitted. Source base unchanged, malformed
     * scenario retained for review.
     */
    void nonscalarMetadataIsRefusedSafely()
    {
        const QString path = resultCatalogFixture(fixturePath_, "nonscalar-metadata");
        const hid_t file = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
        const hid_t data = H5Dopen2(file, "Mesh2_face_species_conc", H5P_DEFAULT);
        QVERIFY(data >= 0);
        QVERIFY(H5Adelete(data, "species_names") >= 0);
        const hsize_t length = 2;
        const hid_t space = H5Screate_simple(1, &length, nullptr);
        const hid_t type = H5Tcopy(H5T_C_S1);
        QVERIFY(H5Tset_size(type, 4) >= 0);
        const hid_t names =
            H5Acreate2(data, "species_names", type, space, H5P_DEFAULT, H5P_DEFAULT);
        const char values[8]{'T', 'S', 'S', 0, 'N', 'O', '3', 0};
        QVERIFY(H5Awrite(names, type, values) >= 0);
        H5Aclose(names);
        H5Tclose(type);
        H5Sclose(space);
        H5Dclose(data);
        H5Fclose(file);
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        QStringList warnings;
        const auto variables = reader.faceVariables(&warnings);
        QVERIFY(!warnings.isEmpty());
        for (const auto &variable : variables)
            QVERIFY(variable.dataset != "Mesh2_face_species_conc");
    }

    /**
     * @brief Verify explicit groundwater species units are honored and static fields ignore
     * requested display frame.
     * @note DeclaredNO 3 ug/Lknown; static aquifer base requested 99 returns 10/11 Valid using
     * static slice. Writes only owned metadata fixture, no unit inference from species label.
     */
    void groundwaterDeclaredSpeciesUnitsAndStaticSlice()
    {
        using S = openswmmvis::io::Mesh2DValueStatus;
        const QString path = resultCatalogFixture(fixturePath_, "declared-gw-units");
        const hid_t file = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
        const hid_t data = H5Dopen2(file, "Mesh2_face_gw_sat_conc", H5P_DEFAULT);
        QVERIFY(data >= 0);
        writeStringAttr(data, "species_units", "mg/L,ug/L,mmol/L,h");
        H5Dclose(data);
        H5Fclose(file);
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        const auto nitrate = catalogVariable(reader, "Mesh2_face_gw_sat_conc", "NO3");
        QCOMPARE(nitrate.units, QString("ug/L"));
        QVERIFY(nitrate.unitsKnown);
        const auto base = catalogVariable(reader, "Mesh2_face_gw_bed_elev");
        std::vector<float> out;
        std::vector<S> status;
        QVERIFY(reader.readFaceVariableAt(base, 99, out, status));
        QCOMPARE(out, std::vector<float>({10, 11}));
        QCOMPARE(status[0], S::Valid);
    }

    /**
     * @brief Verify genuinely deflated HDF5 rainfall chunks decode, beyond merely advertising
     * compression metadata.
     * @note Requires zlib filter, mandatory deflate level 4, chunk filterMask 0 after flush
     * proves compression; frame 1 reads 1e-6/2e-6. Writes compressed.h5 scenario and releases
     * local HDF5 resources.
     */
    void readsGzipCompressedRainfall()
    {
        // Checking metadata alone is insufficient: older builds wrote a
        // deflate declaration but skipped the filter on every data chunk.
        // A genuinely compressed external result then failed on every read.
        QVERIFY2(H5Zfilter_avail(H5Z_FILTER_DEFLATE) > 0,
                 "HDF5 must include zlib support for compressed result files");
        const QString path = catalogCopy(fixturePath_, "compressed");
        QVERIFY(!path.isEmpty());
        const hid_t fid = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
        QVERIFY(fid >= 0);
        const hsize_t dims[] = {3, 2};
        const hsize_t chunk[] = {1, 2};
        const hid_t space = H5Screate_simple(2, dims, nullptr);
        const hid_t props = H5Pcreate(H5P_DATASET_CREATE);
        QVERIFY(H5Pset_chunk(props, 2, chunk) >= 0);
        const unsigned level = 4;
        // Mandatory deflate: a missing encoder must not silently skip it.
        QVERIFY(H5Pset_filter(props, H5Z_FILTER_DEFLATE, 0, 1, &level) >= 0);
        const hid_t data = H5Dcreate2(fid, "Mesh2_face_rainfall", H5T_NATIVE_DOUBLE, space,
                                      H5P_DEFAULT, props, H5P_DEFAULT);
        QVERIFY(data >= 0);
        const double values[] = {0, 0, 1e-6, 2e-6, 3e-6, 4e-6};
        QVERIFY(H5Dwrite(data, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) >= 0);
        QVERIFY(H5Fflush(fid, H5F_SCOPE_LOCAL) >= 0);
        const hsize_t offset[] = {1, 0};
        unsigned filterMask = ~0u;
        haddr_t address;
        hsize_t size;
        QVERIFY(H5Dget_chunk_info_by_coord(data, offset, &filterMask, &address, &size) >= 0);
        QCOMPARE(filterMask, 0u); // this chunk really passed through deflate
        H5Dclose(data);
        H5Pclose(props);
        H5Sclose(space);
        H5Fclose(fid);

        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        std::vector<float> rainfall;
        QVERIFY(reader.readFaceFieldAt("Mesh2_face_rainfall", 1, rainfall));
        QCOMPARE(rainfall.size(), std::size_t(2));
        QCOMPARE(rainfall[0], float(1e-6));
        QCOMPARE(rainfall[1], float(2e-6));
    }

    /**
     * @brief Generate base, optional-head, declared-CRS and mixed-topology HDF5 fixtures for
     * reader tests.
     * @note Validates persistent paths, CRS EPSG 2249/factor.3048 metadata; review root
     * configurable for base files, mixed fixture uses current-directory/test_artifacts. No
     * native engine needed.
     */
    void initTestCase()
    {
        fixturePath_ = writeFixture();
        QVERIFY(!fixturePath_.isEmpty());
        QVERIFY(QFile::exists(fixturePath_));

        fixtureWithHeadsPath_ = writeFixture(/*withNodeHead=*/true);
        QVERIFY(!fixtureWithHeadsPath_.isEmpty());
        QVERIFY(QFile::exists(fixtureWithHeadsPath_));

        // Issue #155 — a second copy carrying the engine 6.0+ /crs variable,
        // so the same suite covers both the declared and the pre-6.0 file.
        fixtureWithCrsPath_ = writeFixture();
        QVERIFY(!fixtureWithCrsPath_.isEmpty());
        appendCrsVariable(fixtureWithCrsPath_, "EPSG:2249", 0.3048);

        fixtureMixedPath_ = writeMixedFixture();
        QVERIFY(!fixtureMixedPath_.isEmpty());
        QVERIFY(QFile::exists(fixtureMixedPath_));
    }

    /**
     * @brief Retain generated reader inputs and catalog scenarios for human review.
     * @note Intentionally no deletion; individual reader objects close handles at scope exit.
     */
    void cleanupTestCase()
    {
        // Retain generated inputs for review alongside the catalog fixtures.
    }

    // ----- Issue #155: coordinate reference --------------------------------

    /**
     * @brief Verify declared CRS/model-unit metadata reads without silently rescaling stored SI
     * geometry.
     * @note EPSG 2249,.3048 metres/modelunit,storedm;rawx 1 still 1. Read-only fixture access.
     */
    void coordinateReferenceReadsDeclaredCrs()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixtureWithCrsPath_), qPrintable(r.lastError()));

        openswmmvis::io::CoordinateReference ref;
        QVERIFY(r.readCoordinateReference(ref));
        QVERIFY(ref.declared);
        QCOMPARE(ref.crs, QStringLiteral("EPSG:2249"));
        QCOMPARE(ref.metresPerModelUnit, 0.3048);
        QCOMPARE(ref.storedUnits, QStringLiteral("m"));

        // readMeshGeometry still returns the raw stored metres — the factor is
        // metadata for the caller, never applied behind its back.
        std::vector<double> vx, vy, vz;
        QVERIFY(r.readMeshGeometry(vx, vy, vz));
        QCOMPARE(vx[1], 1.0);
    }

    /**
     * @brief Verify absent legacy CRS is explicitly undeclared rather than fabricated metric
     * metadata.
     * @note readfalse,declaredfalse,emptyCRS,factor 1 default retained; reader still opens
     * legacy file.
     */
    void coordinateReferenceUndeclaredOnLegacyFile()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixturePath_), qPrintable(r.lastError()));

        openswmmvis::io::CoordinateReference ref;
        // A pre-6.0 file has no /crs — reported as undeclared, not as an
        // error, and never as a fabricated 1.0 that the caller can't tell
        // apart from a genuine metric declaration.
        QVERIFY(!r.readCoordinateReference(ref));
        QVERIFY(!ref.declared);
        QVERIFY(ref.crs.isEmpty());
        QCOMPARE(ref.metresPerModelUnit, 1.0);
    }

    /**
     * @brief Verify basic fixture opens and reports four vertices, two file cells and three
     * frames.
     * @note Read-only HDF5 handle owned by local reader, no file writes.
     */
    void opensAndReportsCounts()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixturePath_), qPrintable(r.lastError()));
        QVERIFY(r.isOpen());
        QCOMPARE(r.vertexCount(), 4);
        QCOMPARE(r.triangleCount(), 2);
        QCOMPARE(r.timeCount(), 3);
    }

    /**
     * @brief Verify triangle geometry, zero-based connectivity and cell/display identity from
     * the legacy-width fixture.
     * @note Exact coordinates/elevations/connectivity,edgeStride 3,2 cells/2
     * displaytriangles/identityfacemap; readCellsreturnsfour-slotrecordswithv 3=-1. No geometry
     * rewriting or model ownership transfer.
     */
    void readsMeshGeometry()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));

        std::vector<double> vx, vy, vz;
        QVERIFY(r.readMeshGeometry(vx, vy, vz));
        QCOMPARE(int(vx.size()), 4);
        QCOMPARE(vx[0], 0.0);
        QCOMPARE(vy[0], 0.0);
        QCOMPARE(vz[0], 10.0);
        QCOMPARE(vx[3], 1.0);
        QCOMPARE(vy[3], 1.0);
        QCOMPARE(vz[3], 11.0);

        std::vector<std::array<int, 3>> tris;
        QVERIFY(r.readTriangles(tris));
        QCOMPARE(int(tris.size()), 2);
        QCOMPARE(tris[0][0], 0);
        QCOMPARE(tris[0][1], 1);
        QCOMPARE(tris[0][2], 3);
        QCOMPARE(tris[1][0], 0);
        QCOMPARE(tris[1][1], 3);
        QCOMPARE(tris[1][2], 2);

        // All-triangle file: the display fan IS the file's connectivity, the
        // face map is the identity, cells carry v3 = -1, file width 3.
        QCOMPARE(r.edgeStride(), 3);
        QCOMPARE(r.cellCount(), 2);
        QCOMPARE(r.displayTriangleCount(), 2);
        QCOMPARE(r.triangleFaceMap(), (std::vector<int>{0, 1}));
        std::vector<std::array<int, 4>> cells;
        QVERIFY(r.readCells(cells));
        QCOMPARE(int(cells.size()), 2);
        QCOMPARE(cells[0][0], 0);
        QCOMPARE(cells[0][1], 1);
        QCOMPARE(cells[0][2], 3);
        QCOMPARE(cells[0][3], -1);
        QCOMPARE(cells[1][3], -1);
    }

    /*! Width-3 edge datasets come back stride 4 (`[cell*4 + e]`, slot 3 = 0)
     *  — both the stored flux and the vertex-derived geometry fallback. */
    void stride3EdgeArraysRepackedToStride4()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        QCOMPARE(Mesh2DH5Reader::kEdgeStride, 4);

        std::vector<float> flux;
        QVERIFY2(r.readEdgeFluxAt(1, flux), qPrintable(r.lastError()));
        QCOMPARE(int(flux.size()), 2 * 4);
        // Fixture value = 0.01 * (t*6 + face*3 + e).
        for (int c = 0; c < 2; ++c) {
            for (int e = 0; e < 3; ++e)
                QCOMPARE(flux[c * 4 + e], float(0.01 * (6 + c * 3 + e)));
            QCOMPARE(flux[c * 4 + 3], 0.0f);
        }

        std::vector<float> len, nx, ny;
        QVERIFY2(r.readEdgeGeometry(len, nx, ny), qPrintable(r.lastError()));
        QCOMPARE(int(len.size()), 2 * 4);
        QCOMPARE(int(nx.size()), 2 * 4);
        // T0 = (0,1,3) with edge e = (v[(e+1)%3], v[(e+2)%3]): edge 0 =
        // (1,3) length 1, edge 1 = (3,0) the diagonal sqrt(2), edge 2 =
        // (0,1) length 1; padding slot 0.
        QVERIFY(qFuzzyCompare(len[0 * 4 + 0], 1.0f));
        QVERIFY(qFuzzyCompare(len[0 * 4 + 1], float(std::sqrt(2.0))));
        QVERIFY(qFuzzyCompare(len[0 * 4 + 2], 1.0f));
        QCOMPARE(len[0 * 4 + 3], 0.0f);
        // Edge 2 of T0 is (0,1) along y = 0; outward normal points -y.
        QVERIFY(qFuzzyCompare(ny[0 * 4 + 2], -1.0f));
        QVERIFY(std::abs(nx[0 * 4 + 2]) < 1e-6f);
    }

    // ── Mixed triangle/quad file (UGRID [n,4] + _FillValue + face_nv) ────

    /**
     * @brief Verify mixed UGRID cells remain native faces while display triangulation splits
     * only the quad.
     * @note Sixvertices/3 cells/4 displaytriangles,facemap 0/1/2/2,exact native connectivity and
     * cellGeomquad subtriangles; depths stay three per cell with quad.3, not duplicated for
     * rendering. Read only.
     */
    void mixedFileReadsCellsAndFaceMap()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixtureMixedPath_), qPrintable(r.lastError()));
        QCOMPARE(r.vertexCount(), kMixNVert);
        QCOMPARE(r.cellCount(), kMixNFace);
        QCOMPARE(r.triangleCount(), kMixNFace); // file face count, not display
        QCOMPARE(r.edgeStride(), 4);
        QCOMPARE(r.displayTriangleCount(), kMixNFace + 1);

        std::vector<std::array<int, 4>> cells;
        QVERIFY2(r.readCells(cells), qPrintable(r.lastError()));
        QCOMPARE(int(cells.size()), kMixNFace);
        for (int c = 0; c < kMixNFace; ++c)
            for (int k = 0; k < 4; ++k)
                QCOMPARE(cells[c][k], kMixConn[c * 4 + k]);

        // Display fan: T0, T1, then the quad's two cellGeom sub-triangles.
        std::vector<std::array<int, 3>> tris;
        QVERIFY2(r.readTriangles(tris), qPrintable(r.lastError()));
        QCOMPARE(int(tris.size()), kMixNFace + 1);
        QCOMPARE(r.triangleFaceMap(), (std::vector<int>{0, 1, 2, 2}));
        QCOMPARE(tris[0], (std::array<int, 3>{0, 1, 4}));
        QCOMPARE(tris[1], (std::array<int, 3>{0, 4, 3}));

        QVector<mesh::MeshVertex> verts(kMixNVert);
        const double xs[kMixNVert] = {0.0, 1.0, 2.0, 0.0, 1.0, 2.0};
        const double ys[kMixNVert] = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
        for (int i = 0; i < kMixNVert; ++i) {
            verts[i].xy = QPointF(xs[i], ys[i]);
            verts[i].z = kMixZ[i];
        }
        mesh::MeshTriangle q;
        q.v0 = 1;
        q.v1 = 2;
        q.v2 = 5;
        q.v3 = 4;
        const mesh::CellGeom g = mesh::cellGeom(verts, q);
        QCOMPARE(g.nSub, 2);
        QCOMPARE(tris[2], g.sub[0]);
        QCOMPARE(tris[3], g.sub[1]);

        // Per-face datasets stay per CELL (3 values, not 4).
        std::vector<float> d;
        QVERIFY(r.readDepthsAt(0, d));
        QCOMPARE(int(d.size()), kMixNFace);
        QCOMPARE(d[2], 0.3f);
    }

    /**
     * @brief Verify mixed-topology edge flux and geometry retain four slots per cell including
     * triangle padding.
     * @note Exact encoded cell/edge values and zero triangle fourth slot, all arrays
     * 3*4,read-only fixture.
     */
    void mixedFileEdgeArraysAreStride4()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixtureMixedPath_), qPrintable(r.lastError()));

        std::vector<float> flux;
        QVERIFY2(r.readEdgeFluxAt(0, flux), qPrintable(r.lastError()));
        QCOMPARE(int(flux.size()), kMixNFace * 4);
        QCOMPARE(flux[mesh::edgeSlot(2, 3)], 23.0f);
        QCOMPARE(flux[mesh::edgeSlot(0, 3)], 0.0f);
        QCOMPARE(flux[mesh::edgeSlot(1, 2)], 12.0f);

        std::vector<float> len, nx, ny;
        QVERIFY2(r.readEdgeGeometry(len, nx, ny), qPrintable(r.lastError()));
        QCOMPARE(int(len.size()), kMixNFace * 4);
        QCOMPARE(len[mesh::edgeSlot(2, 1)], 21.0f);
        QCOMPARE(ny[mesh::edgeSlot(2, 3)], 23.0f);
    }

    /**
     * @brief Verify declared one-based connectivity normalizes to the same zero-based triangle
     * indices.
     * @note Writes separate startIndex 1 fixture then exact triples 0/1/3 and 0/3/2; base files
     * unchanged.
     */
    void normalizesOneBasedFaceNodes()
    {
        const QString path = writeFixture(/*withNodeHead=*/false,
                                          /*startIndex=*/1);
        QVERIFY(!path.isEmpty());

        Mesh2DH5Reader r;
        QVERIFY2(r.open(path), qPrintable(r.lastError()));
        std::vector<std::array<int, 3>> tris;
        QVERIFY2(r.readTriangles(tris), qPrintable(r.lastError()));
        QCOMPARE(int(tris.size()), 2);
        QCOMPARE(tris[0][0], 0);
        QCOMPARE(tris[0][1], 1);
        QCOMPARE(tris[0][2], 3);
        QCOMPARE(tris[1][0], 0);
        QCOMPARE(tris[1][1], 3);
        QCOMPARE(tris[1][2], 2);
    }

    /**
     * @brief Verify stored result times read as seconds 0/60/120 in order.
     * @note No timestamp conversion or time-origin inference asserted, read-only fixture.
     */
    void readsTimes()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<double> times;
        QVERIFY(r.readTimes(times));
        QCOMPARE(int(times.size()), 3);
        QCOMPARE(times[0], 0.0);
        QCOMPARE(times[1], 60.0);
        QCOMPARE(times[2], 120.0);
    }

    /**
     * @brief Verify the first hydraulic depth frame returns two explicit dry zeros.
     * @note Exactly two float values, read-only fixture.
     */
    void readsDepthsAtZero()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<float> d;
        QVERIFY(r.readDepthsAt(0, d));
        QCOMPARE(int(d.size()), 2);
        QCOMPARE(d[0], 0.0f);
        QCOMPARE(d[1], 0.0f);
    }

    /**
     * @brief Verify final hydraulic frame reads per-cell peak depths.2/.3 exactly after float
     * conversion.
     * @note Read-only fixture, no envelope computation or engine run.
     */
    void readsDepthsAtPeak()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<float> d;
        QVERIFY(r.readDepthsAt(2, d));
        QCOMPARE(int(d.size()), 2);
        QCOMPARE(d[0], 0.20f);
        QCOMPARE(d[1], 0.30f);
    }

    /**
     * @brief Verify dynamic depth frame 99 refuses with a nonempty reader error.
     * @note Does not assert postfailure vector contents in this slot; base fixture remains
     * unchanged.
     */
    void rejectsOutOfRangeTimeIdx()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<float> d;
        QVERIFY(!r.readDepthsAt(99, d));
        QVERIFY(!r.lastError().isEmpty());
    }

    // ── VS-vertex — /Mesh2_node_head ────────────────────────────────────

    /**
     * @brief Verify vertex-head arrays preserve supplied double elevations at two available
     * frames.
     * @note Four vertices, selected values match kNodeHeads exactly; no interpolation or scalar
     * rescaling.
     */
    void readsVertexHeads()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixtureWithHeadsPath_), qPrintable(r.lastError()));
        std::vector<double> h;
        QVERIFY2(r.readVertexHeadsAt(1, h), qPrintable(r.lastError()));
        QCOMPARE(int(h.size()), 4);
        QCOMPARE(h[0], kNodeHeads[4 + 0]);
        QCOMPARE(h[3], kNodeHeads[4 + 3]);

        QVERIFY(r.readVertexHeadsAt(2, h));
        QCOMPARE(h[1], kNodeHeads[8 + 1]);
    }

    /**
     * @brief Verify absent optional vertex heads remain safely absent through cached probes
     * without breaking depth reads.
     * @note Two headreads refuse/nonempty error, later cell depth read succeeds. Read-only
     * legacy file.
     */
    void vertexHeadsAbsentReturnsFalse()
    {
        // Older file (no Mesh2_node_head): false both times — the second
        // call exercises the probe-once cache path.
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<double> h;
        QVERIFY(!r.readVertexHeadsAt(0, h));
        QVERIFY(!r.readVertexHeadsAt(1, h));
        QVERIFY(!r.lastError().isEmpty());
        // Other readers keep working after the failed probe.
        std::vector<float> d;
        QVERIFY(r.readDepthsAt(1, d));
    }

    /**
     * @brief Verify available vertex-head dataset rejects frame 99.
     * @note No output-clear or diagnostic-content assertion beyond false, file unchanged.
     */
    void vertexHeadsRejectOutOfRange()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixtureWithHeadsPath_));
        std::vector<double> h;
        QVERIFY(!r.readVertexHeadsAt(99, h));
    }

    // ── Wet-masked render field — /Mesh2_node_depth ─────────────────────

    /**
     * @brief Verify rendering signed depths retain negative subcell-shoreline values through
     * float readback.
     * @note Four values,frame 1 vertex 2 negative matches fixture,frame 2 vertex 3 matches;
     * hydraulic dry clipping must not be applied to this rendering field. Read-only optional
     * dataset.
     */
    void readsVertexSignedDepthsUnclamped()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixtureWithHeadsPath_), qPrintable(r.lastError()));
        std::vector<float> d;
        QVERIFY2(r.readVertexSignedDepthsAt(1, d), qPrintable(r.lastError()));
        QCOMPARE(int(d.size()), 4);
        QCOMPARE(d[0], float(kNodeSignedDepths[4 + 0]));
        // The NEGATIVE sub-cell-shoreline value must survive (no clamping).
        QCOMPARE(d[2], float(kNodeSignedDepths[4 + 2]));
        QVERIFY(d[2] < 0.0f);

        QVERIFY(r.readVertexSignedDepthsAt(2, d));
        QCOMPARE(d[3], float(kNodeSignedDepths[8 + 3]));
    }

    /**
     * @brief Measure actual compressed chunk decodes for contiguous/scattered regional hydraulic
     * selections.
     * @note 65536 cells default or HYDRAULIC_IO_6M selects 6 million; two chunk layouts, custom
     * registered filter 32024 counts reverse calls/bytes exactly matching read statistics.
     * Sixteen selected cells yield 2 chunks contiguous/fullrow or 32 scattered 1024 chunks;
     * stride 4 paddingzero, unwritten last chunkNaN. Writes flow_streaks HDF5/JSON review files,
     * registers/unregisters process filter on normal completion, shared counters require
     * serialized execution; logs timing without hard gate, substantial IO/RAM.
     */
    void regionalChunkReadBudget()
    {
        const bool large = qEnvironmentVariableIsSet("HYDRAULIC_IO_6M");
        const int cells = large ? 6000000 : 65536;
        const H5Z_class2_t filter = {
            H5Z_CLASS_T_VERS, H5Z_filter_t(32024), 1, 1, "regional test decode counter", nullptr,
            nullptr,          countDecodedChunks};
        QVERIFY(H5Zregister(&filter) >= 0);
        QDir().mkpath("tests/verification/flow_streaks");
        QJsonArray measurements;
        for (int chunkCells : {1024, cells}) {
            const QString path =
                QString("tests/verification/flow_streaks/hydraulics-%1-chunk-%2.h5")
                    .arg(cells)
                    .arg(chunkCells);
            hid_t file =
                H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
            QVERIFY(file >= 0);
            hsize_t timeDim = 1;
            hid_t space = H5Screate_simple(1, &timeDim, nullptr);
            hid_t ds = H5Dcreate2(file, "time", H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT,
                                  H5P_DEFAULT);
            const double time = 0;
            QVERIFY(H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, &time) >= 0);
            H5Dclose(ds);
            H5Sclose(space);
            hsize_t geometryDims[] = {hsize_t(cells), 3};
            space = H5Screate_simple(2, geometryDims, nullptr);
            ds = H5Dcreate2(file, "Mesh2_face_nodes", H5T_NATIVE_INT, space, H5P_DEFAULT,
                            H5P_DEFAULT, H5P_DEFAULT);
            H5Dclose(ds);
            H5Sclose(space);
            for (bool edge : {false, true}) {
                const int width = edge ? 3 : 1, rank = edge ? 3 : 2;
                hsize_t dims[] = {1, hsize_t(cells), 3}, chunks[] = {1, hsize_t(chunkCells), 3};
                space = H5Screate_simple(rank, dims, nullptr);
                hid_t props = H5Pcreate(H5P_DATASET_CREATE);
                QVERIFY(H5Pset_chunk(props, rank, chunks) >= 0);
                QVERIFY(H5Pset_filter(props, H5Z_filter_t(32024), H5Z_FLAG_MANDATORY, 0, nullptr) >=
                        0);
                QVERIFY(H5Pset_deflate(props, 1) >= 0);
                ds = H5Dcreate2(file, edge ? "Mesh2_edge_flux" : "Mesh2_face_depth",
                                H5T_NATIVE_FLOAT, space, H5P_DEFAULT, props, H5P_DEFAULT);
                std::vector<float> row(size_t(cells) * width);
                for (size_t i = 0; i < row.size(); ++i)
                    row[i] = float(i % 97) * .01f;
                const hsize_t written = chunkCells == 1024 ? hsize_t(cells - 1024) : hsize_t(cells);
                const hsize_t offsets[] = {0, 0, 0}, counts[] = {1, written, 3},
                              memoryCount = written * width;
                QVERIFY(H5Sselect_hyperslab(space, H5S_SELECT_SET, offsets, nullptr, counts,
                                            nullptr) >= 0);
                const hid_t memory = H5Screate_simple(1, &memoryCount, nullptr);
                QVERIFY(H5Dwrite(ds, H5T_NATIVE_FLOAT, memory, space, H5P_DEFAULT, row.data()) >=
                        0);
                H5Sclose(memory);
                H5Dclose(ds);
                H5Pclose(props);
                H5Sclose(space);
            }
            H5Fclose(file);
            for (bool scattered : {false, true}) {
                std::vector<int> ids;
                for (int i = 0; i < 16; ++i)
                    ids.push_back(scattered ? i * (cells / 16) : 2000 + i);
                decodedChunks = decodedBytes = 0;
                Mesh2DH5Reader reader;
                QVERIFY(reader.open(path));
                std::vector<float> d, f;
                openswmmvis::io::Mesh2DSubsetReadStats stats;
                QElapsedTimer timer;
                timer.start();
                QVERIFY2(reader.readHydraulicCellsAt(0, ids, d, f, &stats),
                         qPrintable(reader.lastError()));
                const double ms = timer.nsecsElapsed() / 1e6;
                QCOMPARE(d.size(), ids.size());
                QCOMPARE(f.size(), 4 * ids.size());
                for (size_t i = 0; i < ids.size(); ++i) {
                    QCOMPARE(d[i], float(ids[i] % 97) * .01f);
                    for (int e = 0; e < 3; ++e)
                        QCOMPARE(f[4 * i + e], float((3 * ids[i] + e) % 97) * .01f);
                    QCOMPARE(f[4 * i + 3], 0.f);
                }
                QCOMPARE(decodedChunks, stats.touchedChunks);
                QCOMPARE(decodedBytes, stats.touchedChunkBytes);
                QCOMPARE(decodedChunks, size_t(chunkCells == cells || !scattered ? 2 : 32));
                measurements.append(QJsonObject{
                    {"nativeCells", cells},
                    {"chunkCells", chunkCells},
                    {"scattered", scattered},
                    {"selectedCells", int(ids.size())},
                    {"readMs", ms},
                    {"decodedChunks", double(decodedChunks)},
                    {"decodedBytes", double(decodedBytes)},
                    {"snapshotBytes",
                     double((d.size() + f.size()) * sizeof(float) + ids.size() * sizeof(int))}});
            }
            if (chunkCells == 1024) {
                Mesh2DH5Reader reader;
                QVERIFY(reader.open(path));
                std::vector<float> d, f;
                QVERIFY(reader.readHydraulicCellsAt(0, {cells - 1}, d, f));
                QVERIFY(std::isnan(d[0]));
                for (int e = 0; e < 3; ++e)
                    QVERIFY(std::isnan(f[size_t(e)]));
                QCOMPARE(f[3], 0.f);
            }
        }
        QFile report(QString("tests/verification/flow_streaks/hydraulic-io-%1.json").arg(cells));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(measurements).toJson());
        QVERIFY(H5Zunregister(H5Z_filter_t(32024)) >= 0);
    }

    /**
     * @brief Verify regional hydraulic decoding matches full rows under fill/scale/offset
     * metadata and enforces selection cap.
     * @note Owned regional-metadata.h5 copy,depthfill 0/scale 2/offset 1;
     * everyframeNaN/valueequal, maxCells+1 request refuses. No source fixture modification or
     * timing gate.
     */
    void regionalDepthMetadata()
    {
        const QString path = "tests/verification/flow_streaks/regional-metadata.h5";
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile::remove(path);
        QVERIFY(QFile::copy(fixturePath_, path));
        hid_t file = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
        hid_t ds = H5Dopen2(file, "Mesh2_face_depth", H5P_DEFAULT), space = H5Screate(H5S_SCALAR);
        for (const auto &entry : std::vector<std::pair<const char *, double>>{
                 {"_FillValue", 0}, {"scale_factor", 2}, {"add_offset", 1}}) {
            hid_t attr =
                H5Acreate2(ds, entry.first, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT);
            QVERIFY(H5Awrite(attr, H5T_NATIVE_DOUBLE, &entry.second) >= 0);
            H5Aclose(attr);
        }
        H5Sclose(space);
        H5Dclose(ds);
        H5Fclose(file);
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(path));
        for (int t = 0; t < reader.timeCount(); ++t) {
            std::vector<float> full, d, f;
            QVERIFY(reader.readDepthsAt(t, full));
            QVERIFY(reader.readHydraulicCellsAt(t, {0, 1}, d, f));
            for (size_t i = 0; i < d.size(); ++i) {
                if (std::isnan(full[i]))
                    QVERIFY(std::isnan(d[i]));
                else
                    QCOMPARE(d[i], full[i]);
            }
        }
        std::vector<float> d, f;
        std::vector<int> ids(openswmmvis::io::Mesh2DHydraulicFrame::maxCells + 1, 0);
        QVERIFY(!reader.readHydraulicCellsAt(0, ids, d, f));
    }

    /**
     * @brief Verify per-cell regional hydraulic and single-depth reads match full rows for
     * triangle/mixed files.
     * @note Depth/stride 4 fluxexact,statsselected<=5;negative/out-of-range/duplicate/unsorted
     * IDs refuse and clear outputs,empty selection succeeds empty,out-of-rangeframe refuses.
     * Read-only fixtures.
     */
    void regionalHydraulicsMatchFullRows()
    {
        for (const auto &path : {fixturePath_, fixtureMixedPath_}) {
            Mesh2DH5Reader reader;
            QVERIFY(reader.open(path));
            for (int t = 0; t < reader.timeCount(); ++t) {
                std::vector<float> depths, flux;
                QVERIFY(reader.readDepthsAt(t, depths));
                const bool hasFlux = reader.readEdgeFluxAt(t, flux);
                for (int c = 0; c < reader.cellCount(); ++c) {
                    std::vector<float> d, f;
                    openswmmvis::io::Mesh2DSubsetReadStats stats;
                    QVERIFY2(reader.readHydraulicCellsAt(t, {c}, d, f, &stats),
                             qPrintable(reader.lastError()));
                    QCOMPARE(d, std::vector<float>{depths[size_t(c)]});
                    if (hasFlux)
                        QCOMPARE(
                            f, std::vector<float>(flux.begin() + 4 * c, flux.begin() + 4 * c + 4));
                    else
                        QVERIFY(f.empty());
                    float single = 0;
                    QVERIFY(reader.readDepthAt(t, c, single));
                    QCOMPARE(single, d[0]);
                    QVERIFY(stats.selectedValues <= 5);
                }
            }
            for (const std::vector<int> &ids :
                 std::vector<std::vector<int>>{{-1}, {reader.cellCount()}, {0, 0}, {1, 0}}) {
                std::vector<float> d{42}, f{42};
                QVERIFY(!reader.readHydraulicCellsAt(0, ids, d, f));
                QVERIFY(d.empty());
                QVERIFY(f.empty());
            }
            std::vector<float> d{42}, f{42};
            QVERIFY(reader.readHydraulicCellsAt(0, {}, d, f));
            QVERIFY(d.empty());
            QVERIFY(f.empty());
            QVERIFY(!reader.readHydraulicCellsAt(reader.timeCount(), {0}, d, f));
        }
    }

    /**
     * @brief Verify absent optional signed rendering depths refuse repeated probes while
     * hydraulic cell depths remain usable.
     * @note Legacy file, two signed reads false then normal depth succeeds, no writes.
     */
    void vertexSignedDepthsAbsentReturnsFalse()
    {
        // Older file (no Mesh2_node_depth): false both times — the second
        // call exercises the probe-once cache path.
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<float> d;
        QVERIFY(!r.readVertexSignedDepthsAt(0, d));
        QVERIFY(!r.readVertexSignedDepthsAt(1, d));
        // Other readers keep working after the failed probe.
        std::vector<float> cd;
        QVERIFY(r.readDepthsAt(1, cd));
    }

    /**
     * @brief Verify signed rendering depth dataset rejects unavailable frame 99.
     * @note Read-only fixture with heads/depths, no output-clear assertion in this slot.
     */
    void vertexSignedDepthsRejectOutOfRange()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixtureWithHeadsPath_));
        std::vector<float> d;
        QVERIFY(!r.readVertexSignedDepthsAt(99, d));
    }

    // ── Generic per-face field reader (rainfall / rain_cum plotting) ─────

    /**
     * @brief Verify generic regional scalar field reads match full rows and reject invalid
     * order/index/dataset.
     * @note Depthframe 2 cell 1 exact,unsortedIDs/out-of-rangecell/absentvxrefuse with cleared
     * subset; read-only fixture, no chunk-budget assertion.
     */
    void regionalFaceFieldsMatchFullRowsAndRefuseInvalidSelections()
    {
        Mesh2DH5Reader reader;
        QVERIFY(reader.open(fixturePath_));
        std::vector<float> full, subset;
        QVERIFY(reader.readFaceFieldAt("Mesh2_face_depth", 2, full));
        QVERIFY(reader.readFaceFieldCellsAt("Mesh2_face_depth", 2, {1}, subset));
        QCOMPARE(subset.size(), size_t(1));
        QCOMPARE(subset[0], full[1]);
        QVERIFY(!reader.readFaceFieldCellsAt("Mesh2_face_depth", 2, {1, 0}, subset));
        QVERIFY(subset.empty());
        QVERIFY(!reader.readFaceFieldCellsAt("Mesh2_face_depth", 2, {2}, subset));
        QVERIFY(subset.empty());
        QVERIFY(!reader.readFaceFieldCellsAt("Mesh2_face_vx", 2, {0}, subset));
        QVERIFY(subset.empty());
    }
    /**
     * @brief Verify generic named scalar reads equal specialized depths and absent dataset
     * probes do not poison reader state.
     * @note Depthpresence/readexact,absentrain/raincumrefuse,laterdepthsucceed;read-only legacy
     * fixture.
     */
    void readsNamedFaceFieldAndProbesAbsence()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        // Present dataset via the generic path matches readDepthsAt.
        QVERIFY(r.hasFaceField("Mesh2_face_depth"));
        std::vector<float> v, d;
        QVERIFY2(r.readFaceFieldAt("Mesh2_face_depth", 2, v), qPrintable(r.lastError()));
        QVERIFY(r.readDepthsAt(2, d));
        QCOMPARE(v, d);
        // Absent datasets (older engine): false, cached probe, no fallout.
        QVERIFY(!r.hasFaceField("Mesh2_face_rain_cum"));
        QVERIFY(!r.readFaceFieldAt("Mesh2_face_rain_cum", 0, v));
        QVERIFY(!r.readFaceFieldAt("Mesh2_face_rainfall", 0, v));
        QVERIFY(r.readDepthsAt(1, d));
    }

private:
    QString fixturePath_;
    QString fixtureWithHeadsPath_;
    QString fixtureWithCrsPath_;
    QString fixtureMixedPath_;
};

QTEST_GUILESS_MAIN(TestMesh2DH5Reader)
#include "test_mesh2dh5reader.moc"
