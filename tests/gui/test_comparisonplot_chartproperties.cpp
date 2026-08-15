/*!
 * \file   test_comparisonplot_chartproperties.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AT.3 — pins ChartProperties' wrapper semantics:
 *   - Y-range setters apply to the chart's first vertical axis
 *   - Gridline toggles propagate to QAbstractAxis::isGridLineVisible
 *   - Title text / font setters apply to QChart
 *   - Chart theme enum round-trips
 *   - The wrapper survives chart deletion (setters become no-ops)
 *
 * Self-contained: links chartproperties.cpp + Qt6 Charts.
 */
#include "plot/chartproperties.h"
#include "ui/dialogs/chartpropertiesdialog.h"

#include <qpropertyitemdelegate.h>
#include <qpropertymodel.h>

#include <QChart>
#include <QFont>
#include <QLineSeries>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QTreeView>
#include <QValueAxis>

using openswmmvis::plot::ChartProperties;
using openswmmvis::plot::NumberFormat;
using openswmmvis::plot::NumberFormatMode;

class TestComparisonPlotChartProperties : public QObject
{
    Q_OBJECT
private slots:
    void yRangeSettersApplyToAxis();
    void gridlineTogglesPropagate();
    void titleTextRoundTrips();
    void chartThemeEnumRoundTrips();
    void setterAreNoOpsAfterChartDeleted();
    void dialogUsesQPropertyModelDelegate();
    void statisticsNumberFormatRoundTripsThroughPropertySurface();

private:
    QChart *makeChart()
    {
        auto *chart = new QChart;
        auto *series = new QLineSeries;
        series->append(0.0, 0.0);
        series->append(1.0, 10.0);
        chart->addSeries(series);
        auto *xAxis = new QValueAxis;  xAxis->setRange(0.0, 1.0);
        auto *yAxis = new QValueAxis;  yAxis->setRange(0.0, 10.0);
        chart->addAxis(xAxis, Qt::AlignBottom);
        chart->addAxis(yAxis, Qt::AlignLeft);
        series->attachAxis(xAxis);
        series->attachAxis(yAxis);
        return chart;
    }
};

void TestComparisonPlotChartProperties::yRangeSettersApplyToAxis()
{
    auto *chart = makeChart();
    ChartProperties props(chart);

    QSignalSpy minSpy(&props, &ChartProperties::yMinChanged);
    QSignalSpy maxSpy(&props, &ChartProperties::yMaxChanged);

    props.setYMin(2.5);
    props.setYMax(7.5);
    QCOMPARE(props.yMin(), 2.5);
    QCOMPARE(props.yMax(), 7.5);

    auto *yAxis = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
    QVERIFY(yAxis);
    QCOMPARE(yAxis->min(), 2.5);
    QCOMPARE(yAxis->max(), 7.5);

    QCOMPARE(minSpy.count(), 1);
    QCOMPARE(maxSpy.count(), 1);
    delete chart;
}

void TestComparisonPlotChartProperties::gridlineTogglesPropagate()
{
    auto *chart = makeChart();
    ChartProperties props(chart);

    props.setXGridVisible(false);
    props.setYGridVisible(false);
    QVERIFY(!chart->axes(Qt::Horizontal).first()->isGridLineVisible());
    QVERIFY(!chart->axes(Qt::Vertical).first()->isGridLineVisible());

    props.setXGridVisible(true);
    props.setYGridVisible(true);
    QVERIFY(chart->axes(Qt::Horizontal).first()->isGridLineVisible());
    QVERIFY(chart->axes(Qt::Vertical).first()->isGridLineVisible());
    delete chart;
}

void TestComparisonPlotChartProperties::titleTextRoundTrips()
{
    auto *chart = makeChart();
    ChartProperties props(chart);

    QSignalSpy spy(&props, &ChartProperties::titleTextChanged);
    props.setTitleText(QStringLiteral("Custom"));
    QCOMPARE(props.titleText(), QStringLiteral("Custom"));
    QCOMPARE(chart->title(), QStringLiteral("Custom"));
    QCOMPARE(spy.count(), 1);

    // Same value — no signal.
    props.setTitleText(QStringLiteral("Custom"));
    QCOMPARE(spy.count(), 1);
    delete chart;
}

void TestComparisonPlotChartProperties::chartThemeEnumRoundTrips()
{
    auto *chart = makeChart();
    ChartProperties props(chart);

    const int dark = static_cast<int>(QChart::ChartThemeDark);
    props.setChartTheme(dark);
    QCOMPARE(props.chartTheme(), dark);
    QCOMPARE(static_cast<int>(chart->theme()), dark);
    delete chart;
}

void TestComparisonPlotChartProperties::setterAreNoOpsAfterChartDeleted()
{
    auto *chart = makeChart();
    ChartProperties props(chart);
    delete chart;

    // QPointer goes null; setters must not crash.
    props.setYMin(99.0);
    props.setYMax(101.0);
    props.setTitleText(QStringLiteral("x"));
    QCOMPARE(props.chart(), nullptr);
}

void TestComparisonPlotChartProperties::dialogUsesQPropertyModelDelegate()
{
    auto *chart = makeChart();
    openswmmvis::ui::ChartPropertiesDialog dlg(new ChartProperties(chart));

    auto *tree = dlg.findChild<QTreeView *>();
    QVERIFY(tree);
    QVERIFY(qobject_cast<QPropertyModel *>(tree->model()));
    QVERIFY(qobject_cast<QPropertyItemDelegate *>(tree->itemDelegate()));

    delete chart;
}

void TestComparisonPlotChartProperties::statisticsNumberFormatRoundTripsThroughPropertySurface()
{
    auto *chart = makeChart();
    ChartProperties props(chart);

    // The mode enum + free integer precision were collapsed into ONE
    // dropdown: "4" on its own never said whether it meant decimals or
    // significant figures. Mode/precision remain the internal representation
    // and stay callable, they are simply no longer separate grid rows.
    QVERIFY(props.metaObject()->indexOfProperty("statisticsFormatPreset") >= 0);
    QVERIFY(props.metaObject()->indexOfProperty("statisticsFormatMode") < 0);
    QVERIFY(props.metaObject()->indexOfProperty("statisticsPrecision") < 0);
    QVERIFY(props.metaObject()->indexOfProperty("statisticsFormat") >= 0);
    QCOMPARE(props.displayLabelFor(QStringLiteral("statisticsFormatPreset")),
             QStringLiteral("Statistics — Number format"));

    // The combined property drives both stored fields...
    props.setStatisticsFormatPreset(ChartProperties::SigFigs4);
    QCOMPARE(static_cast<int>(props.statisticsFormatMode()),
             static_cast<int>(ChartProperties::SignificantFigures));
    QCOMPARE(props.statisticsPrecision(), 4);
    // ...and reading it back reports the preset that pair stands for.
    QCOMPARE(props.statisticsFormatPreset(), ChartProperties::SigFigs4);

    props.setStatisticsFormatMode(ChartProperties::SignificantFigures);
    props.setStatisticsPrecision(4);
    props.setStatisticsFormat(QStringLiteral("%.3g ft"));

    auto format = props.statisticsNumberFormat();
    QCOMPARE(static_cast<int>(format.mode),
             static_cast<int>(NumberFormatMode::SignificantFigures));
    QCOMPARE(format.count, 4);
    QCOMPARE(format.custom, QStringLiteral("%.3g ft"));

    props.setStatisticsNumberFormat(
        NumberFormat{NumberFormatMode::Decimals, 2, QStringLiteral("%.2f")});
    format = props.statisticsNumberFormat();
    QCOMPARE(static_cast<int>(format.mode),
             static_cast<int>(NumberFormatMode::Decimals));
    QCOMPARE(format.count, 2);
    QCOMPARE(format.custom, QStringLiteral("%.2f"));

    delete chart;
}

QTEST_MAIN(TestComparisonPlotChartProperties)
#include "test_comparisonplot_chartproperties.moc"
