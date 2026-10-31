/**
 * @file      test_spatialassignment.cpp
 * @brief     Phase 3b (2026-10-31): service-level gates for spatial assignment of scoped mesh
 *            rows and groundwater point sources.
 *
 * Every deck, raster, GeoPackage and saved model lands under SWMMVIS_SPATIAL_ASSIGNMENT_OUTPUT
 * (tests/output/spatial_assignment_2026-10-31/service/<test>) for review. The fixture mesh mixes
 * triangles and a quad; one triangle carries tag PAD, which [2D_AQUIFER] and [2D_LOADINGS] TAG
 * rows use, so moving a cell out of it must be reported.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "assignment/groundwatertransportassignment.h"
#include "assignment/meshassignmentsampling.h"
#include "assignment/rasterfluxintegration.h"
#include "assignment/spatialassignment.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_sq2d.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QFile>
#include <QtTest/QtTest>

#include <cmath>

using namespace openswmmvis::assignment;
using Section = openswmmvis::ui::Mesh2DSectionRowModel::Section;

namespace
{
// Cells (engine order: [2D_TRIANGLES] then [2D_QUADS]):
//   0  triangle (0,0) (2,0) (2,2)            area 2, untagged
//   1  triangle (2,2) (4,2) (4,4)            area 2, tag PAD
//   2  quad     (2,0) (4,0) (4,2) (2,2)      area 4, untagged
const char *kDeck = R"([OPTIONS]
FLOW_UNITS       CMS
INFILTRATION     HORTON
FLOW_ROUTING     DYNWAVE
START_DATE       01/01/2026
START_TIME       00:00:00
END_DATE         01/01/2026
END_TIME         00:05:00
REPORT_STEP      00:01:00
ROUTING_STEP     5

[JUNCTIONS]
J1  -2  4  0  0  0

[OUTFALLS]
O1  -2.5  FREE  NO

[CONDUITS]
C1  J1  O1  30  0.013  0  0  0

[XSECTIONS]
C1  CIRCULAR  0.5  0  0  0  1

[POLLUTANTS]
TSS  MG/L  0  0  0  0  NO  *  0  0  0

[LANDUSES]
Urban  0  0  0
Grass  0  0  0

[BUILDUP]
Urban  TSS  NONE  0  0  0  AREA
Grass  TSS  NONE  0  0  0  AREA

[WASHOFF]
Urban  TSS  EXP  0.1  1.0  0  0
Grass  TSS  EXP  0.1  1.0  0  0

[2D_OPTIONS]
MAX_TIMESTEP  5
DRY_DEPTH     0.002
REPORT_2D     NO

[2D_VERTICES]
0  0  0
2  0  0
2  2  0
0  2  0
4  0  0
4  2  0
4  4  0

[2D_TRIANGLES]
0  1  2  0.03  0
2  5  6  0.03  0  PAD

[2D_QUADS]
1  4  5  2  0.03  0

[2D_AQUIFER_OPTIONS]
CLOSURE         CLOSED_FORM
NODE_ENROLMENT  ROWS

[2D_AQUIFER]
*  36  1  0.45  0.10  2  HG0  0.5

[2D_LOADINGS]
TAG PAD  TSS  50

[GW_TRANSPORT_OPTIONS]
TRANSPORT_POLLUTANTS  YES

[COORDINATES]
J1  1   1
O1  40  40
)";

constexpr int kTri = 0, kPad = 1, kQuad = 2;

QString outputDir()
{
    const QString dir = QStringLiteral(SWMMVIS_SPATIAL_ASSIGNMENT_OUTPUT) + "/service/" +
                        QString::fromLatin1(QTest::currentTestFunction());
    QDir().mkpath(dir);
    return dir;
}

bool writeText(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

struct Engine {
    SWMM_Engine e = swmm_engine_create();
    QString dir;
    ~Engine()
    {
        if (e) {
            swmm_engine_close(e);
            swmm_engine_destroy(e);
        }
    }
    /** Open @p inp (default: the fixture deck written to the test's folder). */
    bool open(const QString &inp = {})
    {
        dir = outputDir();
        QString path = inp;
        if (path.isEmpty()) {
            path = dir + "/input.inp";
            if (!writeText(path, kDeck))
                return false;
        }
        const QString stem = QFileInfo(path).absolutePath() + "/" + QFileInfo(path).baseName();
        if (swmm_engine_open(e, path.toUtf8().constData(), (stem + ".rpt").toUtf8().constData(),
                             (stem + ".out").toUtf8().constData(), nullptr) != SWMM_OK)
            return false;
        // A TAG aquifer row on PAD, so retagging that cell is an inheritance conflict.
        return swmm_gw2d_row_add(e, 1, "PAD", -1, 36, 1, .45, .10, 2) == SWMM_OK;
    }
};

/** The fixture mesh in GUI form, same vertex and cell order as the deck. */
void fixtureMesh(QVector<mesh::MeshVertex> *vertices, QVector<mesh::MeshTriangle> *cells)
{
    vertices->clear();
    for (const QPointF p : {QPointF(0, 0), QPointF(2, 0), QPointF(2, 2), QPointF(0, 2),
                            QPointF(4, 0), QPointF(4, 2), QPointF(4, 4)})
        vertices->append({p, 0});
    mesh::MeshTriangle tri, pad, quad;
    tri.v0 = 0;
    tri.v1 = 1;
    tri.v2 = 2;
    pad.v0 = 2;
    pad.v1 = 5;
    pad.v2 = 6;
    pad.tag = "PAD";
    quad.v0 = 1;
    quad.v1 = 4;
    quad.v2 = 5;
    quad.v3 = 2;
    *cells = {tri, pad, quad};
}

QVector<QPointF> footprint(int cell)
{
    QVector<mesh::MeshVertex> v;
    QVector<mesh::MeshTriangle> c;
    fixtureMesh(&v, &c);
    QVector<QPointF> out;
    for (int k = 0; k < c[cell].vertexCount(); ++k)
        out.append(v[c[cell].vertex(k)].xy);
    return out;
}

QString utm18()
{
    OGRSpatialReference srs;
    srs.importFromEPSG(32618);
    char *wkt = nullptr;
    srs.exportToWkt(&wkt);
    const QString out = QString::fromUtf8(wkt);
    CPLFree(wkt);
    return out;
}

/** 4 x 2 unit-pixel raster over x 0..4, y 0..2: column 0 holds @p a, columns 1..3 hold @p b. */
QString writeRaster(const QString &path, double a, double b, GDALDataType type)
{
    GDALAllRegister();
    auto *ds = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
        path.toUtf8().constData(), 4, 2, 1, type, nullptr);
    if (!ds)
        return {};
    double gt[] = {0, 1, 0, 0, 0, 1};
    ds->SetGeoTransform(gt);
    ds->SetProjection(utm18().toUtf8().constData());
    QVector<double> values{a, b, b, b, a, b, b, b};
    auto *band = ds->GetRasterBand(1);
    band->SetNoDataValue(-9999);
    const bool ok =
        band->RasterIO(GF_Write, 0, 0, 4, 2, values.data(), 4, 2, GDT_Float64, 0, 0) == CE_None;
    GDALClose(ds);
    return ok ? path : QString();
}

/** Polygons x 0..1 and x 1..4 (y 0..2) with LU text and LOAD numbers. */
QString writePolygons(const QString &path, const QString &leftUse, double leftLoad,
                      const QString &rightUse, double rightLoad)
{
    GDALAllRegister();
    QFile::remove(path);
    auto *ds = GetGDALDriverManager()->GetDriverByName("GPKG")->Create(
        path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        return {};
    OGRSpatialReference srs;
    srs.importFromEPSG(32618);
    auto *layer = ds->CreateLayer("zones", &srs, wkbPolygon, nullptr);
    OGRFieldDefn lu("LU", OFTString), load("LOAD", OFTReal);
    layer->CreateField(&lu);
    layer->CreateField(&load);
    const auto add = [&](const char *wkt, const QString &use, double value) {
        OGRFeature f(layer->GetLayerDefn());
        f.SetField("LU", use.toUtf8().constData());
        f.SetField("LOAD", value);
        OGRGeometry *g = nullptr;
        OGRGeometryFactory::createFromWkt(wkt, nullptr, &g);
        f.SetGeometryDirectly(g);
        return layer->CreateFeature(&f) == OGRERR_NONE;
    };
    const bool ok = add("POLYGON ((0 0, 1 0, 1 2, 0 2, 0 0))", leftUse, leftLoad) &&
                    add("POLYGON ((1 0, 4 0, 4 2, 1 2, 1 0))", rightUse, rightLoad);
    GDALClose(ds);
    return ok ? path : QString();
}

/** Points with NAME, FLOW and TSS fields. */
QString writePoints(const QString &path)
{
    GDALAllRegister();
    QFile::remove(path);
    auto *ds = GetGDALDriverManager()->GetDriverByName("GPKG")->Create(
        path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        return {};
    OGRSpatialReference srs;
    srs.importFromEPSG(32618);
    auto *layer = ds->CreateLayer("wells", &srs, wkbPoint, nullptr);
    OGRFieldDefn name("NAME", OFTString), flow("FLOW", OFTReal), tss("TSS", OFTReal);
    layer->CreateField(&name);
    layer->CreateField(&flow);
    layer->CreateField(&tss);
    bool ok = true;
    const struct {
        const char *name;
        double x, y, flow, tss;
    } points[] = {{"A", 1.5, .5, .002, 10},   // cell 0
                  {"B", 3, 1, -.001, 20},     // quad
                  {"C", 2, 1, .0005, 30},     // shared edge between cell 0 and the quad
                  {"D", 10, 10, .001, 40},    // outside the mesh
                  {"E", 3, 3, .001, 50}};     // PAD cell, outside the scope
    for (const auto &p : points) {
        OGRFeature f(layer->GetLayerDefn());
        f.SetField("NAME", p.name);
        f.SetField("FLOW", p.flow);
        f.SetField("TSS", p.tss);
        OGRPoint pt(p.x, p.y);
        f.SetGeometry(&pt);
        ok = ok && layer->CreateFeature(&f) == OGRERR_NONE;
    }
    GDALClose(ds);
    return ok ? path : QString();
}

/** Run a saved deck to the end; returns the groundwater source water ledger (m³). */
bool simulateSourceIn(const QString &path, double *waterIn, QString *error)
{
    SWMM_Engine e = swmm_engine_create();
    const QString stem = QFileInfo(path).absolutePath() + "/" + QFileInfo(path).baseName();
    bool ok = swmm_engine_open(e, path.toUtf8().constData(), (stem + ".rpt").toUtf8().constData(),
                               (stem + ".out").toUtf8().constData(), nullptr) == SWMM_OK &&
              swmm_engine_initialize(e) == SWMM_OK && swmm_engine_start(e, 1) == SWMM_OK;
    int active = 0;
    ok = ok && swmm_gw2d_is_active(e, &active) == SWMM_OK && active == 1;
    for (int n = 0; ok && n < 100000; ++n) {
        double elapsed = 0;
        ok = swmm_engine_step(e, &elapsed) == SWMM_OK;
        if (elapsed == 0)
            break;
    }
    ok = ok && swmm_engine_end(e) == SWMM_OK &&
         swmm_gw2d_get_ledger(e, SWMM_GW2D_LED_SOURCE_IN, waterIn) == SWMM_OK;
    if (!ok)
        *error = QStringLiteral("Simulating %1 failed or groundwater was inactive.").arg(path);
    swmm_engine_close(e);
    swmm_engine_destroy(e);
    return ok;
}

SectionRow findRow(const QList<SectionRow> &rows, int scope, const QString &tag, int cell)
{
    for (const auto &r : rows)
        if (r.scope == scope && (scope != 1 || r.tag == tag) && (scope != 2 || r.cell == cell))
            return r;
    return {};
}
} // namespace

class TestSpatialAssignment : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { GDALAllRegister(); }

    /** The fixture's engine cell order matches the GUI arrays the jobs are built from. */
    void fixtureCellOrder()
    {
        Engine en;
        QVERIFY(en.open());
        const QStringList tags = engineCellTags(en.e);
        QCOMPARE(tags, QStringList({QString(), "PAD", QString()}));
    }

    /**
     * Gate (equivalence): a land-use polygon layer and a categorical raster that draw the same
     * map give the same area-weighted coverage percents and the same area-weighted means, on a
     * triangle split by a class boundary and on a quad.
     */
    void polygonAndRasterSharesAgree()
    {
        const QString dir = outputDir();
        const QString classes =
            writeRaster(dir + "/landuse_classes.tif", 1, 2, GDT_Int32); // 1 = Urban, 2 = Grass
        const QString loads = writeRaster(dir + "/loading.tif", 10, 20, GDT_Float64);
        const QString zones = writePolygons(dir + "/zones.gpkg", "Urban", 10, "Grass", 20);
        QVERIFY(!classes.isEmpty() && !loads.isEmpty() && !zones.isEmpty());
        const QVector<int> cells{kTri, kQuad};
        RasterZonalRequest raster;
        raster.path = classes;
        raster.meshCrsWkt = utm18();
        raster.cells = cells;
        raster.footprints = {footprint(kTri), footprint(kQuad)};
        raster.categorical = true;
        const auto z = integrateRasterZonal(raster);
        QVERIFY2(z.error.isEmpty(), qPrintable(z.error));
        QCOMPARE(z.classAreas[0].value(1), .5);
        QCOMPARE(z.classAreas[0].value(2), 1.5);
        QCOMPARE(z.classAreas[1].value(2), 4.);
        QString error;
        const auto lookup = parseClassLookup("1=urban, 2=Grass", {"Urban", "Grass"}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVector<QMap<QString, double>> rasterPercents;
        for (int i = 0; i < 2; ++i) {
            QMap<QString, double> fractions;
            for (auto it = z.classAreas[i].cbegin(); it != z.classAreas[i].cend(); ++it)
                fractions[QString::number(it.key())] = it.value() / z.cellAreas[i];
            rasterPercents << sharesToPercents(fractions, lookup, {"Urban", "Grass"}, nullptr);
        }

        QVector<mesh::MeshVertex> v;
        QVector<mesh::MeshTriangle> c;
        fixtureMesh(&v, &c);
        MeshAssignmentSampling::Job job;
        job.mode = MeshAssignmentSampling::Mode::CategoryShare;
        job.source = MeshAssignmentSampling::Source::Vector;
        job.sampling = MeshAssignmentSampling::Sampling::AreaWeightedMean;
        job.strictCrs = true;
        job.rejectOverlaps = true;
        job.meshCrsWkt = utm18();
        job.vectorPath = zones;
        job.keyField1 = "LU";
        job.triangles = cells;
        for (int cell : cells) {
            const auto fp = footprint(cell);
            for (int k = 0; k < 4; ++k)
                job.triVerts << fp[k < fp.size() ? k : 0];
        }
        job.centroids = {QPointF(4. / 3, 2. / 3), QPointF(3, 1)};
        const auto shares = sampleMeshAssignment(job);
        QVERIFY2(shares.error.isEmpty(), qPrintable(shares.error));
        QCOMPARE(shares.triangles, cells);
        for (int i = 0; i < 2; ++i) {
            const auto percents = sharesToPercents(shares.shares[i], {}, {"Urban", "Grass"}, nullptr);
            QCOMPARE(percents.keys(), rasterPercents[i].keys());
            for (const QString &use : percents.keys())
                QVERIFY(std::abs(percents[use] - rasterPercents[i][use]) < 1e-9);
        }
        QVERIFY(std::abs(rasterPercents[0].value("Urban") - 25) < 1e-12);
        QVERIFY(std::abs(rasterPercents[0].value("Grass") - 75) < 1e-12);
        QVERIFY(std::abs(rasterPercents[1].value("Grass") - 100) < 1e-12);

        // Intensive values: raster area mean (exact overlap) == polygon area-weighted mean.
        raster.path = loads;
        raster.categorical = false;
        const auto means = integrateRasterZonal(raster);
        QVERIFY2(means.error.isEmpty(), qPrintable(means.error));
        job.mode = MeshAssignmentSampling::Mode::SingleNumeric;
        job.fields = {"LOAD"};
        job.targetKeys = {"LOAD"};
        job.targetMin = {-1e300};
        job.targetMax = {1e300};
        const auto polygonMeans = sampleMeshAssignment(job);
        QVERIFY2(polygonMeans.error.isEmpty(), qPrintable(polygonMeans.error));
        QCOMPARE(means.means[0], 17.5);
        QCOMPARE(means.means[1], 20.);
        QVERIFY(std::abs(polygonMeans.values[0][0] - 17.5) < 1e-9);
        QVERIFY(std::abs(polygonMeans.values[0][1] - 20.) < 1e-9);
    }

    /**
     * Gate (rows, undo, save/reopen): equal coverages on two untagged cells become one TAG row
     * with the cells tagged; undo restores rows and tags exactly; the saved and reopened model
     * keeps the TAG row and the tags.
     */
    void coverageTagRowUndoSaveReopen()
    {
        Engine en;
        QVERIFY(en.open());
        RowAssignmentRequest r;
        QString error;
        QVERIFY2(readRowAssignmentSnapshot(en.e, Section::Coverages, &r.before, &error),
                 qPrintable(error));
        QCOMPARE(r.before.landUses, QStringList({"Urban", "Grass"}));
        r.cells = {kTri, kQuad};
        r.percents = {{{"Urban", 60}, {"Grass", 40}}, {{"urban", 60}, {"Grass", 40}}};
        const auto p = previewRowAssignment(r);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QVERIFY(p.tagRow);
        QCOMPARE(p.tag, QStringLiteral("COVERAGE_1"));
        QCOMPARE(p.retagged, QVector<int>({kTri, kQuad}));
        QCOMPARE(p.after.cellTags, QStringList({"COVERAGE_1", "PAD", "COVERAGE_1"}));
        RowAssignmentSnapshot now;
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Coverages, &now));
        QVERIFY(now == r.before); // preview wrote nothing
        const auto applied = applyRowAssignment(en.e, p);
        QVERIFY2(applied.success, qPrintable(applied.error));
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Coverages, &now));
        QVERIFY(now == p.after);
        const auto row = findRow(now.rows, 1, "COVERAGE_1", -1);
        QCOMPARE(row.percents, (QMap<QString, double>{{"Urban", 60}, {"Grass", 40}}));
        const auto undone = undoRowAssignment(en.e, p);
        QVERIFY2(undone.success, qPrintable(undone.error));
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Coverages, &now));
        QVERIFY(now == r.before);
        QVERIFY(applyRowAssignment(en.e, p).success); // redo
        const QString saved = en.dir + "/saved.inp";
        QCOMPARE(swmm_model_write(en.e, saved.toUtf8().constData()), SWMM_OK);
        Engine reopened;
        QVERIFY(reopened.open(saved));
        RowAssignmentSnapshot back;
        QVERIFY(readRowAssignmentSnapshot(reopened.e, Section::Coverages, &back));
        QCOMPARE(back.cellTags, p.after.cellTags);
        QVERIFY(back.rows == p.after.rows);
    }

    /**
     * A cell in another tag: Automatic falls back to CELL rows and says why; an explicit TAG row
     * is refused naming the tag and what uses it; allowing the retag writes it and says what the
     * cell stops inheriting.
     */
    void retagConflictIsReported()
    {
        Engine en;
        QVERIFY(en.open());
        RowAssignmentRequest r;
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::CurbLength, &r.before));
        r.cells = {kTri, kPad};
        r.values = {12.5, 12.5};
        r.tagUsage = tagReferences(en.e);
        QVERIFY(r.tagUsage.value("PAD").contains("[2D_AQUIFER]"));
        QVERIFY(r.tagUsage.value("PAD").contains("[2D_LOADINGS]"));
        auto p = previewRowAssignment(r);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QVERIFY(!p.tagRow);
        QCOMPARE(p.cells.size(), 2);
        QVERIFY(p.notes.join(' ').contains("PAD"));
        r.strategy = RowWriteStrategy::TagRow;
        p = previewRowAssignment(r);
        QVERIFY(p.error.contains("PAD"));
        QVERIFY(p.error.contains("[2D_AQUIFER]"));
        QVERIFY(p.after == p.before);
        r.allowRetag = true;
        p = previewRowAssignment(r);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QVERIFY(p.tagRow);
        QCOMPARE(p.retaggedFrom, QStringList({QString(), "PAD"}));
        QVERIFY(p.notes.join(' ').contains("stop inheriting"));
        // A tag that cells outside the scope carry would reach them too: refused.
        r.tag = "PAD";
        r.cells = {kTri, kQuad};
        p = previewRowAssignment(r);
        QVERIFY(p.error.contains("outside the assignment"));
    }

    /**
     * Gate (injected API failure): a refused write after the tags were changed leaves the
     * previous rows and tags; so does a refusal between tag writes; a stale preview writes
     * nothing.
     */
    void injectedFailureRollsBack()
    {
        Engine en;
        QVERIFY(en.open());
        RowAssignmentRequest r;
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Loadings, &r.before));
        r.cells = {kTri, kQuad};
        r.values = {5, 5};
        r.name = "tss";
        const auto p = previewRowAssignment(r);
        QVERIFY2(p.error.isEmpty() && p.tagRow, qPrintable(p.error));
        for (int failAt : {3, 2, 1}) {
            const auto result =
                applyRowAssignment(en.e, p, [failAt](int ordinal) { return ordinal != failAt; });
            QVERIFY(!result.success);
            QVERIFY(result.rollbackComplete);
            QCOMPARE(result.changed, failAt > 1);
            RowAssignmentSnapshot now;
            QVERIFY(readRowAssignmentSnapshot(en.e, Section::Loadings, &now));
            QVERIFY(now == r.before);
        }
        // The whole row batch is validated before the first (tag) write.
        auto invalid = p;
        invalid.after.rows.last().values = {-1};
        const auto refused = applyRowAssignment(en.e, invalid);
        QVERIFY(!refused.success);
        QVERIFY(!refused.changed);
        RowAssignmentSnapshot untouched;
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Loadings, &untouched));
        QVERIFY(untouched == r.before);
        QCOMPARE(swmm_2d_loading_set(en.e, SWMM_SQ2D_SCOPE_GLOBAL, nullptr, -1, "TSS", 1), SWMM_OK);
        const auto stale = applyRowAssignment(en.e, p);
        QVERIFY(!stale.success);
        QVERIFY(!stale.changed);
        QVERIFY(stale.error.contains("changed since the preview"));
    }

    /** CELL rows upsert by key and skip cells whose effective value already matches. */
    void cellRowsUpsertAndSkipUnchanged()
    {
        Engine en;
        QVERIFY(en.open());
        RowAssignmentRequest r;
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Loadings, &r.before));
        r.cells = {kTri, kPad};
        r.values = {7, 50}; // PAD already inherits 50 from its TAG row
        r.name = "TSS";
        r.strategy = RowWriteStrategy::CellRows;
        auto p = previewRowAssignment(r);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QCOMPARE(p.cells, QVector<int>{kTri});
        QCOMPARE(p.unchangedCells, QVector<int>{kPad});
        QCOMPARE(p.oldValues, QStringList{"(none)"});
        QVERIFY(applyRowAssignment(en.e, p).success);
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Loadings, &r.before));
        r.values = {9, 50};
        p = previewRowAssignment(r);
        QCOMPARE(p.oldValues, QStringList{"7"});
        QCOMPARE(p.after.rows.size(), r.before.rows.size()); // replaced, not appended
        QVERIFY(applyRowAssignment(en.e, p).success);
        RowAssignmentSnapshot now;
        QVERIFY(readRowAssignmentSnapshot(en.e, Section::Loadings, &now));
        QCOMPARE(findRow(now.rows, 2, {}, kTri).values, QVector<double>{9});
    }

    /**
     * Gate (points): each point lands in its containing cell (lowest index on a shared edge);
     * points outside the mesh or scope are reported, not assigned; flow and species values come
     * from fields; CELL and XY rows read back exactly, and undo restores the sources.
     */
    void pointSourcesFromFeatures()
    {
        Engine en;
        QVERIFY(en.open());
        const QString points = writePoints(en.dir + "/wells.gpkg");
        QVERIFY(!points.isEmpty());
        for (bool byXy : {false, true}) {
            PointSourceJob job;
            job.path = points;
            job.meshCrsWkt = utm18();
            job.flowField = "FLOW";
            job.nameField = "NAME";
            job.termFields = {"TSS"};
            fixtureMesh(&job.vertices, &job.cells);
            job.scope = {kTri, kQuad};
            job.byXy = byXy;
            const auto sample = samplePointSources(job);
            QVERIFY2(sample.error.isEmpty(), qPrintable(sample.error));
            QVERIFY(sample.pointLayer);
            QCOMPARE(sample.features.size(), 5);
            QCOMPARE(sample.features[0].cell, kTri);
            QCOMPARE(sample.features[1].cell, kQuad);
            QCOMPARE(sample.features[2].cell, kTri);
            QVERIFY(sample.features[2].sharedEdge);
            QVERIFY(sample.features[3].problem.contains("outside the mesh"));
            QVERIFY(sample.features[4].problem.contains("outside the assignment scope"));
            GroundwaterTransportRequest request;
            QString error;
            QVERIFY2(readGroundwaterTransportSnapshot(en.e, &request.before, &error),
                     qPrintable(error));
            request.target = GroundwaterTransportTarget::PointSources;
            for (const auto &f : sample.features) {
                if (!f.problem.isEmpty())
                    continue;
                GroundwaterSourceRow row;
                row.name = f.name;
                row.cell = byXy ? f.xyCell : f.cell;
                row.flow = f.flow;
                row.byXy = byXy;
                row.x = f.xy.x();
                row.y = f.xy.y();
                row.terms = {{"TSS", "CONC", {}, f.termValues[0]}};
                request.points << row;
            }
            const auto p = previewGroundwaterTransport(request);
            QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
            QCOMPARE(p.after.sources.size(), 3);
            double total = 0;
            for (const auto &s : p.after.sources)
                total += s.flow * s.scale;
            QVERIFY(std::abs(total - .0015) < 1e-15);
            const auto applied = applyGroundwaterTransport(en.e, p);
            QVERIFY2(applied.success, qPrintable(applied.error));
            GroundwaterTransportSnapshot now;
            QVERIFY(readGroundwaterTransportSnapshot(en.e, &now));
            QCOMPARE(now.sources, p.after.sources);
            QCOMPARE(now.sources[1].terms[0].value, 20.);
            QCOMPARE(now.sources[0].byXy, byXy);
            if (byXy) {
                const QString saved = en.dir + "/saved_xy.inp";
                QCOMPARE(swmm_model_write(en.e, saved.toUtf8().constData()), SWMM_OK);
            }
            QVERIFY(undoGroundwaterTransport(en.e, p).success);
            QVERIFY(readGroundwaterTransportSnapshot(en.e, &now));
            QVERIFY(now == request.before);
        }
    }

    /**
     * Gate (conservation): a region total written as one TAG source keeps the requested total
     * (the engine area-weights it); the cells are tagged and undo restores sources and tags.
     */
    void tagSourceCarriesRegionTotal()
    {
        Engine en;
        QVERIFY(en.open());
        GroundwaterTransportRequest r;
        QVERIFY(readGroundwaterTransportSnapshot(en.e, &r.before));
        r.target = GroundwaterTransportTarget::Source;
        r.cells = {kTri, kQuad};
        r.values = {-.003, -.003};
        r.distribution = GroundwaterSourceDistribution::RegionTotal;
        r.areaWeights = {2, 4};
        r.sourceName = "Wellfield";
        r.tag = "WELLS";
        const auto p = previewGroundwaterTransport(r);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QCOMPARE(p.after.sources.size(), 1);
        QCOMPARE(p.after.sources[0].scope, 1);
        QCOMPARE(p.after.sources[0].flow, -.003);
        QCOMPARE(p.after.cellTags, QStringList({"WELLS", "PAD", "WELLS"}));
        const auto applied = applyGroundwaterTransport(en.e, p);
        QVERIFY2(applied.success, qPrintable(applied.error));
        GroundwaterTransportSnapshot now;
        QVERIFY(readGroundwaterTransportSnapshot(en.e, &now));
        QVERIFY(now == p.after);
        QVERIFY(undoGroundwaterTransport(en.e, p).success);
        QVERIFY(readGroundwaterTransportSnapshot(en.e, &now));
        QVERIFY(now == r.before);
        // The PAD cell would leave a tag the aquifer uses: refused unless allowed.
        r.cells = {kPad, kQuad};
        QVERIFY(previewGroundwaterTransport(r).error.contains("PAD"));
        r.allowRetag = true;
        QVERIFY2(previewGroundwaterTransport(r).error.isEmpty(),
                 qPrintable(previewGroundwaterTransport(r).error));
    }

    /**
     * [2D_AQUIFER] properties: a TAG row is written only when it is lossless (every cell shares
     * one effective record and value); undo and an injected failure restore rows and tags.
     */
    void aquiferTagRowIsLossless()
    {
        Engine en;
        QVERIFY(en.open());
        AquiferRequest r;
        QVERIFY(readAquiferSnapshot(en.e, &r.before, nullptr));
        r.cells = {kTri, kQuad};
        r.cellTags = {QString(), QString()};
        r.target = "KS";
        r.values = {12, 12};
        r.strategy = AquiferWriteStrategy::Automatic;
        r.allCellTags = engineCellTags(en.e);
        const auto p = previewAquiferAssignment(r);
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QCOMPARE(p.appended.size(), 1);
        QCOMPARE(p.appended[0].scope, 1);
        QCOMPARE(p.appended[0].tag, QStringLiteral("AQUIFER_1"));
        QCOMPARE(p.appended[0].ks, 12.);
        QCOMPARE(p.tagsAfter, QStringList({"AQUIFER_1", "PAD", "AQUIFER_1"}));
        QString error;
        QVERIFY(!applyAquiferPreview(en.e, p, &error, [](int n) { return n != 2; }));
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(en.e, &now, nullptr));
        QCOMPARE(now, r.before);
        QCOMPARE(engineCellTags(en.e), r.allCellTags);
        QVERIFY2(applyAquiferPreview(en.e, p, &error), qPrintable(error));
        QCOMPARE(engineCellTags(en.e), p.tagsAfter);
        QVERIFY(readAquiferSnapshot(en.e, &now, nullptr));
        QCOMPARE(now.rows.size(), r.before.rows.size() + 1);
        QVERIFY2(undoAquiferPreview(en.e, p, &error), qPrintable(error));
        QVERIFY(readAquiferSnapshot(en.e, &now, nullptr));
        QCOMPARE(now, r.before);
        QCOMPARE(engineCellTags(en.e), r.allCellTags);
        // PAD resolves to its own TAG record (no HG0), so one row would not be lossless.
        r.cells = {kTri, kPad};
        r.cellTags = {QString(), "PAD"};
        const auto mixed = previewAquiferAssignment(r);
        QVERIFY2(mixed.error.isEmpty(), qPrintable(mixed.error));
        QVERIFY(mixed.tagsAfter.isEmpty());
        QCOMPARE(mixed.appended.size(), 2);
        QVERIFY(mixed.notes.join(' ').contains("differ"));
        r.strategy = AquiferWriteStrategy::TagRow;
        QVERIFY(previewAquiferAssignment(r).error.contains("differ"));
    }

    /**
     * Gate (integrated rate in the engine): the same region total written as CELL rows with
     * area scales and as one TAG source adds the same water to the aquifer in a saved, reopened
     * and simulated model (0.006 m³/s for 300 s = 1.8 m³).
     */
    void tagAndCellRegionTotalsGiveSameLedger()
    {
        double waterIn[2] = {0, 0};
        for (int tagged = 0; tagged < 2; ++tagged) {
            Engine en;
            QVERIFY(en.open());
            GroundwaterTransportRequest r;
            QVERIFY(readGroundwaterTransportSnapshot(en.e, &r.before));
            r.target = GroundwaterTransportTarget::Source;
            r.cells = {kTri, kQuad};
            r.values = {.006, .006};
            r.distribution = GroundwaterSourceDistribution::RegionTotal;
            r.areaWeights = {2, 4};
            r.sourceName = "Wellfield";
            r.tag = tagged ? QStringLiteral("WELLS") : QString();
            const auto p = previewGroundwaterTransport(r);
            QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
            QVERIFY(applyGroundwaterTransport(en.e, p).success);
            const QString saved = en.dir + (tagged ? "/tag_source.inp" : "/cell_sources.inp");
            QCOMPARE(swmm_model_write(en.e, saved.toUtf8().constData()), SWMM_OK);
            QString error;
            QVERIFY2(simulateSourceIn(saved, &waterIn[tagged], &error), qPrintable(error));
        }
        QVERIFY2(std::abs(waterIn[0] - 1.8) < 1e-9,
                 qPrintable(QString::number(waterIn[0], 'g', 17)));
        QVERIFY2(std::abs(waterIn[1] - waterIn[0]) < 1e-12,
                 qPrintable(QString("%1 vs %2").arg(waterIn[1], 0, 'g', 17).arg(waterIn[0], 0, 'g', 17)));
    }
};

QTEST_MAIN(TestSpatialAssignment)
#include "test_spatialassignment.moc"
