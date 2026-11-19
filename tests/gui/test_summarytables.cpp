// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_summarytables.cpp
 * @brief     Report summary tables (review G-08): captured from the engine after a run, saved
 *            beside the output, read back, and shown as Statistics Dashboard tabs.
 * @details   The fixture (tests/gui/data/summary_tables/) is run once into
 *            tests/output/summary_tables_g08/; its `.summary.json` is kept for review.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "results/summarytables.h"
#include "selection/selectionmanager.h"
#include "simulation/simulationrunner.h"
#include "ui/dialogs/statisticsdashboarddialog.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableView>
#include <QTest>

#include <cmath>
#include <memory>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_statistics.h>

using openswmmvis::results::SummaryTable;
using openswmmvis::results::SummaryTables;

namespace
{
QString outputDir()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .absoluteFilePath(QStringLiteral("../../output/summary_tables_g08"));
}

const SummaryTable *table(const SummaryTables &tables, int id)
{
    for (const auto &t : tables.tables)
        if (t.id == id)
            return &t;
    return nullptr;
}

int column(const SummaryTable &t, const QString &units)
{
    for (int c = 0; c < t.columns.size(); ++c)
        if (t.columns.at(c).units == units)
            return c;
    return -1;
}
} // namespace

class TestSummaryTables : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void captureHoldsEveryReportTable();
    void savedFileReadsBackExactly();
    void unreadableFilesAreRefused();
    void dashboardShowsOneTabPerTable();
    void dashboardWithoutTablesSaysWhy();
    void runnerSavesAndReplacesTheTables();

private:
    QString m_inp, m_out, m_summary;
    SummaryTables m_tables;
    QString m_rpt;
    SummaryTables m_cells; ///< the same tables read cell by cell (swmm_summary_value)
};

void TestSummaryTables::initTestCase()
{
    const QString dir = outputDir() + QStringLiteral("/run");
    QDir(dir).removeRecursively();
    QVERIFY(QDir().mkpath(dir));
    m_inp = dir + QStringLiteral("/summary_model.inp");
    m_out = dir + QStringLiteral("/summary_model.out");
    m_rpt = dir + QStringLiteral("/summary_model.rpt");
    QVERIFY(QFile::copy(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
                            .absoluteFilePath(QStringLiteral("summary_tables/summary_model.inp")),
                        m_inp));
    // The runner's order: end, capture and save the tables, then report and close.
    SWMM_Engine e = swmm_engine_create();
    QCOMPARE(swmm_engine_open(e, m_inp.toUtf8().constData(), m_rpt.toUtf8().constData(),
                              m_out.toUtf8().constData(), nullptr),
             SWMM_OK);
    QCOMPARE(swmm_engine_initialize(e), SWMM_OK);
    char title[64] = {};
    QCOMPARE(swmm_summary_table_title(e, SWMM_SUMMARY_PUMPING, title, int(sizeof title)),
             SWMM_ERR_LIFECYCLE); // not before the run has ended
    QCOMPARE(swmm_engine_start(e, 1), SWMM_OK);
    double elapsed = 0;
    int rc = SWMM_OK;
    do
        rc = swmm_engine_step(e, &elapsed);
    while (rc == SWMM_OK && elapsed > 0);
    QCOMPARE(rc, SWMM_OK);
    QCOMPARE(swmm_engine_end(e), SWMM_OK);
    QString error;
    const auto tables = openswmmvis::results::captureSummaryTables(e, &error);
    QVERIFY2(tables, qPrintable(error));
    m_tables = *tables;
    m_summary = openswmmvis::results::summaryTablesPath(m_out);
    QVERIFY2(openswmmvis::results::saveSummaryTables(m_tables, m_summary, &error),
             qPrintable(error));
    // An independent read of every cell, to check the whole-table reads the capture uses.
    for (const auto &t : m_tables.tables) {
        SummaryTable cells = t;
        for (int r = 0; r < cells.rows.size(); ++r) {
            auto &row = cells.rows[r];
            char label[256] = {};
            QCOMPARE(swmm_summary_row(e, t.id, r, &row.objectType, &row.objectIndex, label,
                                      int(sizeof label)),
                     SWMM_OK);
            row.label = QString::fromUtf8(label);
            for (int c = 0; c < t.columns.size(); ++c)
                QCOMPARE(swmm_summary_value(e, t.id, r, c, &row.values[c]), SWMM_OK);
        }
        m_cells.tables.append(cells);
    }
    QCOMPARE(swmm_engine_report(e), SWMM_OK);
    swmm_engine_close(e);
    swmm_engine_destroy(e);
}

void TestSummaryTables::captureHoldsEveryReportTable()
{
    QCOMPARE(m_tables.tables.size(), int(SWMM_SUMMARY_TABLE_COUNT));
    QFile report(m_rpt);
    QVERIFY(report.open(QIODevice::ReadOnly));
    const QByteArray rpt = report.readAll();
    for (const auto &t : m_tables.tables) {
        QVERIFY2(!t.title.isEmpty(), qPrintable(QString::number(t.id)));
        // A table with rows is printed in the report (no LID units in this model).
        if (!t.rows.isEmpty())
            QVERIFY2(rpt.contains(t.title.toLatin1()), qPrintable(t.title));
        for (const auto &row : t.rows)
            QCOMPARE(row.values.size(), t.columns.size());
    }

    for (int t = 0; t < m_tables.tables.size(); ++t)
        for (int r = 0; r < m_tables.tables.at(t).rows.size(); ++r) {
            const auto &a = m_tables.tables.at(t).rows.at(r);
            const auto &b = m_cells.tables.at(t).rows.at(r);
            QCOMPARE(a.objectType, b.objectType);
            QCOMPARE(a.objectIndex, b.objectIndex);
            QCOMPARE(a.label, b.label);
            for (int c = 0; c < a.values.size(); ++c)
                QVERIFY2(std::isnan(a.values.at(c)) ? std::isnan(b.values.at(c))
                                                    : a.values.at(c) == b.values.at(c),
                         qPrintable(QStringLiteral("%1 row %2 column %3")
                                        .arg(m_tables.tables.at(t).title)
                                        .arg(r)
                                        .arg(c)));
        }

    const auto *depth = table(m_tables, SWMM_SUMMARY_NODE_DEPTH);
    QVERIFY(depth);
    QCOMPARE(depth->rows.size(), 5);
    QCOMPARE(depth->rows.first().objectType, SWMM_SUMMARY_OBJECT_NODE);
    QCOMPARE(depth->rows.first().objectName, QStringLiteral("J1"));
    QCOMPARE(depth->rows.first().label, QStringLiteral("JUNCTION"));
    QVERIFY(column(*depth, QStringLiteral("days")) >= 0); // time of maximum

    const auto *outfalls = table(m_tables, SWMM_SUMMARY_OUTFALL_LOADING);
    QVERIFY(outfalls && !outfalls->rows.isEmpty());
    QCOMPARE(outfalls->rows.first().objectName, QStringLiteral("O1"));
    QCOMPARE(outfalls->rows.last().objectType, SWMM_SUMMARY_OBJECT_SYSTEM);
    QCOMPARE(outfalls->rows.last().objectName, QStringLiteral("System"));
    QVERIFY(outfalls->rows.last().label.isEmpty());
    bool tss = false;
    for (const auto &c : outfalls->columns)
        tss = tss || c.name.contains(QStringLiteral("TSS"));
    QVERIFY(tss);

    const auto *pumps = table(m_tables, SWMM_SUMMARY_PUMPING);
    QVERIFY(pumps && !pumps->rows.isEmpty());
    QCOMPARE(pumps->rows.first().objectType, SWMM_SUMMARY_OBJECT_LINK);
    QCOMPARE(pumps->rows.first().objectName, QStringLiteral("P1"));
    QVERIFY(table(m_tables, SWMM_SUMMARY_LID_PERFORMANCE)->rows.isEmpty());
}

void TestSummaryTables::savedFileReadsBackExactly()
{
    QString error;
    const auto loaded = openswmmvis::results::loadSummaryTables(m_summary, &error);
    QVERIFY2(loaded, qPrintable(error));
    QCOMPARE(loaded->tables.size(), m_tables.tables.size());
    for (int t = 0; t < m_tables.tables.size(); ++t) {
        const auto &a = m_tables.tables.at(t);
        const auto &b = loaded->tables.at(t);
        QCOMPARE(b.id, a.id);
        QCOMPARE(b.title, a.title);
        QCOMPARE(b.columns.size(), a.columns.size());
        for (int c = 0; c < a.columns.size(); ++c) {
            QCOMPARE(b.columns.at(c).name, a.columns.at(c).name);
            QCOMPARE(b.columns.at(c).units, a.columns.at(c).units);
        }
        QCOMPARE(b.rows.size(), a.rows.size());
        for (int r = 0; r < a.rows.size(); ++r) {
            QCOMPARE(b.rows.at(r).objectName, a.rows.at(r).objectName);
            QCOMPARE(b.rows.at(r).objectType, a.rows.at(r).objectType);
            QCOMPARE(b.rows.at(r).label, a.rows.at(r).label);
            for (int c = 0; c < a.columns.size(); ++c) {
                const double x = a.rows.at(r).values.at(c), y = b.rows.at(r).values.at(c);
                QVERIFY(std::isnan(x) ? std::isnan(y) : x == y); // NaN stays missing, not 0
            }
        }
    }
}

void TestSummaryTables::unreadableFilesAreRefused()
{
    const QString dir = outputDir() + QStringLiteral("/refused");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    QString error;
    QVERIFY(!openswmmvis::results::loadSummaryTables(dir + QStringLiteral("/none.json"), &error));
    const auto write = [&](const QString &name, const QByteArray &text) {
        QFile f(dir + QLatin1Char('/') + name);
        f.open(QIODevice::WriteOnly);
        f.write(text);
        return f.fileName();
    };
    QVERIFY(!openswmmvis::results::loadSummaryTables(write("other.json", R"({"format":"x","version":1})"),
                                                     &error));
    QVERIFY(!openswmmvis::results::loadSummaryTables(
        write("short.json", R"({"format":"openswmm-summary-tables","version":1,"tables":[{"id":0,)"
                            R"("title":"T","columns":[{"name":"A","units":""}],)"
                            R"("rows":[{"type":0,"index":0,"name":"J1","label":"","values":[]}]}]})"),
        &error));
    QVERIFY2(error.contains(QStringLiteral("0 values for 1 columns")), qPrintable(error));
}

void TestSummaryTables::dashboardShowsOneTabPerTable()
{
    SWMMModelLayer model(m_inp);
    QList<QString> warnings, errors;
    QVERIFY2(model.loadModel(warnings, errors), qPrintable(errors.join('\n')));
    SWMMResultsLayer results(m_out, &model);
    QVERIFY(results.openResults(warnings, errors));
    SelectionManager selection;
    openswmmvis::ui::StatisticsDashboardDialog dlg(&results);
    dlg.setProject(&model, &selection, nullptr);

    auto *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("tabs"));
    QVERIFY(tabs);
    int withRows = 0;
    for (const auto &t : m_tables.tables)
        withRows += !t.rows.isEmpty();
    QCOMPARE(tabs->count(), 4 + withRows); // nodes, links, subcatchments, 2D regions + tables
    QVERIFY(!dlg.findChild<QLabel *>(QStringLiteral("reportTablesNote")));

    auto *pumpView = dlg.findChild<QTableView *>(QStringLiteral("reportTable_%1").arg(SWMM_SUMMARY_PUMPING));
    QVERIFY(pumpView);
    QCOMPARE(tabs->tabText(tabs->indexOf(pumpView)), QStringLiteral("Pumping"));
    QCOMPARE(pumpView->model()->index(0, 0).data().toString(), QStringLiteral("P1"));

    // Times of maxima read "d hh:mm" and sort by their value.
    auto *depthView = dlg.findChild<QTableView *>(QStringLiteral("reportTable_%1").arg(SWMM_SUMMARY_NODE_DEPTH));
    QVERIFY(depthView);
    const auto *depth = table(m_tables, SWMM_SUMMARY_NODE_DEPTH);
    const int timeColumn = 2 + column(*depth, QStringLiteral("days")); // name, type, values
    int j1 = -1; // the view sorts (descending by default), so find J1 by name
    for (int r = 0; r < depthView->model()->rowCount(); ++r)
        if (depthView->model()->index(r, 0).data().toString() == QLatin1String("J1"))
            j1 = r;
    QCOMPARE(j1, 0); // rows open in the report's order
    const QModelIndex time = depthView->model()->index(j1, timeColumn);
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d+ d \\d\\d:\\d\\d$")).match(time.data().toString()).hasMatch(),
             qPrintable(time.data().toString()));
    QCOMPARE(time.data(Qt::EditRole).toDouble(), depth->rows.first().values.at(timeColumn - 2));

    // Selecting a row selects its object; the System row selects nothing.
    tabs->setCurrentWidget(pumpView);
    QSignalSpy changed(&selection, &SelectionManager::selectionChanged);
    pumpView->selectRow(0);
    QVERIFY(selection.selection().contains(SWMMObjectRef(SWMMObjectRef::Link, QStringLiteral("P1"))));
    auto *outfallView = dlg.findChild<QTableView *>(QStringLiteral("reportTable_%1").arg(SWMM_SUMMARY_OUTFALL_LOADING));
    QVERIFY(outfallView);
    tabs->setCurrentWidget(outfallView);
    const int systemRow = outfallView->model()->rowCount() - 1;
    QCOMPARE(outfallView->model()->index(systemRow, 0).data().toString(), QStringLiteral("System"));
    const int before = changed.size();
    outfallView->selectionModel()->clearSelection();
    const int afterClear = changed.size();
    outfallView->selectRow(systemRow);
    QCOMPARE(changed.size(), afterClear); // nothing pushed for the System row
    QVERIFY(afterClear >= before);

    // The map selection shows in the report tabs too.
    selection.select({SWMMObjectRef(SWMMObjectRef::Node, QStringLiteral("J1"))},
                     SelectionManager::Replace);
    QCOMPARE(depthView->selectionModel()->selectedRows().size(), 1);

    // Export includes every report table.
    const QString csv = outputDir() + QStringLiteral("/run/dashboard.csv");
    QVERIFY(dlg.exportTo(csv));
    QFile exported(csv);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const QByteArray text = exported.readAll();
    QVERIFY(text.contains("# Pumping"));
    QVERIFY(text.contains("# Outfall Loading"));
}

void TestSummaryTables::dashboardWithoutTablesSaysWhy()
{
    const QString dir = outputDir() + QStringLiteral("/no_tables");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    for (const char *ext : {".inp", ".out"})
        QVERIFY(QFile::copy(outputDir() + QStringLiteral("/run/summary_model") + QLatin1String(ext),
                            dir + QStringLiteral("/summary_model") + QLatin1String(ext)));
    SWMMModelLayer model(dir + QStringLiteral("/summary_model.inp"));
    QList<QString> warnings, errors;
    QVERIFY(model.loadModel(warnings, errors));
    SWMMResultsLayer results(dir + QStringLiteral("/summary_model.out"), &model);
    QVERIFY(results.openResults(warnings, errors));
    openswmmvis::ui::StatisticsDashboardDialog dlg(&results);
    auto *note = dlg.findChild<QLabel *>(QStringLiteral("reportTablesNote"));
    QVERIFY(note);
    QVERIFY(note->text().contains(QStringLiteral("Run the model")));
    QVERIFY(!dlg.findChild<QTableView *>(QStringLiteral("reportTable_%1").arg(SWMM_SUMMARY_PUMPING)));
}

void TestSummaryTables::runnerSavesAndReplacesTheTables()
{
    const QString dir = outputDir() + QStringLiteral("/runner");
    QDir(dir).removeRecursively();
    QVERIFY(QDir().mkpath(dir));
    const QString inp = dir + QStringLiteral("/summary_model.inp");
    const QString out = dir + QStringLiteral("/summary_model.out");
    QVERIFY(QFile::copy(m_inp, inp));
    const QString summary = openswmmvis::results::summaryTablesPath(out);
    for (int run = 0; run < 2; ++run) {
        if (run == 1) { // a stale file from an earlier run must not survive the next one
            QFile stale(summary);
            QVERIFY(stale.open(QIODevice::WriteOnly | QIODevice::Truncate));
            stale.write("stale");
        }
        SimulationRunner worker(run + 1, QStringLiteral("Report tables"), inp,
                                dir + QStringLiteral("/summary_model.rpt"), out,
                                QStringLiteral("6.0.0"));
        QSignalSpy finished(&worker, &SimulationRunner::finished);
        worker.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 60000);
        const auto result = finished.takeFirst();
        QVERIFY2(result[1].toBool(), qPrintable(result[3].toString()));
        QString error;
        const auto saved = openswmmvis::results::loadSummaryTables(summary, &error);
        QVERIFY2(saved, qPrintable(error));
        QCOMPARE(table(*saved, SWMM_SUMMARY_PUMPING)->rows.size(), 1);
    }
    QVERIFY(SimulationRunner::runOutputPaths(inp, dir + QStringLiteral("/summary_model.rpt"), out)
                .contains(QFileInfo(summary).absoluteFilePath()));
}

QTEST_MAIN(TestSummaryTables)
#include "test_summarytables.moc"
