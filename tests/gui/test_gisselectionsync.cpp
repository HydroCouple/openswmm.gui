/**
 * @file      test_gisselectionsync.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     GIS feature layers ↔ SelectionManager ↔ Attribute Table, both
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/gisobjectref.h"
#include "layers/gisvectorlayer.h"
#include "layers/featurelayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "map/tools/maptoolidentify.h"
#include "map/tools/maptoolselect.h"
#include "map/tools/maptoolselectpolygon.h"
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

#include <limits>

namespace
{

/**
 *
 * @brief Ensure the relative GIS-selection evidence directory is requested.
 * @return Owned test_gisselectionsync_output path relative to current working
 * directory. Directory-creation success is unchecked; callers discover later IO failure.
 *
 */
QString outDir()
{
    QDir().mkpath(QStringLiteral("test_gisselectionsync_output"));
    return QStringLiteral("test_gisselectionsync_output");
}

/**
 *
 * @brief Resolve the persistent square/triangle selection GeoPackage path.
 * @return Owned sync_polys.gpkg path under outDir(); requests directory creation
 * but does not open or validate the dataset.
 *
 */
QString gpkgPath()
{
    return outDir() + QStringLiteral("/sync_polys.gpkg");
}

/*! Three disjoint unit squares at x = 0, 10, 20 plus one right TRIANGLE at
 *  x = 30 whose bbox corner near (31, 1) is empty — the precise-vs-bbox
 *  rubber-band gate needs a feature whose geometry != its bbox. */
/**
 *
 * @brief Recreate the persistent selection GeoPackage with three squares and one right triangle.
 * @return True on checked driver/dataset/layer/field/feature creation, false on
 * failure with partial artifacts possible. Registers GDAL globally and replaces
 * sync_polys.gpkg; deletion and individual geometry/field setter statuses are
 * unchecked. Coordinates have no declared CRS. The local add callback borrows its
 * point list, closes a copied ring, writes sequential fixture attributes and
 * destroys its OGR feature; the final triangle has null depth and an apostrophe name.
 *
 */
bool buildFixture()
{
    GDALAllRegister();
    GDALDriver *gpkg = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!gpkg)
        return false;
    const QByteArray gp = gpkgPath().toUtf8();
    if (QFile::exists(gpkgPath()))
        gpkg->Delete(gp.constData());

    GDALDataset *ds = gpkg->Create(gp.constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        return false;
    OGRLayer *layer = ds->CreateLayer("polys", nullptr, wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }

    OGRFieldDefn depth("depth value", OFTReal), label("name", OFTString);
    if (layer->CreateField(&depth) != OGRERR_NONE || layer->CreateField(&label) != OGRERR_NONE) {
        GDALClose(ds);
        return false;
    }
    int featureNumber = 0;
    const auto add = [&](std::initializer_list<QPointF> pts) {
        OGRLinearRing ring;
        for (const QPointF &p : pts)
            ring.addPoint(p.x(), p.y());
        ring.closeRings();
        OGRPolygon poly;
        poly.addRing(&ring);
        OGRFeature *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
        f->SetGeometry(&poly);
        if (featureNumber < 3)
            f->SetField("depth value", featureNumber == 0 ? 2.0 : featureNumber == 1 ? 10.0 : 30.0);
        f->SetField("name", featureNumber < 3 ? "Junction" : "O'Brien");
        ++featureNumber;
        const bool ok = (layer->CreateFeature(f) == OGRERR_NONE);
        OGRFeature::DestroyFeature(f);
        return ok;
    };
    bool ok = true;
    for (double x0 : {0.0, 10.0, 20.0})
        ok = ok && add({{x0, 0.0}, {x0 + 1.0, 0.0}, {x0 + 1.0, 1.0}, {x0, 1.0}});
    ok = ok && add({{30.0, 0.0}, {31.0, 0.0}, {30.0, 1.0}}); // triangle
    GDALClose(ds);
    return ok;
}

/**
 *
 * @brief Resolve the persistent heterogeneous polygon-query GeoPackage path.
 * @return Owned polygon_query.gpkg path under outDir(), without opening it.
 *
 */
QString polygonQueryPath()
{
    return outDir() + QStringLiteral("/polygon_query.gpkg");
}

/**
 *
 * @brief Recreate EPSG:4326 mixed geometries with six explicit 64-bit feature identities starting
 * at five billion.
 * @return True on checked allocation/WKT parsing/feature writes, false otherwise.
 * Requires GDAL registration, supplied by buildFixture in initTestCase. Replaces
 * the GeoPackage and closes its dataset; deletion, CRS import and geometry setters
 * are unchecked. Includes a polygon hole, multipart geometry and empty-bbox-corner point.
 *
 */
bool buildPolygonQueryFixture()
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver)
        return false;
    const QByteArray path = polygonQueryPath().toUtf8();
    if (QFile::exists(polygonQueryPath()))
        driver->Delete(path.constData());
    auto *dataset = driver->Create(path.constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dataset)
        return false;
    OGRSpatialReference srs;
    srs.importFromEPSG(4326);
    srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    auto *layer = dataset->CreateLayer("geometries", &srs, wkbUnknown, nullptr);
    if (!layer) {
        GDALClose(dataset);
        return false;
    }
    const char *geometries[] = {
        "POINT (0.5 0.5)",
        "LINESTRING (-1 0.75, 2 0.75)",
        "POLYGON ((0 0, 1 0, 0 1, 0 0))",
        "POLYGON ((3 0, 6 0, 6 3, 3 3, 3 0), (4 1, 4 2, 5 2, 5 1, 4 1))",
        "MULTIPOLYGON (((8 0, 9 0, 9 1, 8 1, 8 0)), ((12 0, 13 0, 13 1, 12 1, 12 0)))",
        "POINT (0.85 0.85)"};
    bool ok = true;
    long long fid = 5'000'000'000LL;
    for (const char *wkt : geometries) {
        OGRGeometry *geometry = nullptr;
        if (OGRGeometryFactory::createFromWkt(wkt, nullptr, &geometry) != OGRERR_NONE) {
            ok = false;
            break;
        }
        auto *feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feature->SetFID(fid++);
        feature->SetGeometryDirectly(geometry);
        ok = ok && layer->CreateFeature(feature) == OGRERR_NONE;
        OGRFeature::DestroyFeature(feature);
    }
    GDALClose(dataset);
    return ok;
}

/*! Everything one test needs, wired the way SWMMVisProjectWindow wires it. */
struct Rig {
    MapCanvas canvas;
    GISVectorLayer *layer; // owned by the canvas
    SelectionManager sel;
    GisSelectionBridge bridge{&sel, &canvas};

    /**
     *
     * @brief Build canvas-owned GIS layer and bidirectional selection wiring.
     * @param editable Use FeatureLayer/openTable when true, GISVectorLayer otherwise.
     * Requires the persistent polys fixture; editable open status is deliberately
     * unchecked. The canvas owns layer and receives it without undo. Selection and
     * bridge are fixture members; the bridge is destroyed before their dependencies.
     *
     */
    explicit Rig(bool editable = false)
    {
        if (editable) {
            auto *feature = new FeatureLayer;
            (void)feature->openTable(gpkgPath(), QStringLiteral("polys"));
            layer = feature;
        } else {
            layer = new GISVectorLayer(gpkgPath(), QStringLiteral("polys"));
        }
        canvas.addLayer(layer, /*pushUndo=*/false);
    }

    /*! fid of the square whose min-x is \p x0, straight from OGR. */
    /**
     *
     * @brief Find the last fixture geometry whose envelope minimum X matches within half a
     * coordinate unit.
     * @param x0 Requested local-coordinate minimum X, normally zero/ten/twenty.
     * @return Feature ID or -1 if no OGR layer/match. Borrows the layer, clears its
     * spatial filter and resets its cursor before/after scanning; does not restore a
     * prior filter and does not change dataset contents. Destroys each scanned feature.
     *
     */
    long long fidAt(double x0) const
    {
        OGRLayer *ol = layer->ogrLayer();
        if (!ol)
            return -1;
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

    /**
     *
     * @brief Require both persistent GeoPackage fixtures to be created successfully before
     * synchronization/query tests.
     *
     */
    void initTestCase()
    {
        QVERIFY(buildFixture());
        QVERIFY(buildPolygonQueryFixture());
    }

    /**
     *
     * @brief Require valid FID queries to synchronize layer/bus/table, invalid fields to preserve
     * prior results and each query selection mode to use the full population.
     * Checks Add/Subtract/Intersect/Invert/Replace, selected-only behavior and sort
     * stability. The local mode selector borrows the panel and checks exact-label radios.
     *
     */
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
                if (button->text() == label)
                    button->setChecked(true);
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
            if (action->text() == "Show selected only")
                action->setChecked(true);
        query->setText(QString("FID NOT IN (%1)").arg(fid));
        QVERIFY(QMetaObject::invokeMethod(&panel, "onQueryApplyClicked"));
        QCOMPARE(rig.sel.size(), 3);
        QVERIFY(!rig.sel.selection().contains(excluded));
        view->sortByColumn(0, Qt::DescendingOrder);
        QCOMPARE(rig.sel.selection(), validSelection);
    }

    /**
     *
     * @brief Require spaced-field NOT BETWEEN, null tests, escaped apostrophe strings and NOT
     * LIKE to select expected GIS rows.
     *
     */
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

    /**
     *
     * @brief Require query selection across loaded model categories and all three mesh table
     * kinds, while tabular filtering preserves the selection bus.
     * Loads both committed input fixtures, creates a canvas-owned synthetic triangle
     * and rewrites query-table.csv in the working directory; CSV byte count is unchecked.
     *
     */
    void queriesCoverModelDataMeshAndTabularSources()
    {
        // All categories actually selectable in either representative model.
        for (const QString &fixture :
             {QString("typed_selection_fixture.inp"), QString("data_object_table_fixture.inp")}) {
            SWMMModelLayer model(
                QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", ".")).filePath(fixture),
                nullptr);
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
                if (!total)
                    continue;
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
        for (const QPointF &point : {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1)}) {
            mesh::MeshVertex vertex;
            vertex.xy = point;
            result.vertices.append(vertex);
        }
        mesh::MeshTriangle triangle;
        triangle.v0 = 0;
        triangle.v1 = 1;
        triangle.v2 = 2;
        result.triangles.append(triangle);
        result.ok = true;
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
        csv.write("Name,Value\nA,2\nB,10\n");
        csv.close();
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

    /**
     *
     * @brief Require explicit query completion to replace only the cursor token, preserve
     * suffix/cursor position and refresh fields after source changes.
     * Writes checked attribute-query.png/query-suggestions.png captures and an
     * unchecked-write completion-table.csv fixture in the current directory.
     *
     */
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
        csv.write("Other field\nvalue\n");
        csv.close();
        auto *tabular = new TabularDataLayer("completion source");
        QVERIFY(tabular->loadFromFile(csv.fileName()));
        rig.canvas.addLayer(tabular, false);
        panel.showLayerSource(tabular);
        query->setText("Ot");
        query->setCursorPosition(2);
        QTest::keyClick(query, Qt::Key_Space, Qt::ControlModifier);
        QCOMPARE(completer->completionCount(), 1);
        QCOMPARE(completer->completionModel()->index(0, 0).data().toString(),
                 QString("\"Other field\""));
    }

    /**
     *
     * @brief Require sorted/filtered sublayer and profile-path pickers to retain original
     * dataset/path indices in selection and hover events.
     * Uses metadata-only fixtures; does not open query.gpkg or route a model. Also
     * checks the profile table retains interactive column resizing.
     *
     */
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
        paths[0].weight = 10;
        paths[1].weight = 2;
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
            QCOMPARE(table->horizontalHeader()->sectionResizeMode(column),
                     QHeaderView::Interactive);
        QVERIFY(!table->horizontalHeader()->stretchLastSection());
    }

    /**
     *
     * @brief Require a feature identity containing punctuation and a five-billion FID to
     * round-trip exactly, while wrong-kind/malformed identities refuse.
     *
     */
    void gisObjectRef_roundTrips()
    {
        const long long bigFid = 5'000'000'000LL; // > 32 bits
        const QString weirdId = QStringLiteral("layer#7::odd");
        const SWMMObjectRef ref = GisObjectRef::feature(weirdId, bigFid);
        QCOMPARE(ref.objectType, SWMMObjectRef::Feature);

        QString outId;
        long long outFid = -1;
        QVERIFY(GisObjectRef::parseFeature(ref, &outId, &outFid));
        QCOMPARE(outId, weirdId);
        QCOMPARE(outFid, bigFid);

        // Non-feature refs and malformed names are refused untouched.
        QVERIFY(!GisObjectRef::parseFeature(
            SWMMObjectRef(SWMMObjectRef::Node, QStringLiteral("J1")), nullptr, nullptr));
        QVERIFY(!GisObjectRef::parseFeature(
            SWMMObjectRef(SWMMObjectRef::Feature, QStringLiteral("gis::x")), nullptr, nullptr));
    }

    /*! layer → bus → table rows (the direction the user reported dead). */
    /**
     *
     * @brief Require layer-selected FIDs to appear as exact typed bus identities and matching
     * selected table rows with a displayed FID column.
     *
     */
    void layerToBus_toTable()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);

        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view && view->model());
        QCOMPARE(view->model()->rowCount(), 4);
        QCOMPARE(view->model()->headerData(0, Qt::Horizontal).toString(), QStringLiteral("FID"));

        const long long f1 = rig.fidAt(0.0), f3 = rig.fidAt(20.0);
        QVERIFY(f1 >= 0 && f3 >= 0);
        rig.layer->setSelectedFeatureIds({f1, f3});

        // Bus carries exactly the two Feature refs…
        int featureRefs = 0;
        for (const SWMMObjectRef &r : rig.sel.selection()) {
            QString lid;
            long long fid = -1;
            QVERIFY(GisObjectRef::parseFeature(r, &lid, &fid));
            QCOMPARE(lid, rig.layer->layerId());
            QVERIFY(fid == f1 || fid == f3);
            ++featureRefs;
        }
        QCOMPARE(featureRefs, 2);

        // …and the table's selected rows display exactly those FIDs.
        QSet<QString> selectedFids;
        for (const QModelIndex &idx : view->selectionModel()->selectedRows(0))
            selectedFids.insert(idx.data().toString());
        QCOMPARE(selectedFids, (QSet<QString>{QString::number(f1), QString::number(f3)}));
    }

    /*! table rows → bus → layer, with exactly ONE bus emission for the
     *  gesture (busy guards on both bridge directions + the panel's
     *  applying-from-bus flag: nothing bounces). */
    /**
     *
     * @brief Require selecting one table row to update the layer through exactly one
     * selection-bus emission.
     *
     */
    void tableToBus_toLayer()
    {
        Rig rig;
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);

        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view && view->model());
        QSignalSpy spy(&rig.sel, &SelectionManager::selectionChanged);

        view->selectionModel()->select(view->model()->index(1, 0),
                                       QItemSelectionModel::ClearAndSelect |
                                           QItemSelectionModel::Rows);

        QCOMPARE(spy.count(), 1);
        const QString shownFid = view->model()->index(1, 0).data().toString();
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{shownFid.toLongLong()}));
    }

    /*! Precise geometry hits, not bbox hits, and modifier-free replace. */
    /**
     *
     * @brief Require rectangle queries to select intersecting squares and exclude the right
     * triangle empty bounding-box corner.
     *
     */
    void rectSelect_hitsExactFeatures()
    {
        Rig rig;
        const long long f1 = rig.fidAt(0.0), f2 = rig.fidAt(10.0);

        // A rect spanning squares 1 and 2.
        QCOMPARE(rig.layer->featureIdsInRect(MapExtent(-0.5, -0.5, 11.5, 1.5)),
                 (QSet<long long>{f1, f2}));

        // The triangle's EMPTY bbox corner: overlaps its bounding box but
        // not its geometry — a bbox test would (wrongly) select it.
        QVERIFY(rig.layer->featureIdsInRect(MapExtent(30.8, 0.8, 30.95, 0.95)).isEmpty());
    }

    /**
     *
     * @brief Register imported-vector and editable-feature layer variants for polygon gesture
     * synchronization.
     *
     */
    void polygonSelect_syncsFeatures_data()
    {
        QTest::addColumn<bool>("editable");
        QTest::newRow("imported vector") << false;
        QTest::newRow("editable feature") << true;
    }

    /**
     *
     * @brief Require polygon queries to respect precise points/lines/concavity/holes/multipart
     * geometry and 64-bit FIDs.
     * Checks invalid two-point lasso refusal, restored spatial filter and retained
     * attribute filter. The local box helper accepts four bounds in dataset degrees
     * and returns a by-value four-corner polygon without validation or explicit closure.
     *
     */
    void polygonQuery_matchesGeometryAndPreservesFilters()
    {
        GISVectorLayer layer(polygonQueryPath(), QStringLiteral("geometries"));
        QVERIFY(layer.ogrLayer());
        const long long base = 5'000'000'000LL;
        const auto box = [](double x0, double y0, double x1, double y1) {
            return QPolygonF{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        };
        // Points, crossing lines with both ends outside, and polygon overlap.
        QCOMPARE(layer.featureIdsInPolygon(box(0.4, 0.4, 0.6, 0.8)),
                 (QSet<long long>{base, base + 1, base + 2}));
        // A nonrectangular lasso excludes the point in its empty bbox corner.
        QCOMPARE(layer.featureIdsInPolygon(QPolygonF{{0, 0}, {1, 0}, {0, 1}}),
                 (QSet<long long>{base, base + 1, base + 2}));
        QCOMPARE(layer.featureIdsInPolygon(QPolygonF{
                     {-0.1, -0.1}, {1.1, -0.1}, {1.1, 0.3}, {0.3, 0.3}, {0.3, 1.1}, {-0.1, 1.1}}),
                 (QSet<long long>{base + 1, base + 2}));
        QCOMPARE(layer.featureIdsInPolygon(box(0.8, 0.8, 0.9, 0.9)), (QSet<long long>{base + 5}));
        QVERIFY(layer.featureIdsInPolygon(box(4.2, 1.2, 4.8, 1.8)).isEmpty());
        QCOMPARE(layer.featureIdsInPolygon(box(5.5, 0.5, 6.5, 1.5)), (QSet<long long>{base + 3}));
        QCOMPARE(layer.featureIdsInPolygon(box(12.2, 0.2, 12.8, 0.8)), (QSet<long long>{base + 4}));
        QVERIFY(layer.featureIdsInPolygon(QPolygonF{{0, 0}, {1, 1}}).isEmpty());

        layer.ogrLayer()->SetSpatialFilterRect(8, 0, 9, 1);
        const std::string savedWkt = layer.ogrLayer()->GetSpatialFilter()->exportToWkt();
        // Selection temporarily overrides the scan filter, then restores it.
        QCOMPARE(layer.featureIdsInPolygon(box(0.4, 0.4, 0.6, 0.8)).size(), 3);
        QCOMPARE(layer.ogrLayer()->GetSpatialFilter()->exportToWkt(), savedWkt);
        layer.ogrLayer()->SetSpatialFilter(nullptr);
        QVERIFY(layer.ogrLayer()->SetAttributeFilter("fid = 5000000001") == OGRERR_NONE);
        QCOMPARE(layer.featureIdsInPolygon(box(0.4, 0.4, 0.6, 0.8)), (QSet<long long>{base + 1}));
        layer.ogrLayer()->SetAttributeFilter(nullptr);
    }

    /**
     *
     * @brief Require an EPSG:3857 metre-coordinate lasso to select the same two geographic
     * features with explicit canvas CRS and the no-CRS overload.
     *
     */
    void polygonQuery_reprojectsCanvasCoordinates()
    {
        GISVectorLayer layer(polygonQueryPath(), QStringLiteral("geometries"));
        std::unique_ptr<SpatialReferenceSystem> canvasSRS(
            SpatialReferenceSystem::fromWktOrProj(QStringLiteral("EPSG:3857")));
        QVERIFY(canvasSRS);
        // Metres on a Web Mercator canvas around the (0.5, 0.5) degree point.
        const QPolygonF polygon{{54000, 54000}, {57000, 54000}, {57000, 57000}, {54000, 57000}};
        const QSet<long long> expected{5'000'000'000LL, 5'000'000'002LL};
        QCOMPARE(layer.featureIdsInPolygon(polygon, canvasSRS.get()), expected);
        QCOMPARE(layer.featureIdsInPolygon(polygon), expected);
    }

    /** @brief Exercise exact identify distance and source-filter restoration in geographic and projected canvases. */
    void identifyQuery_canvasCoordinates_data()
    {
        QTest::addColumn<int>("canvasEpsg");
        QTest::newRow("same CRS") << 4326;
        QTest::newRow("reprojected CRS") << 3857;
    }

    /**
     * @brief Require explicit-CRS identify to rank nearest geometry, reject holes/empty bbox corners and preserve filters.
     * @details The legacy overload must still accept source coordinates after a projected query.
     * Invalid coordinates/tolerance and an untransformable local target must yield no hits.
     */
    void identifyQuery_canvasCoordinates()
    {
        QFETCH(int, canvasEpsg);
        GISVectorLayer layer(polygonQueryPath(), QStringLiteral("geometries"));
        QVERIFY(layer.ogrLayer());
        SpatialReferenceSystem source(QStringLiteral("EPSG"), 4326);
        SpatialReferenceSystem target(QStringLiteral("EPSG"), canvasEpsg);
        auto *transform = source.createTransformationTo(target);
        QVERIFY(transform);
        // Isolated point, first point, polygon hole, empty triangle bbox corner,
        // line interior, and one degree of canvas X for the radius scale.
        double xs[]{0.85, 0.5, 4.5, 0.7, 0.5, 0, 1};
        double ys[]{0.85, 0.5, 1.5, 0.7, 0.75, 0, 0};
        const bool transformed = transform->Transform(7, xs, ys);
        OGRCoordinateTransformation::DestroyCT(transform);
        QVERIFY(transformed);
        const double unit = xs[6] - xs[5];
        const long long base = 5'000'000'000LL;
        const auto ids = [](const QList<QVariantMap> &hits) {
            QSet<long long> result;
            for (const auto &hit : hits)
                result.insert(hit.value(QStringLiteral("fid")).toLongLong());
            return result;
        };

        const auto near = layer.identifyAt(xs[0], ys[0], &target, 0.6 * unit);
        QVERIFY(near.size() > 1);
        QCOMPARE(near.first().value(QStringLiteral("fid")).toLongLong(), base + 5);
        QCOMPARE(ids(layer.identifyAt(xs[1], ys[1], &target, 0.01 * unit)),
                 (QSet<long long>{base, base + 2}));
        QVERIFY(layer.identifyAt(xs[2], ys[2], &target, 0.01 * unit).isEmpty());
        QVERIFY(layer.identifyAt(xs[3], ys[3], &target, 0.01 * unit).isEmpty());
        QCOMPARE(ids(layer.identifyAt(xs[4], ys[4], &target, 0.01 * unit)),
                 (QSet<long long>{base + 1}));
        // The cached canvas projection must never reinterpret legacy inputs.
        QCOMPARE(ids(layer.identifyAt(0.5, 0.5, 0.01)), (QSet<long long>{base, base + 2}));

        layer.ogrLayer()->SetSpatialFilterRect(8, 0, 9, 1);
        const std::string saved = layer.ogrLayer()->GetSpatialFilter()->exportToWkt();
        QCOMPARE(ids(layer.identifyAt(xs[0], ys[0], &target, 0.01 * unit)),
                 (QSet<long long>{base + 5}));
        QCOMPARE(layer.ogrLayer()->GetSpatialFilter()->exportToWkt(), saved);
        QVERIFY(layer.ogrLayer()->SetAttributeFilter("fid = 5000000000") == OGRERR_NONE);
        QVERIFY(layer.identifyAt(xs[0], ys[0], &target, 0.01 * unit).isEmpty());
        QCOMPARE(layer.ogrLayer()->GetSpatialFilter()->exportToWkt(), saved);
        QCOMPARE(ids(layer.identifyAt(xs[1], ys[1], &target, 0.01 * unit)),
                 (QSet<long long>{base}));
        layer.ogrLayer()->SetAttributeFilter(nullptr);

        QVERIFY(layer.identifyAt(xs[0], ys[0], &target, -1).isEmpty());
        QVERIFY(
            layer.identifyAt(std::numeric_limits<double>::infinity(), ys[0], &target, 1).isEmpty());
        std::unique_ptr<SpatialReferenceSystem> local(SpatialReferenceSystem::untitled());
        QVERIFY(layer.identifyAt(0.5, 0.5, local.get(), 1).isEmpty());
        QCOMPARE(layer.ogrLayer()->GetSpatialFilter()->exportToWkt(), saved);
    }

    /** @brief Run real click/drag gestures for imported temporary and editable layers in both canvas CRSes. */
    void clickSelect_canvasCoordinates_data()
    {
        QTest::addColumn<int>("canvasEpsg");
        QTest::addColumn<bool>("editable");
        QTest::newRow("temporary same CRS") << 4326 << false;
        QTest::newRow("temporary reprojected") << 3857 << false;
        QTest::newRow("editable same CRS") << 4326 << true;
        QTest::newRow("editable reprojected") << 3857 << true;
    }

    /**
     * @brief Require canvas click selection and modifiers to synchronize layer/bus/table in the rendered coordinate frame.
     * @details Hidden and attribute-filtered points cannot be picked. A projected rectangle and
     * the Identify tool must keep targeting the same feature; no datasource edit is requested.
     */
    void clickSelect_canvasCoordinates()
    {
        QFETCH(int, canvasEpsg);
        QFETCH(bool, editable);
        std::unique_ptr<GISVectorLayer> layer;
        if (editable) {
            auto feature = std::make_unique<FeatureLayer>();
            QVERIFY(feature->openTable(polygonQueryPath(), QStringLiteral("geometries")));
            layer = std::move(feature);
        } else {
            layer =
                std::make_unique<GISVectorLayer>(polygonQueryPath(), QStringLiteral("geometries"));
            layer->setTemporary(true);
        }
        QVERIFY(layer->ogrLayer());
        // Isolate the two point features from the mixed-geometry query fixture.
        const char *pointFilter = "fid IN (5000000000, 5000000005)";
        QVERIFY(layer->ogrLayer()->SetAttributeFilter(pointFilter) == OGRERR_NONE);
        MapCanvas canvas;
        canvas.resize(960, 480);
        QVERIFY(canvas.setCanvasSRSByCode(QStringLiteral("EPSG"), canvasEpsg));
        canvas.addLayer(layer.get(), false);
        SelectionManager selection;
        GisSelectionBridge bridge(&selection, &canvas);
        AttributeTablePanel panel;
        panel.setProject(nullptr, &selection, &canvas);
        panel.showLayerSource(layer.get());
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view && view->model());

        SpatialReferenceSystem source(QStringLiteral("EPSG"), 4326);
        auto *transform = source.createTransformationTo(*canvas.canvasSRS());
        QVERIFY(transform);
        double xs[]{0.5, 0.85, -0.2, 1.2, 0.8, 0.9};
        double ys[]{0.5, 0.85, -0.2, 1.2, 0.8, 0.9};
        const bool transformed = transform->Transform(6, xs, ys);
        OGRCoordinateTransformation::DestroyCT(transform);
        QVERIFY(transformed);
        canvas.show();
        canvas.setExtent(MapExtent(xs[2], ys[2], xs[3], ys[3]), false);
        OpenSWMMVisMapToolSelect tool(&canvas);
        tool.setPixelTolerance(6);
        canvas.setActiveTool(&tool);
        const auto pixel = [&](int index) {
            int x, y;
            canvas.toPixelCoords(xs[index], ys[index], x, y);
            return QPoint(x, y);
        };
        const auto click = [&](int index, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QTest::mouseClick(&canvas, Qt::LeftButton, modifiers, pixel(index));
        };
        const auto tableIds = [&]() {
            QSet<long long> result;
            for (const QModelIndex &row : view->selectionModel()->selectedRows(0))
                result.insert(row.data().toLongLong());
            return result;
        };
        const long long first = 5'000'000'000LL, second = first + 5;
        click(0);
        QCOMPARE(layer->selectedFeatureIds(), (QSet<long long>{first}));
        QCOMPARE(selection.selection(),
                 (QSet<SWMMObjectRef>{GisObjectRef::feature(layer->layerId(), first)}));
        QCOMPARE(tableIds(), (QSet<long long>{first}));
        click(1, Qt::ShiftModifier);
        QCOMPARE(layer->selectedFeatureIds(), (QSet<long long>{first, second}));
        QCOMPARE(tableIds(), (QSet<long long>{first, second}));
        click(0, Qt::ControlModifier);
        QCOMPARE(layer->selectedFeatureIds(), (QSet<long long>{second}));

        QVERIFY(layer->ogrLayer()->SetAttributeFilter("fid = 5000000000") == OGRERR_NONE);
        click(1);
        QVERIFY(layer->selectedFeatureIds().isEmpty());
        click(0);
        QCOMPARE(layer->selectedFeatureIds(), (QSet<long long>{first}));
        QVERIFY(layer->ogrLayer()->SetAttributeFilter(pointFilter) == OGRERR_NONE);
        layer->setVisible(false);
        click(1, Qt::ShiftModifier);
        QCOMPARE(layer->selectedFeatureIds(), (QSet<long long>{first}));
        layer->setVisible(true);

        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, pixel(4));
        QTest::mouseMove(&canvas, pixel(5));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, pixel(5));
        QCOMPARE(layer->selectedFeatureIds(), (QSet<long long>{second}));
        QCOMPARE(tableIds(), (QSet<long long>{second}));

        OpenSWMMVisMapToolIdentify identify(&canvas);
        identify.setMapTolerance((xs[3] - xs[2]) / 100);
        QList<IdentifyResult> identified;
        connect(&identify, &OpenSWMMVisMapToolIdentify::identifyResult, &canvas,
                [&](const QList<IdentifyResult> &results) { identified = results; });
        canvas.setActiveTool(&identify);
        click(1);
        QCOMPARE(identified.size(), 1);
        QCOMPARE(identified.first().features.first().value(QStringLiteral("fid")).toLongLong(),
                 second);
        canvas.setActiveTool(nullptr);
    }

    /** @brief Cover topmost GIS picks in matching and reprojected canvas coordinate systems. */
    void clickSelect_respectsVisibleLayerOrder_data()
    {
        QTest::addColumn<int>("canvasEpsg");
        QTest::newRow("geographic") << 4326;
        QTest::newRow("projected") << 3857;
    }

    /** @brief A boundary below a gage cannot steal its click; reordering reverses the chosen layer. */
    void clickSelect_respectsVisibleLayerOrder()
    {
        QFETCH(int, canvasEpsg);
        const auto prefix = outDir() + QStringLiteral("/topmost-%1-").arg(canvasEpsg);
        const auto write = [](const QString &name, const QByteArray &bytes) {
            QFile file(name);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write(prefix + "boundary.geojson", R"json({"type":"FeatureCollection","features":[{"type":"Feature","id":10,"properties":{"name":"Boundary"},"geometry":{"type":"Polygon","coordinates":[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}}]})json"));
        QVERIFY(write(prefix + "gage.geojson", R"json({"type":"FeatureCollection","features":[{"type":"Feature","id":20,"properties":{"name":"Gage"},"geometry":{"type":"Point","coordinates":[0.5,0.5]}}]})json"));
        GISVectorLayer boundary(prefix + "boundary.geojson");
        GISVectorLayer gage(prefix + "gage.geojson");
        gage.setTemporary(true);
        QVERIFY(boundary.ogrLayer() && gage.ogrLayer());
        MapCanvas canvas;
        canvas.resize(640, 480);
        QVERIFY(canvas.setCanvasSRSByCode("EPSG", canvasEpsg));
        canvas.addLayer(&boundary, false);
        canvas.addLayer(&gage, false);
        SelectionManager selection;
        GisSelectionBridge bridge(&selection, &canvas);
        SpatialReferenceSystem source("EPSG", 4326);
        auto *projection = source.createTransformationTo(*canvas.canvasSRS());
        QVERIFY(projection);
        double xs[]{0.5, -0.1, 1.1}, ys[]{0.5, -0.1, 1.1};
        const bool transformed = projection->Transform(3, xs, ys);
        OGRCoordinateTransformation::DestroyCT(projection);
        QVERIFY(transformed);
        canvas.show();
        canvas.setExtent(MapExtent(xs[1], ys[1], xs[2], ys[2]), false);
        OpenSWMMVisMapToolSelect tool(&canvas);
        tool.setPixelTolerance(6);
        canvas.setActiveTool(&tool);
        int px, py;
        canvas.toPixelCoords(xs[0], ys[0], px, py);
        QVERIFY(canvas.layers().indexOf(&boundary) < canvas.layers().indexOf(&gage));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(px, py));
        QCOMPARE(gage.selectedFeatureIds(), (QSet<long long>{20}));
        QVERIFY(boundary.selectedFeatureIds().isEmpty());
        QCOMPARE(selection.selection(),
                 (QSet<SWMMObjectRef>{GisObjectRef::feature(gage.layerId(), 20)}));
        gage.clearSelection();
        canvas.moveLayer(canvas.layers().indexOf(&gage), canvas.layers().indexOf(&boundary), false);
        QVERIFY(canvas.layers().indexOf(&gage) < canvas.layers().indexOf(&boundary));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(px, py));
        QCOMPARE(boundary.selectedFeatureIds(), (QSet<long long>{10}));
        QVERIFY(gage.selectedFeatureIds().isEmpty());
        QCOMPARE(selection.selection(),
                 (QSet<SWMMObjectRef>{GisObjectRef::feature(boundary.layerId(), 10)}));
        boundary.clearSelection();
        boundary.setVisible(false);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(px, py));
        QCOMPARE(gage.selectedFeatureIds(), (QSet<long long>{20}));
        canvas.setActiveTool(nullptr);
        canvas.takeLayer(canvas.layers().indexOf(&gage), false);
        canvas.takeLayer(canvas.layers().indexOf(&boundary), false);
    }

    /**
     *
     * @brief Require polygon mouse/key completion and modifiers to synchronize layer, bus and
     * table for imported/editable layers.
     * Consumes editable; checks replace/add/toggle, hidden-layer/cancel/incomplete
     * retention and empty-hit clearing. The local draw callback borrows canvas, uses
     * x0 in fixture coordinates, applies modifiers only at completion and maps finish
     * zero/one/other to right click/Enter/double click. The row collector returns a
     * copied FID set from column-zero selection; no dataset writes are requested.
     *
     */
    void polygonSelect_syncsFeatures()
    {
        QFETCH(bool, editable);
        Rig rig(editable);
        QVERIFY(rig.layer->ogrLayer());
        rig.canvas.resize(960, 480);
        rig.canvas.show();
        rig.canvas.setExtent(MapExtent(-2, -3, 33, 3), false);
        OpenSWMMVisMapToolSelectPolygon tool(&rig.canvas);
        rig.canvas.setActiveTool(&tool);
        AttributeTablePanel panel;
        panel.setProject(nullptr, &rig.sel, &rig.canvas);
        panel.showLayerSource(rig.layer);
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view && view->model());
        QSignalSpy busChanged(&rig.sel, &SelectionManager::selectionChanged);
        QSignalSpy toolChanged(&tool, &OpenSWMMVisMapToolSelectPolygon::selectionChanged);

        const long long f1 = rig.fidAt(0), f2 = rig.fidAt(10), f3 = rig.fidAt(20);
        const auto draw = [&](double x0, Qt::KeyboardModifiers mods, int finish) {
            for (const QPointF &p : {QPointF(x0 - 0.3, -0.3), QPointF(x0 + 1.3, -0.3),
                                     QPointF(x0 + 1.3, 1.3), QPointF(x0 - 0.3, 1.3)}) {
                int x, y;
                rig.canvas.toPixelCoords(p.x(), p.y(), x, y);
                QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(x, y));
            }
            if (finish == 0)
                QTest::mouseClick(&rig.canvas, Qt::RightButton, mods, QPoint(1, 1));
            else if (finish == 1)
                QTest::keyClick(&rig.canvas, Qt::Key_Return, mods);
            else
                QTest::mouseDClick(&rig.canvas, Qt::LeftButton, mods, QPoint(1, 1));
        };
        const auto selectedRows = [&]() {
            QSet<long long> ids;
            for (const QModelIndex &idx : view->selectionModel()->selectedRows(0))
                ids.insert(idx.data().toLongLong());
            return ids;
        };

        rig.layer->setSelectedFeatureIds({f3});
        busChanged.clear();
        draw(0, Qt::NoModifier, 0);
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{f1}));
        QCOMPARE(rig.sel.selection(),
                 (QSet<SWMMObjectRef>{GisObjectRef::feature(rig.layer->layerId(), f1)}));
        QCOMPARE(selectedRows(), (QSet<long long>{f1}));
        QCOMPARE(busChanged.count(), 1);
        QCOMPARE(toolChanged.count(), 1);

        draw(10, Qt::ShiftModifier, 1);
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{f1, f2}));
        QCOMPARE(selectedRows(), (QSet<long long>{f1, f2}));
        draw(0, Qt::ControlModifier, 2);
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{f2}));
        QCOMPARE(selectedRows(), (QSet<long long>{f2}));

        // Hidden layers and cancelled/incomplete polygons keep their selection.
        rig.layer->setVisible(false);
        draw(20, Qt::NoModifier, 0);
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{f2}));
        rig.layer->setVisible(true);
        QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        QTest::keyClick(&rig.canvas, Qt::Key_Escape);
        QTest::keyClick(&rig.canvas, Qt::Key_Return);
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{f2}));
        draw(25, Qt::ShiftModifier, 0);
        QCOMPARE(rig.layer->selectedFeatureIds(), (QSet<long long>{f2}));
        draw(25, Qt::NoModifier, 0);
        QVERIFY(rig.layer->selectedFeatureIds().isEmpty());
        QVERIFY(rig.sel.selection().isEmpty());
        QVERIFY(selectedRows().isEmpty());
        rig.canvas.setActiveTool(nullptr);
    }

    /*! Feature refs on the bus leave non-GIS consumers untouched, and
     *  clearing the bus clears the layer. */
    /**
     *
     * @brief Require adding a foreign node identity to leave feature selection intact and
     * clearing the bus to clear the GIS layer.
     *
     */
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
