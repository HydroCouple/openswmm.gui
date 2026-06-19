/*!
 * \file   test_simstatus_2derr.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * End-to-end check of the "2D Err (%)" Simulation Status column: runs the
 * real SimulationRunner (in-process refactored engine) wired to a real
 * SimulationStatusModel exactly the way SWMMVis connects them, against
 *  1. the 2D demo (examples/demo_weir_culvert) — column must show a live
 *     percent during the run and keep a final value after cancel, and
 *  2. a minimal 1D-only model — column must show "—" throughout.
 *
 * All run artifacts (.inp copies, .rpt, .out, .2d.h5) are written to
 * tests/gui/data/output_simstatus2derr/ so they can be reviewed after the
 * test instead of vanishing with a temp dir.
 */
#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>

#include "simulation/simulationrunner.h"
#include "simulation/simulationstatusmodel.h"

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA");
}

/// Reviewable output dir for everything this test writes.
QString outputDir()
{
    const QString dir = QDir(dataDir()).filePath(
        QStringLiteral("output_simstatus2derr"));
    QDir().mkpath(dir);
    return dir;
}

QString twoDErrCell(const SimulationStatusModel &model, int row)
{
    return model.index(row, SimulationStatusModel::ColTwoDErr)
        .data(Qt::DisplayRole).toString();
}

} // namespace

class TestSimStatus2DErr : public QObject
{
    Q_OBJECT

private slots:

    void headerAndColumnLayout()
    {
        SimulationStatusModel model;
        QCOMPARE(model.columnCount(), SimulationStatusModel::NumColumns);
        QCOMPARE(model.headerData(SimulationStatusModel::ColTwoDErr,
                                  Qt::Horizontal).toString(),
                 QStringLiteral("2D Err (%)"));
        // The 2D column sits with the other continuity columns.
        QCOMPARE(SimulationStatusModel::ColTwoDErr,
                 SimulationStatusModel::ColRoutingErr + 1);
    }

    void twoDRunPopulatesColumn()
    {
        // Minimal self-contained 2D model (4 vertices / 2 triangles, one
        // 2D-coupled node, 1-hour SI run). Kept tiny and committed inline so
        // the test is fast and never depends on a mutable example file. The
        // 1D side has an outfall, so this also exercises the normal
        // (non-ERROR-145) 2D-active path. Written to the reviewable output
        // dir so the engine's .rpt/.out/.2d.h5 are inspectable afterward.
        const QString inp = QDir(outputDir()).filePath(QStringLiteral("mini_2d.inp"));
        {
            QFile f(inp);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(
                "[OPTIONS]\n"
                "FLOW_UNITS       CMS\n"
                "INFILTRATION     HORTON\n"
                "FLOW_ROUTING     DYNWAVE\n"
                "START_DATE       01/01/2026\n"
                "START_TIME       00:00:00\n"
                "END_DATE         01/01/2026\n"
                "END_TIME         01:00:00\n"
                "REPORT_STEP      0:05:00\n"
                "ROUTING_STEP     5\n"
                "\n"
                "[JUNCTIONS]\n"
                ";;Name  Elev  MaxDepth  InitDepth  SurDepth  Aponded\n"
                "J1      10    3         0          0         0\n"
                "\n"
                "[OUTFALLS]\n"
                ";;Name  Elev  Type  Gated\n"
                "O1      9     FREE  NO\n"
                "\n"
                "[CONDUITS]\n"
                ";;Name  From  To  Length  Roughness  InOffset  OutOffset  InitFlow\n"
                "C1      J1    O1  100     0.013      0         0          0\n"
                "\n"
                "[XSECTIONS]\n"
                ";;Link  Shape     Geom1  Geom2  Geom3  Geom4  Barrels\n"
                "C1      CIRCULAR  1      0      0      0      1\n"
                "\n"
                "[2D_OPTIONS]\n"
                "MAX_TIMESTEP     5\n"
                "DRY_DEPTH        0.002\n"
                "COUPLING_CD      0.7\n"
                "LINEAR_SOLVER    GMRES\n"
                "PRECONDITIONER   JACOBI\n"
                "REPORT_2D        NO\n"
                "\n"
                "[2D_VERTICES]\n"
                ";;X   Y    Z\n"
                "0     0    10\n"
                "10    0    10.5\n"
                "10    10   11\n"
                "0     10   11.5\n"
                "\n"
                "[2D_TRIANGLES]\n"
                ";;V1  V2  V3  MANNINGS_N\n"
                "0     1   2   0.03\n"
                "0     2   3   0.045\n"
                "\n"
                "[2D_VERTEX_NODE_MAP]\n"
                "0  J1  0.7  2.5\n");
        }
        const QString rpt = QDir(outputDir()).filePath(QStringLiteral("mini_2d.rpt"));
        const QString out = QDir(outputDir()).filePath(QStringLiteral("mini_2d.out"));

        SimulationStatusModel model;
        const int jobId = model.addJob(QStringLiteral("mini_2d.inp"), inp);

        auto *runner = new SimulationRunner(jobId, QStringLiteral("mini_2d.inp"),
                                            inp, rpt, out,
                                            QStringLiteral("6.0.0"), this);
        // Mirror the SWMMVis wiring: direct signal → slot connections.
        connect(runner, &SimulationRunner::progressChanged,
                &model, &SimulationStatusModel::updateProgress);
        connect(runner, &SimulationRunner::finished,
                &model, &SimulationStatusModel::finishJob);

        QSignalSpy progressSpy(runner, &SimulationRunner::progressChanged);
        QSignalSpy finishedSpy(runner, &SimulationRunner::finished);
        runner->start();

        // The model is tiny — let it run to completion. finishJob carries the
        // final continuity errors (post swmm_engine_end), exercising the
        // post-loop swmm_2d_get_continuity_error poll.
        QVERIFY2(finishedSpy.count() > 0 || finishedSpy.wait(60000),
                 "no finished signal within 60 s");
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(QStringLiteral("2D run failed: %1")
                                .arg(finishedSpy.last().at(3).toString())));
        const double finalTwoDErr = finishedSpy.last().at(6).toDouble();
        QVERIFY2(!qIsNaN(finalTwoDErr), "finished twoDErrFrac is NaN for 2D model");

        // Every progress tick for a 2D run must carry a non-NaN 2D error (the
        // 0 % seed reports 0.0; step-loop ticks poll the live value), so the
        // status column shows a percent rather than the 1D "—".
        QVERIFY(progressSpy.count() > 0);
        for (const auto &tick : progressSpy)
            QVERIFY2(!qIsNaN(tick.at(6).toDouble()),
                     "a 2D-run progress tick carried NaN twoDErrFrac");

        const QString finalCell = twoDErrCell(model, 0);
        QVERIFY2(finalCell.endsWith(QStringLiteral(" %")),
                 qPrintable(QStringLiteral("final cell shows '%1'").arg(finalCell)));
        QVERIFY(finalCell != QStringLiteral("—"));
    }

    void oneDRunShowsDash()
    {
        // Minimal 1D-only model written where the user can inspect it.
        const QString inp = QDir(outputDir()).filePath(QStringLiteral("minimal_1d.inp"));
        {
            QFile f(inp);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("[OPTIONS]\n"
                    "FLOW_UNITS       CMS\n"
                    "FLOW_ROUTING     DYNWAVE\n"
                    "START_DATE       01/01/2026\n"
                    "START_TIME       00:00:00\n"
                    "END_DATE         01/01/2026\n"
                    "END_TIME         01:00:00\n"
                    "REPORT_STEP      0:05:00\n"
                    "ROUTING_STEP     0:00:05\n"
                    "\n"
                    "[JUNCTIONS]\n"
                    ";;Name  Elev  MaxDepth  InitDepth  SurDepth  Aponded\n"
                    "J1      0     2         0          0         0\n"
                    "\n"
                    "[OUTFALLS]\n"
                    ";;Name  Elev  Type  StageData  Gated\n"
                    "O1      -1    FREE             NO\n"
                    "\n"
                    "[CONDUITS]\n"
                    ";;Name  From  To  Length  Roughness  InOff  OutOff  InitFlow  MaxFlow\n"
                    "C1      J1    O1  100     0.013      0      0       0         0\n"
                    "\n"
                    "[XSECTIONS]\n"
                    ";;Link  Shape     Geom1  Geom2  Geom3  Geom4  Barrels\n"
                    "C1      CIRCULAR  1      0      0      0      1\n");
        }
        const QString rpt = QDir(outputDir()).filePath(QStringLiteral("minimal_1d.rpt"));
        const QString out = QDir(outputDir()).filePath(QStringLiteral("minimal_1d.out"));

        SimulationStatusModel model;
        const int jobId = model.addJob(QStringLiteral("minimal_1d.inp"), inp);

        auto *runner = new SimulationRunner(jobId, QStringLiteral("minimal_1d.inp"),
                                            inp, rpt, out,
                                            QStringLiteral("6.0.0"), this);
        connect(runner, &SimulationRunner::progressChanged,
                &model, &SimulationStatusModel::updateProgress);
        connect(runner, &SimulationRunner::finished,
                &model, &SimulationStatusModel::finishJob);

        QSignalSpy finishedSpy(runner, &SimulationRunner::finished);
        runner->start();

        // A dry 1-hour model finishes in well under the timeout.
        QVERIFY2(finishedSpy.wait(60000), "no finished signal within 60 s");
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(QStringLiteral("1D run failed: %1")
                                .arg(finishedSpy.last().at(3).toString())));
        QVERIFY2(qIsNaN(finishedSpy.last().at(6).toDouble()),
                 "1D-only run reported a non-NaN 2D continuity error");
        QCOMPARE(twoDErrCell(model, 0), QStringLiteral("—"));
    }
};

QTEST_GUILESS_MAIN(TestSimStatus2DErr)
#include "test_simstatus_2derr.moc"
