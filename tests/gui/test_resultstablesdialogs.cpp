/**
 * @file      test_resultstablesdialogs.cpp
 * @brief     Phase 4B (V6, R4, V0): Tabular Results and the Statistics
 *            dashboard on ResultDescriptor + IRunLayer series.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/BELLINGE_GW_QUALITY_PHASE4B_AGENT_BRIEF_2026-10-30.md. A small
 * SI deck (a rained-on subcatchment, three nodes, two conduits, TSS in mg/L
 * and Lead in ug/L) is run with the engine; the dialogs are then driven over
 * the real `.out`.
 *
 * Hydraulic characterization (written and passed against the dialogs
 * BEFORE the descriptor migration, kept unchanged after it): the Statistics
 * maxima / sample sums and the Tabular by-object / by-variable values equal
 * the `.out` series read directly through the engine reader.
 *
 * Everything this test writes goes to PHASE4B_OUTPUT
 * (tests/output/tables_statistics_loads_2026-10-30).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "plot/mesh2drunlayer.h"
#include "plot/pollutantload.h"
#include "plot/swmmoutrunlayer.h"
#include "swmmvis.h"
#include "ui/dialogs/statisticsdashboarddialog.h"
#include "ui/dialogs/tabularresultsdialog.h"
#include "ui/widgets/attributepickermenu.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_output.h>

#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableView>
#include <QTimeZone>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

namespace
{

const char *kDeck =
    "[TITLE]\nPhase 4B tables and statistics fixture\n"
    "[OPTIONS]\nFLOW_UNITS CMS\nINFILTRATION HORTON\nFLOW_ROUTING KINWAVE\n"
    "START_DATE 01/01/2026\nSTART_TIME 00:00:00\nREPORT_START_DATE 01/01/2026\n"
    "REPORT_START_TIME 00:00:00\nEND_DATE 01/01/2026\nEND_TIME 02:00:00\n"
    "WET_STEP 00:01:00\nDRY_STEP 00:01:00\nREPORT_STEP 00:01:00\nROUTING_STEP 60\n"
    "[RAINGAGES]\nRG1 INTENSITY 0:01 1.0 TIMESERIES RAIN\n"
    "[SUBCATCHMENTS]\nS1 RG1 J1 2 50 100 0.5 0\n"
    "[SUBAREAS]\nS1 0.01 0.1 0.05 0.05 25 OUTLET\n"
    "[INFILTRATION]\nS1 30 5 4 7 0\n"
    "[JUNCTIONS]\nJ1 10 2 0 0 0\nJ2 9 2 0 0 0\n"
    "[OUTFALLS]\nO1 8 FREE NO\n"
    "[CONDUITS]\nC1 J1 J2 100 0.013 0 0 0\nC2 J2 O1 100 0.013 0 0 0\n"
    "[XSECTIONS]\nC1 CIRCULAR 1 0 0 0 1\nC2 CIRCULAR 1 0 0 0 1\n"
    "[POLLUTANTS]\nTSS MG/L 0 0 0 0 0\nLead UG/L 0 0 0 0 0\n"
    "[TIMESERIES]\nRAIN 0:00 0\nRAIN 0:10 20\nRAIN 0:40 20\nRAIN 0:41 0\n"
    "QIN 0:00 0.0\nQIN 0:20 0.05\nQIN 2:00 0.05\n"
    "[INFLOWS]\nJ1 FLOW QIN FLOW 1.0 1.0\nJ1 TSS \"\" CONCEN 1.0 1.0 10\n"
    "J1 Lead \"\" CONCEN 1.0 1.0 2000\n"
    "[REPORT]\nSUBCATCHMENTS ALL\nNODES ALL\nLINKS ALL\n"
    "[COORDINATES]\nJ1 0 0\nJ2 100 0\nO1 200 0\n"
    "[Polygons]\nS1 -50 -50\nS1 -50 50\nS1 -10 50\nS1 -10 -50\n";

/** @return Column whose header starts with any of \p prefixes, or -1. */
int columnOf(const QAbstractItemModel *m, const QStringList &prefixes)
{
    for (int c = 0; c < m->columnCount(); ++c) {
        const QString h = m->headerData(c, Qt::Horizontal).toString();
        for (const QString &p : prefixes)
            if (h.startsWith(p))
                return c;
    }
    return -1;
}

/** @return Row whose first column reads \p name, or -1. */
int rowOf(const QAbstractItemModel *m, const QString &name)
{
    for (int r = 0; r < m->rowCount(); ++r)
        if (m->index(r, 0).data(Qt::DisplayRole).toString() == name)
            return r;
    return -1;
}

double cell(const QAbstractItemModel *m, int r, int c)
{
    const QVariant v = m->index(r, c).data(Qt::EditRole);
    return v.isValid() && !v.toString().isEmpty() ? v.toDouble()
                                                  : std::numeric_limits<double>::quiet_NaN();
}

/** Numbers on the row of \p object in the `.rpt` block titled \p section. */
QStringList reportRow(const QString &rpt, const QString &section, const QString &object)
{
    QFile f(rpt);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    bool in = false;
    for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
        in = in || line.contains(section);
        const QStringList parts =
            line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (in && !parts.isEmpty() && parts.first() == object)
            return parts.mid(1);
    }
    return {};
}

using openswmmvis::io::Mesh2DResultVariable;
using openswmmvis::io::Mesh2DValueStatus;

/*! Four triangles around an off-centre vertex (areas 0.5, 1.5, 1.5, 0.5 m2),
 *  three one-minute frames, a TSS surface concentration (MG/L) with cell 2
 *  Missing at frame 1 and cell 3 waterless, and depths 0.1(c+1) + 0.01 t
 *  (cell 3 dry). */
class RegionMeshSource : public IMesh2DSource
{
public:
    static constexpr int kFrames = 3;
    int vertexCount() const override { return 5; }
    int triangleCount() const override { return 4; }
    int timeCount() const override { return kFrames; }
    bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy, std::vector<double> &vz,
                          std::vector<std::array<int, 3>> &tris) override
    {
        vx = {0.0, 2.0, 2.0, 0.0, 0.5};
        vy = {0.0, 0.0, 2.0, 2.0, 0.5};
        vz = {1.0, 1.0, 1.0, 1.0, 1.0};
        tris = {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
        return true;
    }
    static double depth(int c, int t) { return c == 3 ? 0.0 : 0.1 * (c + 1) + 0.01 * t; }
    static double conc(int c, int t) { return c == 3 ? 0.0 : 10.0 * (c + 1) + t; }
    static Mesh2DValueStatus state(int c, int t)
    {
        if (c == 3)
            return Mesh2DValueStatus::Waterless;
        return c == 2 && t == 1 ? Mesh2DValueStatus::Missing : Mesh2DValueStatus::Valid;
    }
    bool readDepthsAt(int t, std::vector<float> &depths) override
    {
        if (t < 0 || t >= kFrames)
            return false;
        depths.resize(4);
        for (int c = 0; c < 4; ++c)
            depths[std::size_t(c)] = float(depth(c, t));
        return true;
    }
    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).addSecs(qint64(t) * 60);
    }
    static Mesh2DResultVariable tss()
    {
        Mesh2DResultVariable v;
        v.dataset = QStringLiteral("Mesh2_face_species_conc");
        v.species = QStringLiteral("TSS");
        v.label = QStringLiteral("Surface — TSS — concentration");
        v.units = QStringLiteral("MG/L");
        v.unitsKnown = true;
        v.frameCount = kFrames;
        return v;
    }
    QVector<Mesh2DResultVariable> faceVariables(QStringList *warnings) const override
    {
        if (warnings)
            warnings->clear();
        return {tss()};
    }
    bool readFaceVariableAt(const Mesh2DResultVariable &v, int t, std::vector<float> &values,
                            std::vector<Mesh2DValueStatus> &status) override
    {
        values.clear();
        status.clear();
        if (t < 0 || t >= kFrames || v.key() != tss().key())
            return false;
        for (int c = 0; c < 4; ++c) {
            status.push_back(state(c, t));
            values.push_back(state(c, t) == Mesh2DValueStatus::Missing
                                 ? std::numeric_limits<float>::quiet_NaN()
                                 : float(conc(c, t)));
        }
        return true;
    }
};

const double kArea[4] = {0.5, 1.5, 1.5, 0.5};
const QStringList kTags = {"A", "A", "B", ""};

/** Hand area-weighted mean of the valid cells of \p cells at frame \p t. */
double handAwm(const std::vector<int> &cells, int t)
{
    double sum = 0, area = 0;
    for (int c : cells)
        if (RegionMeshSource::state(c, t) == Mesh2DValueStatus::Valid) {
            sum += RegionMeshSource::conc(c, t) * kArea[c];
            area += kArea[c];
        }
    return area > 0 ? sum / area : std::numeric_limits<double>::quiet_NaN();
}

/** Hand mass (kg) of the cells: mg/L x m x m2 x 1e-3; missing excluded. */
double handMass(const std::vector<int> &cells, int t)
{
    double kg = 0;
    for (int c : cells)
        if (RegionMeshSource::state(c, t) == Mesh2DValueStatus::Valid)
            kg += RegionMeshSource::conc(c, t) * RegionMeshSource::depth(c, t) * kArea[c] * 1e-3;
    return kg;
}

} // namespace

class TestResultsTablesDialogs : public QObject
{
    Q_OBJECT

    QString folder;
    std::unique_ptr<SWMMModelLayer> model;
    std::unique_ptr<SWMMResultsLayer> results;

    QString path(const QString &name) const { return QDir(folder).filePath(name); }

    /** @return The `.out` series of one object (engine reader, file units). */
    std::vector<float> outSeries(int kind, const char *id, int var) const
    {
        SWMM_Output h = results->outputHandle();
        const int periods = swmm_output_get_period_count(h);
        std::vector<float> v(std::size_t(std::max(0, periods)));
        int idx = -1;
        const int n = kind == 0   ? swmm_output_get_node_count(h)
                      : kind == 1 ? swmm_output_get_link_count(h)
                                  : swmm_output_get_subcatch_count(h);
        for (int i = 0; i < n; ++i) {
            const char *name = kind == 0   ? swmm_output_get_node_id(h, i)
                               : kind == 1 ? swmm_output_get_link_id(h, i)
                                           : swmm_output_get_subcatch_id(h, i);
            if (QString::fromUtf8(name) == QLatin1String(id))
                idx = i;
        }
        if (idx < 0 || periods <= 0)
            return {};
        const int rc = kind == 0 ? swmm_output_get_node_series(h, idx, var, 0, periods - 1, v.data())
                       : kind == 1
                           ? swmm_output_get_link_series(h, idx, var, 0, periods - 1, v.data())
                           : swmm_output_get_subcatch_series(h, idx, var, 0, periods - 1, v.data());
        return rc == 0 ? v : std::vector<float>{};
    }

private slots:
    void initTestCase()
    {
        folder = QStringLiteral(PHASE4B_OUTPUT);
        QVERIFY(QDir().mkpath(folder));
        QFile f(path("tables.inp"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(kDeck);
        f.close();
        QCOMPARE(swmm_engine_run(path("tables.inp").toUtf8().constData(),
                                 path("tables.rpt").toUtf8().constData(),
                                 path("tables.out").toUtf8().constData(), nullptr),
                 0);
        model = std::make_unique<SWMMModelLayer>(path("tables.inp"));
        QList<QString> warnings, errors;
        QVERIFY2(model->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        results = std::make_unique<SWMMResultsLayer>(path("tables.out"), model.get());
        QVERIFY(results->openResults(warnings, errors));
        QCOMPARE(results->totalTimeSteps(), 120);
    }

    /*! Statistics dashboard: hydraulic maxima and the sample sums equal the
     *  engine-read `.out` series (display rounding as the dashboard rounds). */
    void statisticsHydraulicColumnsUnchanged()
    {
        openswmmvis::ui::StatisticsDashboardDialog dlg(results.get());
        auto *nodes = dlg.findChild<QTableView *>(QStringLiteral("nodeTable"))->model();
        auto *links = dlg.findChild<QTableView *>(QStringLiteral("linkTable"))->model();
        auto *subs = dlg.findChild<QTableView *>(QStringLiteral("subTable"))->model();
        QCOMPARE(nodes->rowCount(), 3);
        QCOMPARE(links->rowCount(), 2);
        QCOMPARE(subs->rowCount(), 1);

        const auto max = [](const std::vector<float> &v) {
            return double(*std::max_element(v.begin(), v.end()));
        };
        const auto sum = [](const std::vector<float> &v) {
            return std::accumulate(v.begin(), v.end(), 0.0);
        };
        const auto round = [](double v, int d) { return QString::number(v, 'f', d).toDouble(); };

        for (const char *id : {"J1", "J2", "O1"}) {
            const int r = rowOf(nodes, QString::fromLatin1(id));
            QVERIFY(r >= 0);
            QCOMPARE(cell(nodes, r, columnOf(nodes, {"Max depth"})),
                     round(max(outSeries(0, id, SWMM_OUT_NODE_DEPTH)), 3));
            QCOMPARE(cell(nodes, r, columnOf(nodes, {"Max head"})),
                     round(max(outSeries(0, id, SWMM_OUT_NODE_HEAD)), 3));
            QCOMPARE(cell(nodes, r, columnOf(nodes, {"Max overflow"})),
                     round(max(outSeries(0, id, SWMM_OUT_NODE_OVERFLOW)), 3));
            // Before V6 the header was "Volume"; it is now labelled a sample sum.
            QCOMPARE(cell(nodes, r, columnOf(nodes, {"Volume"})),
                     round(sum(outSeries(0, id, SWMM_OUT_NODE_VOLUME)), 1));
        }
        for (const char *id : {"C1", "C2"}) {
            const int r = rowOf(links, QString::fromLatin1(id));
            QVERIFY(r >= 0);
            QCOMPARE(cell(links, r, columnOf(links, {"Max flow"})),
                     round(max(outSeries(1, id, SWMM_OUT_LINK_FLOW)), 3));
            QCOMPARE(cell(links, r, columnOf(links, {"Max depth"})),
                     round(max(outSeries(1, id, SWMM_OUT_LINK_DEPTH)), 3));
            QCOMPARE(cell(links, r, columnOf(links, {"Max velocity"})),
                     round(max(outSeries(1, id, SWMM_OUT_LINK_VELOCITY)), 3));
            QCOMPARE(cell(links, r, columnOf(links, {"Max capacity"})),
                     round(max(outSeries(1, id, SWMM_OUT_LINK_CAPACITY)), 3));
        }
        const int s = rowOf(subs, QStringLiteral("S1"));
        QVERIFY(s >= 0);
        QCOMPARE(cell(subs, s, columnOf(subs, {"Peak runoff"})),
                 round(max(outSeries(2, "S1", SWMM_OUT_SUBCATCH_RUNOFF)), 3));
        QCOMPARE(cell(subs, s, columnOf(subs, {"Total runoff", "Runoff (sample sum)"})),
                 round(sum(outSeries(2, "S1", SWMM_OUT_SUBCATCH_RUNOFF)), 3));
        QCOMPARE(cell(subs, s, columnOf(subs, {"Total infil", "Infiltration (sample sum)"})),
                 round(sum(outSeries(2, "S1", SWMM_OUT_SUBCATCH_INFIL)), 3));
        QCOMPARE(cell(subs, s, columnOf(subs, {"Total evap", "Evaporation (sample sum)"})),
                 round(sum(outSeries(2, "S1", SWMM_OUT_SUBCATCH_EVAP)), 3));
    }

    /*! Tabular Results: the default view (Nodes, first variable = depth, by
     *  object) and the by-variable view of the first node carry the `.out`
     *  values, period by period. */
    void tabularHydraulicValuesUnchanged()
    {
        openswmmvis::ui::TabularResultsDialog dlg(results.get());
        auto *table = dlg.findChild<QTableView *>();
        QVERIFY(table);
        auto *m = table->model();
        QCOMPARE(m->rowCount(), 3);
        const int periods = results->totalTimeSteps();
        for (const char *id : {"J1", "J2", "O1"}) {
            const int r = rowOf(m, QString::fromLatin1(id));
            QVERIFY(r >= 0);
            const auto depth = outSeries(0, id, SWMM_OUT_NODE_DEPTH);
            for (int p = 0; p < periods; ++p)
                QVERIFY2(std::fabs(cell(m, r, 1 + p) - double(depth[std::size_t(p)])) < 1e-6,
                         qPrintable(QStringLiteral("%1 period %2").arg(id).arg(p)));
        }

        QRadioButton *byVariable = nullptr;
        for (auto *b : dlg.findChildren<QRadioButton *>())
            if (b->text().contains(QStringLiteral("variable")))
                byVariable = b;
        QVERIFY(byVariable);
        byVariable->setChecked(true);
        m = table->model();
        QCOMPARE(m->rowCount(), periods);
        const int depthCol = columnOf(m, {"Depth"});
        const int headCol = columnOf(m, {"Head"});
        QVERIFY(depthCol > 0 && headCol > 0);
        const auto depth = outSeries(0, "J1", SWMM_OUT_NODE_DEPTH);
        const auto head = outSeries(0, "J1", SWMM_OUT_NODE_HEAD);
        for (int p = 0; p < periods; ++p) {
            QVERIFY(std::fabs(cell(m, p, depthCol) - double(depth[std::size_t(p)])) < 1e-6);
            QVERIFY(std::fabs(cell(m, p, headCol) - double(head[std::size_t(p)])) < 1e-6);
        }
    }

    /*! V6 + R4: by-variable Tabular Results for an outfall lists every
     *  species (catalog label, the .out's units) and each pollutant's load
     *  and cumulative load; the cumulative ends at the .rpt outfall total. */
    void tabularListsSpeciesAndLoads()
    {
        openswmmvis::ui::TabularResultsDialog dlg(results.get());
        for (auto *b : dlg.findChildren<QRadioButton *>())
            if (b->text().contains(QStringLiteral("variable")))
                b->setChecked(true);
        auto *objects = dlg.findChild<QComboBox *>(QStringLiteral("tabularVariableOrObject"));
        QVERIFY(objects);
        objects->setCurrentIndex(objects->findText(QStringLiteral("O1")));
        const auto &t = dlg.table();
        const int tss = int(t.columnHeaders.indexOf(QStringLiteral("TSS [pollutant]")));
        const int lead = int(t.columnHeaders.indexOf(QStringLiteral("Lead [pollutant]")));
        QVERIFY2(tss > 0 && lead > 0, qPrintable(t.columnHeaders.join(" | ")));
        QCOMPARE(t.columnUnits[tss], QStringLiteral("mg/L"));
        QCOMPARE(t.columnUnits[lead], QStringLiteral("ug/L"));
        // The labels say these come from report-step samples, not the .rpt totals.
        const int rate = int(t.columnHeaders.indexOf(
            QStringLiteral("TSS load (flow × concentration at report steps)")));
        const int cum = int(t.columnHeaders.indexOf(
            QStringLiteral("TSS cumulative load (integrated from report-step samples)")));
        QVERIFY2(rate > 0 && cum > 0, qPrintable(t.columnHeaders.join(" | ")));
        QCOMPARE(t.columnUnits[rate], QStringLiteral("kg/d"));
        QCOMPARE(t.columnUnits[cum], QStringLiteral("kg"));
        // ... and the header tooltip explains how they differ from the .rpt.
        const QString tip = dlg.findChild<QTableView *>()
                                ->model()
                                ->headerData(1 + cum, Qt::Horizontal, Qt::ToolTipRole)
                                .toString();
        QVERIFY2(tip.contains(QStringLiteral("report-step values")) &&
                     tip.contains(QStringLiteral("every routing step")),
                 qPrintable(tip));
        // Rate = total inflow x concentration, converted (CMS x mg/L -> kg/d).
        const auto q = outSeries(0, "O1", SWMM_OUT_NODE_TOTAL_INFLOW);
        const auto c = outSeries(0, "O1", SWMM_OUT_NODE_POLLUT_BASE + 0);
        for (int p = 0; p < t.rowCount(); ++p)
            QVERIFY(std::fabs(t.values[p][rate] - double(q[std::size_t(p)]) *
                                                      double(c[std::size_t(p)]) * 86.4) < 1e-6);
        const QStringList row = reportRow(path("tables.rpt"), "Outfall Loading Summary", "O1");
        QVERIFY(!row.isEmpty());
        const double printed = row.at(4).toDouble();
        QVERIFY2(std::fabs(t.values.last()[cum] - printed) < 0.0005 + printed * 2e-4,
                 qPrintable(QStringLiteral("%1 vs %2").arg(t.values.last()[cum]).arg(printed)));
        // Monotone: a cumulative never decreases while flow is out of the node.
        for (int p = 1; p < t.rowCount(); ++p)
            QVERIFY(t.values[p][cum] >= t.values[p - 1][cum] - 1e-12);
        QVERIFY(t.rowStatus.last().isEmpty());

        // CSV: header, Units row, values, Status.
        const QString csv = path("tabular_O1_by_variable.csv");
        QString error;
        QVERIFY2(dlg.exportTo(csv, QLatin1Char(','), &error), qPrintable(error));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2 + results->totalTimeSteps());
        QVERIFY(lines[0].startsWith(QStringLiteral("Time,")) && lines[0].endsWith(",Status"));
        QVERIFY(lines[1].startsWith(QStringLiteral("Units,")));
        QVERIFY(lines[1].contains(QStringLiteral(",mg/L,")) && lines[1].contains(",kg/d,") &&
                lines[1].contains(",ug/L,"));
    }

    /*! V6 + R4: Statistics lists species maxima and pollutant totals; the
     *  totals equal the .rpt load summaries; sample sums are labelled and
     *  the subcatchment rates gain time integrals. */
    void statisticsListsSpeciesLoadsAndIntegrals()
    {
        openswmmvis::ui::StatisticsDashboardDialog dlg(results.get());
        auto *nodes = dlg.findChild<QTableView *>(QStringLiteral("nodeTable"))->model();
        auto *links = dlg.findChild<QTableView *>(QStringLiteral("linkTable"))->model();
        auto *subs = dlg.findChild<QTableView *>(QStringLiteral("subTable"))->model();
        QVERIFY(columnOf(nodes, {"Volume (sample sum)"}) > 0);
        QVERIFY(columnOf(nodes, {"Max TSS"}) > 0 && columnOf(nodes, {"Max Lead"}) > 0);
        const int o1 = rowOf(nodes, QStringLiteral("O1"));
        const QStringList outfall = reportRow(path("tables.rpt"), "Outfall Loading Summary", "O1");
        for (int p = 0; p < 2; ++p) {
            const double v = cell(nodes, o1, columnOf(nodes, {p == 0 ? "TSS load" : "Lead load"}));
            const double printed = outfall.at(4 + p).toDouble();
            QVERIFY2(std::fabs(v - printed) < 0.0005 + printed * 2e-4,
                     qPrintable(QStringLiteral("%1 vs %2").arg(v).arg(printed)));
        }
        // Link loads (this engine prints them in lb even for SI runs).
        const int c2 = rowOf(links, QStringLiteral("C2"));
        const QStringList link = reportRow(path("tables.rpt"), "Link Pollutant Load Summary", "C2");
        const double kgPerLb = 0.45359237;
        const int linkLoad = columnOf(links, {"TSS net load (integrated from report-step samples)"});
        QVERIFY(linkLoad > 0);
        QVERIFY(links->headerData(linkLoad, Qt::Horizontal, Qt::ToolTipRole)
                    .toString()
                    .contains(QStringLiteral("net of reversing flow")));
        const double tssKg = cell(links, c2, linkLoad);
        QVERIFY2(std::fabs(tssKg / kgPerLb - link.at(0).toDouble()) < 0.0005 + tssKg * 5e-4,
                 qPrintable(QStringLiteral("%1 kg vs %2 lb").arg(tssKg).arg(link.at(0))));
        QVERIFY(columnOf(nodes, {"TSS load (integrated from report-step samples)"}) > 0);
        const QString nodeTip =
            nodes->headerData(columnOf(nodes, {"TSS load"}), Qt::Horizontal, Qt::ToolTipRole)
                .toString();
        QVERIFY2(nodeTip.contains(QStringLiteral("kg")) &&
                     nodeTip.contains(QStringLiteral("every routing step")),
                 qPrintable(nodeTip));

        // Subcatchment: sample sums labelled; runoff volume is the time integral.
        QVERIFY(columnOf(subs, {"Runoff (sample sum)"}) > 0);
        const int s1 = rowOf(subs, QStringLiteral("S1"));
        const auto runoff = outSeries(2, "S1", SWMM_OUT_SUBCATCH_RUNOFF);
        const double volume = std::accumulate(runoff.begin(), runoff.end(), 0.0) * 60.0; // m3
        QVERIFY(std::fabs(cell(subs, s1, columnOf(subs, {"Runoff volume"})) - volume) <
                0.0005 + volume * 1e-6);
        // With a uniform 60 s step the integral is the sample sum x 60 s: the
        // two columns are different quantities (m3 vs m3/s summed).
        QVERIFY(std::fabs(cell(subs, s1, columnOf(subs, {"Runoff (sample sum)"})) * 60.0 - volume) <
                0.06);

        // Export: each section has its Units row and a Status column.
        const QString csv = path("statistics.csv");
        QVERIFY(dlg.exportTo(csv));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
        QVERIFY(lines[1].startsWith(QStringLiteral("Node,Max depth,Max head,Max overflow,"
                                                   "Volume (sample sum),")));
        QVERIFY(lines[1].endsWith(QStringLiteral(",Status")));
        // Flow columns are in the run's flow units (CMS here), not assumed m3/s.
        QVERIFY2(lines[2].startsWith(QStringLiteral("Units,m,m,CMS,m³,")), qPrintable(lines[2]));
        QVERIFY2(lines[2].contains(QStringLiteral(",mg/L,kg,ug/L,kg,")), qPrintable(lines[2]));
    }

    /*! V6 gate: the 2D cell series in Tabular Results equals what the map's
     *  selected-cell table reads (resultFrame) frame by frame; a Missing cell
     *  is blank, not zero. */
    void meshCellSeriesEqualsTheMapSelectedCellTable()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<RegionMeshSource>());
        QCOMPARE(int(layer.triCellMap().size()), 4);
        layer.highlightCells({0, 2});
        openswmmvis::ui::TabularResultsDialog dlg(nullptr, &layer);
        auto *kind = dlg.findChild<QComboBox *>(QStringLiteral("tabularKind"));
        QCOMPARE(kind->count(), 2);
        kind->setCurrentIndex(kind->findData(int(openswmmvis::ui::TabularResultsDialog::Cells2D)));
        auto *variable = dlg.findChild<QComboBox *>(QStringLiteral("tabularVariableOrObject"));
        const int tssIndex = variable->findText(QStringLiteral("Surface — TSS — concentration (MG/L)"));
        QVERIFY2(tssIndex >= 0, qPrintable(variable->itemText(0)));
        variable->setCurrentIndex(tssIndex);
        const auto &t = dlg.table();
        QCOMPARE(t.rowLabels, (QStringList{"Cell 0", "Cell 2"}));
        QCOMPARE(t.columnCount(), RegionMeshSource::kFrames);
        const QString key = RegionMeshSource::tss().key();
        for (int f = 0; f < RegionMeshSource::kFrames; ++f) {
            const auto frame = layer.resultFrame(key, f);
            QVERIFY2(frame && frame->error.isEmpty(), frame ? qPrintable(frame->error) : "null");
            for (int r = 0; r < 2; ++r) {
                const int c = r == 0 ? 0 : 2;
                if (frame->status[std::size_t(c)] == Mesh2DValueStatus::Valid)
                    QCOMPARE(t.values[r][f], double(frame->values[std::size_t(c)]));
                else
                    QVERIFY2(std::isnan(t.values[r][f]), "missing must be blank, not 0");
            }
        }
        QCOMPARE(t.rowStatus[1], QStringLiteral("1 of 3 values missing"));
        auto *table = dlg.findChild<QTableView *>();
        QVERIFY(!table->model()->index(1, 2).data(Qt::EditRole).isValid()); // blank cell
    }

    /*! V6 gate: region aggregates (selection, tags, domain) in Tabular
     *  Results and the Statistics 2D tab equal hand sums on the 4-cell
     *  fixture, including Mass = conc x depth x area. */
    void regionAggregatesMatchHandSums()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<RegionMeshSource>());
        layer.highlightCells({0, 2});
        openswmmvis::ui::TabularResultsDialog dlg(nullptr, &layer);
        dlg.setCellTags(kTags);
        auto *kind = dlg.findChild<QComboBox *>(QStringLiteral("tabularKind"));
        kind->setCurrentIndex(kind->findData(int(openswmmvis::ui::TabularResultsDialog::Regions2D)));
        auto *aggregate = dlg.findChild<QComboBox *>(QStringLiteral("tabularAggregate"));
        auto *variable = dlg.findChild<QComboBox *>(QStringLiteral("tabularVariableOrObject"));
        const auto pick = [&](openswmmvis::results::MeshAggregate a) {
            aggregate->setCurrentIndex(aggregate->findData(int(a)));
            variable->setCurrentIndex(variable->findText(QStringLiteral("Surface — TSS — concentration"),
                                                         Qt::MatchStartsWith));
        };
        const std::vector<std::vector<int>> cells = {{0, 2}, {0, 1}, {2}, {0, 1, 2, 3}};
        const QStringList labels = {"Selection (2 cells)", "Tag: A (2 cells)", "Tag: B (1 cells)",
                                    "Domain (4 cells)"};

        pick(openswmmvis::results::MeshAggregate::AreaWeightedMean);
        QCOMPARE(dlg.table().rowLabels, labels);
        for (int r = 0; r < 4; ++r)
            for (int f = 0; f < 3; ++f) {
                const double hand = handAwm(cells[std::size_t(r)], f);
                if (std::isnan(hand))
                    QVERIFY(std::isnan(dlg.table().values[r][f]));
                else
                    QVERIFY2(std::fabs(dlg.table().values[r][f] - hand) < 1e-4,
                             qPrintable(QStringLiteral("%1 frame %2").arg(labels[r]).arg(f)));
            }
        // Tag B is only cell 2: missing at frame 1 -> blank, not 0.
        QVERIFY(std::isnan(dlg.table().values[2][1]));
        // The selection lost cell 2 at frame 1: its status says so.
        QVERIFY2(dlg.table().rowStatus[0].contains(
                     QStringLiteral("1 of 3 frames aggregate without Missing cells")),
                 qPrintable(dlg.table().rowStatus[0]));
        QVERIFY(dlg.table().rowStatus[1].isEmpty()); // tag A never misses a cell

        pick(openswmmvis::results::MeshAggregate::Mass);
        QCOMPARE(dlg.table().columnUnits.value(0), QStringLiteral("kg"));
        for (int r = 0; r < 4; ++r)
            for (int f = 0; f < 3; ++f)
                if (!(r == 2 && f == 1))
                    QVERIFY2(std::fabs(dlg.table().values[r][f] - handMass(cells[std::size_t(r)], f)) <
                                 1e-7,
                             qPrintable(QStringLiteral("mass %1 frame %2").arg(labels[r]).arg(f)));
        // Depth has no mass: Mass lists only mass-capable variables.
        for (int i = 0; i < variable->count(); ++i)
            QVERIFY(!variable->itemText(i).startsWith(QStringLiteral("Depth")));

        const QString csv = path("regions_mass.csv");
        QString error;
        QVERIFY2(dlg.exportTo(csv, QLatin1Char(','), &error), qPrintable(error));

        // The Statistics dashboard's 2D tab reads the same region series.
        openswmmvis::ui::StatisticsDashboardDialog stats(nullptr);
        stats.set2DResults(&layer, kTags);
        auto *meshVariable = stats.findChild<QComboBox *>(QStringLiteral("meshStatsVariable"));
        auto *meshAggregate = stats.findChild<QComboBox *>(QStringLiteral("meshStatsAggregate"));
        meshVariable->setCurrentIndex(
            meshVariable->findText(QStringLiteral("Surface — TSS — concentration"), Qt::MatchStartsWith));
        meshAggregate->setCurrentIndex(
            meshAggregate->findData(int(openswmmvis::results::MeshAggregate::AreaWeightedMean)));
        auto *mesh = stats.findChild<QTableView *>(QStringLiteral("meshTable"))->model();
        QCOMPARE(mesh->rowCount(), 2 + 4); // Cell 0, Cell 2, selection, A, B, domain
        const int domain = rowOf(mesh, QStringLiteral("Domain (4 cells)"));
        QVERIFY(domain >= 0);
        const double finalHand = handAwm({0, 1, 2, 3}, 2);
        QVERIFY(std::fabs(cell(mesh, domain, columnOf(mesh, {"Final"})) - finalHand) < 1e-4);
        double maxHand = 0;
        for (int f = 0; f < 3; ++f)
            maxHand = std::max(maxHand, handAwm({0, 1, 2, 3}, f));
        QVERIFY(std::fabs(cell(mesh, domain, columnOf(mesh, {"Max"})) - maxHand) < 1e-4);
        // Time integral: frames held over the minute ending at them (frame 0 at the start).
        const double integralHand = (handAwm({0, 1, 2, 3}, 1) + handAwm({0, 1, 2, 3}, 2)) * 60.0;
        QVERIFY(std::fabs(cell(mesh, domain, columnOf(mesh, {"Time integral"})) - integralHand) <
                1e-2);
        // Per-cell rows are the cell values themselves.
        const int cell2 = rowOf(mesh, QStringLiteral("Cell 2"));
        QVERIFY(std::fabs(cell(mesh, cell2, columnOf(mesh, {"Final"})) -
                          RegionMeshSource::conc(2, 2)) < 1e-4);
    }

    /*! V0: the node picker lists every catalog species with kind badge and
     *  units, its loads, and a 2D-only species disabled; descriptors from
     *  the run carry the same labels. */
    void pickersUseTheRunCatalog()
    {
        auto meshVariables = QVector<Mesh2DResultVariable>{RegionMeshSource::tss()};
        auto nh4 = RegionMeshSource::tss();
        nh4.species = QStringLiteral("NH4");
        meshVariables.append(nh4);
        const auto catalog = results->speciesCatalog(meshVariables);
        std::unique_ptr<QMenu> menu(openswmmvis::ui::AttributePickerMenu::createForObjectKind(
            openswmmvis::plot::ObjectRef::Kind::Node, openswmmvis::plot::UnitSystem::SI, nullptr,
            catalog, results->flowUnits()));
        QVERIFY(menu);
        QHash<QString, QAction *> byText;
        for (QAction *a : menu->actions())
            byText.insert(a->text(), a);
        QVERIFY(byText.contains(QStringLiteral("TSS [pollutant] (mg/L)")));
        QVERIFY(byText.contains(QStringLiteral("Lead [pollutant · 1D only] (ug/L)")));
        QAction *msx = byText.value(QStringLiteral("NH4 [species · 2D only] (mg/L)"));
        QVERIFY(msx && !msx->isEnabled());
        QAction *load = byText.value(
            QStringLiteral("TSS cumulative load (integrated from report-step samples) (kg)"));
        QVERIFY(load);
        QVERIFY(load->toolTip().contains(QStringLiteral("every routing step")));
        const auto d = openswmmvis::ui::AttributePickerMenu::descriptorFrom(load);
        QVERIFY(openswmmvis::plot::pollutantload::isLoadDescriptor(d));
        // The same descriptor resolves through the .out run.
        openswmmvis::plot::SwmmOutRunLayer run(results.get());
        openswmmvis::plot::SeriesData series;
        run.getSeriesAt(openswmmvis::plot::ObjectRef::forNode(QStringLiteral("O1")), d, series);
        QVERIFY2(series.ok, qPrintable(series.errorMessage));
        QCOMPARE(int(series.values.size()), results->totalTimeSteps());
        // A link offers NET loads; a subcatchment has no loads.
        std::unique_ptr<QMenu> linkMenu(openswmmvis::ui::AttributePickerMenu::createForObjectKind(
            openswmmvis::plot::ObjectRef::Kind::Link, openswmmvis::plot::UnitSystem::SI, nullptr,
            catalog, results->flowUnits()));
        QAction *net = nullptr;
        for (QAction *a : linkMenu->actions())
            if (a->text() == QStringLiteral("TSS net load (flow × concentration at report steps) "
                                            "(kg/d)"))
                net = a;
        QVERIFY(net && net->toolTip().contains(QStringLiteral("net of reversing flow")));
        std::unique_ptr<QMenu> sub(openswmmvis::ui::AttributePickerMenu::createForObjectKind(
            openswmmvis::plot::ObjectRef::Kind::Subcatch, openswmmvis::plot::UnitSystem::SI,
            nullptr, catalog, results->flowUnits()));
        for (QAction *a : sub->actions())
            QVERIFY(!a->text().contains(QStringLiteral("load")));
        // Water age has no load: the run refuses, never zeros.
        openswmmvis::plot::SeriesData none;
        run.getSeriesAt(openswmmvis::plot::ObjectRef::forNode(QStringLiteral("O1")),
                        openswmmvis::plot::pollutantload::rateDescriptor(
                            QStringLiteral("__WATER_AGE__"), {}, false),
                        none);
        QVERIFY(!none.ok && none.values.empty());
    }

    /*! Real-run cross-check (optional; PHASE4B_BELLINGE_DIR names the
     *  read-only examples/bellinge_2d_gw_quality folder): the catalog, the
     *  outfall loads against the .rpt (report-sampled vs routing-step, so
     *  recorded, not equated) and 2D domain aggregates are written to
     *  bellinge_cross_check.csv. Nothing is written into the example. */
    void bellingeRealRunCrossCheck()
    {
        const QString dir = qEnvironmentVariable("PHASE4B_BELLINGE_DIR");
        if (dir.isEmpty())
            QSKIP("Set PHASE4B_BELLINGE_DIR to the bellinge_2d_gw_quality folder to run.");
        const QString stem = QDir(dir).filePath(QStringLiteral("BellingeSWMM_v021_gw_quality"));
        SWMMResultsLayer out(stem + QStringLiteral(".out"), nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(out.openResults(warnings, errors), qPrintable(errors.join('\n')));
        QStringList log{QStringLiteral("check,object,value,report,units,note")};

        auto source = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(source->open(stem + QStringLiteral(".2d.h5")));
        SWMM2DResultsLayer mesh;
        mesh.setSource(std::move(source));
        const auto catalog = out.speciesCatalog(mesh.resultVariables());
        for (const auto &s : catalog.entries())
            log << QStringLiteral("catalog,%1,%2,,%3,%4")
                       .arg(s.name)
                       .arg(s.reportedIndex)
                       .arg(s.units, catalog.pickerLabel(s));
        QVERIFY(catalog.find(QStringLiteral("SurfPoll")) &&
                catalog.find(QStringLiteral("SurfPoll"))->in1D() &&
                catalog.find(QStringLiteral("SurfPoll"))->in2D());

        // Outfall totals: .out report samples (15 min) vs the engine's 10 s sums.
        openswmmvis::plot::SwmmOutRunLayer run(&out);
        const auto cumulative = [&](const QString &node, const QString &species) {
            QString key;
            for (const auto &d : run.resultDescriptorsForKind(openswmmvis::plot::ObjectRef::Kind::Node))
                if (d.isMeshVariable() && d.variableKey ==
                                              openswmmvis::plot::pollutantload::cumulativeKey(species))
                    key = d.variableKey;
            openswmmvis::plot::SeriesData s;
            run.getSeriesAt(openswmmvis::plot::ObjectRef::forNode(node),
                            openswmmvis::plot::ResultDescriptor::forMeshVariable(key, {}, {}), s);
            return s.ok && !s.values.empty() ? s.values.back()
                                             : std::numeric_limits<double>::quiet_NaN();
        };
        for (const char *outfall : {"F74F360", "G60K220", "G60U02F", "G70U01F", "G80U03F"}) {
            const QStringList row =
                reportRow(stem + QStringLiteral(".rpt"), "Outfall Loading Summary", outfall);
            QVERIFY(row.size() >= 6);
            for (int p = 0; p < 2; ++p) {
                const QString species = p == 0 ? QStringLiteral("SurfPoll") : QStringLiteral("GWTracer");
                const double gui = cumulative(QString::fromLatin1(outfall), species);
                QVERIFY(std::isfinite(gui));
                log << QStringLiteral("outfall load %1,%2,%3,%4,kg,report-sampled integral vs "
                                      "routing-step total")
                           .arg(species, outfall)
                           .arg(gui, 0, 'f', 4)
                           .arg(row.at(4 + p));
            }
        }

        // 2D domain aggregates of the surface SurfPoll concentration.
        openswmmvis::plot::Mesh2DRunLayer run2D(&mesh);
        const QString key = catalog.find(QStringLiteral("SurfPoll"))->meshKey;
        QVERIFY(!key.isEmpty());
        const auto conc = openswmmvis::plot::ResultDescriptor::forMeshVariable(key, {}, {});
        QVector<openswmmvis::plot::SeriesData> mass, awm;
        const QVector<openswmmvis::results::MeshRegion> domain{
            openswmmvis::results::domainRegion(mesh.source()->triangleCount())};
        run2D.getRegionSeries(domain, conc, openswmmvis::results::MeshAggregate::Mass, mass);
        run2D.getRegionSeries(domain, conc, openswmmvis::results::MeshAggregate::AreaWeightedMean,
                              awm);
        QVERIFY2(mass[0].ok, qPrintable(mass[0].errorMessage));
        QCOMPARE(int(mass[0].values.size()), mesh.source()->timeCount());
        const double peak = *std::max_element(mass[0].values.begin(), mass[0].values.end());
        log << QStringLiteral("2D domain SurfPoll surface mass peak,Domain,%1,,kg,frames %2")
                   .arg(peak, 0, 'g', 6)
                   .arg(mass[0].values.size());
        log << QStringLiteral("2D domain SurfPoll area-weighted mean final,Domain,%1,,mg/L,")
                   .arg(awm[0].values.back(), 0, 'g', 6);
        QFile f(path("bellinge_cross_check.csv"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        f.write(log.join('\n').toUtf8() + '\n');
    }

    /*! Tabular Results is reachable: Analysis > Tabular Results… */
    void tabularResultsIsReachable()
    {
        SWMMVis window;
        auto *action = window.findChild<QAction *>(QStringLiteral("actionTabularResults"));
        QVERIFY(action);
        QVERIFY(action->text().contains(QStringLiteral("Tabular")));
        QMenu *analysis = nullptr;
        for (QAction *top : window.menuBar()->actions())
            if (top->menu() && top->menu()->objectName() == QLatin1String("menuAnalysis"))
                analysis = top->menu();
        QVERIFY(analysis);
        QVERIFY(analysis->actions().contains(action));
    }
};

QTEST_MAIN(TestResultsTablesDialogs)
#include "test_resultstablesdialogs.moc"
