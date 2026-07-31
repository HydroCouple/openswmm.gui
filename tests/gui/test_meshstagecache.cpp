/*!
 * \file   test_meshstagecache.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * mesh::MeshStageCache — bit-exact payload round-trips, key sensitivity,
 * corrupt-entry rejection, pruning, and graceful degradation.
 */
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "mesh/meshstagecache.h"

using mesh::MeshStageCache;

namespace {

MeshStageCache::BoundaryPrep sampleBoundary()
{
    MeshStageCache::BoundaryPrep v;
    v.domains = {QPolygonF(QVector<QPointF>{
        {0.123456789012, 0.0}, {100.0, 0.0}, {100.0, 50.5}, {0.123456789012, 0.0}})};
    v.holeRings = {
        {{10, 10}, {12, 10}, {12, 12}, {10, 12}, {10, 10}},
        {{20, 20}, {22, 20}, {21, 22}, {20, 20}}};
    v.holeSeeds = {QPointF(11.000000001, 11.0), QPointF(21.0, 20.7)};
    v.holeValid = {true, false};
    v.skippedRings = 1;
    return v;
}

MeshStageCache::TerrainPoints sampleTerrain()
{
    MeshStageCache::TerrainPoints v;
    for (int i = 0; i < 1000; ++i)
    {
        v.xyDtm.append(QPointF(i * 0.333333333333331, i * 0.777777777777779));
        v.z.append(1234.56789 + i * 1e-9);
    }
    return v;
}

MeshStageCache::FileIdentity ident(const QString &p, qint64 mt, qint64 sz)
{
    MeshStageCache::FileIdentity id;
    id.absPath = p; id.mtimeMs = mt; id.sizeBytes = sz;
    return id;
}

} // namespace

class TestMeshStageCache : public QObject
{
    Q_OBJECT

private slots:

    void boundaryRoundTrip_bitExact()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MeshStageCache cache(dir.filePath("model.inp"));
        QVERIFY(cache.isUsable());

        const auto v = sampleBoundary();
        const QByteArray key = MeshStageCache::boundaryKey(
            ident("/a/b.gpkg", 111, 222), {}, "bnd", "WKT_B", "WKT_M", 0.1, 2.0);

        QVERIFY(!cache.loadBoundary(key, nullptr));
        MeshStageCache::BoundaryPrep miss;
        QVERIFY(!cache.loadBoundary(key, &miss));

        QVERIFY(cache.storeBoundary(key, v));
        MeshStageCache::BoundaryPrep got;
        QVERIFY(cache.loadBoundary(key, &got));

        QCOMPARE(got.domains, v.domains);
        QCOMPARE(got.holeRings, v.holeRings);
        QCOMPARE(got.holeSeeds, v.holeSeeds);
        QCOMPARE(got.holeValid, v.holeValid);
        QCOMPARE(got.skippedRings, v.skippedRings);
        // Bit-exact doubles.
        QVERIFY(std::memcmp(&got.holeSeeds[0], &v.holeSeeds[0],
                            sizeof(QPointF)) == 0);
    }

    void terrainRoundTrip_bitExact()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MeshStageCache cache(dir.filePath("model.inp"));

        const auto v = sampleTerrain();
        const QByteArray key = MeshStageCache::terrainKey(
            ident("/dem.tif", 5, 6), 1, {}, true,
            QRectF(QPointF(0, 0), QPointF(10, 10)));

        QVERIFY(cache.storeTerrain(key, v));
        MeshStageCache::TerrainPoints got;
        QVERIFY(cache.loadTerrain(key, &got));
        QCOMPARE(got.xyDtm.size(), v.xyDtm.size());
        QVERIFY(std::memcmp(got.xyDtm.constData(), v.xyDtm.constData(),
                            size_t(v.xyDtm.size()) * sizeof(QPointF)) == 0);
        QVERIFY(std::memcmp(got.z.constData(), v.z.constData(),
                            size_t(v.z.size()) * sizeof(double)) == 0);
    }

    void keys_changeWithEveryInput()
    {
        const auto base = MeshStageCache::boundaryKey(
            ident("/a", 1, 2), {}, "L", "B", "M", 0.1, 2.0);
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a2", 1, 2), {}, "L", "B", "M", 0.1, 2.0));   // path
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 9, 2), {}, "L", "B", "M", 0.1, 2.0));    // mtime
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 1, 9), {}, "L", "B", "M", 0.1, 2.0));    // size
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 1, 2), "H", "L", "B", "M", 0.1, 2.0));   // subcatch hash
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 1, 2), {}, "L2", "B", "M", 0.1, 2.0));   // layer
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 1, 2), {}, "L", "B2", "M", 0.1, 2.0));   // boundary CRS
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 1, 2), {}, "L", "B", "M2", 0.1, 2.0));   // mesh CRS
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 1, 2), {}, "L", "B", "M", 0.2, 2.0));    // eps
        QVERIFY(base != MeshStageCache::boundaryKey(
            ident("/a", 1, 2), {}, "L", "B", "M", 0.1, 3.0));    // maxLen

        mesh::DTMThinnerOptions o;
        const QRectF bb(QPointF(0, 0), QPointF(10, 10));
        const auto tb = MeshStageCache::terrainKey(ident("/d", 1, 2), 1, o, true, bb);
        QVERIFY(tb != MeshStageCache::terrainKey(ident("/d", 2, 2), 1, o, true, bb));
        QVERIFY(tb != MeshStageCache::terrainKey(ident("/d", 1, 2), 2, o, true, bb));
        QVERIFY(tb != MeshStageCache::terrainKey(ident("/d", 1, 2), 1, o, false, bb));
        mesh::DTMThinnerOptions o2 = o; o2.normalDotThreshold = 0.9;
        QVERIFY(tb != MeshStageCache::terrainKey(ident("/d", 1, 2), 1, o2, true, bb));
        mesh::DTMThinnerOptions o3 = o; o3.maxIterations = 5;
        QVERIFY(tb != MeshStageCache::terrainKey(ident("/d", 1, 2), 1, o3, true, bb));
        QVERIFY(tb != MeshStageCache::terrainKey(ident("/d", 1, 2), 1, o, true,
                    QRectF(QPointF(0, 0), QPointF(10, 11))));
        // Poisson-disk fields are deliberately NOT part of the key.
        mesh::DTMThinnerOptions o4 = o; o4.useMinSpacing = true; o4.minSpacing = 3.0;
        QCOMPARE(tb, MeshStageCache::terrainKey(ident("/d", 1, 2), 1, o4, true, bb));
    }

    void corruptEntry_isMiss()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MeshStageCache cache(dir.filePath("model.inp"));
        const QByteArray key = MeshStageCache::boundaryKey(
            ident("/a", 1, 2), {}, "L", "B", "M", 0.1, 2.0);
        QVERIFY(cache.storeBoundary(key, sampleBoundary()));

        const QString path =
            cache.dir() + "/A-" + QString::fromLatin1(key) + ".bin";
        QVERIFY(QFile::exists(path));

        // Truncate → miss.
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadWrite));
            f.resize(f.size() / 2);
        }
        MeshStageCache::BoundaryPrep got;
        QVERIFY(!cache.loadBoundary(key, &got));

        // Garbage magic → miss.
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("garbage");
        }
        QVERIFY(!cache.loadBoundary(key, &got));
    }

    void prune_keepsNewestPerStage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MeshStageCache cache(dir.filePath("model.inp"));

        for (int i = 0; i < 12; ++i)
        {
            const QByteArray key = MeshStageCache::boundaryKey(
                ident(QStringLiteral("/f%1").arg(i), i, i), {}, "L", "B", "M",
                0.1, 2.0);
            QVERIFY(cache.storeBoundary(key, sampleBoundary()));
        }
        const QStringList entries =
            QDir(cache.dir()).entryList({QStringLiteral("A-*.bin")}, QDir::Files);
        QCOMPARE(entries.size(), 8);   // storeBoundary prunes after each store
    }

    void unusableDir_degradesGracefully()
    {
        MeshStageCache empty{QString()};
        QVERIFY(!empty.isUsable());
        QVERIFY(!empty.storeBoundary("00", sampleBoundary()));
        MeshStageCache::BoundaryPrep got;
        QVERIFY(!empty.loadBoundary("00", &got));
        empty.prune();   // no crash
    }
};

QTEST_MAIN(TestMeshStageCache)
#include "test_meshstagecache.moc"
