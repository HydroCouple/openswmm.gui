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
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/tabulardatalayer.h"
#include "mesh/meshresult.h"
#include "ui/dialogs/profilepathpickerdialog.h"
#include "ui/dialogs/sublayerselectiondialog.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QAction>
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QHeaderView>
#include <QLineEdit>
#include <QRadioButton>
#include <QTableWidget>
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

    OGRFieldDefn depth("depth value", OFTReal), label("name", OFTString);
    if (layer->CreateField(&depth) != OGRERR_NONE || layer->CreateField(&label) != OGRERR_NONE) {
        GDALClose(ds); return false;
    }
    int featureNumber = 0;
    const auto add = [&](std::initializer_list<QPointF> pts) {
        OGRLinearRing ring;
        for (const QPointF &p : pts) ring.addPoint(p.x(), p.y());
        ring.closeRings();
        OGRPolygon poly;
        poly.addRing(&ring);
        OGRFeature *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
        f->SetGeometry(&poly);
        if (featureNumber < 3) f->SetField("depth value", featureNumber == 0 ? 2.0 : featureNumber == 1 ? 10.0 : 30.0);
        f->SetField("name", featureNumber < 3 ? "Junction" : "O'Brien");
        ++featureNumber;
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

    void querySelectsGisRowsAndPreservesOnError()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);
        auto *view = panel.findChild<QTableView *>();
        auto *query = panel.findChild<QLineEdit *>("attributeQuery");
        QVERIFY(query && view);
        const auto fid = rig.fidAt(0.0);
        const auto excluded = GisObjectRef::feature(rig.layer->layerId(), fid);
        query->setText(QString("FID NOT IN (%1)").arg(fid));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(view->model()->rowCount(), 3);
        QCOMPARE(rig.sel.size(), 3);
        QVERIFY(!rig.sel.selection().contains(excluded));
        QCOMPARE(rig.layer->selectedFeatureIds().size(), 3);
        const auto validSelection = rig.sel.selection();
        query->setText("NOT MissingColumn = 1");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(view->model()->rowCount(), 3);
        QCOMPARE(rig.sel.selection(), validSelection);
        query->setText(QString("NOT (FID <> %1)").arg(fid));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(view->model()->rowCount(), 1);
        QCOMPARE(rig.sel.selection(), QSet<SWMMObjectRef>{excluded});

        auto chooseMode = [&](const QString &label) {
            for (auto *button : panel.findChildren<QRadioButton *>())
                if (button->text() == label) button->setChecked(true);
        };
        // Filtering must not erase the pre-query selection before the operation.
        chooseMode("Add");
        query->setText(QString("FID NOT IN (%1)").arg(fid));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(rig.sel.size(), 4);
        chooseMode("Subtract");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(rig.sel.selection(), QSet<SWMMObjectRef>{excluded});
        chooseMode("Intersect");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(rig.sel.size(), 0);
        chooseMode("Invert");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(rig.sel.size(), 4);
        chooseMode("Replace");
        query->setText(QString("FID = %1").arg(fid));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(rig.sel.selection(), QSet<SWMMObjectRef>{excluded});

        // Selected-only must not shrink the population used by query selection.
        for (auto *action : panel.findChildren<QAction *>())
            if (action->text() == "Show selected only") action->setChecked(true);
        query->setText(QString("FID NOT IN (%1)").arg(fid));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(rig.sel.size(), 3);
        QVERIFY(!rig.sel.selection().contains(excluded));
        view->sortByColumn(0, Qt::DescendingOrder);
        QCOMPARE(rig.sel.selection(), validSelection);
    }

    void gisAttributesSupportNegationAndNull()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);
        auto *query = panel.findChild<QLineEdit *>("attributeQuery");
        auto *view = panel.findChild<QTableView *>();
        query->setText("[depth value] NOT BETWEEN 3 AND 20");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(view->model()->rowCount(), 2);
        QCOMPARE(rig.sel.size(), 2);
        query->setText("[depth value] IS NULL AND name = 'O''Brien'");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(view->model()->rowCount(), 1);
        QCOMPARE(rig.sel.size(), 1);
        query->setText("name NOT LIKE 'J%'");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(view->model()->rowCount(), 1);
    }

    void queriesCoverModelDataMeshAndTabularSources()
    {
        // All categories actually selectable in either representative model.
        for (const QString &fixture : {QString("typed_selection_fixture.inp"),
                                       QString("data_object_table_fixture.inp")}) {
            SWMMModelLayer model(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", ".")).filePath(fixture), nullptr);
            QList<QString> warnings, errors;
            QVERIFY(model.loadModel(warnings, errors));
            MapCanvas canvas;
            SelectionManager selection;
            AttributeTablePanel panel;
            panel.setProject(&model, &selection, &canvas);
            auto *combo = panel.findChild<QComboBox *>();
            auto *view = panel.findChild<QTableView *>();
            auto *query = panel.findChild<QLineEdit *>("attributeQuery");
            QVERIFY(combo && query && view);
            int checked = 0;
            for (int category = 0; category < combo->count(); ++category) {
                combo->setCurrentIndex(category);
                const int total = view->model()->rowCount();
                if (!total) continue;
                QString field = view->model()->headerData(0, Qt::Horizontal).toString();
                field.replace('"', "\"\"");
                query->setText(QString("\"%1\" NOT IN ('__no_such_object__')").arg(field));
                QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
                QCOMPARE(view->model()->rowCount(), total);
                QCOMPARE(selection.size(), total);
                query->setText(QString("\"%1\" NOT LIKE '%'").arg(field));
                QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
                QCOMPARE(view->model()->rowCount(), 0);
                QCOMPARE(selection.size(), 0);
                ++checked;
            }
            QVERIFY(checked >= 3);
        }
        MapCanvas canvas;
        SelectionManager selection;
        mesh::MeshResult result;
        for (const QPointF &point : {QPointF(0,0), QPointF(1,0), QPointF(0,1)}) {
            mesh::MeshVertex vertex; vertex.xy = point; result.vertices.append(vertex);
        }
        mesh::MeshTriangle triangle; triangle.v0 = 0; triangle.v1 = 1; triangle.v2 = 2;
        result.triangles.append(triangle); result.ok = true;
        auto *meshLayer = new SWMM2DMeshLayer(result, "query-test.2dm");
        canvas.addLayer(meshLayer, false);
        AttributeTablePanel panel;
        panel.setProject(nullptr, &selection, &canvas);
        auto *view = panel.findChild<QTableView *>();
        auto *query = panel.findChild<QLineEdit *>("attributeQuery");
        for (int kind = 0; kind < 3; ++kind) {
            panel.showMeshTable(meshLayer, kind);
            const int count = view->model()->rowCount();
            QVERIFY(count > 0);
            const QString field = view->model()->headerData(0, Qt::Horizontal).toString();
            query->setText(QString("[%1] IS NOT NULL").arg(field));
            QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
            QCOMPARE(view->model()->rowCount(), count);
            QCOMPARE(selection.size(), count);
        }
        QFile csv("query-table.csv");
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("Name,Value\nA,2\nB,10\n"); csv.close();
        auto *tabular = new TabularDataLayer("query table");
        QVERIFY(tabular->loadFromFile(csv.fileName()));
        canvas.addLayer(tabular, false);
        panel.showLayerSource(tabular);
        const auto before = selection.selection();
        query->setText("Value NOT BETWEEN 0 AND 5");
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(view->model()->rowCount(), 1);
        QCOMPARE(view->model()->index(0, 0).data().toString(), QString("B"));
        QCOMPARE(selection.selection(), before);
    }

    void completionPreservesSuffixAndFollowsSource()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);
        panel.resize(980, 460);
        panel.show();
        auto *query = panel.findChild<QLineEdit *>("attributeQuery");
        auto *completer = query->findChild<QCompleter *>();
        QVERIFY(completer);
        query->setFocus();
        query->setText("FI = 1 OR FID = 2");
        query->setCursorPosition(2);
        QTest::keyClick(query, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(completer->completionCount() > 0);
        QVERIFY(completer->popup()->width() >= 240);
        QVERIFY(panel.grab().save("attribute-query.png"));
        QVERIFY(completer->popup()->grab().save("query-suggestions.png"));
        QTest::keyClick(completer->popup(), Qt::Key_Down);
        QTest::keyClick(completer->popup(), Qt::Key_Return);
        QCOMPARE(query->text(), QString("\"FID\" = 1 OR FID = 2"));
        QCOMPARE(query->cursorPosition(), 5);
        query->clear();
        QTest::keyClicks(query, "FID = 1");
        QVERIFY(!completer->popup()->isVisible());
        QTest::keyClick(query, Qt::Key_Return);
        QCOMPARE(panel.findChild<QTableView *>()->model()->rowCount(), 1);

        QFile csv("completion-table.csv");
        QVERIFY(csv.open(QIODevice::WriteOnly));
        csv.write("Other field\nvalue\n"); csv.close();
        auto *tabular = new TabularDataLayer("completion source");
        QVERIFY(tabular->loadFromFile(csv.fileName()));
        rig.canvas.addLayer(tabular, false);
        panel.showLayerSource(tabular);
        query->setText("Ot"); query->setCursorPosition(2);
        QTest::keyClick(query, Qt::Key_Space, Qt::ControlModifier);
        QCOMPARE(completer->completionCount(), 1);
        QCOMPARE(completer->completionModel()->index(0, 0).data().toString(), QString("\"Other field\""));
    }

    void sortedPickersKeepOriginalIdentity()
    {
        QList<GISVectorLayer::OgrSublayerInfo> layers;
        layers.append({"large", "Polygon", 10, "", 0});
        layers.append({"small", "Point", 2, "", 1});
        SublayerSelectionDialog chooser("query.gpkg", layers);
        auto *table = chooser.findChild<QTableWidget *>();
        QVERIFY(table->isSortingEnabled());
        table->sortItems(2, Qt::AscendingOrder);
        QCOMPARE(table->item(0, 0)->text(), QString("small"));
        table->item(1, 0)->setCheckState(Qt::Unchecked);
        QCOMPARE(chooser.selectedLayerNames(), QStringList{"small"});
        QCOMPARE(chooser.selectedSublayers().first().index, 1);
        chooser.findChild<QLineEdit *>()->setText("small");
        QVERIFY(!table->isRowHidden(0));
        QVERIFY(table->isRowHidden(1));

        QVector<ProfileRouter::Path> paths(2);
        paths[0].weight = 10; paths[1].weight = 2;
        ProfilePathPickerDialog picker(nullptr, paths);
        table = picker.findChild<QTableWidget *>();
        table->sortItems(1, Qt::AscendingOrder);
        QCOMPARE(table->item(0, 0)->data(Qt::UserRole).toInt(), 1);
        QSignalSpy hover(&picker, &ProfilePathPickerDialog::hoveredPathChanged);
        table->setCurrentCell(0, 0);
        QVERIFY(!hover.isEmpty());
        QCOMPARE(hover.last().first().toInt(), 1);
        auto *buttons = picker.findChild<QDialogButtonBox *>();
        QVERIFY(QMetaObject::invokeMethod(buttons, "accepted"));
        QCOMPARE(picker.selectedPathIndex(), 1);
        for (int column = 0; column < table->columnCount(); ++column)
            QCOMPARE(table->horizontalHeader()->sectionResizeMode(column), QHeaderView::Interactive);
        QVERIFY(!table->horizontalHeader()->stretchLastSection());
    }

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
