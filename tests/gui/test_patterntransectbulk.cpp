#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"
#include "transect/transectprovider.h"
#include "transect/transectregistry.h"
#include "ui/panels/dataobjectaggregateselection.h"
#include "ui/panels/propertiespanel.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/dialogs/transecteditordialog.h"
#include "ui/dialogs/transectpropertybag.h"
#include "ui/properties/swmmtransectpropertyadapter.h"
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_model.h>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTreeView>
#include <QTest>
#include <memory>

using DC = SWMMModelLayer;
using Model = DataObjectAttributeTableModel;
using openswmmvis::pattern::PatternProvider;
using openswmmvis::pattern::PatternRegistry;
using openswmmvis::transect::TransectProvider;
using openswmmvis::transect::TransectRegistry;
namespace {
QString folder()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
        .absoluteFilePath("../../../workplans/bulk-pattern-transect/review");
}
QStringList names(int type)
{
    const auto prefix = QStringList{"M", "D", "H", "W"}.value(type);
    return {prefix + "_A", prefix + "_B", prefix + "_C"};
}
QString factorKey(int type, int i) { return QString("factor/%1/%2").arg(type).arg(i); }
std::unique_ptr<DC> fixture(bool si = false)
{
    QDir().mkpath(folder());
    QFile base(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).filePath("data_object_table_fixture.inp"));
    if (!base.open(QIODevice::ReadOnly)) return {};
    auto text = base.readAll();
    if (si) text.replace("CFS", "CMS");
    const auto path = QDir(folder()).filePath(si ? "initial-si.inp" : "initial-us.inp");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(text); file.close();
    auto layer = std::make_unique<DC>(path);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) { qWarning() << errors; return {}; }
    const auto e = layer->engine();
    for (int t = 0; t < 4; ++t) {
        auto ids = names(t); ids << QString("%1_Control").arg(t);
        for (int n = 0; n < ids.size(); ++n) {
            const auto id = ids[n].toUtf8();
            if (swmm_pattern_add(e, id.constData(), t) != SWMM_OK) return {};
            QVector<double> factors;
            for (int i = 0; i < PatternProvider::factorCountFor(openswmmvis::pattern::PatternType(t)); ++i)
                factors << 1.0 + n * .1 + i * .01;
            if (swmm_pattern_set_factors(e, swmm_pattern_index(e, id.constData()), factors.constData(), factors.size()) != SWMM_OK) return {};
        }
    }
    for (int n = 0; n < 4; ++n) {
        const auto id = QStringList{"T_A", "T_B", "T_C", "T_Control"}[n].toUtf8();
        if (swmm_transect_add(e, id.constData()) != SWMM_OK) return {};
        const int i = swmm_transect_index(e, id.constData());
        swmm_transect_set_roughness(e, i, .02 + n * .01, .03 + n * .01, .04 + n * .01);
        swmm_transect_set_bank_stations(e, i, -2 + n * .1, 5 + n);
        swmm_transect_set_encroachment_stations(e, i, -1 + n * .1, 4 + n);
        swmm_transect_set_modifiers(e, i, 1 + n * .1, n * .5, 1.5 + n * .1);
        swmm_transect_set_comments(e, i, ("Description " + id).constData());
        swmm_transect_add_station(e, i, -5, 5);
        for (int p = 0; p <= n; ++p) swmm_transect_add_station(e, i, p, p * .1);
        swmm_transect_add_station(e, i, 10, 5);
    }
    return layer;
}
int column(const Model &model, const QString &key)
{
    const auto specs = model.columnSpecs();
    for (int c = 0; c < specs.size(); ++c) if (specs[c].key == key) return c;
    return -1;
}
QVariantMap state(DC *layer, DC::DataCategory cat, const QString &name)
{
    Model model; model.setSource(layer, cat); model.refreshObject(name);
    QVariantMap result;
    const int row = model.rowForName(name);
    for (int c = 1; c < model.columnCount(); ++c)
        if (model.cellApplies(row, c)) result[model.columnSpecs()[c].key] = model.index(row, c).data(Qt::EditRole);
    return result;
}
QVariantList stations(DC *layer, const QString &name)
{
    QVariantList values;
    const int idx = swmm_transect_index(layer->engine(), name.toUtf8().constData());
    for (int i = 0; i < swmm_transect_get_station_count(layer->engine(), idx); ++i) {
        double x, y;
        if (swmm_transect_get_station(layer->engine(), idx, i, &x, &y) != SWMM_OK) return {};
        values << QVariant(QVariantList{x, y});
    }
    return values;
}
QModelIndex findProperty(QAbstractItemModel *model, const QString &label, const QModelIndex &parent = {})
{
    for (int r = 0; r < model->rowCount(parent); ++r) {
        const auto cell = model->index(r, 0, parent);
        if (cell.data().toString() == label) return cell.siblingAtColumn(1);
        const auto child = findProperty(model, label, cell);
        if (child.isValid()) return child;
    }
    return {};
}
}

class TestPatternTransectBulk : public QObject
{
    Q_OBJECT
private slots:
    void fields_data()
    {
        QTest::addColumn<int>("category"); QTest::addColumn<int>("type");
        QTest::addColumn<QString>("key"); QTest::addColumn<QVariant>("value"); QTest::addColumn<bool>("si");
        for (bool si : {false, true}) {
            for (int t = 0; t < 4; ++t)
                for (int i = 0; i < PatternProvider::factorCountFor(openswmmvis::pattern::PatternType(t)); ++i)
                    QTest::newRow(qPrintable(QString("factor-%1-%2-%3").arg(t).arg(i).arg(si)))
                        << int(DC::DataPatterns) << t << factorKey(t, i) << QVariant(2.125) << si;
            const QVariantMap values{{"nLeftBank", .15}, {"nRightBank", .16}, {"nChannel", .17},
                {"xLeftBank", -3.5}, {"xRightBank", 8.5}, {"xLeftEncroachment", -2.5},
                {"xRightEncroachment", 8.0}, {"stationMultiplier", 1.25},
                {"elevationOffset", -.75}, {"meanderFactor", 1.75}, {"comments", "Bulk description"}};
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                QTest::newRow(qPrintable(QString("transect-%1-%2").arg(it.key()).arg(si)))
                    << int(DC::DataTransects) << -1 << it.key() << it.value() << si;
        }
    }
    void fields()
    {
        QFETCH(int, category); QFETCH(int, type); QFETCH(QString, key); QFETCH(QVariant, value); QFETCH(bool, si);
        auto layer = fixture(si); QVERIFY(layer);
        const auto cat = DC::DataCategory(category);
        const QStringList ids = type < 0 ? QStringList{"T_A", "T_B", "T_C"} : names(type);
        const auto controlName = type < 0 ? QString("T_Control") : QString("%1_Control").arg(type);
        QList<QVariantMap> before; QList<QVariantList> points;
        for (const auto &id : ids) { before << state(layer.get(), cat, id); if (type < 0) points << stations(layer.get(), id); }
        const auto control = state(layer.get(), cat, controlName);
        QUndoStack undo; QSignalSpy dirty(layer.get(), &DC::modelEdited); QString error;
        QVERIFY2(Model::applyValues(layer.get(), cat, ids, key, value, &undo, &error), qPrintable(error));
        QCOMPARE(undo.count(), 1); QCOMPARE(dirty.count(), 1);
        for (int n = 0; n < ids.size(); ++n) {
            auto expected = before[n]; expected[key] = value;
            QCOMPARE(state(layer.get(), cat, ids[n]), expected);
            if (type < 0) QCOMPARE(stations(layer.get(), ids[n]), points[n]);
        }
        QCOMPARE(state(layer.get(), cat, controlName), control);
        QVERIFY(Model::applyValues(layer.get(), cat, ids, key, value, &undo));
        QCOMPARE(undo.count(), 1); QCOMPARE(dirty.count(), 1);
        undo.undo();
        for (int n = 0; n < ids.size(); ++n) QCOMPARE(state(layer.get(), cat, ids[n]), before[n]);
        undo.redo();
        const auto path = QDir(folder()).filePath(QString("saved-%1-%2-%3.inp").arg(category).arg(QString(key).replace('/', '-')).arg(si));
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
        DC reopened(path); QList<QString> warnings, errors;
        QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join(' ')));
        for (int n = 0; n < ids.size(); ++n) {
            auto expected = before[n]; expected[key] = value;
            QCOMPARE(state(&reopened, cat, ids[n]), expected);
            if (type < 0) QCOMPARE(stations(&reopened, ids[n]), points[n]);
        }
        QCOMPARE(state(&reopened, cat, controlName), control);
    }
    void mixedTypesStrictTargetsAndHistory()
    {
        auto layer = fixture(); QVERIFY(layer); QUndoStack undo; QString error;
        const auto before = state(layer.get(), DC::DataPatterns, "M_A");
        QVERIFY(Model::applyValues(layer.get(), DC::DataPatterns, names(0), factorKey(0, 0), 2, &undo));
        undo.undo(); QVERIFY(undo.canRedo());
        QSignalSpy dirty(layer.get(), &DC::modelEdited);
        for (const QStringList ids : {QStringList{"M_A", "D_A"}, QStringList{"M_A", "Missing"}}) {
            QVERIFY(!Model::applyValues(layer.get(), DC::DataPatterns, ids, factorKey(0, 0), 3, &undo, &error));
            QCOMPARE(state(layer.get(), DC::DataPatterns, "M_A"), before);
            QVERIFY(undo.canRedo());
        }
        for (auto value : {QVariant(-1), QVariant("nan"), QVariant("junk")})
            QVERIFY(!Model::applyValues(layer.get(), DC::DataPatterns, names(0), factorKey(0, 0), value, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataPatterns, names(0), "type", 1, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataTransects, {"T_A", "T_B"}, "xLeftBank", 7, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataTransects, {"T_A", "T_B"}, "xRightEncroachment", -4, &undo));
        QVERIFY(!Model::applyValues(layer.get(), DC::DataTransects, {"T_A", "T_B"}, "nChannel", -1, &undo));
        QCOMPARE(dirty.count(), 0); QVERIFY(undo.canRedo());
        DataObjectAggregateSelection mixed(layer.get(), DC::DataPatterns, {"M_A", "D_A"}, &undo);
        QCOMPARE(mixed.field(column(*mixed.source(), factorKey(0, 0))).editable, 1);
        QVERIFY(!mixed.setValue(column(*mixed.source(), factorKey(0, 0)), 2));
    }
    void independentReplayAndExternalChanges()
    {
        auto layer = fixture(); QVERIFY(layer); QUndoStack undo;
        const auto e = layer->engine();
        QVERIFY(Model::applyValues(layer.get(), DC::DataPatterns, names(0), factorKey(0, 0), 2, &undo));
        auto *reg = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
        QVERIFY(reg->findByName("M_A")->setFactor(1, 4));
        QVERIFY(reg->rename(reg->findByName("M_B"), "Renamed"));
        undo.undo();
        QCOMPARE(state(layer.get(), DC::DataPatterns, "M_A").value(factorKey(0, 1)).toDouble(), 4.0);
        QCOMPARE(state(layer.get(), DC::DataPatterns, "Renamed").value(factorKey(0, 0)).toDouble(), 1.1);
        undo.redo(); QCOMPARE(state(layer.get(), DC::DataPatterns, "Renamed").value(factorKey(0, 0)).toDouble(), 2.0);
        undo.clear();
        QVERIFY(Model::applyValues(layer.get(), DC::DataTransects, {"T_A", "T_B"}, "nLeftBank", .2, &undo));
        const int ti = swmm_transect_index(e, "T_A");
        QCOMPARE(swmm_transect_set_roughness(e, ti, .2, .99, .04), SWMM_OK);
        undo.undo();
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_A").value("nRightBank").toDouble(), .99);
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_A").value("nLeftBank").toDouble(), .02);
        undo.redo();
        DataObjectAggregateSelection selection(layer.get(), DC::DataTransects, {"T_A", "T_B"}, &undo);
        const int left = column(*selection.source(), "nLeftBank"); QVERIFY(selection.stage(left, .3));
        QCOMPARE(swmm_transect_set_roughness(e, ti, .25, .99, .04), SWMM_OK);
        QString error; QVERIFY(!selection.apply(&error)); QVERIFY(error.contains("changed"));
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_B").value("nLeftBank").toDouble(), .2);
        undo.clear();
        QVERIFY(Model::applyValues(layer.get(), DC::DataTransects, {"T_A", "T_B"}, "xLeftBank", -3, &undo));
        QCOMPARE(swmm_transect_set_bank_stations(e, ti, -3, -2.5), SWMM_OK);
        undo.undo(); // Restoring -2 would cross the externally edited right bank: refuse the batch.
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_A").value("xLeftBank").toDouble(), -3.0);
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_B").value("xLeftBank").toDouble(), -3.0);
    }
    void nativeScopeLongCommentsAndProviderSync()
    {
        auto layer = fixture(); QVERIFY(layer);
        auto *tr = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
        auto *pr = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
        auto *control = tr->findByName("T_Control");
        control->setRoughness(.5, .6, .7); // An unselected, unsaved draft must not be flushed.
        const QByteArray longComment(9000, 'x');
        const int idx = swmm_transect_index(layer->engine(), "T_A");
        QCOMPARE(swmm_transect_set_comments(layer->engine(), idx, longComment.constData()), SWMM_OK);
        QUndoStack undo;
        openswmmvis::ui::TransectEditorDialog editor(tr, layer.get(), &undo);
        editor.openForTransect("T_A");
        openswmmvis::ui::PatternEditorDialog patternEditor(pr, &undo);
        patternEditor.openForPattern("M_A");
        QSignalSpy changes(layer.get(), &DC::transectChanged);
        QVERIFY(Model::applyValues(layer.get(), DC::DataTransects, {"T_A", "T_B"}, "nLeftBank", .2, &undo));
        QCOMPARE(changes.count(), 2);
        QCOMPARE(editor.currentProvider()->nLeftBank(), .2);
        QCOMPARE(editor.currentProvider()->comments().size(), longComment.size());
        auto *bag = editor.findChild<openswmmvis::ui::TransectPropertyBag *>(); QVERIFY(bag);
        QCOMPARE(bag->nLeftBank(), .2);
        double l, r, c;
        QCOMPARE(swmm_transect_get_roughness(layer->engine(), swmm_transect_index(layer->engine(), "T_Control"), &l, &r, &c), SWMM_OK);
        QCOMPARE(l, .05); QCOMPARE(control->nLeftBank(), .5);
        QVERIFY(Model::applyValues(layer.get(), DC::DataPatterns, names(0), factorKey(0, 0), 3, &undo));
        QCOMPARE(patternEditor.currentProvider()->factor(0), 3.0);
        undo.undo(); QCOMPARE(patternEditor.currentProvider()->factor(0), 1.0);
        undo.undo(); QCOMPARE(bag->nLeftBank(), .02);
        QCOMPARE(editor.currentProvider()->comments().size(), longComment.size());
    }
    void propertiesFollowRegistryEdits()
    {
        // Review C-06 (found not to reproduce): the Transect editor's Apply writes through the
        // registry (provider then engine), which never emits the layer's transectChanged. The
        // panel still repaints, because a registry edit marks the model edited and that
        // refreshes it. This guards that the view is told to repaint after such an edit.
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get()); panel.resize(1000, 1000); panel.show();
        panel.showDataObject(layer.get(), SWMMObjectRef::Transect, "T_A");
        auto *tree = panel.findChild<QTreeView *>(); QVERIFY(tree);
        QVERIFY(findProperty(tree->model(), "Roughness — Channel n").isValid());
        auto *tr = qobject_cast<TransectRegistry *>(layer->ensureTransectRegistry());
        auto *p = tr->findByName("T_A"); QVERIFY(p);
        QSignalSpy announced(tree->model(), &QAbstractItemModel::dataChanged);
        p->setRoughness(p->nLeftBank(), p->nRightBank(), .123); // as the editor's Apply does
        tr->saveToEngine(tr->engineHandle(), p->name());
        QTRY_VERIFY(announced.count() > 0);
        QCOMPARE(findProperty(tree->model(), "Roughness — Channel n").data(Qt::EditRole).toDouble(),
                 .123);
    }
    void widgetSubsetsNumberAndText()
    {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        PropertiesPanel panel; panel.setProject(layer.get()); panel.resize(1000, 1000); panel.show();
        panel.showSelection(&canvas, {{SWMMObjectRef::TimePattern, "M_A"}, {SWMMObjectRef::TimePattern, "M_B"}, {SWMMObjectRef::TimePattern, "D_A"}});
        auto *scope = panel.findChild<QComboBox *>("propertiesSelectionScope");
        auto *tree = panel.findChild<QTreeView *>(); QVERIFY(scope && tree);
        int all = -1, monthly = -1;
        for (int i = 1; i < scope->count(); ++i) {
            if (scope->itemText(i).startsWith("Time patterns (")) all = i;
            if (scope->itemText(i).contains("Monthly")) monthly = i;
        }
        QVERIFY(all > 0 && monthly > 0); scope->setCurrentIndex(all);
        QVERIFY(!(findProperty(tree->model(), "Monthly — Jan").flags() & Qt::ItemIsEditable));
        scope->setCurrentIndex(monthly);
        QVERIFY(!findProperty(tree->model(), "Daily — Sun").isValid());
        auto cell = findProperty(tree->model(), "Monthly — Jan"); QVERIFY(cell.isValid());
        for (auto key : {Qt::Key_Tab, Qt::Key_Escape}) {
            tree->edit(cell); auto *spin = tree->findChild<QDoubleSpinBox *>(); QVERIFY(spin);
            spin->setFocus(); if (key == Qt::Key_Escape) { spin->selectAll(); QTest::keyClicks(spin, "4"); }
            QTest::keyClick(spin, key); QCOMPARE(canvas.undoStack()->count(), 0);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        tree->edit(cell); auto *spin = tree->findChild<QDoubleSpinBox *>(); QVERIFY(spin);
        spin->setFocus(); spin->selectAll(); QTest::keyClicks(spin, "2.5"); QTest::keyClick(spin, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 1);
        QCOMPARE(state(layer.get(), DC::DataPatterns, "M_A").value(factorKey(0, 0)).toDouble(), 2.5);
        QCOMPARE(state(layer.get(), DC::DataPatterns, "M_B").value(factorKey(0, 0)).toDouble(), 2.5);
        QCOMPARE(state(layer.get(), DC::DataPatterns, "D_A").value(factorKey(1, 0)).toDouble(), 1.0);
        panel.showSelection(&canvas, {{SWMMObjectRef::Transect, "T_A"}, {SWMMObjectRef::Transect, "T_B"}});
        cell = findProperty(tree->model(), "Description"); QVERIFY(cell.isValid());
        tree->edit(cell); auto *line = tree->findChild<QLineEdit *>(); QVERIFY(line);
        line->setFocus(); line->selectAll(); QTest::keyClicks(line, "Shared description"); QTest::keyClick(line, Qt::Key_Return);
        QTRY_COMPARE(canvas.undoStack()->count(), 2);
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_A").value("comments").toString(), QString("Shared description"));
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_B").value("comments").toString(), QString("Shared description"));
        panel.showDataObject(layer.get(), SWMMObjectRef::Transect, "T_A");
        auto *adapter = panel.findChild<SWMMTransectPropertyAdapter *>(); QVERIFY(adapter);
        adapter->setNChannel(.3); QCOMPARE(canvas.undoStack()->count(), 3);
        QCOMPARE(state(layer.get(), DC::DataTransects, "T_A").value("nChannel").toDouble(), .3);
        canvas.undoStack()->undo(); QCOMPARE(state(layer.get(), DC::DataTransects, "T_A").value("nChannel").toDouble(), .04);
    }
    void lifecycleAndStrictProxyRows()
    {
        auto layer = fixture(); QVERIFY(layer); QUndoStack undo;
        Model model; model.setSource(layer.get(), DC::DataPatterns); model.setUndoStack(&undo);
        QSortFilterProxyModel proxy; proxy.setSourceModel(&model); proxy.setFilterRegularExpression("^M_[ABC]$");
        proxy.setFilterKeyColumn(0); proxy.sort(0, Qt::DescendingOrder);
        QList<int> rows; for (int r = 0; r < proxy.rowCount(); ++r) rows << proxy.mapToSource(proxy.index(r, 0)).row();
        QCOMPARE(rows.size(), 3);
        QVERIFY(model.setDataForRows(column(model, factorKey(0, 11)), rows, 2.75));
        QCOMPARE(undo.count(), 1);
        QVERIFY(!model.setDataForRows(column(model, factorKey(0, 11)), rows + QList<int>{-1}, 3));
        auto *pr = qobject_cast<PatternRegistry *>(layer->ensurePatternRegistry());
        DataObjectAggregateSelection captured(layer.get(), DC::DataPatterns, names(0), &undo);
        const int col = column(*captured.source(), factorKey(0, 0));
        pr->remove(pr->findByName("M_B"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents();
        QVERIFY(pr->create("M_B", openswmmvis::pattern::PatternType::Monthly));
        QCoreApplication::processEvents(); QVERIFY(!captured.setValue(col, 4));
        undo.undo(); QCOMPARE(state(layer.get(), DC::DataPatterns, "M_A").value(factorKey(0, 11)).toDouble(), 2.75);
        DataObjectAggregateSelection units(layer.get(), DC::DataTransects, {"T_A", "T_B"}, &undo);
        emit layer->optionsChanged({"FLOW_UNITS"}); QVERIFY(!units.setValue(column(*units.source(), "nChannel"), .2));
        DataObjectAggregateSelection closed(layer.get(), DC::DataPatterns, {"M_A", "M_C"}, &undo);
        emit layer->engineAboutToClose(); QVERIFY(!closed.setValue(column(*closed.source(), factorKey(0, 0)), 5));
    }
    void browserSelections()
    {
        auto layer = fixture(); QVERIFY(layer); MapCanvas canvas; layer->setEditCanvas(&canvas);
        SelectionManager selection; ObjectBrowserPanel browser; PropertiesPanel panel;
        panel.setProject(layer.get()); browser.setProject(layer.get(), &selection, &canvas);
        connect(&selection, &SelectionManager::selectionChanged, &panel,
                [&](const QSet<SWMMObjectRef> &current, const QSet<SWMMObjectRef> &, const QSet<SWMMObjectRef> &) {
                    panel.showSelection(&canvas, current.values());
                });
        auto *tree = browser.findChild<QTreeView *>(); QVERIFY(tree);
        for (const auto type : {SWMMObjectRef::TimePattern, SWMMObjectRef::Transect}) {
            const QStringList ids = type == SWMMObjectRef::TimePattern ? names(0) : QStringList{"T_A", "T_B", "T_C"};
            QItemSelection picks;
            std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &parent) {
                for (int r = 0; r < tree->model()->rowCount(parent); ++r) {
                    const auto index = tree->model()->index(r, 0, parent);
                    const auto ref = index.data(SWMMObjectTreeModel::RoleObjectRef).value<SWMMObjectRef>();
                    if (ref.objectType == type && ids.contains(ref.name)) picks.select(index, index);
                    collect(index);
                }
            };
            collect({}); QCOMPARE(picks.size(), 3);
            tree->selectionModel()->select(picks, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            QCOMPARE(selection.selection().size(), 3);
            auto *properties = panel.findChild<QTreeView *>(); QVERIFY(properties);
            const auto cell = findProperty(properties->model(), type == SWMMObjectRef::TimePattern ? "Monthly — Jan" : "Roughness — Channel n");
            QVERIFY(cell.isValid()); QVERIFY(properties->model()->setData(cell, .875));
            const auto cat = type == SWMMObjectRef::TimePattern ? DC::DataPatterns : DC::DataTransects;
            const auto key = type == SWMMObjectRef::TimePattern ? factorKey(0, 0) : QString("nChannel");
            for (const auto &id : ids) QCOMPARE(state(layer.get(), cat, id).value(key).toDouble(), .875);
        }
        QCOMPARE(canvas.undoStack()->count(), 2);
    }
    void reviewFixture()
    {
        auto layer = fixture(); QVERIFY(layer);
        const auto path = QDir(folder()).filePath("Patterns_Transects_Review.inp");
        QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
    }
};
QTEST_MAIN(TestPatternTransectBulk)
#include "test_patterntransectbulk.moc"
