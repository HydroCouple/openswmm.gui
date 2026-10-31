/**
 * @file      test_resultseriestables.cpp
 * @brief     Phase 4B gates on the pure parts: pollutant loads and cumulative
 *            mass (R4) against the engine's `.rpt` load summaries, the time
 *            integration rule, 2D region aggregates against hand sums, and
 *            the result table (missing -> blank, CSV units + status).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/BELLINGE_GW_QUALITY_PHASE4B_AGENT_BRIEF_2026-10-30.md gates:
 * "loads checked against the .rpt Link Pollutant Load Summary / Outfall
 * Loading Summary on a small deck to the report precision; aggregates checked
 * against hand sums on a 2-4 cell fixture". The load decks route a ramped
 * inflow carrying TSS (mg/L) and Lead (ug/L) through two conduits with the
 * report step equal to the routing step, so the backward-rectangle integral
 * of the report samples is the engine's own per-step accumulation; the test
 * also shows a trapezoid integral would miss the report. Run files and CSVs
 * are written under PHASE4B_OUTPUT.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/pollutantload.h"
#include "plot/seriesintegration.h"
#include "results/meshregionaggregate.h"
#include "results/resulttable.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_output.h>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QtTest>

#include <cmath>
#include <limits>
#include <vector>

using namespace openswmmvis;
namespace load = openswmmvis::plot::pollutantload;
using openswmmvis::io::Mesh2DValueStatus;
using openswmmvis::results::MeshAggregate;

namespace
{

const double kNaN = std::numeric_limits<double>::quiet_NaN();

QString outputPath(const QString &name)
{
    const QString folder = QStringLiteral(PHASE4B_OUTPUT);
    QDir().mkpath(folder);
    return QDir(folder).filePath(name);
}

/** @brief The load deck in \p flowUnits; \p flowScale scales the inflow so US loads
 *  stay well above the report precision. */
QByteArray loadDeck(const char *flowUnits, const char *flowScale)
{
    return QByteArray("[TITLE]\nPhase 4B load gate\n[OPTIONS]\nFLOW_UNITS ") + flowUnits +
           "\nFLOW_ROUTING KINWAVE\nSTART_DATE 01/01/2026\nSTART_TIME 00:00:00\n"
           "REPORT_START_DATE 01/01/2026\nREPORT_START_TIME 00:00:00\nEND_DATE 01/01/2026\n"
           "END_TIME 02:00:00\nREPORT_STEP 00:01:00\nROUTING_STEP 60\n"
           "[REPORT]\nNODES ALL\nLINKS ALL\n"
           "[JUNCTIONS]\nJ1 10 2 0 0 0\nJ2 9 2 0 0 0\n[OUTFALLS]\nO1 8 FREE NO\n"
           "[CONDUITS]\nC1 J1 J2 100 0.013 0 0 0\nC2 J2 O1 100 0.013 0 0 0\n"
           "[XSECTIONS]\nC1 CIRCULAR 1 0 0 0 1\nC2 CIRCULAR 1 0 0 0 1\n"
           "[POLLUTANTS]\nTSS MG/L 0 0 0 0 0\nLead UG/L 0 0 0 0 0\n"
           "[TIMESERIES]\nQIN 0:00 0.0\nQIN 0:20 0.05\nQIN 2:00 0.05\n"
           "[INFLOWS]\nJ1 FLOW QIN FLOW 1.0 " + QByteArray(flowScale) +
           "\nJ1 TSS \"\" CONCEN 1.0 1.0 10\n"
           "J1 Lead \"\" CONCEN 1.0 1.0 2000\n"
           "[COORDINATES]\nJ1 0 0\nJ2 100 0\nO1 200 0\n";
}

/** Numbers on the row of \p object in the block titled \p section. */
QStringList reportRow(const QString &rpt, const QString &section, const QString &object)
{
    QFile f(rpt);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    bool in = false;
    for (const QString &line : lines) {
        if (line.contains(section))
            in = true;
        if (!in)
            continue;
        const QStringList parts =
            line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!parts.isEmpty() && parts.first() == object)
            return parts.mid(1);
    }
    return {};
}

struct Series {
    std::vector<double> t, v;
};

Series outSeries(SWMM_Output h, bool node, const char *id, int var)
{
    Series s;
    const int periods = swmm_output_get_period_count(h);
    const int n = node ? swmm_output_get_node_count(h) : swmm_output_get_link_count(h);
    int idx = -1;
    for (int i = 0; i < n; ++i)
        if (QString::fromUtf8(node ? swmm_output_get_node_id(h, i) : swmm_output_get_link_id(h, i)) ==
            QLatin1String(id))
            idx = i;
    std::vector<float> raw(std::size_t(std::max(periods, 0)));
    if (idx < 0 || periods <= 0 ||
        (node ? swmm_output_get_node_series(h, idx, var, 0, periods - 1, raw.data())
              : swmm_output_get_link_series(h, idx, var, 0, periods - 1, raw.data())) != 0)
        return s;
    for (int p = 0; p < periods; ++p) {
        double t = kNaN;
        swmm_output_get_period_time(h, p, &t);
        s.t.push_back(t);
        s.v.push_back(raw[std::size_t(p)]);
    }
    return s;
}

/** Trapezoid integral over the sample times only (the rule NOT used). */
double trapezoid(const Series &q, const Series &c, double factor)
{
    double sum = 0.0;
    for (std::size_t i = 1; i < q.t.size(); ++i)
        sum += 0.5 * (q.v[i] * c.v[i] + q.v[i - 1] * c.v[i - 1]) * (q.t[i] - q.t[i - 1]) * 86400.0;
    return sum * factor;
}

} // namespace

class TestResultSeriesTables : public QObject
{
    Q_OBJECT

private slots:
    /*! Flow and concentration codes convert exactly to the load units. */
    void loadUnitConversions()
    {
        QCOMPARE(load::litresPerSecond(3), 1000.0);
        QCOMPARE(load::litresPerSecond(4), 1.0);
        QVERIFY(std::fabs(load::litresPerSecond(0) - 28.316846592) < 1e-12);
        QVERIFY(std::fabs(load::litresPerSecond(1) - 3.785411784 / 60.0) < 1e-12);
        QVERIFY(std::fabs(load::litresPerSecond(2) - 43.812636388888) < 1e-9);
        QVERIFY(std::fabs(load::litresPerSecond(5) - 11.574074074074) < 1e-9);
        QVERIFY(std::isnan(load::litresPerSecond(9)));
        QCOMPARE(load::flowUnitsName(4), QStringLiteral("LPS"));
        QVERIFY(load::flowUnitsName(9).isEmpty());

        // 1 CMS x 1 mg/L for a day = 86 400 m3 x 1 g/m3 = 86.4 kg.
        const auto si = load::loadUnits(3, 0);
        QVERIFY(si.valid);
        QCOMPARE(si.rateUnits, QStringLiteral("kg/d"));
        QCOMPARE(si.massUnits, QStringLiteral("kg"));
        QVERIFY(std::fabs(load::rate({1.0}, {1.0}, si)[0] - 86.4) < 1e-9);
        // ug/L is a thousandth of mg/L.
        QVERIFY(std::fabs(load::rate({1.0}, {1.0}, load::loadUnits(3, 1))[0] - 0.0864) < 1e-12);
        // 1 cfs x 1 mg/L for a day, in lb.
        const auto us = load::loadUnits(0, 0);
        QCOMPARE(us.rateUnits, QStringLiteral("lb/d"));
        QVERIFY(std::fabs(load::rate({1.0}, {1.0}, us)[0] -
                          28.316846592 * 86400.0 / 453592.37) < 1e-9);
        // Counts stay counts: 1 L/s x 1 count/L for one second.
        const auto counts = load::loadUnits(4, 2);
        QCOMPARE(counts.massUnits, QStringLiteral("count"));
        QCOMPARE(counts.massPerFlowConcSecond, 1.0);
        QVERIFY(!load::loadUnits(3, 7).valid);
        QVERIFY(load::rate({1.0}, {1.0}, load::loadUnits(3, 7)).empty());
        // A missing input is a missing load, never zero.
        QVERIFY(std::isnan(load::rate({kNaN}, {1.0}, si)[0]));

        // Name-keyed descriptors survive any species name.
        const QString odd = QStringLiteral("N:O3 %");
        QString back;
        bool cumulative = false;
        QVERIFY(load::parseKey(load::cumulativeKey(odd), &back, &cumulative));
        QCOMPARE(back, odd);
        QVERIFY(cumulative);
        QVERIFY(load::parseKey(load::rateKey(odd), &back, &cumulative));
        QVERIFY(!cumulative);
        QVERIFY(load::isLoadDescriptor(load::rateDescriptor(odd, si, false)));
        QCOMPARE(load::rateDescriptor(odd, si, false).unitLabel(plot::UnitSystem::SI),
                 QStringLiteral("kg/d"));
        // Labels and notes say what the series are: report-step products and
        // their integral, net on links — not the .rpt's routing-step totals.
        const auto tss = QStringLiteral("TSS");
        QCOMPARE(load::rateDescriptor(tss, si, false).label(),
                 QStringLiteral("TSS load (flow × concentration at report steps)"));
        QCOMPARE(load::rateDescriptor(tss, si, true).label(),
                 QStringLiteral("TSS net load (flow × concentration at report steps)"));
        QCOMPARE(load::cumulativeDescriptor(tss, si, false).label(),
                 QStringLiteral("TSS cumulative load (integrated from report-step samples)"));
        QCOMPARE(load::cumulativeDescriptor(tss, si, true).label(),
                 QStringLiteral("TSS net cumulative load (integrated from report-step samples)"));
        for (const bool link : {false, true}) {
            const QString note = load::cumulativeDescriptor(tss, si, link).variableNote;
            QVERIFY(note.contains(QStringLiteral("saved report-step values")));
            QVERIFY(note.contains(QStringLiteral("integrate every routing step")));
            QVERIFY(note.contains(QStringLiteral("differ substantially")));
            QCOMPARE(note.contains(QStringLiteral("net of reversing flow")), link);
        }
        // One identity for both: the label is display metadata.
        QCOMPARE(load::rateDescriptor(tss, si, true), load::rateDescriptor(tss, si, false));
        QVERIFY(!load::isLoadDescriptor(plot::ResultDescriptor::forSpecies(odd)));
    }

    /*! Each report value is held over the interval ending at it; the first
     *  covers the time since the start capped at one step; a gap poisons
     *  the integral from there on. */
    void integrationRule()
    {
        const double start = 46023.0; // 2026-01-01
        const double m = 60.0 / 86400.0;
        // Reports at +1, +2, +3 min: three full minutes.
        auto dt = plot::sampleIntervalsSeconds({start + m, start + 2 * m, start + 3 * m}, start, 60);
        QCOMPARE(dt, (std::vector<double>{60.0, 60.0, 60.0}));
        QCOMPARE(plot::timeIntegral({start + m, start + 2 * m, start + 3 * m}, {1.0, 2.0, 3.0},
                                    start, 60),
                 360.0);
        // A report at the start instant covers no time.
        dt = plot::sampleIntervalsSeconds({start, start + m}, start, 60);
        QCOMPARE(dt[0], 0.0);
        // A delayed REPORT_START does not book the unreported lead-in.
        dt = plot::sampleIntervalsSeconds({start + 30 * m, start + 31 * m}, start, 60);
        QCOMPARE(dt[0], 60.0);
        // A missing sample makes the total unknown, not smaller.
        const auto c = plot::cumulativeIntegral({start + m, start + 2 * m, start + 3 * m},
                                                {1.0, kNaN, 3.0}, start, 60);
        QCOMPARE(c[0], 60.0);
        QVERIFY(std::isnan(c[1]) && std::isnan(c[2]));
        QVERIFY(std::isnan(plot::timeIntegral({start + m}, {kNaN}, start, 60)));
        // The time-weighted mean skips the gap instead.
        QCOMPARE(plot::timeWeightedMean({start + m, start + 2 * m, start + 3 * m},
                                        {1.0, kNaN, 3.0}, start, 60),
                 2.0);
    }

    /*! R4 gate: cumulative loads from the `.out` equal the `.rpt` Outfall
     *  Loading Summary and Link Pollutant Load Summary to report precision,
     *  for an SI and a US deck. */
    void loadsMatchTheReportSummaries_data()
    {
        QTest::addColumn<QByteArray>("units");
        QTest::addColumn<bool>("si");
        QTest::newRow("CMS") << QByteArray("CMS") << true;
        QTest::newRow("CFS") << QByteArray("CFS") << false;
    }
    void loadsMatchTheReportSummaries()
    {
        QFETCH(QByteArray, units);
        QFETCH(bool, si);
        const QString stem = QStringLiteral("loads_%1").arg(QString::fromLatin1(units).toLower());
        QFile f(outputPath(stem + ".inp"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(loadDeck(units.constData(), si ? "1.0" : "10.0"));
        f.close();
        const QString rpt = outputPath(stem + ".rpt");
        const QString out = outputPath(stem + ".out");
        QCOMPARE(swmm_engine_run(outputPath(stem + ".inp").toUtf8().constData(),
                                 rpt.toUtf8().constData(), out.toUtf8().constData(), nullptr),
                 0);
        SWMM_Output h = swmm_output_open(out.toUtf8().constData());
        QVERIFY(h);
        const int flowCode = swmm_output_get_flow_units(h);
        double start = kNaN;
        QCOMPARE(swmm_output_get_start_date(h, &start), 0);
        const int step = 60;

        // .rpt constants vs exact ones: 28.317/0.02832 L per m3 (SI), and
        // 2.203e-6 lb/mg for the US outfall table (DefaultReportPlugin).
        const double relSi = std::fabs(28.317 / 0.02832 / 1000.0 - 1.0);
        const double relUsOutfall = std::fabs(2.203e-6 * 453592.37 - 1.0) + 1e-5;
        const double lbPerKg = 1.0e6 / 453592.37;

        struct Check {
            bool node;
            const char *id;
            const char *section;
        };
        const Check checks[] = {{true, "O1", "Outfall Loading Summary"},
                                {false, "C1", "Link Pollutant Load Summary"},
                                {false, "C2", "Link Pollutant Load Summary"}};
        QStringList log;
        for (const Check &chk : checks) {
            const QStringList row = reportRow(rpt, QString::fromLatin1(chk.section),
                                              QString::fromLatin1(chk.id));
            QVERIFY2(!row.isEmpty(), chk.id);
            const Series q = outSeries(h, chk.node, chk.id,
                                       chk.node ? int(SWMM_OUT_NODE_TOTAL_INFLOW)
                                                : int(SWMM_OUT_LINK_FLOW));
            QVERIFY(!q.v.empty());
            for (int p = 0; p < 2; ++p) {
                const Series c = outSeries(h, chk.node, chk.id,
                                           (chk.node ? int(SWMM_OUT_NODE_POLLUT_BASE)
                                                     : int(SWMM_OUT_LINK_POLLUT_BASE)) + p);
                const auto u = load::loadUnits(flowCode, swmm_output_get_pollut_units(h, p));
                QVERIFY(u.valid);
                const double gui =
                    load::cumulative(q.t, q.v, c.v, start, step, u).back();
                // Outfall table: kg (SI) / lb (US) as printed; columns after
                // freq, avg, max and volume. Link table: always lb in this engine.
                const double printed = row.at(chk.node ? 4 + p : p).toDouble();
                double expected = gui;
                double rel = si ? relSi : 0.0;
                if (!chk.node && si) {
                    expected = gui * lbPerKg;
                } else if (chk.node && !si) {
                    rel = relUsOutfall;
                }
                const double tol = 0.0005 + std::fabs(expected) * (rel + 1e-6);
                log << QStringLiteral("%1 %2 species %3: gui %4 %5, report %6, tol %7")
                           .arg(QString::fromLatin1(units), chk.id)
                           .arg(p)
                           .arg(expected, 0, 'f', 6)
                           .arg(!chk.node && si ? QStringLiteral("lb") : u.massUnits)
                           .arg(printed, 0, 'f', 3)
                           .arg(tol, 0, 'g', 3);
                QVERIFY2(printed > 0.1, qPrintable(log.last())); // a meaningful load
                QVERIFY2(std::fabs(expected - printed) <= tol, qPrintable(log.last()));
                // The gate discriminates: a trapezoid over the samples misses.
                if (p == 0) {
                    double trap = trapezoid(q, c, u.massPerFlowConcSecond);
                    if (!chk.node && si)
                        trap *= lbPerKg;
                    QVERIFY2(std::fabs(trap - printed) > tol,
                             qPrintable(QStringLiteral("trapezoid %1").arg(trap)));
                }
            }
        }
        QFile logFile(outputPath(stem + "_load_check.txt"));
        QVERIFY(logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        logFile.write(log.join('\n').toUtf8() + '\n');
        swmm_output_close(h);
    }

    /*! Region aggregates on a 4-cell fixture equal hand sums; Missing cells
     *  are excluded (never zero); an all-missing region is NaN. */
    void regionAggregatesMatchHandSums()
    {
        // Cells 0..3: areas 1, 2, 3, 4 m2; cell 2 Missing; cell 3 waterless.
        const std::vector<double> area = {1.0, 2.0, 3.0, 4.0};
        const std::vector<float> conc = {10.0f, 20.0f, std::numeric_limits<float>::quiet_NaN(),
                                         0.0f};
        const std::vector<Mesh2DValueStatus> st = {Mesh2DValueStatus::Valid,
                                                   Mesh2DValueStatus::Valid,
                                                   Mesh2DValueStatus::Missing,
                                                   Mesh2DValueStatus::Waterless};
        const std::vector<float> depth = {0.5f, 0.25f, 0.4f, 0.0f};
        const std::vector<int> all = {0, 1, 2, 3};
        int used = -1, missing = -1;
        // Sum / mean over the Valid cells 0, 1 (cell 3 is waterless, not valid).
        QCOMPARE(results::aggregateFrame(conc, &st, area, nullptr, all, MeshAggregate::Sum, 0.0,
                                         &used, &missing),
                 30.0);
        QCOMPARE(used, 2);
        QCOMPARE(missing, 1); // cell 2: the aggregate covers fewer cells than the region
        QCOMPARE(results::aggregateFrame(conc, &st, area, nullptr, all, MeshAggregate::Mean, 0.0),
                 15.0);
        // (10*1 + 20*2) / (1 + 2) = 50/3.
        QVERIFY(std::fabs(results::aggregateFrame(conc, &st, area, nullptr, all,
                                                  MeshAggregate::AreaWeightedMean, 0.0) -
                          50.0 / 3.0) < 1e-12);
        // Mass (mg/L = g/m3 -> kg): 10*0.5*1 + 20*0.25*2 = 15 g = 0.015 kg; the
        // waterless cell holds no water (zero), the missing one is excluded.
        double factor = 0.0;
        QString massUnits;
        QVERIFY(results::massFactorForUnits(QStringLiteral("MG/L"), &factor, &massUnits));
        QCOMPARE(massUnits, QStringLiteral("kg"));
        QVERIFY(std::fabs(results::aggregateFrame(conc, &st, area, &depth, all,
                                                  MeshAggregate::Mass, factor, &used) -
                          0.015) < 1e-12);
        QCOMPARE(used, 3);
        QVERIFY(!results::massFactorForUnits(QStringLiteral("m"), nullptr, nullptr));
        QCOMPARE(results::aggregateUnits(MeshAggregate::Mass, QStringLiteral("ug/L")),
                 QStringLiteral("kg"));
        QCOMPARE(results::aggregateUnits(MeshAggregate::Mean, QStringLiteral("m")),
                 QStringLiteral("m"));
        // Mass needs the depth.
        QVERIFY(std::isnan(results::aggregateFrame(conc, &st, area, nullptr, all,
                                                   MeshAggregate::Mass, factor)));
        // A region of only the missing cell is missing, not zero.
        QVERIFY(std::isnan(
            results::aggregateFrame(conc, &st, area, nullptr, {2}, MeshAggregate::Sum, 0.0)));
        // Without a status vector, finite values count (fixed depth fields).
        QCOMPARE(results::aggregateFrame(depth, nullptr, area, nullptr, {0, 1},
                                         MeshAggregate::Sum, 0.0),
                 0.75);

        // Tags group cells; untagged cells belong to no tag region.
        const auto regions = results::tagRegions({"B", "a", "", "B"});
        QCOMPARE(regions.size(), 2);
        QCOMPARE(regions[0].label, QStringLiteral("Tag: a"));
        QCOMPARE(regions[0].cells, (std::vector<int>{1}));
        QCOMPARE(regions[1].cells, (std::vector<int>{0, 3}));
        QCOMPARE(results::domainRegion(3).cells, (std::vector<int>{0, 1, 2}));
    }

    /*! Missing samples are blank (not 0) with a status; the CSV carries a
     *  Units row and the Status column. */
    void tablesKeepMissingBlankAndExportUnits()
    {
        const double t0 = 46023.0, m = 60.0 / 86400.0;
        plot::SeriesData a; // complete
        a.ok = true;
        a.timesJulian = {t0 + m, t0 + 2 * m, t0 + 3 * m};
        a.values = {1.0, 0.0, 3.0};
        plot::SeriesData b; // a rate: no first sample, and a Missing cell
        b.ok = true;
        b.timesJulian = {t0 + 2 * m, t0 + 3 * m};
        b.values = {kNaN, 6.0};
        plot::SeriesData c; // failed outright
        c.errorMessage = QStringLiteral("Run does not carry species 'X'");

        const auto byVar = results::tableByVariable({"Depth", "Rate", "X"}, {"m", "kg/d", ""},
                                                    {a, b, c});
        QCOMPARE(byVar.rowCount(), 3);
        QCOMPARE(byVar.values[1][0], 0.0); // a real zero stays zero
        QVERIFY(std::isnan(byVar.values[0][1]) && std::isnan(byVar.values[1][1]));
        QCOMPARE(byVar.values[2][1], 6.0);
        QVERIFY(byVar.rowStatus[0].contains(QStringLiteral("Rate")));
        QVERIFY(byVar.rowStatus[2].contains(QStringLiteral("X")));
        QVERIFY(!byVar.rowStatus[2].contains(QStringLiteral("Rate")));

        QStandardItemModel model;
        results::fillItemModel(byVar, model);
        QCOMPARE(model.columnCount(), 5); // Time, 3 values, Status
        QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QStringLiteral("Depth (m)"));
        QCOMPARE(model.index(1, 1).data(Qt::EditRole).toDouble(), 0.0);
        QVERIFY(!model.index(0, 2).data(Qt::EditRole).isValid()); // blank, not 0
        QVERIFY(model.index(0, 2).data(Qt::DisplayRole).toString().isEmpty());

        const auto byObj = results::tableByObject({"J1", "J2", "J3"}, {a, b, c}, "m");
        QCOMPARE(byObj.columnCount(), 3);
        QVERIFY(std::isnan(byObj.values[1][0]));
        QCOMPARE(byObj.rowStatus[0], QString());
        QCOMPARE(byObj.rowStatus[1], QStringLiteral("2 of 3 values missing"));
        QCOMPARE(byObj.rowStatus[2], c.errorMessage);

        const QString csv = outputPath(QStringLiteral("table_by_variable.csv"));
        QString error;
        QVERIFY2(results::writeDelimited(byVar, csv, QLatin1Char(','), &error), qPrintable(error));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines[0], QStringLiteral("Time,Depth,Rate,X,Status"));
        QCOMPARE(lines[1], QStringLiteral("Units,m,kg/d,,"));
        QVERIFY(lines[2].startsWith(QStringLiteral("2026-01-01 00:01:00,1,,,missing: Rate; X")));
        QVERIFY(lines[3].startsWith(QStringLiteral("2026-01-01 00:02:00,0,,,")));
        QVERIFY(lines[4].startsWith(QStringLiteral("2026-01-01 00:03:00,3,6,,missing: X")));
    }
};

QTEST_MAIN(TestResultSeriesTables)
#include "test_resultseriestables.moc"
