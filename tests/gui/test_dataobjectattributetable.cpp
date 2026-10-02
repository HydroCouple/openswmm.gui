/*!
 * \file   test_dataobjectattributetable.cpp
 * \brief  Data objects in the Attribute Table + the open-for-object editor
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

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(QStringLiteral("data_object_table_fixture.inp")),
        nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

int colFor(const DataObjectAttributeTableModel &m, const QString &key)
{
    const QList<ColumnSpec> specs = m.columnSpecs();
    for (int i = 0; i < specs.size(); ++i)
        if (specs[i].key == key) return i;
    return -1;
}

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

void TestDataObjectAttributeTable::everyEditableCategoryOpensExistingObjects()
{
    const auto &reg = ComprehensiveEditorRegistry::instance();
    for (int i = 0; i < DC::NumDataCategories; ++i) {
        const auto cat = static_cast<SWMMModelLayer::DataCategory>(i);
        if (!reg.hasEditor(cat)) continue;
        const auto *entry = reg.find(cat);
        QVERIFY2(entry && entry->openForObject,
                 qPrintable(QStringLiteral("category %1 enables Edit… but cannot "
                                           "open an existing object").arg(i)));
    }
}

void TestDataObjectAttributeTable::rowsMatchRegistry_data()
{
    QTest::addColumn<int>("category");
    QTest::addColumn<int>("expectedRows");
    QTest::newRow("pollutants") << int(DC::DataPollutants) << 2;
    QTest::newRow("landuses")   << int(DC::DataLandUses)   << 2;
    QTest::newRow("aquifers")   << int(DC::DataAquifers)   << 2;
    QTest::newRow("streets")    << int(DC::DataStreets)    << 2;
    QTest::newRow("inlets")     << int(DC::DataInlets)     << 2;
}

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

void TestDataObjectAttributeTable::editWritesProviderEngineAndUndoes()
{
    auto layer = openLayer();
    QVERIFY(layer);
    auto *reg = qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(
        layer->ensurePollutantRegistry());
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

void TestDataObjectAttributeTable::bulkEditUndoesInOneStep()
{
    auto layer = openLayer();
    QVERIFY(layer);
    auto *reg = qobject_cast<openswmmvis::street::StreetRegistry *>(
        layer->ensureStreetRegistry());
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

void TestDataObjectAttributeTable::inletFieldsFollowType()
{
    auto layer = openLayer();
    QVERIFY(layer);
    DataObjectAttributeTableModel model;
    model.setSource(layer.get(), DC::DataInlets);

    const int grate = model.rowForName(QStringLiteral("GRATE1"));
    const int curb  = model.rowForName(QStringLiteral("CURB1"));
    const int grateLen = colFor(model, QStringLiteral("grateLength"));
    const int curbLen  = colFor(model, QStringLiteral("curbLength"));
    QVERIFY(grate >= 0 && curb >= 0 && grateLen >= 0 && curbLen >= 0);

    QVERIFY(model.flags(model.index(grate, grateLen)) & Qt::ItemIsEditable);
    QVERIFY(!(model.flags(model.index(grate, curbLen)) & Qt::ItemIsEditable));
    QCOMPARE(model.data(model.index(grate, curbLen)).toString(), QStringLiteral("—"));

    QVERIFY(model.flags(model.index(curb, curbLen)) & Qt::ItemIsEditable);
    QVERIFY(!(model.flags(model.index(curb, grateLen)) & Qt::ItemIsEditable));
    QVERIFY(!model.setData(model.index(curb, grateLen), 5.0));
}

void TestDataObjectAttributeTable::nameReferenceColumnRoundTrips()
{
    auto layer = openLayer();
    QVERIFY(layer);
    auto *reg = qobject_cast<openswmmvis::aquifer::AquiferRegistry *>(
        layer->ensureAquiferRegistry());
    QVERIFY(reg);

    DataObjectAttributeTableModel model;
    model.setSource(layer.get(), DC::DataAquifers);
    const int col = colFor(model, QStringLiteral("evapPattern"));
    const int aq1 = model.rowForName(QStringLiteral("AQ1"));
    const int aq2 = model.rowForName(QStringLiteral("AQ2"));
    QVERIFY(col >= 0 && aq1 >= 0 && aq2 >= 0);

    QCOMPARE(model.data(model.index(aq2, col), Qt::EditRole).toString(),
             QStringLiteral("EvapPat"));
    QCOMPARE(model.data(model.index(aq2, col)).toString(), QStringLiteral("EvapPat"));
    QCOMPARE(model.data(model.index(aq1, col)).toString(), QStringLiteral("(none)"));

    QVERIFY(model.setData(model.index(aq1, col), QStringLiteral("EvapPat")));
    QCOMPARE(reg->findByName(QStringLiteral("AQ1"))->evapPattern(), QStringLiteral("EvapPat"));
    QVERIFY(model.setData(model.index(aq2, col), QString()));
    QCOMPARE(reg->findByName(QStringLiteral("AQ2"))->evapPattern(), QString());
}

QTEST_MAIN(TestDataObjectAttributeTable)
#include "test_dataobjectattributetable.moc"
