/*!
 * \file   test_meshexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  "Export mesh…": cells, edges and vertices as tables
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.2, R10).
 *
 * A two-triangle-one-quad mesh: counts equal the mesh's (every interior edge
 * once), BC fields only on boundary edges and only those the type reads, and
 * the whole lot written to a GeoPackage under
 * tests/output/feature_layer_roles_2026-09-30/meshexport/ and read back.
 */
#include <gtest/gtest.h>

#include "io/meshexport.h"
#include "mesh/meshcellgeom.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QFile>

#include <cmath>

#ifndef MESHEXPORT_OUT_DIR
#define MESHEXPORT_OUT_DIR "meshexport_artifacts"
#endif

using namespace openswmmvis::io;

namespace {

/*!   3 ---- 4 ---- 5
 *    | \  t1|      |
 *    |  \   |  q   |      t0 = (0,1,4), t1 = (0,4,3), q = (1,2,5,4)
 *    | t0 \ |      |
 *    0 ---- 1 ---- 2      */
mesh::MeshResult sampleMesh()
{
    mesh::MeshResult m;
    const QPointF xy[] = {{0, 0}, {10, 0}, {20, 0}, {0, 10}, {10, 10}, {20, 10}};
    for (int i = 0; i < 6; ++i) {
        mesh::MeshVertex v;
        v.xy = xy[i];
        v.z = 100.0 + i;
        m.vertices << v;
    }
    m.vertices[4].coupledNode = QStringLiteral("J1");
    mesh::MeshTriangle t0; t0.v0 = 0; t0.v1 = 1; t0.v2 = 4; t0.mannings = 0.02;
    mesh::MeshTriangle t1; t1.v0 = 0; t1.v1 = 4; t1.v2 = 3;
    mesh::MeshTriangle q;  q.v0 = 1; q.v1 = 2; q.v2 = 5; q.v3 = 4; q.tag = QStringLiteral("park");
    m.triangles << t0 << t1 << q;
    m.ok = true;
    return m;
}

int column(const ExportTable &t, const char *name)
{
    for (int i = 0; i < t.fields.size(); ++i)
        if (t.fields.at(i).name == QLatin1String(name)) return i;
    return -1;
}

class GdalEnvironment : public ::testing::Environment
{
public:
    void SetUp() override { GDALAllRegister(); }
};
const auto *kGdalEnv = ::testing::AddGlobalTestEnvironment(new GdalEnvironment);

}   // namespace

TEST(MeshExport, CountsEqualTheMesh)
{
    const mesh::MeshResult m = sampleMesh();
    const QVector<ExportTable> tables = meshTables(m, {}, MeshExportAll, QStringLiteral("m"));
    ASSERT_EQ(tables.size(), 3);
    EXPECT_EQ(tables.at(0).name, QStringLiteral("mesh_cells"));
    EXPECT_EQ(tables.at(0).rows.size(), 3);
    EXPECT_EQ(tables.at(1).name, QStringLiteral("mesh_edges"));
    EXPECT_EQ(tables.at(1).rows.size(), 8);   // V − E + F = 1 → E = 6 + 3 − 1
    EXPECT_EQ(tables.at(2).name, QStringLiteral("mesh_vertices"));
    EXPECT_EQ(tables.at(2).rows.size(), 6);

    int boundary = 0;
    const int b = column(tables.at(1), "boundary");
    for (const ExportRow &r : tables.at(1).rows) boundary += r.values.at(b).toBool() ? 1 : 0;
    EXPECT_EQ(boundary, 6);

    // Parts are honoured.
    EXPECT_EQ(meshTables(m, {}, MeshExportEdges).size(), 1);
}

TEST(MeshExport, CellsCarryTheirParameters)
{
    const ExportTable cells = meshTables(sampleMesh(), {}, MeshExportCells, QStringLiteral("m"))
                                  .first();
    const int n = column(cells, "mannings");
    const int d = column(cells, "initDepth");
    const int tag = column(cells, "tag");
    const int z = column(cells, "bed_z");
    const int method = column(cells, "infil_method");
    ASSERT_GE(n, 0);
    ASSERT_GE(d, 0);
    ASSERT_GE(tag, 0);
    ASSERT_GE(z, 0);
    ASSERT_GE(method, 0);
    EXPECT_EQ(cells.fields.at(d).unit, QStringLiteral("m"));
    EXPECT_FALSE(cells.fields.at(method).choices.isEmpty());
    EXPECT_DOUBLE_EQ(cells.rows.at(0).values.at(n).toDouble(), 0.02);
    EXPECT_FALSE(cells.rows.at(1).values.at(n).isValid()) << "unset stays null";
    EXPECT_EQ(cells.rows.at(2).values.at(tag).toString(), QStringLiteral("park"));
    EXPECT_EQ(cells.rows.at(2).points.size(), 4) << "a quad is a four-corner polygon";
    EXPECT_DOUBLE_EQ(cells.rows.at(0).values.at(z).toDouble(), (100.0 + 101.0 + 104.0) / 3.0);
    EXPECT_FALSE(cells.rows.at(0).values.at(method).isValid()) << "no infiltration → null";
}

TEST(MeshExport, BoundaryConditionsOnlyOnBoundaryEdges)
{
    const mesh::MeshResult m = sampleMesh();
    QVector<mesh::MeshEdgeBC> bcs(mesh::edgeSlotCount(m.triangles.size()));
    // t0's local edge 2 is (0, 1): on the boundary.
    mesh::MeshEdgeBC stage;
    stage.type = mesh::MeshBCTypes::Type::SpecifiedStageConst;
    stage.head = 3.5;
    stage.slope = 0.01;   // not read by a stage boundary
    stage.group = QStringLiteral("south");
    stage.conveyance = 0.5;
    bcs[mesh::edgeSlot(0, 2)] = stage;
    // t0's local edge 0 is (1, 4): interior, shared with the quad.
    bcs[mesh::edgeSlot(0, 0)].conveyance = 0.25;

    const ExportTable edges = meshTables(m, bcs, MeshExportEdges).first();
    const int cell = column(edges, "cell"), edge = column(edges, "edge");
    const int type = column(edges, "bc_type"), head = column(edges, "head");
    const int slope = column(edges, "slope"), group = column(edges, "group");
    const int conv = column(edges, "conveyance"), boundary = column(edges, "boundary");
    bool sawStage = false, sawInterior = false;
    for (const ExportRow &r : edges.rows) {
        if (r.values.at(cell).toInt() == 0 && r.values.at(edge).toInt() == 2) {
            sawStage = true;
            EXPECT_TRUE(r.values.at(boundary).toBool());
            EXPECT_EQ(r.values.at(type).toString(), QStringLiteral("SPECIFIED_STAGE"));
            EXPECT_DOUBLE_EQ(r.values.at(head).toDouble(), 3.5);
            EXPECT_FALSE(r.values.at(slope).isValid()) << "a stage boundary reads no slope";
            EXPECT_EQ(r.values.at(group).toString(), QStringLiteral("south"));
            EXPECT_DOUBLE_EQ(r.values.at(conv).toDouble(), 0.5);
        }
        if (r.values.at(cell).toInt() == 0 && r.values.at(edge).toInt() == 0) {
            sawInterior = true;
            EXPECT_FALSE(r.values.at(boundary).toBool());
            EXPECT_FALSE(r.values.at(type).isValid());
            EXPECT_DOUBLE_EQ(r.values.at(conv).toDouble(), 0.25) << "conveyance applies to every edge";
        }
    }
    EXPECT_TRUE(sawStage);
    EXPECT_TRUE(sawInterior);
}

TEST(MeshExport, WrittenAndReadBack)
{
    const QString dir = QStringLiteral(MESHEXPORT_OUT_DIR);
    QDir().mkpath(dir);
    ExportOptions o;
    o.driver = QStringLiteral("GPKG");
    o.destination = QDir(dir).absoluteFilePath(QStringLiteral("sample_mesh.gpkg"));
    QFile::remove(o.destination);
    ExportReport rep;
    ASSERT_TRUE(exportVectorTables(meshTables(sampleMesh(), {}, MeshExportAll), o, {}, &rep))
        << rep.error.toStdString();
    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpenEx(
        o.destination.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
        nullptr, nullptr, nullptr));
    ASSERT_TRUE(ds);
    EXPECT_EQ(ds->GetLayerByName("mesh_cells")->GetFeatureCount(), 3);
    EXPECT_EQ(ds->GetLayerByName("mesh_edges")->GetFeatureCount(), 8);
    EXPECT_EQ(ds->GetLayerByName("mesh_vertices")->GetFeatureCount(), 6);
    EXPECT_EQ(wkbFlatten(ds->GetLayerByName("mesh_cells")->GetGeomType()), wkbPolygon);
    GDALClose(ds);
}
