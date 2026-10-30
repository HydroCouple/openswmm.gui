/**
 * @file      test_comparisonplot_pairs.cpp
 * @brief     Tests for comparisonplot pairs.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5 — pins the ComparisonPair
 * surface on ComparisonPlotModel:
 *   - addPair validation (bad indices, self-pairs, cross-attribute pairs,
 *     duplicates) and pairsChanged emission,
 *   - removePair / clearPairs,
 *   - pair reindexing when a series is removed (drop referencing pairs,
 *     shift higher indices),
 *   - pair cleanup when a run source is removed.
 *
 * Self-contained: links comparisonplotmodel.cpp + a stub IRunLayer.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/comparisonplotmodel.h"
#include "plot/irunlayer.h"
#include "plot/plotattribute.h"

#include <QObject>
#include <QPair>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace openswmmvis::plot;

class StubRunLayer : public IRunLayer
{
public:
    /** @brief Name the synthetic empty run.
     * @return Stub. */
    QString scenarioName() const override { return QStringLiteral("Stub"); }
    /** @brief Identify the stub series units.
     * @return SI. */
    UnitSystem unitSystem() const override { return UnitSystem::SI; }
    /** @brief Expose the empty stub start timestamp.
     * @return Zero Julian days. */
    double startDateJulian() const override { return 0.0; }
    /** @brief Expose the stub report count.
     * @return Zero periods. */
    int periodCount() const override { return 0; }
    /** @brief Expose the empty stub report interval.
     * @return Zero seconds. */
    int reportStepSeconds() const override { return 0; }
    /** @brief Reject every series request with the fixed stub error.
     * The unnamed object and attribute arguments are ignored.
     * @param[out] out Required series object whose success flag becomes false and error becomes stub; other fields are left unchanged. */
    void getSeriesAt(const ObjectRef &, PlotAttribute, SeriesData &out) const override
    {
        out.ok = false;
        out.errorMessage = QStringLiteral("stub");
    }
};

class TestComparisonPlotPairs : public QObject
{
    Q_OBJECT
private slots:
    void batchAdditionEmitsOnce();
    void addPairValidates();
    void addPairEmitsAndStores();
    void removePairRemoves();
    void clearPairsResetsToAuto();
    void removeSeriesReindexesPairs();
    void removeRunSourceDropsReferencingPairs();

private:
    /*! Two runs × series listed per (run, attr) in the given order. */
    std::unique_ptr<ComparisonPlotModel>
    makeModel(const QVector<QPair<int, PlotAttribute>> &seriesSpecs);
};

std::unique_ptr<ComparisonPlotModel>
/** @brief Build a uniquely owned model with two stub runs and ordered J1 series.
 * @param[in] seriesSpecs Pairs of zero-based run index (0 or 1) and plot attribute to add; add failures are not checked in this helper.
 * @return New model owning both shared run sources and accepted series. */
TestComparisonPlotPairs::makeModel(const QVector<QPair<int, PlotAttribute>> &seriesSpecs)
{
    auto model = std::make_unique<ComparisonPlotModel>();
    for (int r = 0; r < 2; ++r) {
        RunSource rs;
        rs.layer = std::make_shared<StubRunLayer>();
        rs.label = QStringLiteral("Run %1").arg(r);
        model->addRunSource(std::move(rs));
    }
    for (const auto &p : seriesSpecs) {
        SeriesSpec spec;
        spec.runIndex = p.first;
        spec.objectRef = ObjectRef::forNode(QStringLiteral("J1"));
        spec.attribute = p.second;
        model->addSeries(std::move(spec));
    }
    return model;
}

/** @brief Verify adding fifty mesh-depth series emits one batch and one row signal, no individual-add signal, and one attribute row.
 * An empty batch returns zero without emitting again. */
void TestComparisonPlotPairs::batchAdditionEmitsOnce()
{
    auto model = makeModel({});
    QSignalSpy single(model.get(), &ComparisonPlotModel::seriesAdded);
    QSignalSpy batch(model.get(), &ComparisonPlotModel::seriesBatchAdded);
    QSignalSpy rows(model.get(), &ComparisonPlotModel::rowsChanged);
    QVector<SeriesSpec> specs;
    for (int i = 0; i < 50; ++i) {
        SeriesSpec s;
        s.runIndex = 0;
        s.objectRef = ObjectRef::forMesh2DCell(i);
        s.attribute = PlotAttribute::Mesh2DDepth;
        specs.append(s);
    }
    QCOMPARE(model->addSeriesBatch(specs), 50);
    QCOMPARE(model->seriesCount(), 50);
    QCOMPARE(single.count(), 0);
    QCOMPARE(batch.count(), 1);
    QCOMPARE(rows.count(), 1);
    QCOMPARE(model->rows().size(), 1);
    QCOMPARE(model->rows()[0].seriesIndices.size(), 50);
    QCOMPARE(model->spec(49).objectRef.triIdx, 49);
    QCOMPARE(model->addSeriesBatch({}), 0);
    QCOMPARE(batch.count(), 1);
}

/** @brief Verify invalid indices, self-pairing and mixed attributes reject pairing without changing pairs or emitting. */
void TestComparisonPlotPairs::addPairValidates()
{
    // Series 0/1 = NodeDepth, series 2 = LinkFlow.
    auto model = makeModel({{0, PlotAttribute::NodeDepth},
                            {1, PlotAttribute::NodeDepth},
                            {1, PlotAttribute::LinkFlow}});
    QSignalSpy spy(model.get(), &ComparisonPlotModel::pairsChanged);

    QCOMPARE(model->addPair({-1, 0}), -1); // bad X index
    QCOMPARE(model->addPair({0, 99}), -1); // bad Y index
    QCOMPARE(model->addPair({1, 1}), -1);  // self-pair
    QCOMPARE(model->addPair({0, 2}), -1);  // cross-attribute
    QCOMPARE(spy.count(), 0);              // rejected adds don't emit
    QVERIFY(model->pairs().isEmpty());
}

/** @brief Verify a valid pair stores its indices and emits once, duplicates are refused and the reversed pair is distinct. */
void TestComparisonPlotPairs::addPairEmitsAndStores()
{
    auto model = makeModel({{0, PlotAttribute::NodeDepth}, {1, PlotAttribute::NodeDepth}});
    QSignalSpy spy(model.get(), &ComparisonPlotModel::pairsChanged);

    QCOMPARE(model->addPair({0, 1}), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model->pairs().size(), 1);
    QCOMPARE(model->pairs().first().xSeriesIndex, 0);
    QCOMPARE(model->pairs().first().ySeriesIndex, 1);

    QCOMPARE(model->addPair({0, 1}), -1); // duplicate
    QCOMPARE(spy.count(), 1);

    QCOMPARE(model->addPair({1, 0}), 1); // reversed = distinct pair
    QCOMPARE(spy.count(), 2);
}

/** @brief Verify removing a valid pair emits once while an out-of-range removal is silent. */
void TestComparisonPlotPairs::removePairRemoves()
{
    auto model = makeModel({{0, PlotAttribute::NodeDepth}, {1, PlotAttribute::NodeDepth}});
    model->addPair({0, 1});
    model->addPair({1, 0});
    QSignalSpy spy(model.get(), &ComparisonPlotModel::pairsChanged);

    model->removePair(0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model->pairs().size(), 1);
    QCOMPARE(model->pairs().first().xSeriesIndex, 1);

    model->removePair(5); // out of range — no-op
    QCOMPARE(spy.count(), 1);
}

/** @brief Verify clearing populated pairs removes them and emits, while clearing an already empty list is silent. */
void TestComparisonPlotPairs::clearPairsResetsToAuto()
{
    auto model = makeModel({{0, PlotAttribute::NodeDepth}, {1, PlotAttribute::NodeDepth}});
    QSignalSpy spy(model.get(), &ComparisonPlotModel::pairsChanged);

    model->clearPairs(); // already empty — no emit
    QCOMPARE(spy.count(), 0);

    model->addPair({0, 1});
    model->clearPairs();
    QCOMPARE(spy.count(), 2); // add + clear
    QVERIFY(model->pairs().isEmpty());
}

/** @brief Verify removing a series drops pairs referencing it and shifts surviving higher indices with one notification. */
void TestComparisonPlotPairs::removeSeriesReindexesPairs()
{
    // Series 0,1,2 all NodeDepth: pairs (0,2) and (1,2).
    auto model = makeModel({{0, PlotAttribute::NodeDepth},
                            {1, PlotAttribute::NodeDepth},
                            {1, PlotAttribute::NodeDepth}});
    model->addPair({0, 2});
    model->addPair({1, 2});
    QSignalSpy spy(model.get(), &ComparisonPlotModel::pairsChanged);

    // Removing series 1 drops the (1,2) pair and shifts (0,2) → (0,1).
    model->removeSeries(1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model->pairs().size(), 1);
    QCOMPARE(model->pairs().first().xSeriesIndex, 0);
    QCOMPARE(model->pairs().first().ySeriesIndex, 1);
}

/** @brief Verify deleting a run source removes its referencing comparison pair and emits once. */
void TestComparisonPlotPairs::removeRunSourceDropsReferencingPairs()
{
    // Series 0 (run 0) + series 1 (run 1), paired. Removing run 1 removes
    // series 1, which must drop the pair.
    auto model = makeModel({{0, PlotAttribute::NodeDepth}, {1, PlotAttribute::NodeDepth}});
    model->addPair({0, 1});
    QSignalSpy spy(model.get(), &ComparisonPlotModel::pairsChanged);

    model->removeRunSource(1);
    QCOMPARE(spy.count(), 1);
    QVERIFY(model->pairs().isEmpty());
}

QTEST_MAIN(TestComparisonPlotPairs)
#include "test_comparisonplot_pairs.moc"
