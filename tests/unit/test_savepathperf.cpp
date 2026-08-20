/*!
 * \file   test_savepathperf.cpp
 * \brief  Save-path perf harness — times the stages of a project save against
 *         a large synthetic mesh, without needing a GUI or a canvas.
 *
 * The save path's dominant cost is mesh::pushMeshEditsToEngine, which the GUI
 * runs on every save. This harness reproduces exactly the engine state that
 * `SWMMVisProjectWindow::saveAs` runs in — engine OPENED, never INITIALIZED
 * (the GUI only initializes a separate engine, in simulationrunner.cpp) — so
 * the numbers it reports are the numbers the user feels.
 *
 * Fixtures come from tests/tools/meshperfgen.cpp; regenerate with
 *
 *   cmake --build build --target mesh_perf_generator
 *   build/tests/tools/mesh_perf_generator tests/perf-data/mesh 20000 40000 …
 *
 * Point the harness at one with SWMM_PROFILE_SAVE_INP (absolute path). The
 * test SKIPs when that is unset, so CI is unaffected:
 *
 *   QT_LOGGING_RULES="openswmm.save.perf=true" \
 *     SWMM_PROFILE_SAVE_INP=$PWD/tests/perf-data/mesh/mesh_160000tri.inp \
 *     build/tests/unit/test_savepathperf
 *
 * Per CLAUDE.md §4.1 the written model lands next to the fixture, not in a
 * temp dir, so it can be reviewed.
 */
#include <gtest/gtest.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QString>
#include <QVector>

#include "mesh/inpmeshreader.h"
#include "mesh/meshedgebc.h"
#include "mesh/meshenginesync.h"
#include "mesh/meshresult.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_2d.h>

#include <cstdlib>
#include <cstdio>

namespace {

QString profileTarget()
{
    const char *p = std::getenv("SWMM_PROFILE_SAVE_INP");
    return (p && *p) ? QString::fromUtf8(p) : QString();
}

} // namespace

TEST(SavePathPerf, StageBreakdown)
{
    const QString inPath = profileTarget();
    if (inPath.isEmpty())
        GTEST_SKIP() << "set SWMM_PROFILE_SAVE_INP=<abs path to .inp> to run";
    ASSERT_TRUE(QFileInfo::exists(inPath))
        << "fixture missing: " << inPath.toStdString()
        << " — regenerate with the mesh_perf_generator target";

    QElapsedTimer t;

    // ---- Stage 1: the GUI's layer-side mesh read -------------------------
    t.start();
    const mesh::InpMeshReadResult rr = mesh::InpMeshReader::read(inPath);
    const qint64 readMs = t.restart();
    ASSERT_TRUE(rr.hasMesh) << rr.errorMsg.toStdString();

    mesh::MeshResult          meshState = rr.mesh;
    QVector<mesh::MeshEdgeBC> bcs       = rr.edgeBCs;

    // ---- Stage 2: engine open (OPENED, deliberately NOT initialized) -----
    t.restart();
    SWMM_Engine e = swmm_engine_create();
    ASSERT_NE(e, nullptr);
    const QByteArray inUtf8  = inPath.toUtf8();
    const QByteArray rptUtf8 = (inPath + QStringLiteral(".saveperf.rpt")).toUtf8();
    const QByteArray outUtf8 = (inPath + QStringLiteral(".saveperf.out")).toUtf8();
    ASSERT_EQ(swmm_engine_open(e, inUtf8.constData(), rptUtf8.constData(),
                               outUtf8.constData(), nullptr), 0);
    const qint64 openMs = t.restart();

    // ---- Stage 3: the mesh push — the hot spot ---------------------------
    QStringList warnings;
    bool        trianglesSynced = false;
    t.restart();
    const bool pushed = mesh::pushMeshEditsToEngine(e, meshState, bcs,
                                                    &warnings, &trianglesSynced);
    const qint64 syncMs = t.restart();
    for (const QString &w : warnings)
        std::fprintf(stderr, "  warn: %s\n", w.toUtf8().constData());

    // ---- Stage 4: the engine's model write -------------------------------
    const QByteArray writeUtf8 =
        (inPath + QStringLiteral(".saveperf.inp")).toUtf8();
    t.restart();
    const int rc = swmm_model_write_with_plugin(e, writeUtf8.constData(), nullptr);
    const qint64 writeMs = t.restart();

    std::fprintf(stderr,
                 "\n[save-perf] %s\n"
                 "  vertices     = %lld\n"
                 "  triangles    = %lld\n"
                 "  edgeBCs      = %lld\n"
                 "  meshRead     = %lld ms\n"
                 "  engineOpen   = %lld ms\n"
                 "  meshPush     = %lld ms   <-- pushMeshEditsToEngine\n"
                 "  engineWrite  = %lld ms\n"
                 "  pushed=%d trianglesSynced=%d writeRc=%d\n\n",
                 QFileInfo(inPath).fileName().toUtf8().constData(),
                 static_cast<long long>(meshState.vertices.size()),
                 static_cast<long long>(meshState.triangles.size()),
                 static_cast<long long>(bcs.size()),
                 static_cast<long long>(readMs),
                 static_cast<long long>(openMs),
                 static_cast<long long>(syncMs),
                 static_cast<long long>(writeMs),
                 pushed ? 1 : 0, trianglesSynced ? 1 : 0, rc);

    EXPECT_TRUE(pushed) << "mesh push bailed out — the timing above is not "
                           "representative of a real save";
    EXPECT_EQ(rc, 0);

    swmm_engine_destroy(e);
}
