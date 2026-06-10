/*!
 * \file   test_comparisonplot_scatter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 2 — pins the 1v1 scatter pairing
 * that ComparisonPlotDialog::rebuildCharts builds per attribute row:
 *   - `pairSamplesNearest` pairs samples on identical timesteps,
 *   - single-sample streams still pair on exact timestamps (the legacy
 *     walk degenerated to halfStep = 0 → exact double equality, which
 *     produced silently-empty scatters),
 *   - differing report steps pair on common timestamps only (tolerance =
 *     half the SMALLER step, so a coarse baseline can't mis-pair against
 *     off-step fine-run samples),
 *   - non-finite values are dropped,
 *   - disjoint time ranges produce an empty pairing,
 *   - the dialog's per-row pairing recipe (group by attribute row, index
 *     baseline by objectRef, pair every comparison series) yields pairs
 *     for EVERY row — the second-row regression.
 *
 * Self-contained: links seriespairing.cpp + comparisonplotmodel.cpp and a
 * stub IRunLayer; no dialog/widget linkage (the dialog pulls GDAL-heavy
 * layer deps).
 */
#include "plot/comparisonplotmodel.h"
#include "plot/irunlayer.h"
#include "plot/plotattribute.h"
#include "plot/seriespairing.h"

#include <QObject>
#include <QTest>

#include <cmath>
#include <limits>
#include <memory>

using namespace openswmmvis::plot;

// Stub layer producing v = base + k at t = t0 + k·step for every requested
// object/attribute, so pairing outcomes are exactly predictable.
class RampRunLayer : public IRunLayer
{
public:
    RampRunLayer(QString name, double t0, double step, int n, double base)
        : m_name(std::move(name)), m_t0(t0), m_step(step), m_n(n), m_base(base) {}

    QString    scenarioName()      const override { return m_name; }
    UnitSystem unitSystem()        const override { return UnitSystem::SI; }
    double     startDateJulian()   const override { return m_t0; }
    int        periodCount()       const override { return m_n; }
    int        reportStepSeconds() const override
    { return static_cast<int>(m_step * 86400.0); }

    void getSeriesAt(const ObjectRef&, PlotAttribute, SeriesData& out) const override
    {
        out.timesJulian.clear();
        out.values.clear();
        for (int k = 0; k < m_n; ++k) {
            out.timesJulian.push_back(m_t0 + k * m_step);
            out.values.push_back(m_base + k);
        }
        out.ok = true;
    }

private:
    QString m_name;
    double  m_t0;
    double  m_step;
    int     m_n;
    double  m_base;
};

class TestComparisonPlotScatter : public QObject
{
    Q_OBJECT
private slots:
    void identicalStepsPairAllSamples();
    void singleSampleStreamsPairOnExactTimestamp();
    void differingStepsPairCommonTimestampsOnly();
    void nonFiniteValuesAreDropped();
    void disjointRangesYieldEmptyPairing();
    void everyAttributeRowProducesPairs();
};

void TestComparisonPlotScatter::identicalStepsPairAllSamples()
{
    const std::vector<double> t  = {0.0, 0.25, 0.5, 0.75};
    const std::vector<double> bv = {1.0, 2.0, 3.0, 4.0};
    const std::vector<double> cv = {1.5, 2.5, 3.5, 4.5};

    const PairedSamples ps = pairSamplesNearest(t, bv, t, cv);
    QCOMPARE(ps.x.size(), std::size_t(4));
    QCOMPARE(ps.x[2], 3.0);
    QCOMPARE(ps.y[2], 3.5);
}

void TestComparisonPlotScatter::singleSampleStreamsPairOnExactTimestamp()
{
    // Regression: with <2 baseline samples the legacy walk used
    // halfStep = 0 → required exact double equality. Exact timestamps
    // must still pair via the epsilon fallback.
    const PairedSamples ps = pairSamplesNearest({1.5}, {10.0}, {1.5}, {20.0});
    QCOMPARE(ps.x.size(), std::size_t(1));
    QCOMPARE(ps.x[0], 10.0);
    QCOMPARE(ps.y[0], 20.0);
}

void TestComparisonPlotScatter::differingStepsPairCommonTimestampsOnly()
{
    // Baseline hourly (step 1/24), comparison every 15 min (step 1/96):
    // only top-of-hour samples coincide. Tolerance = half the smaller
    // step, so off-hour fine-run samples must NOT pair.
    std::vector<double> bt, bv, ct, cv;
    for (int k = 0; k < 4; ++k)  { bt.push_back(k / 24.0); bv.push_back(k); }
    for (int k = 0; k < 13; ++k) { ct.push_back(k / 96.0); cv.push_back(100.0 + k); }

    const PairedSamples ps = pairSamplesNearest(bt, bv, ct, cv);
    QCOMPARE(ps.x.size(), std::size_t(4));
    for (std::size_t k = 0; k < ps.x.size(); ++k) {
        QCOMPARE(ps.x[k], double(k));
        QCOMPARE(ps.y[k], 100.0 + 4.0 * k);   // every 4th fine sample
    }
}

void TestComparisonPlotScatter::nonFiniteValuesAreDropped()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> t  = {0.0, 0.25, 0.5};
    const PairedSamples ps = pairSamplesNearest(t, {1.0, nan, 3.0},
                                                t, {4.0, 5.0, 6.0});
    QCOMPARE(ps.x.size(), std::size_t(2));
    QCOMPARE(ps.x[1], 3.0);
    QCOMPARE(ps.y[1], 6.0);
}

void TestComparisonPlotScatter::disjointRangesYieldEmptyPairing()
{
    const PairedSamples ps = pairSamplesNearest({0.0, 0.25}, {1.0, 2.0},
                                                {10.0, 10.25}, {3.0, 4.0});
    QVERIFY(ps.x.empty());
}

void TestComparisonPlotScatter::everyAttributeRowProducesPairs()
{
    // Second-row regression: 2 runs × 2 attribute rows, same object and
    // timestep. Mirrors the dialog's rebuildCharts recipe — index baseline
    // series by objectRef per row, then pair every comparison series.
    // Both rows must produce non-empty pairings.
    ComparisonPlotModel model;

    RunSource baseline;
    baseline.layer = std::make_shared<RampRunLayer>(
        QStringLiteral("baseline.out"), 0.0, 0.25, 8, 0.0);
    const int baseIdx = model.addRunSource(std::move(baseline));   // run 0 = default baseline

    RunSource comparison;
    comparison.layer = std::make_shared<RampRunLayer>(
        QStringLiteral("scenario.out"), 0.0, 0.25, 8, 50.0);
    model.addRunSource(std::move(comparison));

    const QVector<PlotAttribute> attrs = {PlotAttribute::NodeDepth,
                                          PlotAttribute::NodeTotalInflow};
    for (PlotAttribute a : attrs) {
        for (int run = 0; run < 2; ++run) {
            SeriesSpec spec;
            spec.runIndex  = run;
            spec.objectRef = ObjectRef::forNode(QStringLiteral("J1"));
            spec.attribute = a;
            QVERIFY(model.addSeries(std::move(spec)) >= 0);
        }
    }

    QCOMPARE(model.rows().size(), 2);
    QCOMPARE(model.baselineRunIndex(), baseIdx);

    for (const AttributeRow &row : model.rows()) {
        // First pass — baseline columns keyed by objectRef (dialog recipe).
        QMap<QString, SeriesData> baseByObj;
        for (int sIdx : row.seriesIndices) {
            const SeriesSpec &spec = model.spec(sIdx);
            if (spec.runIndex != baseIdx) continue;
            SeriesData data;
            model.resolveSeries(sIdx, data);
            QVERIFY(data.ok);
            baseByObj[spec.objectRef.name] = std::move(data);
        }
        QVERIFY(!baseByObj.isEmpty());

        // Second pass — every comparison series must pair.
        int paired = 0;
        for (int sIdx : row.seriesIndices) {
            const SeriesSpec &spec = model.spec(sIdx);
            if (spec.runIndex == baseIdx) continue;
            auto baseIt = baseByObj.find(spec.objectRef.name);
            QVERIFY(baseIt != baseByObj.end());
            SeriesData data;
            model.resolveSeries(sIdx, data);
            QVERIFY(data.ok);
            const PairedSamples ps = pairSamplesNearest(
                baseIt.value().timesJulian, baseIt.value().values,
                data.timesJulian, data.values);
            QCOMPARE(ps.x.size(), std::size_t(8));
            ++paired;
        }
        QCOMPARE(paired, 1);
    }
}

QTEST_MAIN(TestComparisonPlotScatter)
#include "test_comparisonplot_scatter.moc"
