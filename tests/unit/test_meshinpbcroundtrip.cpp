/*!
 * \file   test_meshinpbcroundtrip.cpp
 * \brief  Slice §V.VD.1 — [2D_BOUNDARY_CONDITIONS] writer ↔ reader round trip.
 *
 * Builds a small mesh, populates a mix of BC types on its boundary edges,
 * writes them through InpMeshWriter, then reads them back through
 * InpMeshReader and asserts the per-edge values survived intact.
 *
 * Tests:
 *   1. All-Wall BC vector → writer emits no section (keeps .inp pristine)
 *   2. Single NormalFlow BC round-trip (slope value preserved)
 *   3. Single SpecifiedStage constant BC round-trip (head value preserved)
 *   4. Single TS_STAGE BC round-trip (timeseries name preserved)
 *   5. RatingCurve BC round-trip (curve name preserved)
 *   6. Group label round-trip
 */

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>

#include "mesh/inpmeshwriter.h"
#include "mesh/inpmeshreader.h"
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
#include "mesh/meshbctype.h"

using mesh::MeshBCTypes;

namespace {

mesh::MeshResult makeUnitSquareMesh()
{
    mesh::MeshResult m;
    m.vertices = {
        {{0.0, 0.0}, 0.0, 0, {}},
        {{1.0, 0.0}, 0.0, 0, {}},
        {{0.0, 1.0}, 0.0, 0, {}},
        {{1.0, 1.0}, 0.0, 0, {}},
    };
    m.triangles = {
        {0, 1, 3, {}},
        {0, 3, 2, {}},
    };
    m.ok = true;
    return m;
}

QString makeInpFile(QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("test.inp"));
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("[TITLE]\nUnit test\n");
    f.close();
    return path;
}

} // namespace

TEST(MeshInpBCRoundtrip, AllWallEmitsNoSection)
{
    QVector<mesh::MeshEdgeBC> bcs(6);  // 2 triangles * 3 edges
    const QString text = mesh::InpMeshWriter::buildBCSectionText(bcs);
    EXPECT_TRUE(text.isEmpty()) << text.toStdString();
}

TEST(MeshInpBCRoundtrip, NormalFlowSlopeSurvives)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    bcs[0 * 3 + 1].type  = MeshBCTypes::Type::NormalFlow;
    bcs[0 * 3 + 1].slope = 0.003;

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err))
        << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.edgeBCs.size(), 6);
    const auto &back = read.edgeBCs[0 * 3 + 1];
    EXPECT_EQ(back.type, MeshBCTypes::Type::NormalFlow);
    EXPECT_NEAR(back.slope, 0.003, 1e-9);
}

TEST(MeshInpBCRoundtrip, SpecifiedStageHeadSurvives)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    bcs[1 * 3 + 0].type = MeshBCTypes::Type::SpecifiedStageConst;
    bcs[1 * 3 + 0].head = 95.42;

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err)) << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh);
    ASSERT_EQ(read.edgeBCs.size(), 6);
    const auto &back = read.edgeBCs[1 * 3 + 0];
    EXPECT_EQ(back.type, MeshBCTypes::Type::SpecifiedStageConst);
    EXPECT_NEAR(back.head, 95.42, 1e-9);
}

TEST(MeshInpBCRoundtrip, TimeseriesNameSurvives)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    bcs[1 * 3 + 2].type    = MeshBCTypes::Type::SpecifiedStageTS;
    bcs[1 * 3 + 2].tseries = QStringLiteral("DownstreamTS");

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err)) << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh);
    ASSERT_EQ(read.edgeBCs.size(), 6);
    const auto &back = read.edgeBCs[1 * 3 + 2];
    EXPECT_EQ(back.type, MeshBCTypes::Type::SpecifiedStageTS);
    EXPECT_EQ(back.tseries, QStringLiteral("DownstreamTS"));
}

TEST(MeshInpBCRoundtrip, RatingCurveNameSurvives)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    bcs[0 * 3 + 2].type  = MeshBCTypes::Type::RatingCurve;
    bcs[0 * 3 + 2].curve = QStringLiteral("WeirRC");

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err)) << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh);
    ASSERT_EQ(read.edgeBCs.size(), 6);
    const auto &back = read.edgeBCs[0 * 3 + 2];
    EXPECT_EQ(back.type,  MeshBCTypes::Type::RatingCurve);
    EXPECT_EQ(back.curve, QStringLiteral("WeirRC"));
}

TEST(MeshInpBCRoundtrip, GroupLabelSurvives)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    bcs[0 * 3 + 1].type  = MeshBCTypes::Type::NormalFlow;
    bcs[0 * 3 + 1].slope = 0.001;
    bcs[0 * 3 + 1].group = QStringLiteral("Outlet");

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err)) << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh);
    ASSERT_EQ(read.edgeBCs.size(), 6);
    EXPECT_EQ(read.edgeBCs[0 * 3 + 1].group, QStringLiteral("Outlet"));
}

// =============================================================================
// Engine §11A — [2D_EDGE_CONVEYANCE] round-trip
// =============================================================================
//
// Mesh layout (unit square split on the (0,3) diagonal):
//   T0 = (0, 1, 3)  → edges:  0:(1-3) boundary, 1:(3-0) INTERIOR, 2:(0-1) boundary
//   T1 = (0, 3, 2)  → edges:  0:(3-2) boundary, 1:(2-0) boundary,  2:(0-3) INTERIOR
// The two interior slots (T0,1) and (T1,2) share the diagonal — the writer
// must emit ONE row for it; the reader must apply that row to BOTH slots.

TEST(MeshInpConveyanceRoundtrip, AllDefaultEmitsNoSection)
{
    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);  // all ψ default = 1.0
    const QString text = mesh::InpMeshWriter::buildConveyanceSectionText(m, bcs);
    EXPECT_TRUE(text.isEmpty()) << text.toStdString();
}

TEST(MeshInpConveyanceRoundtrip, BoundaryEdgeRoundTrips)
{
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    // Boundary edge T0·3 + 0 — endpoints (v1, v2) = (1, 3).
    bcs[0 * 3 + 0].conveyance = 0.25;

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err))
        << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.edgeBCs.size(), 6);
    EXPECT_NEAR(read.edgeBCs[0 * 3 + 0].conveyance, 0.25, 1e-9);
    // Other slots remain at default.
    EXPECT_NEAR(read.edgeBCs[0 * 3 + 1].conveyance, 1.0, 1e-9);
    EXPECT_NEAR(read.edgeBCs[1 * 3 + 0].conveyance, 1.0, 1e-9);
}

TEST(MeshInpConveyanceRoundtrip, InteriorEdgeMirrorsAndDedupes)
{
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    // Interior edge (0,3): T0·3+1 and T1·3+2 share it. Set only one half;
    // the writer must emit ONE row and the reader must apply it to both.
    bcs[0 * 3 + 1].conveyance = 0.5;
    bcs[1 * 3 + 2].conveyance = 0.5;  // GUI helper keeps both halves in sync

    // Writer dedupe — section text contains a single ψ data row.
    const QString text = mesh::InpMeshWriter::buildConveyanceSectionText(m, bcs);
    EXPECT_FALSE(text.isEmpty()) << "expected [2D_EDGE_CONVEYANCE] section";
    int dataRows = 0;
    for (const QString &line : text.split(QChar('\n'), Qt::SkipEmptyParts)) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QChar('[')) || t.startsWith(QStringLiteral(";;")))
            continue;
        ++dataRows;
    }
    EXPECT_EQ(dataRows, 1) << "expected exactly one row in:\n" << text.toStdString();

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err))
        << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.edgeBCs.size(), 6);
    // Reader symmetry — both halves of the interior edge come back at 0.5.
    EXPECT_NEAR(read.edgeBCs[0 * 3 + 1].conveyance, 0.5, 1e-9);
    EXPECT_NEAR(read.edgeBCs[1 * 3 + 2].conveyance, 0.5, 1e-9);
}

TEST(MeshInpConveyanceRoundtrip, CoexistsWithBC)
{
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    QVector<mesh::MeshEdgeBC> bcs(6);
    // A boundary edge gets BOTH a BC type AND a non-default ψ — they're
    // orthogonal and must round-trip independently.
    bcs[1 * 3 + 1].type       = MeshBCTypes::Type::NormalFlow;
    bcs[1 * 3 + 1].slope      = 0.004;
    bcs[1 * 3 + 1].conveyance = 0.8;

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, bcs, 0.035, &err))
        << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.edgeBCs.size(), 6);
    const auto &back = read.edgeBCs[1 * 3 + 1];
    EXPECT_EQ(back.type, MeshBCTypes::Type::NormalFlow);
    EXPECT_NEAR(back.slope,      0.004, 1e-9);
    EXPECT_NEAR(back.conveyance, 0.8,   1e-9);
}

// =============================================================================
// [2D_VERTEX_NODE_MAP] — optional CD / AREA columns
// =============================================================================

TEST(MeshInpCouplingRoundtrip, CdAreaSurvivesWriteRead)
{
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    m.vertices[1].coupledNode  = QStringLiteral("J2");
    m.vertices[1].couplingCd   = 0.8;
    m.vertices[1].couplingArea = 2.5;
    mesh::CouplingMap c;
    c.vertexToNode.insert(1, QStringLiteral("J2"));

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, c, 0.035, &err))
        << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.mesh.vertices.size(), 4);
    EXPECT_EQ(read.mesh.vertices[1].coupledNode, QStringLiteral("J2"));
    EXPECT_NEAR(read.mesh.vertices[1].couplingCd,   0.8, 1e-9);
    EXPECT_NEAR(read.mesh.vertices[1].couplingArea, 2.5, 1e-9);
    // Uncoupled vertices keep the engine defaults.
    EXPECT_NEAR(read.mesh.vertices[0].couplingCd,   0.65, 1e-9);
    EXPECT_NEAR(read.mesh.vertices[0].couplingArea, 1.0,  1e-9);
}

TEST(MeshInpCouplingRoundtrip, OmittedColumnsGetEngineDefaults)
{
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = dir.filePath(QStringLiteral("map2tok.inp"));
    QFile f(inpPath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(
        "[TITLE]\nUnit test\n"
        "[2D_VERTICES]\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
        "[2D_TRIANGLES]\n"
        "0 1 3 0.035\n0 3 2 0.035\n"
        "[2D_VERTEX_NODE_MAP]\n"
        "1 J2\n");
    f.close();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    EXPECT_EQ(read.mesh.vertices[1].coupledNode, QStringLiteral("J2"));
    EXPECT_NEAR(read.mesh.vertices[1].couplingCd,   0.65, 1e-9);
    EXPECT_NEAR(read.mesh.vertices[1].couplingArea, 1.0,  1e-9);
}

TEST(MeshInpCouplingRoundtrip, VertexTagFormResolvesOnRead)
{
    // Regression (Plan Part C.3): the writer PREFERS the tag form for the
    // first column, but the reader only accepted integer indices — so a
    // GUI-written vertex map never round-tripped through InpMeshReader.
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    m.vertices[2].tag          = QStringLiteral("J5");   // writer emits "J5"
    m.vertices[2].coupledNode  = QStringLiteral("J5");
    m.vertices[2].couplingCd   = 0.7;
    m.vertices[2].couplingArea = 3.0;
    mesh::CouplingMap c;
    c.vertexToNode.insert(2, QStringLiteral("J5"));

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, c, 0.035, &err))
        << err.toStdString();

    // The section really is in tag form — otherwise this test would pass
    // for the wrong reason (index form always worked).
    QFile f(inpPath);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    EXPECT_TRUE(text.contains(QStringLiteral("J5        J5")))
        << "expected tag-form row in:\n" << text.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.mesh.vertices.size(), 4);
    EXPECT_EQ(read.mesh.vertices[2].coupledNode, QStringLiteral("J5"));
    EXPECT_NEAR(read.mesh.vertices[2].couplingCd,   0.7, 1e-9);
    EXPECT_NEAR(read.mesh.vertices[2].couplingArea, 3.0, 1e-9);
    // No other vertex picked up the coupling.
    EXPECT_TRUE(read.mesh.vertices[0].coupledNode.isEmpty());
    EXPECT_TRUE(read.mesh.vertices[1].coupledNode.isEmpty());
}

// =============================================================================
// [2D_TRIANGLE_NODE_MAP] — repeated-row node→cell couplings (Plan Part C)
// =============================================================================

TEST(MeshInpCellCouplingRoundtrip, SharedTriangleRowsSurviveWriteRead)
{
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = makeInpFile(dir);

    auto m = makeUnitSquareMesh();
    // Two nodes in ONE cell — the weir/orifice-endpoint case the plan
    // exists for. Last-line-wins would drop the first row.
    m.cellCouplings = {
        { 0, QStringLiteral("W_UP"), 0.65, 2.0 },
        { 0, QStringLiteral("W_DN"), 0.65, 2.0 },
        { 1, QStringLiteral("J9"),   0.8,  4.5 },
    };

    QString err;
    ASSERT_TRUE(mesh::InpMeshWriter::writeInline(inpPath, m, {}, 0.035, &err))
        << err.toStdString();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.mesh.cellCouplings.size(), 3);

    EXPECT_EQ(read.mesh.cellCouplings[0].tri,    0);
    EXPECT_EQ(read.mesh.cellCouplings[0].nodeId, QStringLiteral("W_UP"));
    EXPECT_NEAR(read.mesh.cellCouplings[0].cd,   0.65, 1e-9);
    EXPECT_NEAR(read.mesh.cellCouplings[0].area, 2.0,  1e-9);

    EXPECT_EQ(read.mesh.cellCouplings[1].tri,    0);
    EXPECT_EQ(read.mesh.cellCouplings[1].nodeId, QStringLiteral("W_DN"));

    EXPECT_EQ(read.mesh.cellCouplings[2].tri,    1);
    EXPECT_EQ(read.mesh.cellCouplings[2].nodeId, QStringLiteral("J9"));
    EXPECT_NEAR(read.mesh.cellCouplings[2].cd,   0.8, 1e-9);
    EXPECT_NEAR(read.mesh.cellCouplings[2].area, 4.5, 1e-9);
}

TEST(MeshInpCellCouplingRoundtrip, NoRowsEmitsNoSection)
{
    auto m = makeUnitSquareMesh();
    const QString text = mesh::InpMeshWriter::buildSectionText(m, {});
    EXPECT_FALSE(text.contains(QStringLiteral("[2D_TRIANGLE_NODE_MAP]")))
        << text.toStdString();
}

TEST(MeshInpCellCouplingRoundtrip, OmittedColumnsGetMapperDefaults)
{
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = dir.filePath(QStringLiteral("tri2tok.inp"));
    QFile f(inpPath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(
        "[TITLE]\nUnit test\n"
        "[2D_VERTICES]\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
        "[2D_TRIANGLES]\n"
        "0 1 3 0.035\n0 3 2 0.035\n"
        "[2D_TRIANGLE_NODE_MAP]\n"
        "1 J2\n");
    f.close();

    const auto read = mesh::InpMeshReader::read(inpPath);
    ASSERT_TRUE(read.hasMesh) << read.errorMsg.toStdString();
    ASSERT_EQ(read.mesh.cellCouplings.size(), 1);
    EXPECT_EQ(read.mesh.cellCouplings[0].tri,    1);
    EXPECT_EQ(read.mesh.cellCouplings[0].nodeId, QStringLiteral("J2"));
    EXPECT_NEAR(read.mesh.cellCouplings[0].cd,   0.65, 1e-9);
    EXPECT_NEAR(read.mesh.cellCouplings[0].area, 2.0,  1e-9);
}

TEST(MeshInpConveyanceRoundtrip, OutOfRangeIsRejected)
{
    // Engine spec is strict [0, 1]; the reader rejects rows outside the
    // range by surfacing an error from the section parser.
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const QString inpPath = dir.filePath(QStringLiteral("bad.inp"));
    QFile f(inpPath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(
        "[TITLE]\nUnit test\n"
        "[2D_VERTICES]\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
        "[2D_TRIANGLES]\n"
        "0 1 3 0.035\n0 3 2 0.035\n"
        "[2D_EDGE_CONVEYANCE]\n"
        "0 3 1.5\n");
    f.close();

    const auto read = mesh::InpMeshReader::read(inpPath);
    EXPECT_FALSE(read.errorMsg.isEmpty()) << "expected reader to reject ψ=1.5";
    EXPECT_TRUE(read.errorMsg.contains(QStringLiteral("CONVEYANCE")))
        << read.errorMsg.toStdString();
}
