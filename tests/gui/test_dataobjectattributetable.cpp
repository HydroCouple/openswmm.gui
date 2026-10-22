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
 *   4. A bulk edit wrapped in a macro (the panel's "apply to selected rows")
 *      reverts in one undo.
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

#include <openswmm/engine/openswmm_pollutants.h>

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
};

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

    // Same shape as AttributeTablePanel::applyValueToSelectedRows.
    stack.beginMacro(QStringLiteral("Apply value to 2 rows"));
    for (int r = 0; r < model.rowCount(); ++r)
        QVERIFY(model.setData(model.index(r, col), 0.02));
    stack.endMacro();
    QCOMPARE(stack.count(), 1);
    QCOMPARE(reg->findByName(QStringLiteral("ST1"))->roadRoughness(), 0.02);
    QCOMPARE(reg->findByName(QStringLiteral("ST2"))->roadRoughness(), 0.02);

    stack.undo();
    QCOMPARE(reg->findByName(QStringLiteral("ST1"))->roadRoughness(), 0.016);
    QCOMPARE(reg->findByName(QStringLiteral("ST2"))->roadRoughness(), 0.016);
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
