/*!
 * \file   test_meshenginesync.cpp
 * \brief  Mesh-edit persistence — layer edits → engine → .inp round trip.
 *
 * Reproduces the reported bug ("edits of the mesh are not persisted to file")
 * end-to-end against a real engine:
 *
 *   1. Open a 2D model and read its mesh + per-edge BCs through InpMeshReader
 *      (exactly what the GUI does on project load).
 *   2. Edit a vertex elevation, an edge conveyance, and an edge stage BC on
 *      that layer-side state.
 *   3. Push the edits into the engine via mesh::pushMeshEditsToEngine (the new
 *      save-path step) and write the model with the built-in writer.
 *   4. Assert the edits survive in the written .inp, AND that Manning's n and
 *      the 1D<->2D coupling maps — which the GUI mesh model does not carry —
 *      are preserved because the engine remained the source of truth.
 *
 * Output .inp is written next to the fixture (the CTest WORKING_DIRECTORY) so
 * it can be reviewed rather than hidden in a temp dir.
 */
#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mesh/inpmeshreader.h"
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
#include "mesh/meshbctype.h"
#include "mesh/meshenginesync.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_2d.h>

namespace {

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

TEST(MeshEngineSync, EditsPersistAndFidelityPreserved)
{
    const QString inPath  = QStringLiteral("mesh_sync_fixture.inp");
    const QString outPath = QStringLiteral("mesh_sync_out.inp");

    // ---- Layer-side state, exactly as the GUI builds it on load ----------
    const mesh::InpMeshReadResult rr = mesh::InpMeshReader::read(inPath);
    ASSERT_TRUE(rr.hasMesh) << rr.errorMsg.toStdString();
    ASSERT_GT(rr.mesh.vertices.size(), 0);
    ASSERT_FALSE(rr.edgeBCs.isEmpty());

    mesh::MeshResult         meshState = rr.mesh;
    QVector<mesh::MeshEdgeBC> bcs      = rr.edgeBCs;

    // The reader lifts the engine's [2D_VERTEX_NODE_MAP] coupling onto the
    // vertex's coupledNode field (distinct from the descriptive tag). Vertex 4
    // couples to J1.
    ASSERT_GT(meshState.vertices.size(), 4);
    EXPECT_EQ(meshState.vertices[4].coupledNode, QStringLiteral("J1"))
        << "[2D_VERTEX_NODE_MAP] coupling not read onto coupledNode";
    // The optional CD / AREA columns land on couplingCd / couplingArea.
    EXPECT_DOUBLE_EQ(meshState.vertices[4].couplingCd,   0.65);
    EXPECT_DOUBLE_EQ(meshState.vertices[4].couplingArea, 2.0);

    // ---- Open the engine on the same model -------------------------------
    // swmm_engine_create, NOT swmm_engine_new: _new opens a programmatic
    // model-BUILDING session, which swmm_engine_open rejects (lifecycle).
    SWMM_Engine e = swmm_engine_create();
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(swmm_engine_open(e, inPath.toUtf8().constData(),
                               "mesh_sync_fixture.rpt", "mesh_sync_fixture.out",
                               nullptr), 0);
    ASSERT_EQ(swmm_engine_initialize(e), 0);

    int nv = 0, nt = 0;
    ASSERT_EQ(swmm_2d_vertex_count(e, &nv), 0);
    ASSERT_EQ(swmm_2d_triangle_count(e, &nt), 0);
    ASSERT_EQ(nv, meshState.vertices.size());
    ASSERT_EQ(bcs.size(), nt * 3);

    // ---- Make the mesh edits ---------------------------------------------
    const double kZ = 555.5;   // vertex 0 elevation
    const double kC = 0.25;    // conveyance on tri 0, edge 0
    const double kH = 97.7;    // stage head on tri 5, edge 0
    const double kF = 0.077;   // specified-flow magnitude on tri 7, edge 0
    const double kN = 0.099;   // Manning's n on triangle 0
    const double kD = 0.313;   // initial depth on triangle 0
    ASSERT_GT(meshState.triangles.size(), 7);
    meshState.vertices[0].z           = kZ;
    meshState.vertices[0].coupledNode = QStringLiteral("J1");      // new coupling
    meshState.vertices[4].couplingCd  = 0.9;                       // toolbar Cd edit
    meshState.vertices[1].tag         = QStringLiteral("VTAG1");   // descriptive tag
    meshState.triangles[0].mannings   = kN;                        // roughness edit
    meshState.triangles[0].initDepth  = kD;                        // init-depth edit
    meshState.triangles[0].tag        = QStringLiteral("REGION_X");// triangle tag
    bcs[0 * 3 + 0].conveyance = kC;
    bcs[5 * 3 + 0].type       = mesh::MeshBCTypes::Type::SpecifiedStageConst;
    bcs[5 * 3 + 0].head       = kH;
    bcs[5 * 3 + 0].tseries.clear();   // ensure no stale name masks the head
    bcs[7 * 3 + 0].type       = mesh::MeshBCTypes::Type::SpecifiedFlowConst;
    bcs[7 * 3 + 0].flow       = kF;
    bcs[7 * 3 + 0].tseries.clear();

    // ---- Push edits into the engine, then serialise ----------------------
    QStringList warnings;
    ASSERT_TRUE(mesh::pushMeshEditsToEngine(e, meshState, bcs, &warnings));
    EXPECT_TRUE(warnings.isEmpty()) << warnings.join("; ").toStdString();
    ASSERT_EQ(swmm_model_write(e, outPath.toUtf8().constData()), 0);
    swmm_engine_destroy(e);

    // ---- Assert the edits landed in the written .inp ---------------------
    const QString text = readAll(outPath);
    ASSERT_FALSE(text.isEmpty());
    EXPECT_TRUE(text.contains(QStringLiteral("555.5")))       // vertex Z
        << "edited vertex elevation not persisted";
    EXPECT_TRUE(text.contains(QStringLiteral("0.25")))        // conveyance
        << "edited edge conveyance not persisted";
    EXPECT_TRUE(text.contains(QStringLiteral("97.7")))        // stage head
        << "edited edge stage BC not persisted";
    EXPECT_TRUE(text.contains(QStringLiteral("0.077")))       // specified flow
        << "edited specified-flow BC not persisted";
    EXPECT_TRUE(text.contains(QStringLiteral("0.099")))       // Manning's n
        << "edited triangle Manning's n not persisted";
    // INIT_DEPTH carries the mesh's length units, so an SI deck round-trips
    // the authored number verbatim (no conversion on either side).
    EXPECT_TRUE(text.contains(QStringLiteral("0.313")))       // initial depth
        << "edited triangle initial depth not persisted";
    EXPECT_TRUE(text.contains(QStringLiteral("REGION_X")))    // triangle tag
        << "edited triangle tag not persisted";
    EXPECT_TRUE(text.contains(QStringLiteral("VTAG1")))       // vertex descriptive tag
        << "edited descriptive vertex tag not persisted";

    // New vertex 0 -> J1 coupling must appear in [2D_VERTEX_NODE_MAP].
    {
        const int sec = text.indexOf(QStringLiteral("[2D_VERTEX_NODE_MAP]"));
        ASSERT_GE(sec, 0) << "vertex node map missing";
        int end = text.indexOf(QStringLiteral("\n["), sec + 1);
        if (end < 0) end = text.size();
        const QString block = text.mid(sec, end - sec);
        bool v0Coupled = false, v4CdArea = false, v0Defaults = false;
        for (const QString &line : block.split('\n')) {
            const QString t = line.trimmed();
            if (t.startsWith(QStringLiteral(";;")) || t.isEmpty()) continue;
            const QStringList tok = t.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
            if (tok.size() >= 2 && tok[0] == QStringLiteral("0")
                && tok[1] == QStringLiteral("J1")) {
                v0Coupled = true;
                // A freshly coupled vertex is pushed with the defaults.
                if (tok.size() >= 4 && tok[2].toDouble() == 0.65
                    && tok[3].toDouble() == 1.0)
                    v0Defaults = true;
            }
            // The toolbar's Cd edit persisted; the fixture AREA survived.
            if (tok.size() >= 4 && tok[0] == QStringLiteral("4")
                && tok[1] == QStringLiteral("J1")
                && tok[2].toDouble() == 0.9 && tok[3].toDouble() == 2.0)
                v4CdArea = true;
        }
        EXPECT_TRUE(v0Coupled)  << "edited vertex coupling not persisted";
        EXPECT_TRUE(v0Defaults) << "fresh coupling did not carry default Cd/Area";
        EXPECT_TRUE(v4CdArea)   << "edited coupling Cd (or fixture AREA) not persisted";
    }

    // ---- Assert no regression on data the GUI model does not carry -------
    EXPECT_TRUE(text.contains(QStringLiteral("0.018")))
        << "per-triangle Manning's n was flattened (regression)";
    EXPECT_TRUE(text.contains(QStringLiteral("[2D_VERTEX_NODE_MAP]")))
        << "vertex coupling map dropped (regression)";
    EXPECT_TRUE(text.contains(QStringLiteral("J1")))
        << "vertex->node coupling lost (regression)";
}

// Node→cell couplings (Plan Part C) — the full GUI persistence chain for the
// rows the Remap 1D↔2D action authors: reader → layer state → engine push →
// swmm_model_write → reader again. Several nodes may share one cell, so the
// written section must carry a repeated triangle index.
TEST(MeshEngineSync, CellCouplingRowsPersistThroughEngine)
{
    const QString inPath  = QStringLiteral("mesh_sync_fixture.inp");
    const QString outPath = QStringLiteral("mesh_sync_cellcoupling_out.inp");

    const mesh::InpMeshReadResult rr = mesh::InpMeshReader::read(inPath);
    ASSERT_TRUE(rr.hasMesh) << rr.errorMsg.toStdString();

    // The fixture authors one row in TAG form ("vault" → ST1) — the reader
    // must resolve it to a triangle index.
    ASSERT_EQ(rr.mesh.cellCouplings.size(), 1);
    const int vaultTri = rr.mesh.cellCouplings[0].tri;
    EXPECT_GE(vaultTri, 0);
    EXPECT_EQ(rr.mesh.cellCouplings[0].nodeId, QStringLiteral("ST1"));
    EXPECT_DOUBLE_EQ(rr.mesh.cellCouplings[0].cd,   0.60);
    EXPECT_DOUBLE_EQ(rr.mesh.cellCouplings[0].area, 10.0);

    mesh::MeshResult          meshState = rr.mesh;
    QVector<mesh::MeshEdgeBC> bcs       = rr.edgeBCs;

    // Two more nodes mapped into ONE cell — what Remap produces for a pair
    // of 1D nodes that fall inside the same triangle.
    const int sharedTri = (vaultTri == 0) ? 1 : 0;
    meshState.cellCouplings.append({ sharedTri, QStringLiteral("J1"),   0.65, 2.0 });
    meshState.cellCouplings.append({ sharedTri, QStringLiteral("OUT1"), 0.65, 2.0 });

    // OPENED, not INITIALIZED — the state the GUI saves from.
    SWMM_Engine e = swmm_engine_create();
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(swmm_engine_open(e, inPath.toUtf8().constData(),
                               "mesh_sync_cellcoupling.rpt",
                               "mesh_sync_cellcoupling.out", nullptr), 0);

    QStringList warnings;
    ASSERT_TRUE(mesh::pushMeshEditsToEngine(e, meshState, bcs, &warnings));
    EXPECT_TRUE(warnings.isEmpty()) << warnings.join("; ").toStdString();

    int engineRows = -1;
    ASSERT_EQ(swmm_2d_triangle_coupling_rows(e, &engineRows), 0);
    EXPECT_EQ(engineRows, 3) << "push must re-author every row, not just the last";

    ASSERT_EQ(swmm_model_write(e, outPath.toUtf8().constData()), 0);
    swmm_engine_destroy(e);

    // Reader sees all three rows back, including the repeated triangle.
    const mesh::InpMeshReadResult back = mesh::InpMeshReader::read(outPath);
    ASSERT_TRUE(back.hasMesh) << back.errorMsg.toStdString();
    ASSERT_EQ(back.mesh.cellCouplings.size(), 3);

    int sharedCount = 0;
    bool vaultBack = false;
    for (const mesh::CellCoupling &cc : back.mesh.cellCouplings) {
        if (cc.tri == sharedTri) ++sharedCount;
        if (cc.nodeId == QStringLiteral("ST1")) {
            vaultBack = (cc.tri == vaultTri);
            EXPECT_NEAR(cc.cd,   0.60, 1e-9);
            EXPECT_NEAR(cc.area, 10.0, 1e-9);
        }
    }
    EXPECT_EQ(sharedCount, 2) << "both nodes sharing one cell must survive";
    EXPECT_TRUE(vaultBack)    << "pre-existing tag-form coupling lost or moved";
}

// The GUI keeps the engine OPENED (never INITIALIZED) so 1D property edits stay
// legal. Mesh edits must still persist in that state — this is the scenario the
// inline-mesh save bug came from. Same edits as above, but no initialize().
TEST(MeshEngineSync, EditsPersistInOpenedState)
{
    const QString inPath  = QStringLiteral("mesh_sync_fixture.inp");
    const QString outPath = QStringLiteral("mesh_sync_opened_out.inp");

    const mesh::InpMeshReadResult rr = mesh::InpMeshReader::read(inPath);
    ASSERT_TRUE(rr.hasMesh) << rr.errorMsg.toStdString();
    mesh::MeshResult         meshState = rr.mesh;
    QVector<mesh::MeshEdgeBC> bcs      = rr.edgeBCs;

    // swmm_engine_create, NOT swmm_engine_new: _new opens a programmatic
    // model-BUILDING session, which swmm_engine_open rejects (lifecycle).
    SWMM_Engine e = swmm_engine_create();
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(swmm_engine_open(e, inPath.toUtf8().constData(),
                               "mesh_sync_opened.rpt", "mesh_sync_opened.out",
                               nullptr), 0);
    // NOTE: deliberately NOT calling swmm_engine_initialize — mirrors the GUI.

    int nv = 0, nt = 0;
    ASSERT_EQ(swmm_2d_vertex_count(e, &nv), 0)
        << "vertex_count must work in OPENED state";
    ASSERT_EQ(swmm_2d_triangle_count(e, &nt), 0);
    ASSERT_EQ(nv, meshState.vertices.size());
    ASSERT_EQ(bcs.size(), nt * 3);

    // Edit a vertex Z, an edge conveyance, and an edge stage BC.
    meshState.vertices[0].z   = 333.3;
    bcs[0 * 3 + 0].conveyance = 0.41;
    bcs[5 * 3 + 0].type       = mesh::MeshBCTypes::Type::SpecifiedStageConst;
    bcs[5 * 3 + 0].head       = 88.8;
    bcs[5 * 3 + 0].tseries.clear();

    QStringList warnings;
    ASSERT_TRUE(mesh::pushMeshEditsToEngine(e, meshState, bcs, &warnings));
    EXPECT_TRUE(warnings.isEmpty()) << warnings.join("; ").toStdString();
    ASSERT_EQ(swmm_model_write(e, outPath.toUtf8().constData()), 0);
    swmm_engine_destroy(e);

    const QString text = readAll(outPath);
    ASSERT_FALSE(text.isEmpty());
    EXPECT_TRUE(text.contains(QStringLiteral("333.3")))
        << "vertex Z edit not saved in OPENED state";
    EXPECT_TRUE(text.contains(QStringLiteral("0.41")))
        << "edge conveyance edit not saved in OPENED state";
    EXPECT_TRUE(text.contains(QStringLiteral("88.8")))
        << "edge stage BC edit not saved in OPENED state";
}

// SVBC A8 — GUI/engine BC parity, per slot, read back through the engine's
// OPENED-state getters (the setters' new mirror image). Against a pre-A8
// engine every getter refuses with SWMM_ERR_BADPARAM (they carried the
// solver-state guard) and the first ASSERT below goes red — that IS the
// falsifier for this gate.
TEST(MeshEngineSync, PushedBcStateReadsBackFromEngine)
{
    const QString inPath = QStringLiteral("mesh_sync_fixture.inp");
    const mesh::InpMeshReadResult rr = mesh::InpMeshReader::read(inPath);
    ASSERT_TRUE(rr.hasMesh) << rr.errorMsg.toStdString();
    mesh::MeshResult          meshState = rr.mesh;
    QVector<mesh::MeshEdgeBC> bcs       = rr.edgeBCs;

    SWMM_Engine e = swmm_engine_create();
    ASSERT_NE(e, nullptr);
    // Deliberately NO swmm_engine_initialize: the GUI holds its engine in
    // the plain OPENED state, and initialize activates the 2D router —
    // which would let the pre-A8 solver-state guard pass and blunt this
    // gate (the EditsPersistInOpenedState convention).
    ASSERT_EQ(swmm_engine_open(e, inPath.toUtf8().constData(),
                               "mesh_sync_parity.rpt", "mesh_sync_parity.out",
                               nullptr), 0);
    int nt = 0;
    ASSERT_EQ(swmm_2d_triangle_count(e, &nt), 0);
    ASSERT_EQ(bcs.size(), nt * 3);
    ASSERT_GT(nt, 7);
    // Pre-drain gate: before prepare_for_edit runs (inside
    // pushMeshEditsToEngine), BoundaryData's live arrays are UNSIZED even
    // for a deck with authored BC rows — the getter must refuse cleanly,
    // not read past the end (unchecked indexing here segfaulted when this
    // gate was first written; the CHECK_BC_SIZED guard is the fix).
    { int et0 = -1;
      EXPECT_NE(swmm_2d_get_edge_bc_type(e, 0, 0, &et0), 0)
          << "pre-drain BC getter must refuse, not read unsized arrays"; }

    // Name-carrying edits on the two slots the fixture keeps as boundary
    // BCs; the names need not resolve — the setters store them verbatim
    // with deferred resolution, so a missing-TS warning is acceptable.
    bcs[5 * 3 + 0].type    = mesh::MeshBCTypes::Type::SpecifiedStageTS;
    bcs[5 * 3 + 0].tseries = QStringLiteral("TS_PARITY");
    bcs[7 * 3 + 0].type    = mesh::MeshBCTypes::Type::SpecifiedFlowTS;
    bcs[7 * 3 + 0].tseries = QStringLiteral("TS_PARITY_Q");

    QStringList warnings;
    ASSERT_TRUE(mesh::pushMeshEditsToEngine(e, meshState, bcs, &warnings));

    using T = mesh::MeshBCTypes::Type;
    const auto engTypeOf = [](T t) {
        switch (t) {
        case T::Wall:                return SWMM_2D_BC_WALL;
        case T::NormalFlow:          return SWMM_2D_BC_NORMAL_FLOW;
        case T::SpecifiedStageConst:
        case T::SpecifiedStageTS:    return SWMM_2D_BC_SPECIFIED_STAGE;
        case T::SpecifiedFlowConst:
        case T::SpecifiedFlowTS:     return SWMM_2D_BC_SPECIFIED_FLOW;
        case T::RatingCurve:         return SWMM_2D_BC_RATING_CURVE;
        }
        return SWMM_2D_BC_WALL;
    };

    char buf[128];
    for (int t = 0; t < nt; ++t) {
        for (int k = 0; k < 3; ++k) {
            const mesh::MeshEdgeBC &b = bcs[t * 3 + k];
            int et = -1;
            ASSERT_EQ(swmm_2d_get_edge_bc_type(e, t, k, &et), 0)
                << "OPENED-state BC getter refused (pre-A8 solver guard?)";
            EXPECT_EQ(et, engTypeOf(b.type)) << "type mismatch at " << t
                                             << ":" << k;
            ASSERT_EQ(swmm_2d_get_edge_bc_tseries_name(e, t, k, buf,
                                                       sizeof buf), 0);
            EXPECT_EQ(QString::fromUtf8(buf),
                      b.type == T::SpecifiedStageTS ? b.tseries : QString())
                << "stage-TS name mismatch at " << t << ":" << k;
            ASSERT_EQ(swmm_2d_get_edge_bc_flow_tseries_name(e, t, k, buf,
                                                            sizeof buf), 0);
            EXPECT_EQ(QString::fromUtf8(buf),
                      b.type == T::SpecifiedFlowTS ? b.tseries : QString())
                << "flow-TS name mismatch at " << t << ":" << k;
            ASSERT_EQ(swmm_2d_get_edge_bc_rating_curve_name(e, t, k, buf,
                                                            sizeof buf), 0);
            EXPECT_EQ(QString::fromUtf8(buf),
                      b.type == T::RatingCurve ? b.curve : QString())
                << "curve name mismatch at " << t << ":" << k;
        }
    }
    swmm_engine_destroy(e);
}
