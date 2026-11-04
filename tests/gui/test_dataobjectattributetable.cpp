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
#include <QTreeView>
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
std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("data_object_table_fixture.inp")), nullptr);
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
    void everyEditableCategoryOpensExistingObjects();
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

/** @brief Verify every category advertising an editor also registers an existing-object dispatch callback. */
void TestDataObjectAttributeTable::everyEditableCategoryOpensExistingObjects()
{
    const auto &reg = ComprehensiveEditorRegistry::instance();
    for (int i = 0; i < DC::NumDataCategories; ++i) {
        const auto cat = static_cast<SWMMModelLayer::DataCategory>(i);
        if (!reg.hasEditor(cat))
            continue;
        const auto *entry = reg.find(cat);
        QVERIFY2(entry && entry->openForObject,
                 qPrintable(QStringLiteral("category %1 enables Edit… but cannot "
                                           "open an existing object")
                                .arg(i)));
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
