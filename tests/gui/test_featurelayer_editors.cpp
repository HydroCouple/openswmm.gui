/*!
 * \file   test_featurelayer_editors.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The feature-layer editors and consumers inside the real panels
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md R3, R5,
 *         R6, R7):
 *   - an edit through FeatureFieldDelegate in the Attribute Table and in the
 *     Features dock pushes ONE undo command and stores the token;
 *   - "Update fields to role…" on an old Region layer: retired fields
 *     dropped, quad_spacing renamed to h with its values, cells added — one
 *     undo step, and Undo brings the old schema and values back;
 *   - the mesh dialog's pickers offer only layers of a usable geometry, list
 *     the matching role first, and show the role;
 *   - the Attribute Table panel switches to an object type (showModelCategory)
 *     and to a mesh table (showMeshTable).
 * GeoPackages land in tests/output/feature_layer_roles_2026-09-30/editors/
 * (reviewable — CLAUDE.md §4.1).
 */

#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "feature/featureroles.h"
#include "layers/featurelayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "project/openswmmvisworkspace.h"
#include "selection/selectionmanager.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshgenerationdialog.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/panels/featurefielddelegate.h"
#include "ui/panels/featurelayerpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QSet>
#include <QSettings>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTableWidget>
#include <QTest>

#include <memory>

#ifndef FEATURE_EDITORS_OUT_DIR
#define FEATURE_EDITORS_OUT_DIR "feature_editors_output"
#endif

using namespace openswmmvis::feature;
using openswmmvis::ui::FeatureFieldDelegate;
using openswmmvis::ui::FeatureLayerPanel;

namespace {

QString freshGpkg(const char *stem)
{
    QDir().mkpath(QStringLiteral(FEATURE_EDITORS_OUT_DIR));
    const QString p = QDir(QStringLiteral(FEATURE_EDITORS_OUT_DIR))
                          .absoluteFilePath(QLatin1String(stem) + QStringLiteral(".gpkg"));
    for (const char *sfx : {"", "-wal", "-shm"}) QFile::remove(p + QLatin1String(sfx));
    return p;
}

FeatureGeometry line(double y)
{
    Ring r;
    r.pts << QPointF(0.0, y) << QPointF(100.0, y);
    Part p;
    p.exterior = r;
    FeatureGeometry g(GeometryType::LineString);
    g.addPart(p);
    return g;
}

FeatureGeometry square(double x, double w)
{
    Ring r;
    r.pts << QPointF(x, 0) << QPointF(x + w, 0) << QPointF(x + w, w) << QPointF(x, w);
    Part p;
    p.exterior = r;
    FeatureGeometry g(GeometryType::Polygon);
    g.addPart(p);
    return g;
}

FeatureGeometry point(double x)
{
    Ring r;
    r.pts << QPointF(x, x);
    Part p;
    p.exterior = r;
    FeatureGeometry g(GeometryType::Point);
    g.addPart(p);
    return g;
}

FeatureLayer *makeLayer(const char *stem, GeometryType type, const Schema &schema,
                        FeatureLayerRole role)
{
    QString err;
    FeatureLayer *fl = FeatureLayer::create(freshGpkg(stem), QLatin1String(stem), type,
                                            ZPolicy{}, schema, QString(), role, &err, nullptr);
    if (!fl) qWarning() << "FeatureLayer::create:" << err;
    return fl;
}

int columnOf(const QAbstractItemModel *m, const QString &header)
{
    for (int c = 0; c < m->columnCount(); ++c)
        if (m->headerData(c, Qt::Horizontal).toString() == header) return c;
    return -1;
}

/*! Pick \p token in the delegate's combo editor and commit it to \p model. */
void editThrough(FeatureFieldDelegate *del, QWidget *viewport, QAbstractItemModel *model,
                 const QModelIndex &idx, const QString &token)
{
    std::unique_ptr<QWidget> editor(del->createEditor(viewport, QStyleOptionViewItem(), idx));
    auto *combo = qobject_cast<QComboBox *>(editor.get());
    QVERIFY(combo);
    del->setEditorData(combo, idx);
    combo->setCurrentIndex(combo->findData(token));
    QVERIFY(combo->currentIndex() >= 0);
    del->setModelData(combo, model, idx);
}

}   // namespace

class TestFeatureLayerEditors : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
        QCoreApplication::setApplicationName(QStringLiteral("featurelayer-editors-test"));
        PreferencesManager::instance()->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
        m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(m_workspace);
        const QString inp = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
                                .filePath(QStringLiteral("typed_selection_fixture.inp"));
        m_window = new SWMMVisProjectWindow(m_workspace, inp, nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(m_window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        UnitSystem::setActiveProject(m_window->unitSystem());
        QTest::qWait(50);
    }

    void cleanupTestCase()
    {
        UnitSystem::setActiveProject(nullptr);
        delete m_window;
        delete m_workspace;
    }

    void attributeTableEditsThroughTheDelegate()
    {
        MapCanvas canvas;
        SelectionManager sel;
        FeatureLayer *fl = makeLayer("bc_attribute_table", GeometryType::LineString,
                                     featureLayerRoleTemplate(FeatureLayerRole::BoundaryCondition),
                                     FeatureLayerRole::BoundaryCondition);
        QVERIFY(fl);
        canvas.addLayer(fl, /*pushUndo=*/false);
        Feature f;
        f.geometry = line(0.0);
        f.attributes = fl->schema().defaultAttributes();
        const FeatureId id = fl->addFeature(f);
        QVERIFY(id != kInvalidFeatureId);
        fl->setEditing(true);

        AttributeTablePanel panel;
        panel.setProject(nullptr, &sel, &canvas);
        panel.showLayerSource(fl);
        auto *view = panel.findChild<QTableView *>();
        QVERIFY(view && view->model());
        const int col = columnOf(view->model(), QStringLiteral("bc_type"));
        QVERIFY(col > 0);
        auto *del = qobject_cast<FeatureFieldDelegate *>(view->itemDelegateForColumn(col));
        QVERIFY2(del, "the Attribute Table installs FeatureFieldDelegate on feature columns");
        const QModelIndex idx = view->model()->index(0, col);
        QCOMPARE(idx.data(Qt::EditRole).toString(), QStringLiteral("WALL"));   // the column default

        const int before = canvas.undoStack()->count();
        editThrough(del, view->viewport(), view->model(), idx, QStringLiteral("RATING_CURVE"));
        QCOMPARE(canvas.undoStack()->count(), before + 1);
        Feature back;
        QVERIFY(fl->feature(id, back));
        QCOMPARE(back.attributes.value(QStringLiteral("bc_type")).toString(),
                 QStringLiteral("RATING_CURVE"));
        canvas.undoStack()->undo();
        QVERIFY(fl->feature(id, back));
        QCOMPARE(back.attributes.value(QStringLiteral("bc_type")).toString(), QStringLiteral("WALL"));
    }

    void featuresDockEditsThroughTheDelegate()
    {
        MapCanvas canvas;
        FeatureLayer *fl = makeLayer("bc_dock", GeometryType::LineString,
                                     featureLayerRoleTemplate(FeatureLayerRole::BoundaryCondition),
                                     FeatureLayerRole::BoundaryCondition);
        QVERIFY(fl);
        canvas.addLayer(fl, false);
        Feature f;
        f.geometry = line(0.0);
        f.attributes = fl->schema().defaultAttributes();
        const FeatureId id = fl->addFeature(f);
        fl->setEditing(true);

        FeatureLayerPanel dock;
        dock.setCanvas(&canvas);
        dock.selectLayer(fl);
        QCOMPARE(dock.activeLayer(), fl);
        auto *grid = dock.findChild<QTableWidget *>(QStringLiteral("featureAttributeTable"));
        QVERIFY(grid);
        const int col = columnOf(grid->model(), QStringLiteral("bc_type"));
        QVERIFY(col > 0);
        auto *del = qobject_cast<FeatureFieldDelegate *>(grid->itemDelegateForColumn(col));
        QVERIFY2(del, "the Features dock installs FeatureFieldDelegate on its grid");

        const int before = canvas.undoStack()->count();
        editThrough(del, grid->viewport(), grid->model(), grid->model()->index(0, col),
                    QStringLiteral("NORMAL_FLOW"));
        QCOMPARE(canvas.undoStack()->count(), before + 1);
        Feature back;
        QVERIFY(fl->feature(id, back));
        QCOMPARE(back.attributes.value(QStringLiteral("bc_type")).toString(),
                 QStringLiteral("NORMAL_FLOW"));
    }

    void updateFieldsToRoleOnAnOldRegionLayer()
    {
        Schema old;
        for (const auto &p : {std::pair{"max_area", FieldType::Real},
                              std::pair{"min_cell", FieldType::Real},
                              std::pair{"quad_mode", FieldType::Text},
                              std::pair{"quad_spacing", FieldType::Real},
                              std::pair{"quad_aspect", FieldType::Real},
                              std::pair{"quad_angle", FieldType::Real},
                              std::pair{"tag", FieldType::Text}}) {
            FieldDef fd;
            fd.name = QLatin1String(p.first);
            fd.type = p.second;
            old.append(fd);
        }
        MapCanvas canvas;
        FeatureLayer *fl = makeLayer("old_regions", GeometryType::Polygon, old,
                                     FeatureLayerRole::Region);
        QVERIFY(fl);
        canvas.addLayer(fl, false);
        Feature f;
        f.geometry = square(0.0, 50.0);
        f.attributes.insert(QStringLiteral("quad_spacing"), 4.5);
        f.attributes.insert(QStringLiteral("quad_mode"), QStringLiteral("Mapped"));
        f.attributes.insert(QStringLiteral("tag"), QStringLiteral("park"));
        const FeatureId id = fl->addFeature(f);
        QVERIFY(id != kInvalidFeatureId);
        fl->setEditing(true);

        // The preview's data: 5 retired fields (one holding a value), one rename.
        QHash<QString, int> counts;
        counts.insert(QStringLiteral("quad_mode"), 1);
        const FieldUpdatePlan plan = planFieldUpdate(FeatureLayerRole::Region, fl->schema(), counts);
        QCOMPARE(plan.retired.size(), 5);
        QCOMPARE(plan.rename.size(), 1);

        FeatureLayerPanel dock;
        dock.setCanvas(&canvas);
        dock.selectLayer(fl);
        const int before = canvas.undoStack()->count();
        const QStringList drop = {QStringLiteral("max_area"), QStringLiteral("min_cell"),
                                  QStringLiteral("quad_mode"), QStringLiteral("quad_aspect"),
                                  QStringLiteral("quad_angle")};
        const QString err = dock.applyFieldUpdate(plan, drop);
        if (err.contains(QStringLiteral("cannot delete")))
            QSKIP("this GDAL cannot drop a GeoPackage column");
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(canvas.undoStack()->count(), before + 1);   // one undo step

        const QStringList names = fl->schema().names();
        QCOMPARE(QSet<QString>(names.cbegin(), names.cend()),
                 (QSet<QString>{QStringLiteral("h"), QStringLiteral("tag"), QStringLiteral("cells")}));
        Feature back;
        QVERIFY(fl->feature(id, back));
        QCOMPARE(back.attributes.value(QStringLiteral("h")).toDouble(), 4.5);
        QCOMPARE(back.attributes.value(QStringLiteral("tag")).toString(), QStringLiteral("park"));
        QCOMPARE(back.attributes.value(QStringLiteral("cells")).toString(), QStringLiteral("auto"));
        QVERIFY(planFieldUpdate(FeatureLayerRole::Region, fl->schema(), {}).isEmpty());

        canvas.undoStack()->undo();
        const QStringList restored = fl->schema().names();
        const QStringList oldNames = old.names();
        QCOMPARE(QSet<QString>(restored.cbegin(), restored.cend()),
                 QSet<QString>(oldNames.cbegin(), oldNames.cend()));
        QVERIFY(fl->feature(id, back));
        QCOMPARE(back.attributes.value(QStringLiteral("quad_spacing")).toDouble(), 4.5);
        QCOMPARE(back.attributes.value(QStringLiteral("quad_mode")).toString(), QStringLiteral("Mapped"));
    }

    void meshPickersFilterByGeometryAndShowTheRole()
    {
        MapCanvas *canvas = m_window->canvas();
        QVERIFY(canvas);
        FeatureLayer *regions = makeLayer("picker_regions", GeometryType::Polygon,
                                          featureLayerRoleTemplate(FeatureLayerRole::Region),
                                          FeatureLayerRole::Region);
        FeatureLayer *zones = makeLayer("picker_zones", GeometryType::Polygon,
                                        featureLayerRoleTemplate(FeatureLayerRole::ParameterZone),
                                        FeatureLayerRole::ParameterZone);
        FeatureLayer *lines = makeLayer("picker_breaklines", GeometryType::LineString,
                                        featureLayerRoleTemplate(FeatureLayerRole::Breakline),
                                        FeatureLayerRole::Breakline);
        FeatureLayer *points = makeLayer("picker_points", GeometryType::Point,
                                         featureLayerRoleTemplate(FeatureLayerRole::General),
                                         FeatureLayerRole::General);
        QVERIFY(regions && zones && lines && points);
        // Zones first, so "matching role first" is a real reorder.
        for (FeatureLayer *l : {zones, regions, lines, points}) canvas->addLayer(l, false);
        QVERIFY(regions->addFeature(Feature{kInvalidFeatureId, square(0, 10), {}}) != kInvalidFeatureId);
        QVERIFY(lines->addFeature(Feature{kInvalidFeatureId, line(5), {}}) != kInvalidFeatureId);
        QVERIFY(points->addFeature(Feature{kInvalidFeatureId, point(3), {}}) != kInvalidFeatureId);

        MeshGenerationDialog dlg(m_window, m_window);
        auto *regionCombo = dlg.findChild<QComboBox *>(QStringLiteral("meshQuadRegionLayerCombo"));
        auto *domainCombo = dlg.findChild<QComboBox *>(QStringLiteral("meshBoundaryLayerCombo"));
        auto *lineList  = dlg.findChild<QListWidget *>(QStringLiteral("meshLineLayersList"));
        auto *pointList = dlg.findChild<QListWidget *>(QStringLiteral("meshPointLayersList"));
        QVERIFY(regionCombo && domainCombo && lineList && pointList);

        const auto texts = [](QComboBox *c) {
            QStringList t;
            for (int i = 0; i < c->count(); ++i) t << c->itemText(i);
            return t;
        };
        const QStringList regionItems = texts(regionCombo);
        QVERIFY2(regionItems.size() >= 3, qPrintable(regionItems.join(" | ")));
        // "(none)", then the Region-role layer, labelled with its role.
        QVERIFY2(regionItems.at(1).contains(QStringLiteral("picker_regions"))
                     && regionItems.at(1).contains(featureLayerRoleLabel(FeatureLayerRole::Region)),
                 qPrintable(regionItems.join(" | ")));
        QVERIFY(regionItems.join(' ').contains(QStringLiteral("picker_zones")));
        for (const QString &t : regionItems + texts(domainCombo)) {
            QVERIFY2(!t.contains(QStringLiteral("picker_breaklines")), qPrintable(t));
            QVERIFY2(!t.contains(QStringLiteral("picker_points")), qPrintable(t));
        }

        const auto listTexts = [](QListWidget *list) {
            QStringList t;
            for (QCheckBox *b : list->findChildren<QCheckBox *>()) t << b->text();
            return t.join(QLatin1Char('|'));
        };
        QVERIFY(listTexts(lineList).contains(QStringLiteral("picker_breaklines")));
        QVERIFY(!listTexts(lineList).contains(QStringLiteral("picker_regions")));
        QVERIFY(!listTexts(lineList).contains(QStringLiteral("picker_points")));
        QVERIFY(listTexts(pointList).contains(QStringLiteral("picker_points")));
        QVERIFY(!listTexts(pointList).contains(QStringLiteral("picker_breaklines")));

        for (FeatureLayer *l : {zones, regions, lines, points})
            delete canvas->takeLayer(canvas->layers().indexOf(l), /*pushUndo=*/false);
    }

    void panelSwitchesToTheObjectTypeAndTheMeshTable()
    {
        SWMMModelLayer *model = m_window->modelLayer();
        QVERIFY(model);
        AttributeTablePanel panel;
        panel.setProject(model, m_window->selectionManager(), m_window->canvas());
        panel.refresh();
        auto *combo = panel.findChild<QComboBox *>();
        QVERIFY(combo);

        panel.showModelCategory(model, SWMMModelLayer::CatConduits, nullptr);
        QCOMPARE(combo->currentData().toInt(), int(SWMMModelLayer::CatConduits));
        panel.showModelCategory(model, SWMMModelLayer::CatSubcatchments, nullptr);
        QCOMPARE(combo->currentData().toInt(), int(SWMMModelLayer::CatSubcatchments));

        mesh::MeshResult m;
        for (const QPointF &p : {QPointF(0, 0), QPointF(10, 0), QPointF(0, 10)}) {
            mesh::MeshVertex v;
            v.xy = p;
            m.vertices << v;
        }
        mesh::MeshTriangle t;
        t.v0 = 0; t.v1 = 1; t.v2 = 2;
        m.triangles << t;
        m.ok = true;
        auto *meshLayer = new SWMM2DMeshLayer(m);
        m_window->canvas()->addLayer(meshLayer, false);
        panel.showMeshTable(meshLayer, 1);
        QVERIFY2(combo->currentData().toString().endsWith(QStringLiteral(":e")),
                 qPrintable(combo->currentData().toString()));
        panel.showMeshTable(meshLayer, 2);
        QVERIFY(combo->currentData().toString().endsWith(QStringLiteral(":c")));
        // Rebind the panel before the mesh goes, then take it off the canvas.
        panel.showModelCategory(model, SWMMModelLayer::CatConduits, nullptr);
        delete m_window->canvas()->takeLayer(m_window->canvas()->layers().indexOf(meshLayer), false);
    }

private:
    OpenSWMMVisWorkspace *m_workspace = nullptr;
    SWMMVisProjectWindow *m_window = nullptr;
};

QTEST_MAIN(TestFeatureLayerEditors)
#include "test_featurelayer_editors.moc"
