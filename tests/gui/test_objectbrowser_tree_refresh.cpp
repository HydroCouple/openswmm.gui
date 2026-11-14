/**
 * @file      test_objectbrowser_tree_refresh.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Object Browser refresh on data-object mutations
 *         (SWMMModelLayer::dataObjectsChanged → SWMMObjectTreeModel
 *         coalesced reload, registry-preferred counts).
 *
 *         Pins the contract that made the fix necessary: registry
 *         add/remove/rename fires at dialog submit — possibly BEFORE the
 *         deferred saveToEngine flush — and saveToEngine never deletes
 *         engine rows, so the tree must read the live registry, not the
 *         engine, whenever one is bound.
 *
 *         Uses tests/gui/data/typed_selection_fixture.inp (one inline
 *         time series "TS1"). Each slot loads a fresh layer so slots are
 *         independent of execution order.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmmmodellayer.h"
#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "ui/panels/swmmobjecttreemodel.h"

#include <openswmm/engine/openswmm_tables.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using openswmmvis::timeseries::TimeseriesRegistry;
using DataCategory = SWMMModelLayer::DataCategory;

namespace
{

/** @brief Resolve the object-browser test fixture directory.
 * @return SWMMVIS_GUI_TEST_DATA or "." when unset. */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Locate the typed-selection model containing TS1.
 * @return Joined fixture path without opening it. */
QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

/** @brief Load a fresh owned fixture layer for an independent test.
 * @return Unique loaded layer or nullptr on failure; local diagnostics are discarded and a failed layer is destroyed. */
std::unique_ptr<SWMMModelLayer> openFixtureLayer()
{
    auto layer = std::make_unique<SWMMModelLayer>(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

// The coalesced reload lands via a 0-ms singleShot — drain it.
/** @brief Process GUI events twice to deliver coalesced zero-delay tree reloads.
 * This pumps other pending events too and does not wait for arbitrary future work. */
void drainEventLoop()
{
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

// Leaf display names under the Time Series data-category row, or an empty
// list when the category is not currently shown.
/** @brief Collect displayed time-series leaf names.
 * @param[in] model Borrowed tree model queried without mutation.
 * @return Column-zero display names in row order, or an empty list if its time-series category is hidden. */
QStringList timeSeriesLeafNames(const SWMMObjectTreeModel &model)
{
    const int top = model.topRowForDataCategory(SWMMModelLayer::DataTimeSeries);
    if (top < 0)
        return {};
    const QModelIndex parent = model.index(top, 0, QModelIndex());
    QStringList names;
    const int n = model.rowCount(parent);
    names.reserve(n);
    for (int r = 0; r < n; ++r)
        names << model.data(model.index(r, 0, parent), Qt::DisplayRole).toString();
    return names;
}

} // namespace

class TestObjectBrowserTreeRefresh : public QObject
{
    Q_OBJECT

private slots:

    /** @brief Require the typed-selection fixture file before running independent tree refresh cases. */
    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "typed_selection_fixture.inp missing from the gui-test data dir");
    }

    // 1. A registry create (what every deferred editor dialog does at
    //    submit) must surface in the tree without any saveToEngine call.
    /** @brief Verify a registry creation appears once in the tree and is immediately available in the engine without an explicit flush. */
    void stagedCreateAppearsInTree()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        SWMMObjectTreeModel model;
        model.setLayer(layer.get());

        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        QVERIFY(reg != nullptr);
        drainEventLoop(); // settle any reload scheduled by registry seeding

        const int before = layer->dataObjectCount(SWMMModelLayer::DataTimeSeries);
        QCOMPARE(before, 1); // fixture's TS1, mirrored into the registry

        QVERIFY(reg->create(QStringLiteral("TS_NEW")) != nullptr);
        drainEventLoop();

        QCOMPARE(layer->dataObjectCount(SWMMModelLayer::DataTimeSeries), before + 1);
        const QStringList leaves = timeSeriesLeafNames(model);
        QVERIFY2(leaves.contains(QStringLiteral("TS_NEW")),
                 qPrintable(QStringLiteral("leaves: ") + leaves.join(QLatin1Char(','))));

        // Since 2026-10-19 a create reaches the engine at once (so gages and
        // outfalls can select the series while its editor is open). The
        // count above stays before + 1: the tree must not list it twice.
        QVERIFY(swmm_table_index(layer->engine(), "TS_NEW") >= 0);
        QCOMPARE(leaves.count(QStringLiteral("TS_NEW")), 1);
    }

    // 2. Delete-path staleness: after remove()+saveToEngine() (what
    //    DeleteDataObjectCommand::redo does) the row must disappear. Since
    //    72d188e remove() also deletes the engine-side table (Phase A3), so
    //    the engine no longer carries a row that could keep the leaf alive.
    /** @brief Verify removing/flushing TS1 removes the category and the engine table.
     * Despite the historical slot name, current assertions require removal of the engine row as well. */
    void deletedProviderDisappearsDespiteEngineRow()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        SWMMObjectTreeModel model;
        model.setLayer(layer.get());

        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        QVERIFY(reg != nullptr);
        auto *ts1 = reg->findByName(QStringLiteral("TS1"));
        QVERIFY(ts1 != nullptr);

        reg->remove(ts1);
        reg->saveToEngine(layer->engine());
        drainEventLoop();

        QCOMPARE(layer->dataObjectCount(SWMMModelLayer::DataTimeSeries), 0);
        QCOMPARE(model.topRowForDataCategory(SWMMModelLayer::DataTimeSeries), -1);
        // remove() deleted the engine copy too (registry contract since 72d188e).
        QVERIFY(swmm_table_index(layer->engine(), "TS1") < 0);
    }

    // 3. Rename updates the leaf text.
    /** @brief Verify a registry rename replaces the tree leaf text after coalesced reload. */
    void renameUpdatesLeaf()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        SWMMObjectTreeModel model;
        model.setLayer(layer.get());

        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        QVERIFY(reg != nullptr);
        auto *ts1 = reg->findByName(QStringLiteral("TS1"));
        QVERIFY(ts1 != nullptr);

        QVERIFY(reg->rename(ts1, QStringLiteral("TS_RENAMED")));
        drainEventLoop();

        const QStringList leaves = timeSeriesLeafNames(model);
        QVERIFY(leaves.contains(QStringLiteral("TS_RENAMED")));
        QVERIFY(!leaves.contains(QStringLiteral("TS1")));
    }

    // 4. Burst coalescing: N creates in one event-loop turn produce
    //    exactly ONE model reset.
    /** @brief Verify five registry creates in one event turn cause exactly one model reset and yield six total series. */
    void burstCreatesCoalesceToOneReset()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        SWMMObjectTreeModel model;
        model.setLayer(layer.get());

        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        QVERIFY(reg != nullptr);
        drainEventLoop();

        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        for (int i = 0; i < 5; ++i)
            QVERIFY(reg->create(QStringLiteral("TS_BURST_%1").arg(i)) != nullptr);
        drainEventLoop();

        QCOMPARE(resetSpy.count(), 1);
        QCOMPARE(layer->dataObjectCount(SWMMModelLayer::DataTimeSeries), 6);
    }

    // 5. createDataObject (the NewDataObjectDialog path — direct engine
    //    add) refreshes the tree too, including categories with no live
    //    registry yet.
    /** @brief Verify direct pattern creation reveals its previously absent tree category after event delivery. */
    void createDataObjectRefreshesTree()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        SWMMObjectTreeModel model;
        model.setLayer(layer.get());
        drainEventLoop();

        QCOMPARE(layer->dataObjectCount(SWMMModelLayer::DataPatterns), 0);
        QCOMPARE(model.topRowForDataCategory(SWMMModelLayer::DataPatterns), -1);

        QString err;
        QVERIFY2(layer->createDataObject(SWMMModelLayer::DataPatterns, QStringLiteral("Pattern1"),
                                         {}, &err),
                 qPrintable(err));
        drainEventLoop();

        QCOMPARE(layer->dataObjectCount(SWMMModelLayer::DataPatterns), 1);
        QVERIFY(model.topRowForDataCategory(SWMMModelLayer::DataPatterns) >= 0);
    }

    // 6. createDataObject with a LIVE registry: the engine add must be
    //    mirrored into the registry so registry-preferred counts include it.
    /** @brief Verify engine-facing series creation mirrors into the bound registry and counts it exactly once. */
    void createDataObjectMirrorsIntoLiveRegistry()
    {
        auto layer = openFixtureLayer();
        QVERIFY(layer != nullptr);

        auto *reg = qobject_cast<TimeseriesRegistry *>(layer->ensureTimeseriesRegistry());
        QVERIFY(reg != nullptr);
        QCOMPARE(reg->providerCount(), 1);

        QString err;
        QVERIFY2(layer->createDataObject(SWMMModelLayer::DataTimeSeries,
                                         QStringLiteral("TS_ENGINE"), {}, &err),
                 qPrintable(err));

        QCOMPARE(reg->providerCount(), 2);
        QVERIFY(reg->findByName(QStringLiteral("TS_ENGINE")) != nullptr);
        QCOMPARE(layer->dataObjectCount(SWMMModelLayer::DataTimeSeries), 2);
    }
};

QTEST_MAIN(TestObjectBrowserTreeRefresh)
#include "test_objectbrowser_tree_refresh.moc"
