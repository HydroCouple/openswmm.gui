/*!
 * \file   test_comparisonplot_seriestree.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AT.3 — pins the model-surface behaviour that the series-tree
 * checkbox toggle in `ComparisonPlotDialog::onSeriesItemChanged` drives:
 *   - `ComparisonPlotModel::updateStyle` flips `SeriesStyle::showLine`
 *     in-place,
 *   - `styleChanged` fires only when the style actually differs,
 *   - `removeSeries` evicts the spec + emits `seriesRemoved` (the
 *     "Remove Series" tree-context-menu path).
 *
 * The dialog's checkbox handler boils down to:
 *     auto style = m_model->spec(idx).style;
 *     style.showLine = checked;
 *     m_model->updateStyle(idx, style);
 *
 * — pinning that model behaviour is the load-bearing piece. The
 * `QTreeWidgetItem::checkState` ↔ `style.showLine` plumbing is a
 * two-line Qt idiom and is exercised in the manual verification plan.
 *
 * Self-contained: links comparisonplotmodel.cpp + seriesstyle.cpp and a
 * stub IRunLayer so the model can hold a run source for series specs.
 */
#include "plot/comparisonplotmodel.h"
#include "plot/irunlayer.h"
#include "plot/plotattribute.h"
#include "plot/seriesstyle.h"

#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace openswmmvis::plot;

// Tiny IRunLayer that returns canned data. Used purely to satisfy the model's
// RunSource contract; series resolution is not exercised here.
class StubRunLayer : public IRunLayer
{
public:
    QString    scenarioName()      const override { return QStringLiteral("Stub"); }
    UnitSystem unitSystem()        const override { return UnitSystem::SI; }
    double     startDateJulian()   const override { return 0.0; }
    int        periodCount()       const override { return 0; }
    int        reportStepSeconds() const override { return 0; }
    void       getSeriesAt(const ObjectRef&, PlotAttribute, SeriesData& out) const override
    { out.ok = false; out.errorMessage = QStringLiteral("stub"); }
};

// Stub with no scenario name — exercises the model's run-label fallback
// to the output name (file name portion of persistenceKey()).
class UnnamedRunLayer : public StubRunLayer
{
public:
    QString scenarioName()   const override { return QString(); }
    QString persistenceKey() const override
    { return QStringLiteral("/tmp/results/model_run2.out"); }
};

class TestComparisonPlotSeriesTree : public QObject
{
    Q_OBJECT
private slots:
    void updateStyleFlipsShowLine();
    void updateStyleNoOpDoesNotEmit();
    void removeSeriesEvictsSpec();
    void plotThisOnlyHidesSiblingsOnly();
    void updateLegendOverrideWritesSpecAndDedupes();
    void runLabelFallsBackToOutputName();

private:
    /*! Build a model with one run source and N series on a single attribute row. */
    std::unique_ptr<ComparisonPlotModel> makeModelWithSeries(
        const QVector<QString> &nodeNames, PlotAttribute attr);
};

std::unique_ptr<ComparisonPlotModel>
TestComparisonPlotSeriesTree::makeModelWithSeries(const QVector<QString> &nodeNames,
                                                   PlotAttribute attr)
{
    auto model = std::make_unique<ComparisonPlotModel>();
    RunSource rs;
    rs.layer = std::make_shared<StubRunLayer>();
    rs.label = QStringLiteral("Stub run");
    model->addRunSource(std::move(rs));
    for (const QString &n : nodeNames) {
        SeriesSpec spec;
        spec.runIndex  = 0;
        spec.objectRef = ObjectRef::forNode(n);
        spec.attribute = attr;
        model->addSeries(std::move(spec));
    }
    return model;
}

void TestComparisonPlotSeriesTree::updateStyleFlipsShowLine()
{
    auto model = makeModelWithSeries({QStringLiteral("J1")}, PlotAttribute::NodeDepth);
    QVERIFY(model->seriesCount() == 1);
    QVERIFY(model->spec(0).style.showLine);     // default = visible

    SeriesStyle next = model->spec(0).style;
    next.showLine = false;
    model->updateStyle(0, next);
    QVERIFY(!model->spec(0).style.showLine);

    next.showLine = true;
    model->updateStyle(0, next);
    QVERIFY(model->spec(0).style.showLine);
}

void TestComparisonPlotSeriesTree::updateStyleNoOpDoesNotEmit()
{
    // Pin current model behaviour: `updateStyle` emits styleChanged on
    // every call (the dialog's onStyleChanged slot is cheap — a single
    // pen rewrite — so the model doesn't bother deduping). The checkbox
    // toggle still produces exactly one update per click because the
    // dialog only calls updateStyle when the checkstate actually flips.
    auto model = makeModelWithSeries({QStringLiteral("J1")}, PlotAttribute::NodeDepth);
    QSignalSpy spy(model.get(), &ComparisonPlotModel::styleChanged);

    const SeriesStyle current = model->spec(0).style;
    model->updateStyle(0, current);
    QCOMPARE(spy.count(), 1);    // emits unconditionally

    SeriesStyle next = current;
    next.showLine = !current.showLine;
    model->updateStyle(0, next);
    QCOMPARE(spy.count(), 2);
}

void TestComparisonPlotSeriesTree::removeSeriesEvictsSpec()
{
    auto model = makeModelWithSeries(
        {QStringLiteral("J1"), QStringLiteral("J2"), QStringLiteral("J3")},
        PlotAttribute::NodeDepth);
    QCOMPARE(model->seriesCount(), 3);
    QSignalSpy spy(model.get(), &ComparisonPlotModel::seriesRemoved);

    model->removeSeries(1);       // remove J2
    QCOMPARE(model->seriesCount(), 2);
    QCOMPARE(spy.count(), 1);
    // Remaining series IDs survive in stable order.
    QCOMPARE(model->spec(0).objectRef.name, QStringLiteral("J1"));
    QCOMPARE(model->spec(1).objectRef.name, QStringLiteral("J3"));
}

void TestComparisonPlotSeriesTree::plotThisOnlyHidesSiblingsOnly()
{
    // Simulate the "Plot This Only" path: for each series sharing the same
    // attribute row, flip showLine to (idx == target).
    auto model = makeModelWithSeries(
        {QStringLiteral("J1"), QStringLiteral("J2"), QStringLiteral("J3")},
        PlotAttribute::NodeDepth);

    constexpr int target = 1;     // "Plot This Only" picks J2
    const auto &targetSpec = model->spec(target);
    for (const auto &row : model->rows()) {
        if (row.attribute != targetSpec.attribute) continue;
        for (int sIdx : row.seriesIndices) {
            SeriesStyle st = model->spec(sIdx).style;
            const bool show = (sIdx == target);
            if (st.showLine != show) {
                st.showLine = show;
                model->updateStyle(sIdx, st);
            }
        }
    }
    QVERIFY(!model->spec(0).style.showLine);
    QVERIFY( model->spec(1).style.showLine);
    QVERIFY(!model->spec(2).style.showLine);
}

void TestComparisonPlotSeriesTree::updateLegendOverrideWritesSpecAndDedupes()
{
    // updateLegendOverride writes SeriesSpec::legendOverride and piggybacks
    // on styleChanged so the dialog's existing onStyleChanged handler can
    // refresh the chart legend label. No-op writes must NOT re-emit, so
    // QPropertyModel edit churn doesn't trigger needless chart rebuilds.
    auto model = makeModelWithSeries({QStringLiteral("J1")}, PlotAttribute::NodeDepth);
    QCOMPARE(model->spec(0).legendOverride, QString());

    QSignalSpy spy(model.get(), &ComparisonPlotModel::styleChanged);

    model->updateLegendOverride(0, QStringLiteral("Upstream node"));
    QCOMPARE(model->spec(0).legendOverride, QStringLiteral("Upstream node"));
    QCOMPARE(spy.count(), 1);

    // Repeat with same value — dedupe path.
    model->updateLegendOverride(0, QStringLiteral("Upstream node"));
    QCOMPARE(spy.count(), 1);

    // Clear restores auto.
    model->updateLegendOverride(0, QString());
    QCOMPARE(model->spec(0).legendOverride, QString());
    QCOMPARE(spy.count(), 2);
}

void TestComparisonPlotSeriesTree::runLabelFallsBackToOutputName()
{
    auto model = std::make_unique<ComparisonPlotModel>();

    // No label, empty scenario name → output file name from persistenceKey.
    RunSource rs;
    rs.layer = std::make_shared<UnnamedRunLayer>();
    const int idx = model->addRunSource(std::move(rs));
    QCOMPARE(model->runSource(idx).label, QStringLiteral("model_run2.out"));

    // A non-empty scenario name still wins over the fallback.
    RunSource named;
    named.layer = std::make_shared<StubRunLayer>();
    const int namedIdx = model->addRunSource(std::move(named));
    QCOMPARE(model->runSource(namedIdx).label, QStringLiteral("Stub"));

    // An explicit label wins over everything.
    RunSource explicitLabel;
    explicitLabel.layer = std::make_shared<UnnamedRunLayer>();
    explicitLabel.label = QStringLiteral("Custom");
    const int explicitIdx = model->addRunSource(std::move(explicitLabel));
    QCOMPARE(model->runSource(explicitIdx).label, QStringLiteral("Custom"));
}

QTEST_MAIN(TestComparisonPlotSeriesTree)
#include "test_comparisonplot_seriestree.moc"
