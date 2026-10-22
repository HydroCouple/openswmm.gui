/**
 * @file      test_rainfallvisualizationdialog.cpp
 * @brief     Rainfall Visualization dialog — per-gage series assembly across all
 *         three source kinds, failed-file surfacing, basis switching, and the
 *         per-gage panels' shared time axis.
 *
 * Fixture: `gage_assignment_files.inp` — one TIMESERIES gage, one standard
 * rain-file (.dat, station) gage, one multi-column CSV gage, with the data
 * files alongside (`gage_files_rain.dat` / `gage_files_rain.csv`).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "dialog_a11y_checks.h"
#include "layers/swmmmodellayer.h"
#include "plot/rainfallseriesmodel.h"
#include "ui/dialogs/rainfallvisualizationdialog.h"
#include "ui/properties/swmmraingagepropertyadapter.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>

#include <QChart>
#include <QComboBox>
#include <QDialogButtonBox>
#include "plot/utctimeaxis.h"
#include <QDir>
#include <QLineSeries>
#include <QObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <cmath>

using openswmmvis::plot::RainfallSeriesModel;
using openswmmvis::ui::RainfallVisualizationDialog;

namespace
{

/** @brief Resolve rainfall input fixtures.
 * @return SWMMVIS_GUI_TEST_DATA or "." when unset. */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Load an independent owned rainfall model.
 * @param[in] inp Model filename/path joined under the fixture directory.
 * @return Unique loaded layer or nullptr on failure, discarding local diagnostics. */
std::unique_ptr<SWMMModelLayer> openLayer(const QString &inp)
{
    auto layer = std::make_unique<SWMMModelLayer>(QDir(dataDir()).filePath(inp), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/** @brief Count line-series objects attached to a chart.
 * @param[in] c Required borrowed chart.
 * @return Number of QLineSeries instances; other series types are ignored. */
int lineSeriesCount(QChart *c)
{
    int n = 0;
    for (auto *s : c->series())
        if (qobject_cast<QLineSeries *>(s))
            ++n;
    return n;
}

/*! Check every gage's visibility box in the stats table (default is
 *  only-the-first-gage; comparison tests want all series plotted). */
/** @brief Enable all gage visibility checkboxes and allow ten milliseconds for chart updates.
 * @param[in,out] dlg Dialog expected to contain its statistics table and column-zero items; a missing table records a QtTest failure and returns from this helper. */
void checkAllGages(RainfallVisualizationDialog &dlg)
{
    auto *statsTable = dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
    QVERIFY(statsTable);
    for (int r = 0; r < statsTable->rowCount(); ++r)
        statsTable->item(r, 0)->setCheckState(Qt::Checked);
    QTest::qWait(10);
}

} // namespace

class TestRainfallVisualizationDialog : public QObject
{
    Q_OBJECT

private slots:

    void gageFactorEditsSurviveSave_data()
    {
        QTest::addColumn<QString>("gage");
        QTest::addColumn<bool>("defaultColumn");
        QTest::addColumn<double>("rainScale");
        QTest::addColumn<double>("snowCatch");
        for (const auto &gage : {"TS_NORTH", "STD_EAST", "CSV_SOUTH"}) {
            QTest::newRow(gage) << QString::fromLatin1(gage) << false << 10.0 << 10.0;
            QTest::newRow(qPrintable(QString::fromLatin1(gage) + "-precision"))
                << QString::fromLatin1(gage) << false << 1.23456789 << 0.00123456789;
        }
        QTest::newRow("CSV-default-column")
            << QStringLiteral("CSV_SOUTH") << true << 10.0 << 10.0;
    }

    /** @brief Exercise GUI meta-property edits, resolved plots, and the saved input used by a run. */
    void gageFactorEditsSurviveSave()
    {
        QFETCH(QString, gage);
        QFETCH(bool, defaultColumn);
        QFETCH(double, rainScale);
        QFETCH(double, snowCatch);
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        SWMMRainGagePropertyAdapter adapter(layer->engine(), gage);
        if (defaultColumn)
            adapter.setFileColumn(QString());
        RainfallSeriesModel before;
        before.setEngine(layer->engine());
        QVERIFY(before.reload(true) > 0);
        const int gi = swmm_gage_index(layer->engine(), qPrintable(gage));
        QVERIFY(gi >= 0);
        const auto unscaled = before.gages()[gi].intensity;
        QVERIFY(!unscaled.isEmpty());
        QVERIFY(adapter.setProperty("scaleFactor", rainScale));
        QVERIFY(adapter.setProperty("snowFactor", snowCatch));
        QCOMPARE(adapter.scaleFactor(), rainScale);
        QCOMPARE(adapter.snowFactor(), snowCatch);
        QVERIFY(before.reload(true) > 0);
        for (int k = 0; k < unscaled.size(); ++k)
            QCOMPARE(before.gages()[gi].intensity[k], unscaled[k] * rainScale);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString saved = directory.filePath(QStringLiteral("scaled.inp"));
        QCOMPARE(swmm_model_write(layer->engine(), qPrintable(saved)), SWMM_OK);
        auto reopened = openLayer(saved);
        QVERIFY(reopened);
        SWMMRainGagePropertyAdapter restored(reopened->engine(), gage);
        QCOMPARE(restored.scaleFactor(), rainScale);
        QCOMPARE(restored.snowFactor(), snowCatch);
        RainfallSeriesModel after;
        after.setEngine(reopened->engine());
        QVERIFY(after.reload(true) > 0);
        const auto &scaled = after.gages()[gi];
        QVERIFY(!scaled.fileFailed);
        QCOMPARE(scaled.intensity, before.gages()[gi].intensity);
    }

    /** @brief Verify all six rainfall sources expose their full five-sample record outside simulation dates.
     * Checks a focused cumulative series reaches seventeen inches, represents the first fifteen-minute accumulation correctly and reports all sources OK. */
    void fullFilePlotsOutsideSimulationDates()
    {
        auto layer = openLayer(QStringLiteral("full_rain_record/rain_full_record.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);
        QCOMPARE(dlg.model()->gages().size(), 6);
        for (const auto &g : dlg.model()->gages()) {
            QVERIFY(!g.fileFailed);
            QCOMPARE(g.timesOA.size(), 5);
            const auto stats = RainfallSeriesModel::computeStats(g);
            QCOMPARE(stats.first.date(), QDate(2019, 1, 1));
            QCOMPARE(stats.last.date(), QDate(2019, 1, 10));
        }
        dlg.setFocusGage(QStringLiteral("TSF_CUMULATIVE"));
        auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("basisCombo"));
        QVERIFY(combo);
        combo->setCurrentIndex(2);
        auto *line = qobject_cast<QLineSeries *>(dlg.overlayChart()->series().first());
        QVERIFY(line);
        const auto points = line->points();
        QCOMPARE(points.first().y(), 0.0);
        QVERIFY(std::abs(points.last().y() - 17.0) < 1e-8);
        // The first ten inches accrue from 00:00 to 00:15, including when
        // those records are years outside the simulation period.
        QVERIFY(std::abs(points[1].x() - points[0].x() - 900000.0) < 1.0);
        QCOMPARE(points[1].y(), 10.0);
        auto *stats = dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
        QVERIFY(stats);
        for (int row = 0; row < stats->rowCount(); ++row)
            QCOMPARE(stats->item(row, 11)->text(), QStringLiteral("OK"));
    }

    /** @brief Verify inline, standard-file and CSV gages all resolve while only the first plots by default.
     * Checking all visibility boxes produces three overlay lines and three panels. */
    void assemblesAllThreeSourceKinds()
    {
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);

        const auto &gages = dlg.model()->gages();
        QCOMPARE(gages.size(), 3);
        for (const auto &g : gages) {
            QVERIFY2(g.hasData(), qPrintable(QStringLiteral("gage %1 resolved no data").arg(g.id)));
        }

        // Default: only ONE gage (the first with data) plots; the stats
        // table still lists EVERY gage, with its visibility checkbox
        // checked only on the plotted one.
        QCOMPARE(dlg.visibleGages().size(), 1);
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 1);
        QCOMPARE(dlg.panelCharts().size(), 1);
        auto *statsTable = dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
        QVERIFY(statsTable);
        QCOMPARE(statsTable->rowCount(), 3);
        int checkedRows = 0;
        for (int r = 0; r < statsTable->rowCount(); ++r)
            if (statsTable->item(r, 0)->checkState() == Qt::Checked)
                ++checkedRows;
        QCOMPARE(checkedRows, 1);

        // All three source kinds assemble once every box is checked.
        checkAllGages(dlg);
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 3);
        QCOMPARE(dlg.panelCharts().size(), 3);
    }

    /** @brief Verify a missing-file gage remains visible as a failed statistics row while only the two healthy sources plot. */
    void failedFileGageSurfacesInStatsNotChart()
    {
        // Fixture variant where CSV_SOUTH points at a nonexistent file. The
        // layer opens leniently (the GUI's editing mode), so the model loads
        // with the gage's resolved series empty — the state the dialog must
        // surface as a stats row rather than silently dropping the gage.
        // (A post-open path edit + reload keeps the PREVIOUS series on a
        // failed re-read by engine design, so the broken state is only
        // reachable at open.)
        auto layer = openLayer(QStringLiteral("gage_files_badpath.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);

        const auto &gages = dlg.model()->gages();
        QCOMPARE(gages.size(), 3);
        bool sawFailed = false;
        for (const auto &g : gages)
            if (g.id == QStringLiteral("CSV_SOUTH")) {
                QVERIFY(g.fileFailed);
                sawFailed = true;
            }
        QVERIFY(sawFailed);

        auto *statsTable = dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
        QVERIFY(statsTable);
        QCOMPARE(statsTable->rowCount(), 3);
        bool sawStatus = false;
        for (int r = 0; r < statsTable->rowCount(); ++r) {
            if (statsTable->item(r, 0)->text() == QStringLiteral("CSV_SOUTH"))
                sawStatus = statsTable->item(r, 11)->text().contains(QStringLiteral("failed"));
        }
        QVERIFY(sawStatus);

        // Even with every visibility box checked, only the two healthy gages
        // plot — a failed gage has no series to draw.
        checkAllGages(dlg);
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 2);
        QCOMPARE(dlg.panelCharts().size(), 2);
    }

    /** @brief Verify selecting cumulative-depth basis changes model basis and produces monotone plotted series.
     * The implemented assertions do not inspect the axis title despite the slot name. */
    void basisSwitchRetitlesAxisAndChangesValues()
    {
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);

        auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("basisCombo"));
        QVERIFY(combo);
        QCOMPARE(dlg.basis(), RainfallSeriesModel::Basis::Intensity);

        combo->setCurrentIndex(2); // Cumulative depth
        QTest::qWait(10);
        QCOMPARE(dlg.basis(), RainfallSeriesModel::Basis::CumulativeDepth);

        // Every plotted cumulative series is monotone non-decreasing.
        for (auto *s : dlg.overlayChart()->series()) {
            auto *line = qobject_cast<QLineSeries *>(s);
            if (!line)
                continue;
            double prev = -1.0;
            for (const auto &pt : line->points()) {
                QVERIFY(pt.y() >= prev - 1e-9);
                prev = pt.y();
            }
        }
    }

    /** @brief Verify nudging one gage panel time range by five minutes at each edge synchronizes every panel axis. */
    void perGagePanelsShareTheTimeAxis()
    {
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);
        checkAllGages(dlg); // panels exist only for visible gages

        const auto axes = dlg.panelTimeAxes();
        QVERIFY(axes.size() >= 2);

        // Small nudges — the rain fixtures span well under an hour, so a
        // ±1 h nudge would invert the range (UtcTimeAxis swaps the edges).
        const QDateTime lo = axes[0]->min().addSecs(300);
        const QDateTime hi = axes[0]->max().addSecs(-300);
        axes[0]->setRange(lo, hi);
        QTest::qWait(10);
        for (auto *ax : axes) {
            QCOMPARE(ax->min(), lo);
            QCOMPARE(ax->max(), hi);
        }
    }

    /** @brief Verify gage checkboxes add/remove chart panels and focusing one gage makes it solely visible with matching checkboxes.
     * An unknown focus identifier preserves the current view. */
    void checkboxAndFocusControlVisibility()
    {
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);

        auto *statsTable = dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
        QVERIFY(statsTable);
        QCOMPARE(dlg.visibleGages().size(), 1);

        // Checking a second gage adds its series and panel; unchecking
        // removes them again.
        int otherRow = -1;
        for (int r = 0; r < statsTable->rowCount(); ++r)
            if (statsTable->item(r, 0)->checkState() == Qt::Unchecked) {
                otherRow = r;
                break;
            }
        QVERIFY(otherRow >= 0);
        const QString otherId = statsTable->item(otherRow, 0)->text();
        statsTable->item(otherRow, 0)->setCheckState(Qt::Checked);
        QTest::qWait(10);
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 2);
        QCOMPARE(dlg.panelCharts().size(), 2);
        statsTable->item(otherRow, 0)->setCheckState(Qt::Unchecked);
        QTest::qWait(10);
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 1);
        QCOMPARE(dlg.panelCharts().size(), 1);

        // Focusing (context-menu / property-panel launch) makes the picked
        // gage the ONLY visible one and re-syncs the checkboxes.
        dlg.setFocusGage(otherId);
        QTest::qWait(10);
        QCOMPARE(dlg.visibleGages(), QSet<QString>{otherId});
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 1);
        QCOMPARE(dlg.panelCharts().size(), 1);
        for (int r = 0; r < statsTable->rowCount(); ++r) {
            const bool shouldCheck = statsTable->item(r, 0)->text() == otherId;
            QCOMPARE(statsTable->item(r, 0)->checkState() == Qt::Checked, shouldCheck);
        }

        // An unknown id (stale ref) leaves the current view alone.
        dlg.setFocusGage(QStringLiteral("NO_SUCH_GAGE"));
        QCOMPARE(dlg.visibleGages(), QSet<QString>{otherId});
    }

    // The window used to be dismissable only from the title bar. The Close
    // box must hide it, and must not be the default button — Enter in the
    // stats table has to act on the table, never close the window.
    /** @brief Verify Close is nondefault, the dialog passes shared accessibility checks and clicking Close hides it. */
    void closeButton_HidesDialog()
    {
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));

        auto *box = dlg.findChild<QDialogButtonBox *>(QStringLiteral("rainviz_closeBox"));
        QVERIFY(box);
        auto *btn = box->button(QDialogButtonBox::Close);
        QVERIFY(btn);
        QVERIFY(!btn->isDefault());
        QVERIFY(!btn->autoDefault());
        swmmvis_test::assertDialogA11y(&dlg);

        btn->click();
        QTRY_VERIFY(!dlg.isVisible());
    }
};

QTEST_MAIN(TestRainfallVisualizationDialog)
#include "test_rainfallvisualizationdialog.moc"
