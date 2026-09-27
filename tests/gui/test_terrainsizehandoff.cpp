/*!
 * \file test_terrainsizehandoff.cpp
 * \brief Distinct, bounded terrain survivor input for the size field.
 */
#include "mesh/sizefield.h"

#include <QtTest>
#include <limits>

namespace {

const QRectF kBox(0, 0, 1000, 1000);

mesh::SizeFieldOptions options()
{
    mesh::SizeFieldOptions opt;
    opt.nearSize = 10.0;
    opt.gradation = 0.25;
    opt.terrainDensity = true;
    return opt;
}

mesh::ConstraintSegment feature()
{
    mesh::ConstraintSegment result;
    result.path = {QPointF(100, 500), QPointF(900, 500)};
    return result;
}

void addCluster(mesh::TerrainSizeInput &input)
{
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x)
            input.addAcceptedPoint(QPointF(20.0 + 1.5 * x, 20.0 + 1.5 * y));
}

} // namespace

class TestTerrainSizeHandoff : public QObject
{
    Q_OBJECT
private slots:
    void acceptedTerrainRefinesWithoutCopyingThePointCloud()
    {
        const auto opt = options();
        mesh::TerrainSizeInput terrain, empty;
        QVERIFY(terrain.prepare(kBox, opt));
        QVERIFY(empty.prepare(kBox, opt));
        const qsizetype allocatedCells = terrain.cellCount();
        addCluster(terrain);
        QCOMPARE(terrain.sampleCount(), 1600);
        QCOMPARE(terrain.cellCount(), allocatedCells);
        mesh::SizeField withTerrain, withoutTerrain;
        QVERIFY(withTerrain.build(kBox, {feature()}, {}, {}, opt, &terrain));
        QVERIFY(withoutTerrain.build(kBox, {feature()}, {}, {}, opt, &empty));
        QVERIFY(withTerrain.terrainSizeAt(50, 50) > 0.0);
        QCOMPARE(withoutTerrain.terrainSizeAt(50, 50), 0.0);
        QVERIFY(withTerrain.targetAreaAt(50, 50)
                < 0.5 * withoutTerrain.targetAreaAt(50, 50));
        for (double x = 0; x <= 1000; x += 97)
            for (double y = 0; y <= 1000; y += 97)
                QVERIFY(withTerrain.targetAreaAt(x, y)
                        <= withoutTerrain.targetAreaAt(x, y) + 1e-9);
    }

    void auxiliaryAndTaggedPointsDoNotBecomeTerrain()
    {
        const auto opt = options();
        mesh::TerrainSizeInput terrain;
        QVERIFY(terrain.prepare(kBox, opt));
        QVector<mesh::SteinerPoint> auxiliary;
        for (int n = 0; n < 100; ++n) {
            mesh::SteinerPoint sp;
            sp.xy = QPointF(50, 50);
            sp.marker = n % 2; // auxiliary marker zero and tagged node seeds
            auxiliary.append(sp);
        }
        mesh::SizeField field;
        QVERIFY(field.build(kBox, {feature()}, {}, auxiliary, opt, &terrain));
        QCOMPARE(field.terrainSizeAt(50, 50), 0.0);
        QCOMPARE(terrain.sampleCount(), 0);
        terrain.addAcceptedPoint(QPointF(50, 50));
        QVERIFY(field.build(kBox, {feature()}, {}, auxiliary, opt, &terrain));
        QCOMPARE(field.terrainSizeAt(50, 50), field.pitch());
        QCOMPARE(terrain.sampleCount(), 1);
    }

    void terrainOnlyAndFloorPrecedence()
    {
        auto opt = options();
        opt.areaFloor = 20.0;
        mesh::TerrainSizeInput terrain;
        QVERIFY(terrain.prepare(kBox, opt));
        addCluster(terrain);
        mesh::SizeField field;
        QVERIFY(field.build(kBox, {}, {}, {}, opt, &terrain));
        QVERIFY(field.terrainSizeAt(50, 50) > 0.0);
        QCOMPARE(field.targetAreaAt(50, 50), opt.areaFloor);
    }

    void invalidSamplesAndReprepare()
    {
        const auto opt = options();
        mesh::TerrainSizeInput terrain;
        QVERIFY(terrain.prepare(kBox, opt));
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (const QPointF &point : {QPointF(nan, 50), QPointF(50, inf),
                                     QPointF(1e200, 50), QPointF(-1e200, 50)})
            terrain.addAcceptedPoint(point);
        QCOMPARE(terrain.sampleCount(), 0);
        terrain.addAcceptedPoint(QPointF(50, 50));
        QCOMPARE(terrain.sampleCount(), 1);
        QVERIFY(terrain.prepare(kBox, opt));
        QCOMPARE(terrain.sampleCount(), 0);
        QVERIFY(!terrain.prepare(QRectF(), opt));
        QCOMPARE(terrain.cellCount(), 0);
        terrain.addAcceptedPoint(QPointF(50, 50));
        QCOMPARE(terrain.sampleCount(), 0);
    }

    void mismatchedGridIsRefused()
    {
        const auto opt = options();
        mesh::TerrainSizeInput terrain;
        QVERIFY(terrain.prepare(kBox, opt));
        addCluster(terrain);
        mesh::SizeField field;
        QVERIFY(field.build(kBox, {feature()}, {}, {}, opt, &terrain));
        QVERIFY(!field.build(kBox.translated(5, 5), {feature()}, {}, {}, opt, &terrain));
        QVERIFY(!field.isValid());
        auto changed = opt;
        changed.nearSize = 20;
        QVERIFY(!field.build(kBox, {feature()}, {}, {}, changed, &terrain));
        QVERIFY(!field.isValid());
    }

    void storageRemainsBoundedAndDensityCanBeDisabled()
    {
        auto opt = options();
        opt.maxGridCells = 10000;
        mesh::TerrainSizeInput terrain;
        QVERIFY(terrain.prepare(kBox, opt));
        const qsizetype cells = terrain.cellCount();
        QVERIFY(cells <= opt.maxGridCells * 2);
        for (int n = 0; n < 100000; ++n)
            terrain.addAcceptedPoint(QPointF(50, 50));
        QCOMPARE(terrain.cellCount(), cells);
        QCOMPARE(terrain.sampleCount(), 100000);
        opt.terrainDensity = false;
        mesh::SizeField field;
        QVERIFY(field.build(kBox, {feature()}, {}, {}, opt, &terrain));
        QCOMPARE(field.terrainSizeAt(50, 50), 0.0);
        QVERIFY(!terrain.prepare(kBox, opt));
        QCOMPARE(terrain.cellCount(), 0);
        QCOMPARE(terrain.sampleCount(), 0);
    }
};

QTEST_APPLESS_MAIN(TestTerrainSizeHandoff)
#include "test_terrainsizehandoff.moc"
