/**
 * @file      test_dataobjectattributetable.cpp
 * @brief     Data objects in the Attribute Table + the open-for-object editor
 *         dispatch (workplans/DATA_OBJECT_EDIT_AND_ATTRIBUTE_TABLE_PLAN_2026-10-02.md).
 *
 * Guards:
 *   1. Every data category whose "Edit…" is enabled (hasEditor) can also
 *      open an existing object — the gap that left right-click Edit… dead
 *      for pollutants, land uses, aquifers, snowpacks, LID controls, streets.
 *   2. DataObjectAttributeTableModel lists exactly the registry's objects.
 *   3. A cell edit writes the staged provider AND the engine, is one undo
 *      step, and undo restores both.
 *   4. The panel's bulk write path reverts in one undo after a category switch.
 *   5. Inlet fields the design's type does not use are read-only.
 *   6. Name-reference columns (aquifer evap pattern) round-trip names.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "layers/swmmmodellayer.h"
#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"
#include "street/streetprovider.h"
#include "street/streetregistry.h"
#include "ui/editors/comprehensiveeditorregistry.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "ui/panels/propertiespanel.h"
#include "ui/properties/swmmpollutantpropertyadapter.h"
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include "ui/dialogs/ruleseditordialog.h"
#include "ui/dialogs/timeserieseditordialog.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/panels/timeseriestablemodel.h"
#include "timeseries/timeseriesregistry.h"
#include <QDoubleSpinBox>
#include "ui/dialogs/patterneditordialog.h"
#include "ui/panels/attributetablepanel.h"
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/properties/datarecordsref.h"
#include "controls/controlruleprovider.h"
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QMessageBox>
#include <QTextEdit>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <QSortFilterProxyModel>
#include <QSignalSpy>

#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_model.h>

#include <QDir>
#include <QTest>
#include <QUndoStack>

#include <memory>

using openswmmvis::ColumnSpec;
using DC = SWMMModelLayer;

namespace
{

/** @brief Resolve the data-object table fixture directory.
 * @return SWMMVIS_GUI_TEST_DATA or "." when unset; no filesystem changes. */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Load a fresh owned data-object fixture layer.
 * @return Unique layer for data_object_table_fixture.inp, or nullptr on load failure after destroying the failed layer. Diagnostics are local and discarded. */
std::unique_ptr<SWMMModelLayer>
openLayer(const QString &file = QStringLiteral("data_object_table_fixture.inp"))
{
    auto layer = std::make_unique<SWMMModelLayer>(QDir(dataDir()).filePath(file), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/** @brief Find a table column by canonical metadata key.
 * @param[in] m Borrowed table model queried without mutation.
 * @param[in] key Exact ColumnSpec key to match.
 * @return First zero-based matching column, or -1 when absent. */
int colFor(const DataObjectAttributeTableModel &m, const QString &key)
{
    const QList<ColumnSpec> specs = m.columnSpecs();
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == key)
            return i;
    return -1;
}

/** @brief Read a named pollutant decay coefficient from the live engine.
 * @param[in] layer Required loaded layer owning a valid engine.
 * @param[in] name Required null-terminated pollutant name expected to resolve.
 * @return Coefficient in inverse days on success, or the initialized -1 sentinel if the unchecked getter fails. */
double engineKDecay(SWMMModelLayer *layer, const char *name)
{
    double k = -1.0;
    const int idx = swmm_pollutant_index(layer->engine(), name);
    swmm_pollutant_get_kdecay(layer->engine(), idx, &k);
    return k;
}

} // namespace

class TestDataObjectAttributeTable : public QObject
{
    Q_OBJECT
private slots:
    void everyEditableCategoryOpensExistingObjects_data();
    void everyEditableCategoryOpensExistingObjects();
    void inlineSeriesSurvivesCloseAndReload();
    void switchingProjectsEditsOnlyTheRequestedRule();
    void openingAFilteredPatternRevealsIt();
    void referenceMenusUseTheFieldKind();
    void detailedPropertiesUsesClickedSelection();
    void streetPickerAssignsTheClickedConduit();
    void missingReferenceDoesNotOpenAnotherObject();
    void openingAnUnassignedReferenceDoesNotCreateAnObject();
    void rowsMatchRegistry_data();
    void rowsMatchRegistry();
    void editWritesProviderEngineAndUndoes();
    void bulkEditUndoesInOneStep();
    void inletFieldsFollowType();
    void nameReferenceColumnRoundTrips();
    void propertyAndTableEditsPreserveEachOther();
    void rejectedEngineWriteLeavesValuesAndHistoryUnchanged();
    void categoryEditsPersistAndUndo_data();
    void categoryEditsPersistAndUndo();
};

void TestDataObjectAttributeTable::categoryEditsPersistAndUndo_data()
{
    QTest::addColumn<int>("category");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("key");
    QTest::addColumn<double>("value");
    QTest::newRow("pollutant") << int(DC::DataPollutants) << QString("TSS") << QString("kDecay") << .125;
    QTest::newRow("land-use") << int(DC::DataLandUses) << QString("Residential") << QString("sweepInterval") << 3.0;
    QTest::newRow("aquifer") << int(DC::DataAquifers) << QString("AQ1") << QString("porosity") << .55;
    QTest::newRow("street") << int(DC::DataStreets) << QString("ST1") << QString("roadRoughness") << .025;
    QTest::newRow("inlet") << int(DC::DataInlets) << QString("GRATE1") << QString("grateLength") << 3.5;
}

void TestDataObjectAttributeTable::categoryEditsPersistAndUndo()
{
    QFETCH(int, category);
    QFETCH(QString, name);
    QFETCH(QString, key);
    QFETCH(double, value);
    auto layer = openLayer();
    QVERIFY(layer);
    const auto cat = DC::DataCategory(category);
    DataObjectAttributeTableModel table;
    QUndoStack undo;
    table.setUndoStack(&undo);
    table.setSource(layer.get(), cat);
    const auto cell = table.index(table.rowForName(name), colFor(table, key));
    QVERIFY(cell.isValid());
    const auto before = cell.data(Qt::EditRole);
    QSignalSpy changed(layer.get(), &SWMMModelLayer::attributeChanged);
    QVERIFY(table.setData(cell, value));
    QCOMPARE(cell.data(Qt::EditRole).toDouble(), value);
    QVERIFY(changed.count() > 0);
    undo.undo();
    QCOMPARE(cell.data(Qt::EditRole), before);
    undo.redo();
    QCOMPARE(cell.data(Qt::EditRole).toDouble(), value);
    const QString directory = QDir(dataDir()).filePath("../../../workplans/immediate-mvc-fixtures");
    QVERIFY(QDir().mkpath(directory));
    const QString path = QDir(directory).filePath(QString("%1.inp").arg(QTest::currentDataTag()));
    QCOMPARE(swmm_model_write(layer->engine(), path.toUtf8().constData()), SWMM_OK);
    SWMMModelLayer reopened(path);
    QList<QString> warnings, errors;
    QVERIFY2(reopened.loadModel(warnings, errors), qPrintable(errors.join('\n')));
    DataObjectAttributeTableModel restored;
    restored.setSource(&reopened, cat);
    QCOMPARE(restored.index(restored.rowForName(name), colFor(restored, key)).data(Qt::EditRole).toDouble(), value);
}

void TestDataObjectAttributeTable::propertyAndTableEditsPreserveEachOther()
{
    auto layer = openLayer();
    QVERIFY(layer);
    DataObjectAttributeTableModel table;
    QUndoStack undo;
    table.setUndoStack(&undo);
    table.setSource(layer.get(), DC::DataPollutants);
    PropertiesPanel panel;
    panel.setProject(layer.get());
    panel.showDataObject(layer.get(), SWMMObjectRef::Pollutant, "TSS");
    auto *adapter = panel.findChild<SWMMPollutantPropertyAdapter *>();
    QVERIFY(adapter);
    auto *tree = panel.findChild<QTreeView *>();
    QVERIFY(tree);
    connect(&table, &DataObjectAttributeTableModel::objectEdited, &panel,
            &PropertiesPanel::onObjectEditedExternally);
    adapter->setKDecay(.375);
    // A different record was also edited directly. This cell must preserve both.
    QCOMPARE(swmm_pollutant_set_kdecay(layer->engine(), swmm_pollutant_index(layer->engine(), "Lead"), .42), 0);
    const int row = table.rowForName("TSS");
    QVERIFY(table.setData(table.index(row, colFor(table, "rainConc")), 2.0));
    QCOMPARE(engineKDecay(layer.get(), "TSS"), .375);
    QCOMPARE(engineKDecay(layer.get(), "Lead"), .42);
    QSignalSpy refreshed(tree->model(), &QAbstractItemModel::dataChanged);
    QVERIFY(table.setData(table.index(row, colFor(table, "kDecay")), .5));
    QVERIFY(refreshed.count() > 0);
    QCOMPARE(adapter->kDecay(), .5);
    refreshed.clear();
    undo.undo();
    QCOMPARE(adapter->kDecay(), .375);
    QVERIFY(refreshed.count() > 0);
    QCOMPARE(engineKDecay(layer.get(), "Lead"), .42);
}

void TestDataObjectAttributeTable::rejectedEngineWriteLeavesValuesAndHistoryUnchanged()
{
    auto layer = openLayer();
    QVERIFY(layer);
    DataObjectAttributeTableModel table;
    QUndoStack undo;
    table.setUndoStack(&undo);
    table.setSource(layer.get(), DC::DataPollutants);
    const auto units = table.index(table.rowForName("TSS"), colFor(table, "units"));
    QVERIFY(!(units.flags() & Qt::ItemIsEditable));
    QVERIFY(!table.setData(units, 1));
    QVERIFY(table.setData(table.index(table.rowForName("TSS"), colFor(table, "kDecay")), .5));
    undo.undo();
    QVERIFY(undo.canRedo());
    table.setSource(layer.get(), DC::DataAquifers);
    const auto porosity = table.index(table.rowForName("AQ1"), colFor(table, "porosity"));
    QVERIFY(porosity.isValid());
    const auto before = porosity.data(Qt::EditRole);
    QVERIFY(!table.setData(porosity, 1.5));
    QCOMPARE(porosity.data(Qt::EditRole), before);
    QCOMPARE(undo.index(), 0);
    QVERIFY(undo.canRedo());
    // Zero passes the column's numeric bounds but is rejected by the engine.
    table.setSource(layer.get(), DC::DataInlets);
    const auto grate = table.index(table.rowForName("GRATE1"), colFor(table, "grateLength"));
    const auto grateBefore = grate.data(Qt::EditRole);
    QSignalSpy published(layer.get(), &SWMMModelLayer::attributeChanged);
    QVERIFY(!table.setData(grate, 0.0));
    QCOMPARE(grate.data(Qt::EditRole), grateBefore);
    QCOMPARE(published.count(), 0);
    QCOMPARE(undo.index(), 0);
    QVERIFY(undo.canRedo());
}

void TestDataObjectAttributeTable::everyEditableCategoryOpensExistingObjects_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("editor");
    using R = SWMMObjectRef;
    const auto add = [](R::ObjectType kind, const char *id, const char *editor) {
        QTest::newRow(editor) << int(kind) << QString::fromLatin1(id)
                              << QString::fromLatin1(editor);
    };
    add(R::TimeSeries, "C1", "TimeseriesEditorDialog");
    add(R::Curve, "CurveB", "CurveEditorDialog");
    add(R::Control, "RuleB", "RulesEditorDialog");
    add(R::Hydrograph, "GroupB", "HydrographGroupEditor");
    add(R::TimePattern, "LastPattern", "PatternEditorDialog");
    add(R::Transect, "Profile", "TransectEditorDialog");
    add(R::Inlet, "CURB1", "InletEditorDialog");
    add(R::Street, "ST1", "StreetEditorDialog");
    add(R::LIDControl, "Stack", "LidControlEditorDialog");
    add(R::Snowpack, "Snow", "SnowpackEditorDialog");
    add(R::Pollutant, "TSS", "PollutantEditorDialog");
    add(R::LandUse, "Residential", "LandUseEditorDialog");
    add(R::Aquifer, "AQ1", "AquiferEditorDialog");
}

void TestDataObjectAttributeTable::everyEditableCategoryOpensExistingObjects()
{
    QFETCH(int, kind);
    QFETCH(QString, id);
    QFETCH(QString, editor);
    auto layer = openLayer("data_editor_routing.inp");
    QVERIFY(layer);
    ObjectBrowserPanel panel;
    panel.resize(650, 800);
    panel.setProject(layer.get(), nullptr);
    panel.show();
    auto *tree = panel.findChild<SWMMObjectTreeModel *>();
    auto *view = panel.findChild<QTreeView *>();
    QVERIFY(tree && view);
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
    QVERIFY(proxy);
    const SWMMObjectRef ref{static_cast<SWMMObjectRef::ObjectType>(kind), id};
    const auto leaf = proxy->mapFromSource(tree->indexFor(ref));
    QCOMPARE(leaf.data(SWMMObjectTreeModel::RoleObjectRef).value<SWMMObjectRef>(), ref);
    view->expand(leaf.parent());
    view->scrollTo(leaf);
    const auto pos = view->visualRect(leaf).center();
    QCOMPARE(view->indexAt(pos), leaf);
    const auto category = DataObjectAttributeTableModel::categoryForType(ref.objectType);
    const int initialCount = layer->dataObjectCount(category);
    QSignalSpy edited(layer.get(), &SWMMModelLayer::modelEdited);
    QTimer::singleShot(0, &panel, [tree] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        // A queued refresh can invalidate the original row during menu.exec().
        tree->reload();
        for (auto *action : menu->actions()) {
            if (action->text() == QStringLiteral("Edit…")) {
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                  menu->actionGeometry(action).center());
                return;
            }
        }
        menu->close();
        QFAIL("Missing Edit action");
    });
    emit view->customContextMenuRequested(pos);
    const auto closeRequestedEditor = [&](QWidget &host) {
        for (auto *dialog : host.findChildren<QDialog *>()) {
            if (!dialog->isVisible() ||
                QString::fromLatin1(dialog->metaObject()->className()).section("::", -1) != editor)
                continue;
            bool selected = false;
            for (auto *list : dialog->findChildren<QListView *>())
                selected |= list->isVisible() && list->currentIndex().data().toString() == id;
            dialog->close();
            return selected;
        }
        return false;
    };
    QVERIFY2(closeRequestedEditor(panel), "Browser Edit opened the wrong dialog or ID");
    emit view->doubleClicked(proxy->mapFromSource(tree->indexFor(ref)));
    QVERIFY2(closeRequestedEditor(panel), "Browser double-click opened the wrong dialog or ID");

    PropertiesPanel properties;
    properties.setProject(layer.get());
    properties.show();
    properties.showDataObject(layer.get(), kind, id);
    for (auto *button : properties.findChildren<QPushButton *>()) {
        if (button->isVisible() && button->text().startsWith("Open ")) {
            button->click();
            break;
        }
    }
    QVERIFY2(closeRequestedEditor(properties), "Properties header opened the wrong dialog or ID");

    AttributeTablePanel table;
    table.resize(900, 600);
    table.setProject(layer.get(), nullptr);
    table.show();
    table.showDataCategory(category);
    auto *grid = table.findChild<QTableView *>();
    QVERIFY(grid);
    const auto matches = grid->model()->match(grid->model()->index(0, 0), Qt::DisplayRole, id, 1,
                                              Qt::MatchFixedString);
    QCOMPARE(matches.size(), 1);
    grid->scrollTo(matches.first());
    QTimer::singleShot(0, &table, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        for (auto *action : menu->actions()) {
            if (action->text().startsWith("Edit ")) {
                action->trigger();
                break;
            }
        }
        menu->close();
    });
    emit grid->customContextMenuRequested(grid->visualRect(matches.first()).center());
    QVERIFY2(closeRequestedEditor(table), "Attribute Table Edit opened the wrong dialog or ID");
    QCOMPARE(layer->dataObjectCount(category), initialCount);
    QCOMPARE(edited.count(), 0);
}

void TestDataObjectAttributeTable::switchingProjectsEditsOnlyTheRequestedRule()
{
    auto a = openLayer("data_editor_routing.inp");
    auto b = openLayer("data_editor_routing.inp");
    QVERIFY(a && b);
    QUndoStack undoA, undoB;
    QWidget host;
    auto &registry = ComprehensiveEditorRegistry::instance();
    using Dialog = openswmmvis::ui::RulesEditorDialog;
    QVERIFY(registry.openForObject(DC::DataControls, a.get(), &undoA, "RuleB", &host));
    QPointer<Dialog> previous = host.findChild<Dialog *>();
    QVERIFY(previous && previous->currentProvider());
    auto *providerA = previous->currentProvider();
    const QString before = providerA->body();
    QVERIFY(registry.openForObject(DC::DataControls, b.get(), &undoB, "RuleB", &host));
    Dialog *current = nullptr;
    for (auto *dialog : host.findChildren<Dialog *>())
        if (dialog->isVisible())
            current = dialog;
    QVERIFY(current && current != previous);
    auto *providerB = current->currentProvider();
    QVERIFY(providerB && providerB != providerA);
    QCOMPARE(providerB->name(), QStringLiteral("RuleB"));
    auto *code = current->findChild<QTextEdit *>();
    QVERIFY(code);
    QString changed = code->toPlainText();
    QVERIFY(changed.contains("CLOSED"));
    code->setPlainText(changed.replace("CLOSED", "OPEN"));
    // The text is held until Apply; closing asks, and Apply writes it.
    bool asked = false;
    QTimer::singleShot(0, current, [&asked] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            for (auto *button : box->buttons())
                if (button->text().remove('&') == QStringLiteral("Apply")) {
                    asked = true;
                    button->click();
                }
    });
    current->close();
    QVERIFY(asked);
    QCOMPARE(undoA.count(), 0);
    QCOMPARE(providerA->body(), before);
    QCOMPARE(undoB.count(), 1);
    QVERIFY(providerB->body().contains("OPEN"));
    undoB.undo();
    QCOMPARE(providerB->body(), before);
}

void TestDataObjectAttributeTable::inlineSeriesSurvivesCloseAndReload()
{
    auto layer = openLayer("single_rain_series.inp");
    QVERIFY(layer);
    const QString id = QStringLiteral("InlineTest");
    using openswmmvis::timeseries::TimeseriesRegistry;
    using openswmmvis::ui::TimeseriesEditorDialog;
    auto *registry = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
    QVERIFY(registry && registry->create(id));
    QWidget host;
    QUndoStack undo;
    auto openEditor = [&](SWMMModelLayer *owner) -> TimeseriesEditorDialog * {
        ComprehensiveEditorRegistry::instance().openForObject(DC::DataTimeSeries, owner, &undo, id, &host);
        for (auto *dialog : host.findChildren<TimeseriesEditorDialog *>())
            if (dialog->isVisible() && dialog->currentName() == id)
                return dialog;
        return nullptr;
    };
    auto *editor = openEditor(layer.get());
    QVERIFY(editor);
    QAction *addRow = nullptr;
    for (auto *action : editor->findChildren<QAction *>())
        if (action->text() == "Add Row")
            addRow = action;
    QVERIFY(addRow);
    addRow->trigger();
    addRow->trigger();
    auto *grid = editor->tableModel();
    QCOMPARE(grid->rowCount(), 2);
    const auto time0 = grid->index(0, 0).data(Qt::EditRole).toDateTime();
    const auto time1 = grid->index(1, 0).data(Qt::EditRole).toDateTime();
    QVERIFY(grid->setData(grid->index(0, 1), 2.5));
    // Close directly from an active value editor, without pressing Enter.
    auto *view = editor->findChild<QTableView *>();
    QVERIFY(view);
    view->setCurrentIndex(grid->index(1, 1));
    view->edit(view->currentIndex());
    auto *cellEditor = view->findChild<QDoubleSpinBox *>();
    QVERIFY(cellEditor);
    cellEditor->setValue(7.25);
    auto *closeButton = editor->findChild<QPushButton *>("ts_closeBtn");
    QVERIFY(closeButton);
    // Closing with unapplied edits asks first; choose Apply.
    bool asked = false;
    QTimer::singleShot(0, editor, [&asked] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            for (auto *button : box->buttons())
                if (button->text().remove('&') == QStringLiteral("Apply")) {
                    asked = true;
                    button->click();
                }
    });
    closeButton->click();
    QVERIFY(asked);

    DataObjectAttributeTableModel table;
    table.setSource(layer.get(), DC::DataTimeSeries);
    auto *again = openEditor(layer.get());
    QVERIFY(again);
    auto verify = [&](TimeseriesEditorDialog *dialog, double firstValue) {
        auto *model = dialog->tableModel();
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(0, 0).data(Qt::EditRole).toDateTime(), time0);
        QCOMPARE(model->index(1, 0).data(Qt::EditRole).toDateTime(), time1);
        QCOMPARE(model->index(0, 1).data(Qt::EditRole).toDouble(), firstValue);
        QCOMPARE(model->index(1, 1).data(Qt::EditRole).toDouble(), 7.25);
    };
    verify(again, 2.5);
    // A second edit, stepped through the editor's own undo, then applied and saved.
    QVERIFY(again->tableModel()->setData(again->tableModel()->index(0, 1), 9.5));
    again->undoStack()->undo();
    verify(again, 2.5);
    again->undoStack()->redo();
    // Save writes the applied state, so apply the second edit first.
    QVERIFY(again->draftSession()->apply());
    // Kept for review: <repo>/workplans/data-object-apply/phase1/review.
    QDir out(dataDir());
    QVERIFY(out.cdUp() && out.cdUp() && out.cdUp());
    QVERIFY(out.mkpath("workplans/data-object-apply/phase1/review"));
    const QString output = out.filePath("workplans/data-object-apply/phase1/review/inline_series_close_reload.inp");
    QVERIFY(layer->flushDataObjectRegistries());
    QCOMPARE(swmm_model_write(layer->engine(), output.toUtf8()), SWMM_OK);
    again->close();
    auto saved = openLayer(output);
    QVERIFY(saved);
    auto *reopened = openEditor(saved.get());
    QVERIFY(reopened);
    verify(reopened, 9.5);
}

void TestDataObjectAttributeTable::openingAFilteredPatternRevealsIt()
{
    auto layer = openLayer("data_editor_routing.inp");
    QVERIFY(layer);
    QWidget host;
    auto &registry = ComprehensiveEditorRegistry::instance();
    QVERIFY(registry.openForObject(DC::DataPatterns, layer.get(), nullptr, "LastPattern", &host));
    auto *dialog = host.findChild<openswmmvis::ui::PatternEditorDialog *>();
    QVERIFY(dialog);
    auto *list = dialog->findChild<QListView *>();
    QVERIFY(list);
    for (auto *field : dialog->findChildren<QLineEdit *>())
        if (field->placeholderText().contains("Search patterns"))
            field->setText("LastPattern");
    QCOMPARE(list->model()->rowCount(), 1);
    QVERIFY(registry.openForObject(DC::DataPatterns, layer.get(), nullptr, "FirstPattern", &host));
    QCOMPARE(list->currentIndex().data().toString(), QStringLiteral("FirstPattern"));
}

void TestDataObjectAttributeTable::referenceMenusUseTheFieldKind()
{
    auto layer = openLayer("data_editor_routing.inp");
    QVERIFY(layer);
    QTableView view;
    QStandardItemModel model(1, 1);
    view.setModel(&model);
    const auto index = model.index(0, 0);
    using R = DataObjectRef;
    struct Example {
        R::Kind kind;
        const char *id;
        const char *dialogClass;
    };
    const Example examples[] = {{R::DiversionCurve, "CurveB", "CurveEditorDialog"},
                                {R::TidalCurve, "CurveB", "CurveEditorDialog"},
                                {R::PumpCurve, "CurveB", "CurveEditorDialog"},
                                {R::RatingCurve, "CurveB", "CurveEditorDialog"},
                                {R::StorageCurve, "CurveB", "CurveEditorDialog"},
                                {R::AnyCurve, "CurveB", "CurveEditorDialog"},
                                {R::TimeSeries, "C1", "TimeseriesEditorDialog"},
                                {R::Pattern, "LastPattern", "PatternEditorDialog"},
                                {R::UnitHydrograph, "GroupB", "HydrographGroupEditor"},
                                {R::Transect, "Profile", "TransectEditorDialog"},
                                {R::Street, "ST1", "StreetEditorDialog"},
                                {R::Snowpack, "Snow", "SnowpackEditorDialog"},
                                {R::Pollutant, "TSS", "PollutantEditorDialog"},
                                {R::LidControl, "Stack", "LidControlEditorDialog"},
                                {R::Aquifer, "AQ1", "AquiferEditorDialog"},
                                {R::Inlet, "CURB1", "InletEditorDialog"},
                                {R::Node, "J1", nullptr},
                                {R::RainGage, "G1", nullptr},
                                {R::Subcatchment, "", nullptr},
                                {R::SubcatchOutlet, "", nullptr},
                                {R::CaptureNode, "", nullptr}};
    for (const auto &example : examples) {
        R ref;
        ref.layer = layer.get();
        ref.engine = layer->engine();
        ref.kind = example.kind;
        ref.currentName = QString::fromLatin1(example.id);
        model.setData(index, QVariant::fromValue(ref));
        QMenu menu;
        QCOMPARE(addDataObjectReferenceAction(menu, &view, index), example.dialogClass != nullptr);
        if (!example.dialogClass)
            continue;
        QString actual;
        QStringList selected;
        QTimer::singleShot(0, &view, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            actual = QString::fromLatin1(dialog->metaObject()->className()).section("::", -1);
            for (auto *list : dialog->findChildren<QListView *>())
                if (list->isVisible())
                    selected << list->currentIndex().data().toString();
            dialog->reject();
        });
        menu.actions().first()->trigger();
        QCOMPARE(actual, QString::fromLatin1(example.dialogClass));
        QVERIFY2(selected.contains(ref.currentName),
                 qPrintable(actual + ": " + selected.join(',')));
        QCOMPARE(index.data(Qt::EditRole).value<R>().currentName, ref.currentName);
    }
}

void TestDataObjectAttributeTable::streetPickerAssignsTheClickedConduit()
{
    auto layer = openLayer("data_editor_routing.inp");
    QVERIFY(layer);
    AttributeTablePanel table;
    table.resize(1200, 650);
    table.setProject(layer.get(), nullptr);
    table.show();
    table.showModelCategory(layer.get(), DC::CatConduits);
    auto *grid = table.findChild<QTableView *>();
    QVERIFY(grid);
    QModelIndex field;
    for (int col = 0; col < grid->model()->columnCount(); ++col) {
        const auto index = grid->model()->index(0, col);
        const auto value = index.data(Qt::EditRole);
        if (value.metaType() == QMetaType::fromType<DataObjectRef>() &&
            value.value<DataObjectRef>().kind == DataObjectRef::Street) {
            field = index;
            break;
        }
    }
    QVERIFY(field.isValid());
    QCOMPARE(field.data(Qt::EditRole).value<DataObjectRef>().currentName, "ST1");
    grid->scrollTo(field);
    QTimer::singleShot(0, &table, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        for (auto *action : menu->actions()) {
            if (!action->text().contains("ST1") || !action->text().contains("Street"))
                continue;
            QTimer::singleShot(0, &table, [] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                auto *list = dialog->findChild<QListView *>();
                const auto matches = list->model()->match(
                    list->model()->index(0, 0), Qt::DisplayRole, "ST2", 1, Qt::MatchFixedString);
                if (!matches.isEmpty())
                    list->setCurrentIndex(matches.first());
                dialog->close();
            });
            action->trigger();
            break;
        }
        menu->close();
    });
    emit grid->customContextMenuRequested(grid->visualRect(field).center());
    const auto result =
        grid->model()->index(0, field.column()).data(Qt::EditRole).value<DataObjectRef>();
    QCOMPARE(result.currentName, "ST2");
}

void TestDataObjectAttributeTable::openingAnUnassignedReferenceDoesNotCreateAnObject()
{
    auto layer = openLayer("data_editor_routing.inp");
    QVERIFY(layer);
    QTableView view;
    QStandardItemModel model(1, 1);
    view.setModel(&model);
    const auto index = model.index(0, 0);
    const QList<QPair<DataObjectRef::Kind, DC::DataCategory>> examples{
        {DataObjectRef::Aquifer, DC::DataAquifers}, {DataObjectRef::Street, DC::DataStreets},
        {DataObjectRef::Transect, DC::DataTransects}, {DataObjectRef::Pollutant, DC::DataPollutants}};
    for (const auto &[kind, category] : examples) {
        DataObjectRef ref;
        ref.kind = kind;
        ref.layer = layer.get();
        ref.engine = layer->engine();
        model.setData(index, QVariant::fromValue(ref));
        const auto before = layer->dataObjectCount(category);
        QMenu menu;
        QVERIFY(addDataObjectReferenceAction(menu, &view, index));
        QTimer::singleShot(0, &view, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->close();
        });
        menu.actions().first()->trigger();
        QCOMPARE(layer->dataObjectCount(category), before);
        QCOMPARE(index.data(Qt::EditRole).value<DataObjectRef>(), ref);
    }
}

void TestDataObjectAttributeTable::missingReferenceDoesNotOpenAnotherObject()
{
    auto layer = openLayer("data_editor_routing.inp");
    QVERIFY(layer);
    QTableView view;
    QStandardItemModel model(1, 1);
    view.setModel(&model);
    DataObjectRef ref;
    ref.layer = layer.get();
    ref.engine = layer->engine();
    ref.currentName = "RemovedSeries";
    const auto index = model.index(0, 0);
    model.setData(index, QVariant::fromValue(ref));
    QMenu menu;
    QVERIFY(addDataObjectReferenceAction(menu, &view, index));
    QString warning;
    QTimer::singleShot(0, &view, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (auto *message = qobject_cast<QMessageBox *>(dialog))
            warning = message->text();
        if (dialog)
            dialog->reject();
    });
    menu.actions().first()->trigger();
    QVERIFY(warning.contains(ref.currentName));
    QCOMPARE(index.data(Qt::EditRole).value<DataObjectRef>(), ref);
}

void TestDataObjectAttributeTable::detailedPropertiesUsesClickedSelection()
{
    auto layer = openLayer("data_editor_routing.inp");
    QVERIFY(layer);
    AttributeTablePanel table;
    table.resize(1200, 650);
    table.setProject(layer.get(), nullptr);
    table.show();
    table.showDataCategory(DC::DataTimeSeries);
    auto *view = table.findChild<QTableView *>();
    QVERIFY(view);
    auto *model = view->model();
    int col = 0;
    while (col < model->columnCount() &&
           model->index(0, col).data(Qt::EditRole).metaType().id() != qMetaTypeId<DataRecordsRef>())
        ++col;
    QVERIFY(col < model->columnCount());
    const auto clicked = model->index(1, col);
    view->scrollTo(clicked);
    for (int expected : {1, 2}) {
        view->selectRow(0);
        if (expected == 2)
            view->selectionModel()->select(clicked,
                                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
        bool found = false;
        bool scalarAction = false;
        QTimer::singleShot(0, &table, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            QAction *edit = nullptr;
            for (auto *action : menu->actions()) {
                scalarAction |= action->text().startsWith("Apply");
                if (action->text().startsWith("Edit detailed properties"))
                    edit = action;
            }
            found = edit;
            if (edit)
                edit->trigger();
            menu->close();
        });
        emit view->customContextMenuRequested(view->visualRect(clicked).center());
        QVERIFY(found);
        QVERIFY(!scalarAction);
        auto *dialog = table.findChild<QDialog *>("DataRecordsDialog");
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(dialog->windowTitle(), QString("Properties — %1 selected").arg(expected));
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

/** @brief Register five data categories whose fixture registries each contain two objects. */
void TestDataObjectAttributeTable::rowsMatchRegistry_data()
{
    QTest::addColumn<int>("category");
    QTest::addColumn<int>("expectedRows");
    QTest::newRow("pollutants") << int(DC::DataPollutants) << 2;
    QTest::newRow("landuses") << int(DC::DataLandUses) << 2;
    QTest::newRow("aquifers") << int(DC::DataAquifers) << 2;
    QTest::newRow("streets") << int(DC::DataStreets) << 2;
    QTest::newRow("inlets") << int(DC::DataInlets) << 2;
}

/** @brief Verify table rows match registry count, names are read-only and every name resolves back to its row. */
void TestDataObjectAttributeTable::rowsMatchRegistry()
{
    QFETCH(int, category);
    QFETCH(int, expectedRows);
    auto layer = openLayer();
    QVERIFY(layer);
    const auto cat = static_cast<SWMMModelLayer::DataCategory>(category);

    DataObjectAttributeTableModel model;
    model.setSource(layer.get(), cat);
    QCOMPARE(model.rowCount(), expectedRows);
    QCOMPARE(model.rowCount(), layer->dataObjectCount(cat));
    QVERIFY(model.columnCount() > 1);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Name"));
    // Name is read-only; every row's name resolves back to its row.
    for (int r = 0; r < model.rowCount(); ++r) {
        QVERIFY(!(model.flags(model.index(r, 0)) & Qt::ItemIsEditable));
        QCOMPARE(model.rowForName(model.objectNameAt(r)), r);
    }
}

/** @brief Verify a pollutant decay edit updates table, provider and engine in one undo step.
 * Checks same-value edits add no history and undo/redo restore both provider and engine. */
void TestDataObjectAttributeTable::editWritesProviderEngineAndUndoes()
{
    auto layer = openLayer();
    QVERIFY(layer);
    auto *reg =
        qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(layer->ensurePollutantRegistry());
    QVERIFY(reg);

    QUndoStack stack;
    DataObjectAttributeTableModel model;
    model.setUndoStack(&stack);
    model.setSource(layer.get(), DC::DataPollutants);

    const int row = model.rowForName(QStringLiteral("TSS"));
    const int col = colFor(model, QStringLiteral("kDecay"));
    QVERIFY(row >= 0 && col >= 0);
    QVERIFY(model.flags(model.index(row, col)) & Qt::ItemIsEditable);

    QVERIFY(model.setData(model.index(row, col), 0.25));
    QCOMPARE(stack.count(), 1);
    QCOMPARE(reg->findByName(QStringLiteral("TSS"))->kDecay(), 0.25);
    QCOMPARE(engineKDecay(layer.get(), "TSS"), 0.25);
    QCOMPARE(model.data(model.index(row, col), Qt::EditRole).toDouble(), 0.25);

    // Writing the value it already holds is not an edit.
    QVERIFY(model.setData(model.index(row, col), 0.25));
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(reg->findByName(QStringLiteral("TSS"))->kDecay(), 0.0);
    QCOMPARE(engineKDecay(layer.get(), "TSS"), 0.0);
    stack.redo();
    QCOMPARE(engineKDecay(layer.get(), "TSS"), 0.25);
}

/** @brief Verify two street roughness edits grouped in one undo macro revert together to their original values. */
void TestDataObjectAttributeTable::bulkEditUndoesInOneStep()
{
    auto layer = openLayer();
    QVERIFY(layer);
    auto *reg = qobject_cast<openswmmvis::street::StreetRegistry *>(layer->ensureStreetRegistry());
    QVERIFY(reg);

    QUndoStack stack;
    DataObjectAttributeTableModel model;
    model.setUndoStack(&stack);
    model.setSource(layer.get(), DC::DataStreets);
    const int col = colFor(model, QStringLiteral("roadRoughness"));
    QVERIFY(col >= 0);

    model.setDataForRows(col, {0, 1}, 0.02);
    QCOMPARE(stack.count(), 1);
    QCOMPARE(reg->findByName(QStringLiteral("ST1"))->roadRoughness(), 0.02);
    QCOMPARE(reg->findByName(QStringLiteral("ST2"))->roadRoughness(), 0.02);

    model.setSource(layer.get(), DC::DataPollutants);
    stack.undo();
    QCOMPARE(reg->findByName(QStringLiteral("ST1"))->roadRoughness(), 0.016);
    QCOMPARE(reg->findByName(QStringLiteral("ST2"))->roadRoughness(), 0.016);
    stack.redo();
    QCOMPARE(reg->findByName(QStringLiteral("ST1"))->roadRoughness(), 0.02);
    QCOMPARE(reg->findByName(QStringLiteral("ST2"))->roadRoughness(), 0.02);
}

/** @brief Verify grate/curb inlet fields are editable only for the relevant design type and unused cells are null. */
void TestDataObjectAttributeTable::inletFieldsFollowType()
{
    auto layer = openLayer();
    QVERIFY(layer);
    DataObjectAttributeTableModel model;
    model.setSource(layer.get(), DC::DataInlets);

    const int grate = model.rowForName(QStringLiteral("GRATE1"));
    const int curb = model.rowForName(QStringLiteral("CURB1"));
    const int grateLen = colFor(model, QStringLiteral("grateLength"));
    const int curbLen = colFor(model, QStringLiteral("curbLength"));
    QVERIFY(grate >= 0 && curb >= 0 && grateLen >= 0 && curbLen >= 0);

    QVERIFY(model.flags(model.index(grate, grateLen)) & Qt::ItemIsEditable);
    QVERIFY(!(model.flags(model.index(grate, curbLen)) & Qt::ItemIsEditable));
    QVERIFY(!model.data(model.index(grate, curbLen)).isValid());
    QVERIFY(!model.data(model.index(grate, curbLen), Qt::EditRole).isValid());

    QVERIFY(model.flags(model.index(curb, curbLen)) & Qt::ItemIsEditable);
    QVERIFY(!(model.flags(model.index(curb, grateLen)) & Qt::ItemIsEditable));
    QVERIFY(!model.setData(model.index(curb, grateLen), 5.0));
}

/** @brief Verify aquifer evaporation-pattern references display/edit by name and can be assigned or cleared. */
void TestDataObjectAttributeTable::nameReferenceColumnRoundTrips()
{
    auto layer = openLayer();
    QVERIFY(layer);
    auto *reg =
        qobject_cast<openswmmvis::aquifer::AquiferRegistry *>(layer->ensureAquiferRegistry());
    QVERIFY(reg);

    DataObjectAttributeTableModel model;
    model.setSource(layer.get(), DC::DataAquifers);
    const int col = colFor(model, QStringLiteral("evapPattern"));
    const int aq1 = model.rowForName(QStringLiteral("AQ1"));
    const int aq2 = model.rowForName(QStringLiteral("AQ2"));
    QVERIFY(col >= 0 && aq1 >= 0 && aq2 >= 0);

    QCOMPARE(model.data(model.index(aq2, col), Qt::EditRole).toString(), QStringLiteral("EvapPat"));
    QCOMPARE(model.data(model.index(aq2, col)).toString(), QStringLiteral("EvapPat"));
    QVERIFY(!model.data(model.index(aq1, col)).isValid());

    QVERIFY(model.setData(model.index(aq1, col), QStringLiteral("EvapPat")));
    QCOMPARE(reg->findByName(QStringLiteral("AQ1"))->evapPattern(), QStringLiteral("EvapPat"));
    QVERIFY(model.setData(model.index(aq2, col), QString()));
    QCOMPARE(reg->findByName(QStringLiteral("AQ2"))->evapPattern(), QString());
}

QTEST_MAIN(TestDataObjectAttributeTable)
#include "test_dataobjectattributetable.moc"
