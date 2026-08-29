/*!
 * \file   test_gisvectorpopulate.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  GIS vector layers populate the FULL feature set — the scene culls
 *         per frame; the layer never clips to a viewport.
 *
 * Pins the fix for "polygons partially in view vanish": populateScene used to
 * apply an OGR viewport spatial filter, and refreshScene ignores the extent
 * argument by design — so the feature set froze to whatever extent was
 * current at the last rebuild (open, symbology edit, selection change,
 * reorder), and anything outside it simply did not exist. Also pins the
 * OpenSWMMVisScene::removeItemsForLayer branch for VectorPolygonPathItem,
 * the class every GIS polygon actually uses (a silent leak before).
 *
 * The GPKG fixture is written under ./test_gisvectorpopulate_output/ in the
 * ctest working directory, reviewable per CLAUDE.md §4.1.
 */
#include "layers/gisvectorlayer.h"
#include "map/graphicsitems.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "map/openswmmvisscene.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QSet>
#include <QTest>

#include <cmath>

namespace {

QString outDir()
{
    QDir().mkpath(QStringLiteral("test_gisvectorpopulate_output"));
    return QStringLiteral("test_gisvectorpopulate_output");
}

QString gpkgPath() { return outDir() + QStringLiteral("/three_polys.gpkg"); }
QString crsGpkgPath() { return outDir() + QStringLiteral("/crs_utm33n.gpkg"); }

/*! One square, stamped with EPSG:32633 (UTM 33N) — a PROJECTED CRS that is
 *  unmistakably not the CRS-less fixture above and not WGS84. Used to prove
 *  the layer adopts the file's CRS rather than inheriting the canvas/project
 *  one. Idempotent. */
bool buildCrsFixture()
{
    GDALAllRegister();
    GDALDriver *gpkg = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!gpkg) return false;
    const QByteArray gp = crsGpkgPath().toUtf8();
    if (QFile::exists(crsGpkgPath())) gpkg->Delete(gp.constData());

    GDALDataset *ds = gpkg->Create(gp.constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) return false;
    OGRSpatialReference srs;
    if (srs.importFromEPSG(32633) != OGRERR_NONE) { GDALClose(ds); return false; }
    OGRLayer *layer = ds->CreateLayer("utm_polys", &srs, wkbPolygon, nullptr);
    if (!layer) { GDALClose(ds); return false; }

    OGRLinearRing ring;
    ring.addPoint(500000.0, 4600000.0);
    ring.addPoint(501000.0, 4600000.0);
    ring.addPoint(501000.0, 4601000.0);
    ring.addPoint(500000.0, 4601000.0);
    ring.addPoint(500000.0, 4600000.0);
    OGRPolygon poly;
    poly.addRing(&ring);
    OGRFeature *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
    f->SetGeometry(&poly);
    const bool ok = (layer->CreateFeature(f) == OGRERR_NONE);
    OGRFeature::DestroyFeature(f);
    GDALClose(ds);
    return ok;
}

/*! Three disjoint unit squares at x = [0,1], [10,11], [20,21] (y = [0,1]).
 *  Idempotent — deletes any prior fixture. */
bool buildFixture()
{
    GDALAllRegister();
    GDALDriver *gpkg = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!gpkg) return false;
    const QByteArray gp = gpkgPath().toUtf8();
    if (QFile::exists(gpkgPath())) gpkg->Delete(gp.constData());

    GDALDataset *ds = gpkg->Create(gp.constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) return false;
    OGRLayer *layer = ds->CreateLayer("polys", nullptr, wkbPolygon, nullptr);
    if (!layer) { GDALClose(ds); return false; }

    for (double x0 : {0.0, 10.0, 20.0}) {
        OGRLinearRing ring;
        ring.addPoint(x0, 0.0);
        ring.addPoint(x0 + 1.0, 0.0);
        ring.addPoint(x0 + 1.0, 1.0);
        ring.addPoint(x0, 1.0);
        ring.addPoint(x0, 0.0);
        OGRPolygon poly;
        poly.addRing(&ring);
        OGRFeature *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
        f->SetGeometry(&poly);
        const bool ok = (layer->CreateFeature(f) == OGRERR_NONE);
        OGRFeature::DestroyFeature(f);
        if (!ok) { GDALClose(ds); return false; }
    }
    GDALClose(ds);
    return true;
}

QList<VectorPolygonPathItem *> polygonItems(const QGraphicsScene &scene)
{
    QList<VectorPolygonPathItem *> out;
    for (QGraphicsItem *it : scene.items())
        if (auto *p = dynamic_cast<VectorPolygonPathItem *>(it))
            out.append(p);
    return out;
}

} // namespace

class TestGisVectorPopulate : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase() { QVERIFY(buildFixture()); QVERIFY(buildCrsFixture()); }

    /*! A layer must adopt the CRS its file declares. */
    void vectorLayerAdoptsFileCRS()
    {
        GISVectorLayer layer(crsGpkgPath(), QStringLiteral("utm_polys"));
        SpatialReferenceSystem *s = layer.srs();
        QVERIFY2(s != nullptr,
                 "layer has no SRS after opening a file declaring EPSG:32633");
        QCOMPARE(s->code(), 32633);
        QVERIFY(s->isProjected());
    }

    /*! And it must REPROJECT to the canvas CRS when they differ.
     *
     *  The regression: rebuildTransform() was reachable only from
     *  onCanvasCRSChanged(), so a layer added to a canvas whose CRS never
     *  subsequently changed kept a null transform and drew raw file
     *  coordinates as though they were already in the canvas CRS. A UTM-33N
     *  square then rendered at x≈500000 on a WGS84 canvas instead of lon≈15.
     */
    void vectorLayerReprojectsToCanvasCRS()
    {
        GISVectorLayer layer(crsGpkgPath(), QStringLiteral("utm_polys"));
        SpatialReferenceSystem canvas(QStringLiteral("EPSG"), 4326);
        OpenSWMMVisScene scene;
        layer.populateScene(&scene, MapExtent(-180.0, -90.0, 180.0, 90.0), &canvas);

        const auto items = polygonItems(scene);
        QCOMPARE(items.size(), 1);
        const QRectF b = items.first()->sceneBoundingRect();

        // 500000 E, 4600000 N in UTM 33N is ≈ 15°E, 41.5°N. Scene Y is
        // inverted, so latitude arrives negated — compare |y|.
        QVERIFY2(std::abs(b.left()) < 180.0 && std::abs(b.top()) < 180.0,
                 qPrintable(QStringLiteral("geometry not reprojected — bounds %1,%2")
                                .arg(b.left()).arg(b.top())));
        QVERIFY(std::abs(b.left() - 15.0) < 1.5);
        QVERIFY(std::abs(std::abs(b.top()) - 41.5) < 1.5);
    }

    /*! A file with NO CRS keeps a null SRS and is assumed to be in the canvas
     *  CRS — the long-standing behaviour local-coordinate data relies on.
     *  Its coordinates must therefore pass through untouched. */
    void vectorLayerWithoutFileCRSPassesThrough()
    {
        GISVectorLayer layer(gpkgPath(), QStringLiteral("polys"));
        QVERIFY(layer.srs() == nullptr);

        SpatialReferenceSystem canvas(QStringLiteral("EPSG"), 4326);
        OpenSWMMVisScene scene;
        layer.populateScene(&scene, MapExtent(-1.0, -1.0, 30.0, 30.0), &canvas);
        QCOMPARE(polygonItems(scene).size(), 3);
    }

    /*! The core fix: an extent covering only polygon 1 must still populate
     *  all three features — the viewport is a CULL, not a feature gate. */
    void partialViewport_populatesEveryFeature()
    {
        GISVectorLayer layer(gpkgPath(), QStringLiteral("polys"));
        OpenSWMMVisScene scene;
        layer.populateScene(&scene, MapExtent(0.0, 0.0, 1.5, 1.5), nullptr);
        QCOMPARE(polygonItems(scene).size(), 3);
    }

    /*! refreshScene at a different extent neither drops nor rebuilds the
     *  items (no churn — the set is viewport-independent). */
    void panAfterPopulate_keepsAllFeatures()
    {
        GISVectorLayer layer(gpkgPath(), QStringLiteral("polys"));
        OpenSWMMVisScene scene;
        layer.populateScene(&scene, MapExtent(0.0, 0.0, 1.5, 1.5), nullptr);
        const auto before = polygonItems(scene);
        QCOMPARE(before.size(), 3);

        // Churn probe: a rebuilt item set cannot carry this tag. Raw
        // pointer-set equality is NOT a valid churn gate here — the
        // allocator routinely hands the freed blocks straight back, and a
        // failed compare would stringify dangling QGraphicsItem*s.
        constexpr int kChurnProbe = 7;
        for (auto *p : before) p->setData(kChurnProbe, true);

        layer.refreshScene(&scene, MapExtent(19.0, -1.0, 22.0, 2.0), nullptr);

        const auto after = polygonItems(scene);
        QCOMPARE(after.size(), 3);
        for (auto *p : after)
            QVERIFY2(p->data(kChurnProbe).toBool(),
                     "refreshScene rebuilt the item set after a plain pan");
    }

    /*! Selection arms a rebuild; the rebuild at an extent that EXCLUDES the
     *  selected feature must keep every feature and carry the highlight —
     *  pre-fix this was "select a polygon, pan, and it vanishes". */
    void selectionRefresh_survivesViewportChange()
    {
        GISVectorLayer layer(gpkgPath(), QStringLiteral("polys"));
        OpenSWMMVisScene scene;
        layer.populateScene(&scene, MapExtent(0.0, 0.0, 30.0, 2.0), nullptr);
        const auto items = polygonItems(scene);
        QCOMPARE(items.size(), 3);

        // Pick the middle square (scene x ≈ 10..11) and remember an
        // unselected sibling's paint for the differ check.
        VectorPolygonPathItem *mid = nullptr, *other = nullptr;
        for (auto *p : items) {
            const double cx = p->sceneBoundingRect().center().x();
            if (cx > 9.0 && cx < 12.0) mid = p; else other = p;
        }
        QVERIFY(mid && other);
        const qint64 midFid = mid->featureId();

        layer.setSelectedFeatureIds({midFid});   // arms m_needsRebuild
        layer.refreshScene(&scene, MapExtent(0.0, 0.0, 1.5, 1.5), nullptr);

        const auto rebuilt = polygonItems(scene);
        QCOMPARE(rebuilt.size(), 3);
        VectorPolygonPathItem *midNew = nullptr, *otherNew = nullptr;
        for (auto *p : rebuilt) {
            if (p->featureId() == midFid) midNew = p; else otherNew = p;
        }
        QVERIFY(midNew && otherNew);
        QVERIFY2(midNew->brush() != otherNew->brush()
                     || midNew->pen() != otherNew->pen(),
                 "selected feature must be painted as highlighted");
    }

    /*! The scene helper must remove the polygon items GIS layers actually
     *  create (VectorPolygonPathItem — the VectorPolygonItem branch never
     *  matched them). */
    void removeItemsForLayer_removesPolygonPathItems()
    {
        GISVectorLayer layer(gpkgPath(), QStringLiteral("polys"));
        OpenSWMMVisScene scene;
        layer.populateScene(&scene, MapExtent(0.0, 0.0, 30.0, 2.0), nullptr);
        QCOMPARE(polygonItems(scene).size(), 3);

        scene.removeItemsForLayer(&layer);
        QVERIFY(scene.items().isEmpty());
    }
};

QTEST_MAIN(TestGisVectorPopulate)
#include "test_gisvectorpopulate.moc"
