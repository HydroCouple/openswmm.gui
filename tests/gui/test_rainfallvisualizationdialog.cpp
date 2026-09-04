/*!
 * \file   test_rainfallvisualizationdialog.cpp
 * \brief  Rainfall Visualization dialog — per-gage series assembly across all
 *         three source kinds, failed-file surfacing, basis switching, and the
 *         per-gage panels' shared time axis.
 *
 * Fixture: `gage_assignment_files.inp` — one TIMESERIES gage, one standard
 * rain-file (.dat, station) gage, one multi-column CSV gage, with the data
 * files alongside (`gage_files_rain.dat` / `gage_files_rain.csv`).
 */

#include "layers/swmmmodellayer.h"
#include "plot/rainfallseriesmodel.h"
#include "ui/dialogs/rainfallvisualizationdialog.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_model.h>

#include <QChart>
#include <QComboBox>
#include <QDateTimeAxis>
#include <QDir>
#include <QLineSeries>
#include <QObject>
#include <QTableWidget>
#include <QTest>

#include <memory>

using openswmmvis::plot::RainfallSeriesModel;
using openswmmvis::ui::RainfallVisualizationDialog;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer(const QString &inp)
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(inp), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

int lineSeriesCount(QChart *c)
{
    int n = 0;
    for (auto *s : c->series())
        if (qobject_cast<QLineSeries *>(s)) ++n;
    return n;
}

/*! Check every gage's visibility box in the stats table (default is
 *  only-the-first-gage; comparison tests want all series plotted). */
void checkAllGages(RainfallVisualizationDialog &dlg)
{
    auto *statsTable =
        dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
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
            QVERIFY2(g.hasData(), qPrintable(
                QStringLiteral("gage %1 resolved no data").arg(g.id)));
        }

        // Default: only ONE gage (the first with data) plots; the stats
        // table still lists EVERY gage, with its visibility checkbox
        // checked only on the plotted one.
        QCOMPARE(dlg.visibleGages().size(), 1);
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 1);
        QCOMPARE(dlg.panelCharts().size(), 1);
        auto *statsTable =
            dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
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

        auto *statsTable =
            dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
        QVERIFY(statsTable);
        QCOMPARE(statsTable->rowCount(), 3);
        bool sawStatus = false;
        for (int r = 0; r < statsTable->rowCount(); ++r) {
            if (statsTable->item(r, 0)->text() == QStringLiteral("CSV_SOUTH"))
                sawStatus = statsTable->item(r, 11)->text().contains(
                    QStringLiteral("failed"));
        }
        QVERIFY(sawStatus);

        // Even with every visibility box checked, only the two healthy gages
        // plot — a failed gage has no series to draw.
        checkAllGages(dlg);
        QCOMPARE(lineSeriesCount(dlg.overlayChart()), 2);
        QCOMPARE(dlg.panelCharts().size(), 2);
    }

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

        combo->setCurrentIndex(2);   // Cumulative depth
        QTest::qWait(10);
        QCOMPARE(dlg.basis(), RainfallSeriesModel::Basis::CumulativeDepth);

        // Every plotted cumulative series is monotone non-decreasing.
        for (auto *s : dlg.overlayChart()->series()) {
            auto *line = qobject_cast<QLineSeries *>(s);
            if (!line) continue;
            double prev = -1.0;
            for (const auto &pt : line->points()) {
                QVERIFY(pt.y() >= prev - 1e-9);
                prev = pt.y();
            }
        }
    }

    void perGagePanelsShareTheTimeAxis()
    {
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);
        checkAllGages(dlg);   // panels exist only for visible gages

        const auto axes = dlg.panelTimeAxes();
        QVERIFY(axes.size() >= 2);

        // Small nudges — the rain fixtures span well under an hour, so a
        // ±1 h nudge would invert the range and QDateTimeAxis would refuse it.
        const QDateTime lo = axes[0]->min().addSecs(300);
        const QDateTime hi = axes[0]->max().addSecs(-300);
        axes[0]->setRange(lo, hi);
        QTest::qWait(10);
        for (auto *ax : axes) {
            QCOMPARE(ax->min(), lo);
            QCOMPARE(ax->max(), hi);
        }
    }

    void checkboxAndFocusControlVisibility()
    {
        auto layer = openLayer(QStringLiteral("gage_assignment_files.inp"));
        QVERIFY(layer);
        RainfallVisualizationDialog dlg(layer.get());
        dlg.show();
        QTest::qWait(30);

        auto *statsTable =
            dlg.findChild<QTableWidget *>(QStringLiteral("statsTable"));
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
            const bool shouldCheck =
                statsTable->item(r, 0)->text() == otherId;
            QCOMPARE(statsTable->item(r, 0)->checkState() == Qt::Checked,
                     shouldCheck);
        }

        // An unknown id (stale ref) leaves the current view alone.
        dlg.setFocusGage(QStringLiteral("NO_SUCH_GAGE"));
        QCOMPARE(dlg.visibleGages(), QSet<QString>{otherId});
    }
};

QTEST_MAIN(TestRainfallVisualizationDialog)
#include "test_rainfallvisualizationdialog.moc"
