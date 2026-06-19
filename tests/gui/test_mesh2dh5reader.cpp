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

using openswmmvis::io::Mesh2DH5Reader;

namespace {

// VS-vertex — fixture node heads (z + depth at the vertex), [n_time, n_vert].
// t=0 dry (head == ground), t=1 shallow, t=2 peak. Vertex z = {10,10,11,11}.
constexpr double kNodeHeads[3 * 4] = {
    10.00, 10.00, 11.00, 11.00,   // t=0 — dry: head pinned at ground
    10.05, 10.05, 11.00, 11.02,   // t=1
    10.20, 10.25, 11.10, 11.30,   // t=2 (peak)
};

QString writeFixture(bool withNodeHead = false)
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
            0, 1, 3,   // T0
            0, 3, 2,   // T1
        };
        hsize_t dims[2] = { n_face, 3 };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_face_nodes", H5T_NATIVE_INT, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, conn);
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

    // /Mesh2_node_head [n_time, n_vert] — only on the "new engine" fixture;
    // the plain fixture doubles as the older-file probe case.
    if (withNodeHead) {
        hsize_t dims[2] = { n_time, n_vert };
        hid_t sp = H5Screate_simple(2, dims, nullptr);
        hid_t ds = H5Dcreate2(fid, "Mesh2_node_head", H5T_NATIVE_DOUBLE, sp,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                 kNodeHeads);
        H5Dclose(ds);
        H5Sclose(sp);
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
    }

    void cleanupTestCase()
    {
        if (!fixturePath_.isEmpty()) QFile::remove(fixturePath_);
        // The with-heads fixture stays on disk for review (transparent-IO).
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

private:
    QString fixturePath_;
    QString fixtureWithHeadsPath_;
};

QTEST_GUILESS_MAIN(TestMesh2DH5Reader)
#include "test_mesh2dh5reader.moc"
