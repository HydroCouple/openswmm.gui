/*!
 * \file   test_gisselectionsync.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  GIS feature layers ↔ SelectionManager ↔ Attribute Table, both
 *         directions (SVBC round B).
 *
 * Before this round both bridge signals were dead ends: the map tool's
 * selectionChanged had zero connect sites and GISVectorLayer's
 * selectionChanged had no subscriber, while the panel's slots hard-gated on
 * the SWMM model and the GIS table model discarded FIDs entirely. These
 * gates pin the whole loop: layer → bus → table rows, table rows → bus →
 * layer, exactly-one bus emission per gesture (no bounce), the FID display
 * column, the codec, and precise (not bbox) rubber-band hits.
 *
 * Fixture GPKG written under ./test_gisselectionsync_output/ (CLAUDE.md
 * §4.1 — reviewable location).
 */
#include "layers/gisobjectref.h"
#include "layers/gisvectorlayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "selection/gisselectionbridge.h"
#include "selection/selectionmanager.h"
#include "ui/panels/attributetablepanel.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>

namespace {

QString outDir()
{
    QDir().mkpath(QStringLiteral("test_gisselectionsync_output"));
    return QStringLiteral("test_gisselectionsync_output");
}

QString gpkgPath() { return outDir() + QStringLiteral("/sync_polys.gpkg"); }

/*! Three disjoint unit squares at x = 0, 10, 20 plus one right TRIANGLE at
 *  x = 30 whose bbox corner near (31, 1) is empty — the precise-vs-bbox
 *  rubber-band gate needs a feature whose geometry != its bbox. */
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

    const auto add = [&](std::initializer_list<QPointF> pts) {
        OGRLinearRing ring;
        for (const QPointF &p : pts) ring.addPoint(p.x(), p.y());
        ring.closeRings();
        OGRPolygon poly;
        poly.addRing(&ring);
        OGRFeature *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
        f->SetGeometry(&poly);
        const bool ok = (layer->CreateFeature(f) == OGRERR_NONE);
        OGRFeature::DestroyFeature(f);
        return ok;
    };
    bool ok = true;
    for (double x0 : {0.0, 10.0, 20.0})
        ok = ok && add({{x0, 0.0}, {x0 + 1.0, 0.0}, {x0 + 1.0, 1.0}, {x0, 1.0}});
    ok = ok && add({{30.0, 0.0}, {31.0, 0.0}, {30.0, 1.0}});   // triangle
    GDALClose(ds);
    return ok;
}

/*! Everything one test needs, wired the way SWMMVisProjectWindow wires it. */
struct Rig {
    MapCanvas canvas;
    GISVectorLayer *layer;              // owned by the canvas
    SelectionManager sel;
    GisSelectionBridge bridge{&sel, &canvas};

    explicit Rig() : layer(new GISVectorLayer(gpkgPath(), QStringLiteral("polys")))
    {
        canvas.addLayer(layer, /*pushUndo=*/false);
    }

    /*! fid of the square whose min-x is \p x0, straight from OGR. */
    long long fidAt(double x0) const
    {
        OGRLayer *ol = layer->ogrLayer();
        if (!ol) return -1;
        ol->SetSpatialFilter(nullptr);
        ol->ResetReading();
        OGRFeature *f = nullptr;
        long long fid = -1;
        while ((f = ol->GetNextFeature()) != nullptr) {
            OGREnvelope env;
            if (f->GetGeometryRef()) {
                f->GetGeometryRef()->getEnvelope(&env);
                if (qAbs(env.MinX - x0) < 0.5)
                    fid = static_cast<long long>(f->GetFID());
            }
            OGRFeature::DestroyFeature(f);
        }
        ol->ResetReading();
        return fid;
    }
};

} // namespace

class TestGisSelectionSync : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase() { QVERIFY(buildFixture()); }

    void gisObjectRef_roundTrips()
    {
        const long long bigFid = 5'000'000'000LL;   // > 32 bits
        const QString weirdId  = QStringLiteral("layer#7::odd");
        const SWMMObjectRef ref = GisObjectRef::feature(weirdId, bigFid);
        QCOMPARE(ref.objectType, SWMMObjectRef::Feature);

        QString outId;
        long long outFid = -1;
        QVERIFY(GisObjectRef::parseFeature(ref, &outId, &outFid));
        QCOMPARE(outId, weirdId);
        QCOMPARE(outFid, bigFid);

        // Non-feature refs and malformed names are refused untouched.
        QVERIFY(!GisObjectRef::parseFeature(
            SWMMObjectRef(SWMMObjectRef::Node, QStringLiteral("J1")),
            nullptr, nullptr));
        QVERIFY(!GisObjectRef::parseFeature(
            SWMMObjectRef(SWMMObjectRef::Feature, QStringLiteral("gis::x")),
            nullptr, nullptr));
    }

    /*! layer → bus → table rows (the direction the user reported dead). */
    void layerToBus_toTable()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);

        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view && view->model());
        QCOMPARE(view->model()->rowCount(), 4);
        QCOMPARE(view->model()->headerData(0, Qt::Horizontal).toString(),
                 QStringLiteral("FID"));

        const long long f1 = rig.fidAt(0.0), f3 = rig.fidAt(20.0);
        QVERIFY(f1 >= 0 && f3 >= 0);
        rig.layer->setSelectedFeatureIds({f1, f3});

        // Bus carries exactly the two Feature refs…
        int featureRefs = 0;
        for (const SWMMObjectRef &r : rig.sel.selection()) {
            QString lid; long long fid = -1;
            QVERIFY(GisObjectRef::parseFeature(r, &lid, &fid));
            QCOMPARE(lid, rig.layer->layerId());
            QVERIFY(fid == f1 || fid == f3);
            ++featureRefs;
        }
        QCOMPARE(featureRefs, 2);

        // …and the table's selected rows display exactly those FIDs.
        QSet<QString> selectedFids;
        for (const QModelIndex &idx :
             view->selectionModel()->selectedRows(0))
            selectedFids.insert(idx.data().toString());
        QCOMPARE(selectedFids,
                 (QSet<QString>{QString::number(f1), QString::number(f3)}));
    }

    /*! table rows → bus → layer, with exactly ONE bus emission for the
     *  gesture (busy guards on both bridge directions + the panel's
     *  applying-from-bus flag: nothing bounces). */
    void tableToBus_toLayer()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);

        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view && view->model());
        QSignalSpy spy(&rig.sel, &SelectionManager::selectionChanged);

        view->selectionModel()->select(
            view->model()->index(1, 0),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

        QCOMPARE(spy.count(), 1);
        const QString shownFid = view->model()->index(1, 0).data().toString();
        QCOMPARE(rig.layer->selectedFeatureIds(),
                 (QSet<long long>{shownFid.toLongLong()}));
    }

    /*! Precise geometry hits, not bbox hits, and modifier-free replace. */
    void rectSelect_hitsExactFeatures()
    {
        Rig rig;
        const long long f1 = rig.fidAt(0.0), f2 = rig.fidAt(10.0);

        // A rect spanning squares 1 and 2.
        QCOMPARE(rig.layer->featureIdsInRect(MapExtent(-0.5, -0.5, 11.5, 1.5)),
                 (QSet<long long>{f1, f2}));

        // The triangle's EMPTY bbox corner: overlaps its bounding box but
        // not its geometry — a bbox test would (wrongly) select it.
        QVERIFY(rig.layer->featureIdsInRect(
                        MapExtent(30.8, 0.8, 30.95, 0.95)).isEmpty());
    }

    /*! Feature refs on the bus leave non-GIS consumers untouched, and
     *  clearing the bus clears the layer. */
    void busClear_clearsLayer_andForeignRefsAreInert()
    {
        Rig rig;
        const long long f1 = rig.fidAt(0.0);
        rig.layer->setSelectedFeatureIds({f1});
        QCOMPARE(rig.sel.selection().size(), 1);

        // A non-Feature ref alongside — the bridge must not touch it and
        // must not misparse it.
        QSet<SWMMObjectRef> mixed = rig.sel.selection();
        mixed.insert(SWMMObjectRef(SWMMObjectRef::Node, QStringLiteral("J1")));
        rig.sel.select(mixed, SelectionManager::Replace);
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{f1}));

        // Clearing the bus empties the layer.
        rig.sel.select(QSet<SWMMObjectRef>{}, SelectionManager::Replace);
        QVERIFY(rig.layer->selectedFeatureIds().isEmpty());
    }
};

QTEST_MAIN(TestGisSelectionSync)
#include "test_gisselectionsync.moc"
