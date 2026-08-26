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
#include "map/openswmmvisscene.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QSet>
#include <QTest>

namespace {

QString outDir()
{
    QDir().mkpath(QStringLiteral("test_gisvectorpopulate_output"));
    return QStringLiteral("test_gisvectorpopulate_output");
}

QString gpkgPath() { return outDir() + QStringLiteral("/three_polys.gpkg"); }

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

    void initTestCase() { QVERIFY(buildFixture()); }

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
        QSet<QGraphicsItem *> before;
        for (auto *p : polygonItems(scene)) before.insert(p);
        QCOMPARE(before.size(), 3);

        layer.refreshScene(&scene, MapExtent(19.0, -1.0, 22.0, 2.0), nullptr);

        QSet<QGraphicsItem *> after;
        for (auto *p : polygonItems(scene)) after.insert(p);
        QCOMPARE(after, before);
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
