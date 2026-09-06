/*!
 * \file   test_simrunner_failures.cpp
 * \brief  SimulationRunner as the last point of failure capture: a run that
 *         fails at open, a worker that throws mid-run, and an engine step
 *         that reports a numerical abort must each end in finished(false,
 *         code, message) with the phase named, a red "ERROR [code]" child
 *         row under the job in SimulationStatusModel, and a per-run
 *         <stem>.runlog.txt beside the report — never in std::terminate.
 *
 * The throw and the numerical abort are injected with the test-only env
 * SWMMVIS_TEST_FAULT=<phase>:<kind> read once by SimulationRunner::start().
 * Decks and artefacts land in the reviewable output_simrunner_failures/ dir
 * (under SWMMVIS_GUI_TEST_DATA, else the working directory).
 */
#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>

#include <openswmm/engine/openswmm_engine.h>

#include "simulation/simulationrunner.h"
#include "simulation/simulationstatusmodel.h"

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString outputDir()
{
    const QString dir = QDir(dataDir()).filePath(QStringLiteral("output_simrunner_failures"));
    QDir().mkpath(dir);
    return dir;
}

const char *kMinimal1D =
    "[OPTIONS]\n"
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
    "C1      CIRCULAR  1      0      0      0      1\n";

QString writeDeck(const QString &name, const char *text)
{
    const QString inp = QDir(outputDir()).filePath(name + QStringLiteral(".inp"));
    QFile f(inp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return {};
    f.write(text);
    return inp;
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

struct Finished {
    bool    success   = true;
    int     errorCode = 0;
    QString message;
};

/// Run `inp` through a SimulationRunner wired to `model` exactly as SWMMVis
/// wires it; returns the finished() payload (fails the calling test on timeout).
Finished runToEnd(QObject *parent, SimulationStatusModel &model, const QString &name,
                  const QString &inp)
{
    const QString rpt = QDir(outputDir()).filePath(name + QStringLiteral(".rpt"));
    const QString out = QDir(outputDir()).filePath(name + QStringLiteral(".out"));
    QFile::remove(QDir(outputDir()).filePath(name + QStringLiteral(".runlog.txt")));

    const int jobId = model.addJob(name + QStringLiteral(".inp"), inp);
    auto *runner = new SimulationRunner(jobId, name + QStringLiteral(".inp"), inp, rpt, out,
                                        QStringLiteral("6.0.0"), parent);
    QObject::connect(runner, &SimulationRunner::finished,
                     &model, &SimulationStatusModel::finishJob);
    QSignalSpy finishedSpy(runner, &SimulationRunner::finished);
    runner->start();
    Finished r;
    if (finishedSpy.count() == 0 && !finishedSpy.wait(60000)) {
        r.message = QStringLiteral("no finished signal within 60 s");
        return r;
    }
    const QList<QVariant> a = finishedSpy.last();
    r.success   = a.at(1).toBool();
    r.errorCode = a.at(2).toInt();
    r.message   = a.at(3).toString();
    runner->deleteLater();
    return r;
}

QString errorChildText(const SimulationStatusModel &model, int row)
{
    const QModelIndex job = model.index(row, 0);
    for (int i = 0; i < model.rowCount(job); ++i) {
        const QString t = model.index(i, 0, job).data(Qt::DisplayRole).toString();
        if (t.startsWith(QStringLiteral("ERROR ["))) return t;
    }
    return {};
}

} // namespace

class TestSimRunnerFailures : public QObject
{
    Q_OBJECT

private slots:

    void cleanup() { qunsetenv("SWMMVIS_TEST_FAULT"); }

    void openFailureNamesThePhaseAndWritesTheRunLog()
    {
        const QString inp = writeDeck(QStringLiteral("bad_open"),
            "[OPTIONS]\nFLOW_ROUTING     BOGUS_METHOD\n"
            "START_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 01:00:00\n");
        QVERIFY(!inp.isEmpty());
        SimulationStatusModel model;
        const Finished r = runToEnd(this, model, QStringLiteral("bad_open"), inp);
        QVERIFY2(!r.success, "a deck the engine rejects must not report success");
        QVERIFY(r.errorCode != 0);
        QVERIFY2(!r.message.isEmpty(), "the engine's reason must be forwarded");

        // The reason is a child row (red), copyable — not only a tooltip.
        const QString child = errorChildText(model, 0);
        QVERIFY2(child.startsWith(QStringLiteral("ERROR [%1]").arg(r.errorCode)),
                 qPrintable(child));
        QCOMPARE(model.index(0, SimulationStatusModel::ColStatus).data().toString(),
                 QStringLiteral("Failed"));

        // The run log names the phases and the outcome. The engine tolerates
        // the bogus routing token at parse and rejects the deck at initialize
        // ("no acceptable outlet nodes"), so either pre-step phase is right;
        // what matters is that the phase is named and the log was written.
        const QString log = readAll(QDir(outputDir()).filePath(QStringLiteral("bad_open.runlog.txt")));
        QVERIFY2(log.contains(QStringLiteral(" open")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("finished: FAILED")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("phase=open"))
                     || log.contains(QStringLiteral("phase=initialize")), qPrintable(log));
        QVERIFY2(log.contains(r.message.section(QLatin1Char('\n'), 0, 0)), qPrintable(log));
    }

    void workerExceptionBecomesAFailedRun()
    {
        const QString inp = writeDeck(QStringLiteral("throw_mid_run"), kMinimal1D);
        QVERIFY(!inp.isEmpty());
        qputenv("SWMMVIS_TEST_FAULT", "step:bad_alloc");
        SimulationStatusModel model;
        const Finished r = runToEnd(this, model, QStringLiteral("throw_mid_run"), inp);
        // The process is still here: the throw became a result, not a terminate.
        QVERIFY(!r.success);
        QCOMPARE(r.errorCode, int(SWMM_ERR_INTERNAL));
        QVERIFY2(r.message.contains(QStringLiteral("bad_alloc")), qPrintable(r.message));
        QVERIFY2(r.message.contains(QStringLiteral("step")), qPrintable(r.message));
        QVERIFY(!errorChildText(model, 0).isEmpty());
        const QString log = readAll(QDir(outputDir()).filePath(QStringLiteral("throw_mid_run.runlog.txt")));
        QVERIFY2(log.contains(QStringLiteral("EXCEPTION phase=step")), qPrintable(log));
    }

    void numericalAbortIsWordedAsDivergenceAtASimTime()
    {
        const QString inp = writeDeck(QStringLiteral("diverge"), kMinimal1D);
        QVERIFY(!inp.isEmpty());
        qputenv("SWMMVIS_TEST_FAULT", "step:numerical");
        SimulationStatusModel model;
        const Finished r = runToEnd(this, model, QStringLiteral("diverge"), inp);
        QVERIFY(!r.success);
        QCOMPARE(r.errorCode, int(SWMM_ERR_NUMERICAL));
        QVERIFY2(r.message.startsWith(QStringLiteral("Routing diverged (step at ")),
                 qPrintable(r.message));
        // The report was still written up to the abort (end/report ran).
        QVERIFY(QFileInfo::exists(QDir(outputDir()).filePath(QStringLiteral("diverge.rpt"))));
        const QString log = readAll(QDir(outputDir()).filePath(QStringLiteral("diverge.runlog.txt")));
        QVERIFY2(log.contains(QStringLiteral("step FAILED code=14")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("finished: FAILED code=14 phase=step at ")),
                 qPrintable(log));
    }
};

QTEST_MAIN(TestSimRunnerFailures)
#include "test_simrunner_failures.moc"
