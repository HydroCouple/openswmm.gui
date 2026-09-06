/*!
 * \file   test_mesh2dh5reader.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice CF.MVP — unit test for openswmmvis::io::Mesh2DH5Reader. The test
 * fabricates a tiny CF/UGRID-1.0 HDF5 file (2 triangles, 4 vertices, 3 time
 * steps) that mirrors what Default2DOutputPlugin would write, then exercises
 * the reader against it.
 *
 * Keeping the fixture self-generated means we don't need to run the engine
 * (which requires the full vcpkg HDF5 + SUNDIALS stack to be installed) to
 * exercise the GUI side of the loop.
 */
#include <QtTest/QtTest>
#include <QTemporaryFile>

#include <hdf5.h>

#include "io/mesh2dh5reader.h"
#include "mesh/meshcellgeom.h"

#include <cmath>
#include <cstring>

using openswmmvis::io::Mesh2DH5Reader;

namespace {

// VS-vertex — fixture node heads (z + depth at the vertex), [n_time, n_vert].
// t=0 dry (head == ground), t=1 shallow, t=2 peak. Vertex z = {10,10,11,11}.
constexpr double kNodeHeads[3 * 4] = {
    10.00, 10.00, 11.00, 11.00,   // t=0 — dry: head pinned at ground
    10.05, 10.05, 11.00, 11.02,   // t=1
    10.20, 10.25, 11.10, 11.30,   // t=2 (peak)
};

// SIGNED render depths (η_v − z_v) matching kNodeHeads, with v2 carrying the
// sub-cell-shoreline NEGATIVE at t=1/t=2 that the reader must NOT clamp.
constexpr double kNodeSignedDepths[3 * 4] = {
     0.00,  0.00,  0.00,  0.00,   // t=0 — all dry
     0.05,  0.05, -0.40,  0.02,   // t=1
     0.20,  0.25, -0.10,  0.30,   // t=2 (peak)
};

void writeStringAttr(hid_t obj, const char* name, const char* value)
{
    hid_t type = H5Tcopy(H5T_C_S1);
    H5Tset_size(type, std::strlen(value) + 1);
    H5Tset_strpad(type, H5T_STR_NULLTERM);
    hid_t space = H5Screate(H5S_SCALAR);
    hid_t attr = H5Acreate2(obj, name, type, space,
                            H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, type, value);
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(type);
}

QString writeFixture(bool withNodeHead = false, int startIndex = 0)
{
    QString path;
    if (withNodeHead) {
        // Transparent-IO rule (CLAUDE.md): new test artefacts go to a
        // user-reviewable location (cwd = the build dir under ctest), not a
        // temp folder.
        QDir out(QDir::currentPath() + QStringLiteral("/test_artifacts"));
        if (!out.exists()) QDir().mkpath(out.absolutePath());
        path = out.filePath(QStringLiteral("mesh2d_fixture_with_heads.h5"));
    } else {
        // Use a temp file path — leak the QTemporaryFile (it auto-deletes when
        // cleaned up but we want the file to persist for the H5Fopen call).
        QTemporaryFile tmp(QDir::tempPath() + "/mesh2d_fixture_XXXXXX.h5");
        tmp.setAutoRemove(false);
        if (!tmp.open()) return {};
        path = tmp.fileName();
    }

    hid_t fid = H5Fcreate(path.toUtf8().constData(),
                           H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
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
        auto writeD = [&](const char* name, const double* data) {
            hid_t ds = H5Dcreate2(fid, name, H5T_NATIVE_DOUBLE, sp,
                                    H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
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
            0 + startIndex, 1 + startIndex, 3 + startIndex,   // T0
            0 + startIndex, 3 + startIndex, 2 + startIndex,   // T1
        };
        hsize_t dims[2] = { n_face, 3 };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_nodes", H5T_NATIVE_INT, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
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
        hid_t ds = H5Dcreate2(fid, "time", H5T_NATIVE_DOUBLE, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, times);
        H5Dclose(ds);
        H5Sclose(sp);
    }

    // /Mesh2_face_depth [n_time, n_face]
    {
        const double depths[n_time * n_face] = {
            0.00, 0.00,   // t=0
            0.05, 0.10,   // t=1
            0.20, 0.30,   // t=2 (peak)
        };
        hsize_t dims[2] = { n_time, n_face };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_depth", H5T_NATIVE_DOUBLE, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, depths);
        H5Dclose(ds);
        H5Sclose(sp);
    }

    // /Mesh2_edge_flux [n_time, n_face, 3] — the historical all-triangle
    // width; the reader must repack it to stride 4.
    {
        double flux[n_time * n_face * 3];
        for (int i = 0; i < n_time * n_face * 3; ++i) flux[i] = 0.01 * i;
        hsize_t dims[3] = { n_time, n_face, 3 };
        hid_t sp = H5Screate_simple(3, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_edge_flux", H5T_NATIVE_DOUBLE, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, flux);
        H5Dclose(ds);
        H5Sclose(sp);
    }

    // /Mesh2_node_head + /Mesh2_node_depth [n_time, n_vert] — only on the
    // "new engine" fixture; the plain fixture doubles as the older-file
    // probe case for both datasets.
    if (withNodeHead) {
        hsize_t dims[2] = { n_time, n_vert };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_node_head", H5T_NATIVE_DOUBLE, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                 kNodeHeads);
        H5Dclose(ds);
        hid_t ds2 = H5Dcreate2(fid, "Mesh2_node_depth", H5T_NATIVE_DOUBLE, sp,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds2, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                 kNodeSignedDepths);
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
void appendCrsVariable(const QString& path,
                        const char* modelCrs,
                        double metresPerModelUnit)
{
    hid_t fid = H5Fopen(path.toUtf8().constData(), H5F_ACC_RDWR, H5P_DEFAULT);
    Q_ASSERT(fid >= 0);

    hid_t sp = H5Screate(H5S_SCALAR);
    hid_t ds = H5Dcreate2(fid, "crs", H5T_NATIVE_INT, sp,
                           H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    writeStringAttr(ds, "units", "m");
    if (modelCrs) writeStringAttr(ds, "model_crs", modelCrs);
    {
        hid_t asp  = H5Screate(H5S_SCALAR);
        hid_t attr = H5Acreate2(ds, "metres_per_model_unit", H5T_NATIVE_DOUBLE,
                                asp, H5P_DEFAULT, H5P_DEFAULT);
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
    0, 1, 4, -1,   // T0
    0, 4, 3, -1,   // T1
    1, 2, 5,  4,   // Q0 (cell 2)
};

QString writeMixedFixture()
{
    QDir out(QDir::currentPath() + QStringLiteral("/test_artifacts"));
    if (!out.exists()) QDir().mkpath(out.absolutePath());
    const QString path = out.filePath(QStringLiteral("mesh2d_fixture_mixed.h5"));

    hid_t fid = H5Fcreate(path.toUtf8().constData(),
                           H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    Q_ASSERT(fid >= 0);

    {
        const double xs[kMixNVert] = {0.0, 1.0, 2.0, 0.0, 1.0, 2.0};
        const double ys[kMixNVert] = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
        hsize_t d = kMixNVert;
        hid_t sp = H5Screate_simple(1, &d, nullptr);
        auto writeD = [&](const char* name, const double* data) {
            hid_t ds = H5Dcreate2(fid, name, H5T_NATIVE_DOUBLE, sp,
                                    H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
            H5Dclose(ds);
        };
        writeD("Mesh2_node_x", xs);
        writeD("Mesh2_node_y", ys);
        writeD("Mesh2_node_z", kMixZ);
        H5Sclose(sp);
    }
    {
        hsize_t dims[2] = { kMixNFace, 4 };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_nodes", H5T_NATIVE_INT, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, kMixConn);
        writeStringAttr(ds, "start_index", "0");
        {
            const int fill = -1;
            hid_t asp  = H5Screate(H5S_SCALAR);
            hid_t attr = H5Acreate2(ds, "_FillValue", H5T_NATIVE_INT, asp,
                                    H5P_DEFAULT, H5P_DEFAULT);
            H5Awrite(attr, H5T_NATIVE_INT, &fill);
            H5Aclose(attr);
            H5Sclose(asp);
        }
        H5Dclose(ds);
        H5Sclose(sp);

        const signed char nv[kMixNFace] = {3, 3, 4};
        hsize_t d = kMixNFace;
        hid_t sp1 = H5Screate_simple(1, &d, nullptr);
        hid_t dn = H5Dcreate2(fid, "Mesh2_face_nv", H5T_NATIVE_SCHAR, sp1,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(dn, H5T_NATIVE_SCHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, nv);
        H5Dclose(dn);
        H5Sclose(sp1);
    }
    {
        const double times[1] = {0.0};
        hsize_t d = 1;
        hid_t sp = H5Screate_simple(1, &d, nullptr);
        hid_t ds = H5Dcreate2(fid, "time", H5T_NATIVE_DOUBLE, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, times);
        H5Dclose(ds);
        H5Sclose(sp);
    }
    {
        const double depths[kMixNFace] = {0.1, 0.2, 0.3};
        hsize_t dims[2] = { 1, kMixNFace };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_depth", H5T_NATIVE_DOUBLE, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
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
        hsize_t dims[2] = { kMixNFace, 4 };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        for (const char* name : {"Mesh2_edge_length", "Mesh2_edge_nx", "Mesh2_edge_ny"}) {
            hid_t ds = H5Dcreate2(fid, name, H5T_NATIVE_DOUBLE, sp,
                                   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, v);
            H5Dclose(ds);
        }
        H5Sclose(sp);
        hsize_t fdims[3] = { 1, kMixNFace, 4 };
        hid_t fsp = H5Screate_simple(3, fdims, nullptr);
        hid_t fds = H5Dcreate2(fid, "Mesh2_edge_flux", H5T_NATIVE_DOUBLE, fsp,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(fds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, v);
        H5Dclose(fds);
        H5Sclose(fsp);
    }

    H5Fclose(fid);
    return path;
}

} // namespace

class TestMesh2DH5Reader : public QObject
{
    Q_OBJECT

private slots:
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

    void cleanupTestCase()
    {
        if (!fixturePath_.isEmpty())        QFile::remove(fixturePath_);
        if (!fixtureWithCrsPath_.isEmpty()) QFile::remove(fixtureWithCrsPath_);
        // The with-heads fixture stays on disk for review (transparent-IO).
    }

    // ----- Issue #155: coordinate reference --------------------------------

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

    void opensAndReportsCounts()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixturePath_), qPrintable(r.lastError()));
        QVERIFY(r.isOpen());
        QCOMPARE(r.vertexCount(),   4);
        QCOMPARE(r.triangleCount(), 2);
        QCOMPARE(r.timeCount(),     3);
    }

    void readsMeshGeometry()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));

        std::vector<double> vx, vy, vz;
        QVERIFY(r.readMeshGeometry(vx, vy, vz));
        QCOMPARE(int(vx.size()), 4);
        QCOMPARE(vx[0], 0.0); QCOMPARE(vy[0], 0.0); QCOMPARE(vz[0], 10.0);
        QCOMPARE(vx[3], 1.0); QCOMPARE(vy[3], 1.0); QCOMPARE(vz[3], 11.0);

        std::vector<std::array<int, 3>> tris;
        QVERIFY(r.readTriangles(tris));
        QCOMPARE(int(tris.size()), 2);
        QCOMPARE(tris[0][0], 0); QCOMPARE(tris[0][1], 1); QCOMPARE(tris[0][2], 3);
        QCOMPARE(tris[1][0], 0); QCOMPARE(tris[1][1], 3); QCOMPARE(tris[1][2], 2);

        // All-triangle file: the display fan IS the file's connectivity, the
        // face map is the identity, cells carry v3 = -1, file width 3.
        QCOMPARE(r.edgeStride(), 3);
        QCOMPARE(r.cellCount(), 2);
        QCOMPARE(r.displayTriangleCount(), 2);
        QCOMPARE(r.triangleFaceMap(), (std::vector<int>{0, 1}));
        std::vector<std::array<int, 4>> cells;
        QVERIFY(r.readCells(cells));
        QCOMPARE(int(cells.size()), 2);
        QCOMPARE(cells[0][0], 0); QCOMPARE(cells[0][1], 1); QCOMPARE(cells[0][2], 3);
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
        QCOMPARE(int(nx.size()),  2 * 4);
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

    void mixedFileReadsCellsAndFaceMap()
    {
        Mesh2DH5Reader r;
        QVERIFY2(r.open(fixtureMixedPath_), qPrintable(r.lastError()));
        QCOMPARE(r.vertexCount(), kMixNVert);
        QCOMPARE(r.cellCount(), kMixNFace);
        QCOMPARE(r.triangleCount(), kMixNFace);      // file face count, not display
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
            verts[i].z  = kMixZ[i];
        }
        mesh::MeshTriangle q;
        q.v0 = 1; q.v1 = 2; q.v2 = 5; q.v3 = 4;
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
        QCOMPARE(ny[mesh::edgeSlot(2, 3)],  23.0f);
    }

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
        QCOMPARE(tris[0][0], 0); QCOMPARE(tris[0][1], 1); QCOMPARE(tris[0][2], 3);
        QCOMPARE(tris[1][0], 0); QCOMPARE(tris[1][1], 3); QCOMPARE(tris[1][2], 2);

        QFile::remove(path);
    }

    void readsTimes()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<double> times;
        QVERIFY(r.readTimes(times));
        QCOMPARE(int(times.size()), 3);
        QCOMPARE(times[0],   0.0);
        QCOMPARE(times[1],  60.0);
        QCOMPARE(times[2], 120.0);
    }

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

    void rejectsOutOfRangeTimeIdx()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixturePath_));
        std::vector<float> d;
        QVERIFY(!r.readDepthsAt(99, d));
        QVERIFY(!r.lastError().isEmpty());
    }

    // ── VS-vertex — /Mesh2_node_head ────────────────────────────────────

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

    void vertexHeadsRejectOutOfRange()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixtureWithHeadsPath_));
        std::vector<double> h;
        QVERIFY(!r.readVertexHeadsAt(99, h));
    }

    // ── Wet-masked render field — /Mesh2_node_depth ─────────────────────

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

    void vertexSignedDepthsRejectOutOfRange()
    {
        Mesh2DH5Reader r;
        QVERIFY(r.open(fixtureWithHeadsPath_));
        std::vector<float> d;
        QVERIFY(!r.readVertexSignedDepthsAt(99, d));
    }

    // ── Generic per-face field reader (rainfall / rain_cum plotting) ─────

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
