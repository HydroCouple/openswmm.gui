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
#include <QProcess>
#include <QProcessEnvironment>
#include <filesystem>
#ifdef Q_OS_UNIX
#include <sys/file.h>
#endif

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


    void quotedLastOutputOption_data()
    {
        QTest::addColumn<QByteArray>("options"); QTest::addColumn<QString>("expected");
        QTest::newRow("quoted-space-comment") << QByteArray("[ 2D_OPTIONS ] ; settings\nOUTPUT_FILE \"flow output.h5\" ; comment\n") << QString("flow output.h5");
        QTest::newRow("quoted-semicolon") << QByteArray("[2D_OPTIONS]\nOUTPUT_FILE \"flow;output.h5\"\n") << QString("flow;output.h5");
        QTest::newRow("last-assignment") << QByteArray("[2D_OPTIONS]\nOUTPUT_FILE first.h5\nOUTPUT_FILE final.h5\n") << QString("final.h5");
        QTest::newRow("csv") << QByteArray("[2D_OPTIONS]\nOUTPUT_FILE,flow.h5\n") << QString("flow.h5");
        QTest::newRow("embedded-comma") << QByteArray("[2D_OPTIONS]\nOUTPUT_FILE flow,output.h5\n") << QString("flow,output.h5");
        QTest::newRow("trailing-comma") << QByteArray("[2D_OPTIONS]\nOUTPUT_FILE flow.h5,\n") << QString("flow.h5");
    }

    void quotedLastOutputOption()
    {
        QFETCH(QByteArray, options); QFETCH(QString, expected);
        const QString inp = writeDeck("quoted_output", (QByteArray(kMinimal1D) + "\n" + options).constData());
        QVERIFY(!inp.isEmpty());
        QCOMPARE(SimulationRunner::parseTwoDOutputFile(inp), QFileInfo(inp).absoluteDir().filePath(expected));
    }

    void declaredResourcesAreProtected_data()
    {
        QTest::addColumn<QByteArray>("reference");
        QTest::newRow("series") << QByteArray("[TIMESERIES]\nS FILE \"source data.dat\"\n");
        QTest::newRow("series-column") << QByteArray("[TIMESERIES]\nS FILE \"source data.dat:flow\"\n");
        QTest::newRow("rain") << QByteArray("[RAINGAGES]\nR INTENSITY 0:05 1 FILE \"source data.dat\" STATION MM\n");
        QTest::newRow("hotstart-input") << QByteArray("[FILES]\nUSE HOTSTART \"source data.dat\"\n");
        QTest::newRow("climate") << QByteArray("[TEMPERATURE]\nFILE \"source data.dat\"\n");
        QTest::newRow("mesh") << QByteArray("[2D_MESH_FILE]\nFILE \"source data.dat\"\n");
        QTest::newRow("mesh-unquoted-space") << QByteArray("[2D_MESH_FILE]\nFILE source data.dat\n");
        QTest::newRow("groundwater-quality") << QByteArray("[2D_GW_INITIAL_QUALITY]\nFILE \"source data.dat\"\n");
        QTest::newRow("plugin-library") << QByteArray("[PLUGINS]\n\"source data.dat\"\n");
        QTest::newRow("component-config") << QByteArray("[PROCESS_COMPONENTS]\nHEAT \"config=source data.dat\"\n");
    }

    void declaredResourcesAreProtected()
    {
        QFETCH(QByteArray, reference);
        const QString inp = writeDeck("declared_resource", (QByteArray(kMinimal1D) + "\n" + reference).constData());
        QVERIFY(!inp.isEmpty()); const QDir directory = QFileInfo(inp).absoluteDir();
        QString error;
        QVERIFY(!SimulationRunner::validateRunPaths(inp, directory.filePath("safe.rpt"), directory.filePath("source data.dat"), {}, {}, &error));
        QVERIFY(error.contains("protected input"));
    }

    void additionalAndActiveOutputsAreGuarded()
    {
        const QString inp = writeDeck("additional_outputs", (QByteArray(kMinimal1D) + "\n[FILES]\nSAVE HOTSTART \"saved state.hsf\"\n").constData());
        QVERIFY(!inp.isEmpty()); const QDir directory = QFileInfo(inp).absoluteDir();
        const QString rpt = directory.filePath("additional_outputs.rpt"), out = directory.filePath("additional_outputs.out");
        QString error;
        QVERIFY(SimulationRunner::runOutputPaths(inp, rpt, out).contains(directory.filePath("saved state.hsf")));
        QVERIFY(SimulationRunner::validateRunPaths(inp, rpt, out, {}, {}, &error));
        QVERIFY(!SimulationRunner::validateRunPaths(inp, rpt, out, {}, {}, &error, {inp}));
        QVERIFY(!SimulationRunner::validateRunPaths(inp, rpt, out, {directory.filePath("saved state.hsf")}, {}, &error));
        QVERIFY(!SimulationRunner::validateRunPaths(inp, rpt, out, {}, {directory.filePath("saved state.hsf")}, &error));
        QVERIFY(!SimulationRunner::validateRunPaths(inp, rpt, out, {}, {directory.filePath("additional_outputs.runlog.txt")}, &error));
        SimulationRunner unstarted(105, "paths_before_start", inp, rpt, out);
        QCOMPARE(unstarted.outputPaths(), SimulationRunner::runOutputPaths(inp, rpt, out));
    }

    void duplicateStartRunsOnce()
    {
        const QString inp = writeDeck("single_start", kMinimal1D);
        QVERIFY(!inp.isEmpty());
        qputenv("SWMMVIS_TEST_FAULT", "step:bad_alloc");
        SimulationRunner runner(100, "single_start", inp,
            QDir(outputDir()).filePath("single_start.rpt"), QDir(outputDir()).filePath("single_start.out"));
        QSignalSpy started(&runner, &SimulationRunner::started), finished(&runner, &SimulationRunner::finished);
        runner.start(); runner.start();
        // Drain both old workers before asserting, so the baseline failure
        // does not itself destroy a receiver still used by a worker.
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= started.count(), 15000);
        QCOMPARE(started.count(), 1); QCOMPARE(finished.count(), 1);
    }

    void ownerDestructionStopsPausedWorker()
    {
        if (qEnvironmentVariableIsSet("SWMMVIS_TEST_RUNNER_OWNER_CHILD")) {
            const QString inp = writeDeck("owner_destruction", kMinimal1D);
            QVERIFY(!inp.isEmpty());
            auto *owner = new QObject;
            auto *runner = new SimulationRunner(101, "owner_destruction", inp,
                QDir(outputDir()).filePath("owner_destruction.rpt"),
                QDir(outputDir()).filePath("owner_destruction.out"), "6.0.0", owner);
            runner->setPaused(true);
            QSignalSpy dates(runner, &SimulationRunner::simulationDatesKnown);
            runner->start();
            QVERIFY(dates.count() || dates.wait(15000));
            delete owner;
            return;
        }
        // Isolate the baseline use-after-free / stuck worker from the suite.
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SWMMVIS_TEST_RUNNER_OWNER_CHILD", "1");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"ownerDestructionStopsPausedWorker", "-o", "-,txt"});
        QVERIFY(child.waitForStarted());
        const bool stopped = child.waitForFinished(20000);
        if (!stopped) { child.kill(); child.waitForFinished(); }
        const QByteArray details = child.readAllStandardOutput() + child.readAllStandardError();
        QVERIFY2(stopped, details.constData());
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, details.constData());
    }

    void parallelRelativeFilesLeaveProcessDirectoryAlone()
    {
        const QString originalDirectory = QDir::currentPath();
        const QString root = QFileInfo(outputDir()).absoluteFilePath();
        QStringList inputs, reports, outputs;
        for (const QString &name : {QString("relative_a"), QString("relative_b")}) {
            const QDir directory(QDir(root).filePath(name)); QVERIFY(QDir().mkpath(directory.path()));
            QFile series(directory.filePath("flow.dat")); QVERIFY(series.open(QIODevice::WriteOnly));
            series.write("01/01/2026 00:00 1\n01/01/2026 01:00 1\n"); series.close();
            QFile model(directory.filePath("model.inp")); QVERIFY(model.open(QIODevice::WriteOnly));
            model.write(kMinimal1D); model.write("\n[TIMESERIES]\nF FILE flow.dat\n"); model.close();
            inputs << model.fileName(); reports << directory.filePath("model.rpt"); outputs << directory.filePath("model.out");
        }
        SimulationRunner first(102, "relative_a", inputs[0], reports[0], outputs[0]);
        SimulationRunner second(103, "relative_b", inputs[1], reports[1], outputs[1]);
        QSignalSpy firstDates(&first, &SimulationRunner::simulationDatesKnown), secondDates(&second, &SimulationRunner::simulationDatesKnown);
        QSignalSpy firstDone(&first, &SimulationRunner::finished), secondDone(&second, &SimulationRunner::finished);
        first.setPaused(true); second.setPaused(true);
        first.start();
        const bool firstReady = firstDates.count() || firstDates.wait(15000);
        const QString duringFirst = QDir::currentPath();
        second.start();
        const bool secondReady = secondDates.count() || secondDates.wait(15000);
        const QString duringBoth = QDir::currentPath();
        first.setPaused(false); second.setPaused(false);
        const bool firstFinished = firstDone.count() || firstDone.wait(15000);
        const bool secondFinished = secondDone.count() || secondDone.wait(15000);
        const QString after = QDir::currentPath();
        QDir::setCurrent(originalDirectory); // leave the old baseline clean
        QVERIFY(firstReady); QVERIFY(secondReady); QVERIFY(firstFinished); QVERIFY(secondFinished);
        QVERIFY2(firstDone.first()[1].toBool(), qPrintable(firstDone.first()[3].toString()));
        QVERIFY2(secondDone.first()[1].toBool(), qPrintable(secondDone.first()[3].toString()));
        QCOMPARE(duringFirst, originalDirectory); QCOMPARE(duringBoth, originalDirectory); QCOMPARE(after, originalDirectory);
    }

    void busyHdfOutputIsRefusedBeforeAnyOutputIsWritten_data()
    {
        QTest::addColumn<bool>("empty");
        QTest::addColumn<bool>("writer");
        QTest::newRow("reader") << false << false;
        QTest::newRow("writer") << false << true;
        QTest::newRow("empty-locked-file") << true << false;
    }

    void busyHdfOutputIsRefusedBeforeAnyOutputIsWritten()
    {
        QFETCH(bool,empty);QFETCH(bool,writer);
        const QDir dir(QDir(outputDir()).absoluteFilePath("busy_hdf"));
        QVERIFY(QDir().mkpath(dir.path()));
        const QString inp=dir.filePath("model.inp"),rpt=dir.filePath("run.rpt"),out=dir.filePath("run.out");
        const QString h5=dir.filePath("results.h5"),log=dir.filePath("run.runlog.txt");
        const QByteArray deck=QByteArray(kMinimal1D)+"\n[2D_OPTIONS]\nOUTPUT_FILE results.h5\n";
        const QByteArray saved="previous results must survive a refused run\n";
        for(const auto& path:{inp,rpt,out,h5,log}) {
            QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(path==inp ? deck : path==h5 && empty ? QByteArray() : saved);
        }
        QFile holder(h5);QVERIFY(holder.open(QIODevice::ReadWrite));
#ifdef Q_OS_UNIX
        QVERIFY(::flock(holder.handle(),(writer ? LOCK_EX : LOCK_SH)|LOCK_NB)==0);
#else
        Q_UNUSED(writer)
#endif
        SimulationRunner blocked(105,"busy_hdf",inp,rpt,out);
        QSignalSpy refused(&blocked,&SimulationRunner::finished);
        blocked.start();QVERIFY(refused.count() || refused.wait(15000));
        QVERIFY(!refused.first()[1].toBool());
        const QString message=refused.first()[3].toString();
        QVERIFY2(message.contains("in use") && message.contains("results.h5"),qPrintable(message));
        for(const auto& path:{inp,rpt,out,h5,log}) {
            QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(),path==inp ? deck : path==h5 && empty ? QByteArray() : saved);
        }
        holder.close();
        SimulationRunner available(106,"released_hdf",inp,rpt,out);
        QSignalSpy finished(&available,&SimulationRunner::finished);
        available.start();QVERIFY(finished.count() || finished.wait(15000));
        QVERIFY2(finished.first()[1].toBool(),qPrintable(finished.first()[3].toString()));
    }

    void outputAliasesNeverModifyInputs_data()
    {
        QTest::addColumn<QString>("kind");
        for (const char *kind : {"report-input", "output-input", "report-output", "log-input", "h5-input", "h5-report", "quoted-h5-input", "last-h5-input", "symlink-input", "hardlink-input"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }

    void outputAliasesNeverModifyInputs()
    {
        QFETCH(QString, kind);
        const QDir directory(QDir(outputDir()).absoluteFilePath("path_guard_" + kind));
        QVERIFY(QDir().mkpath(directory.path()));
        QString inp = directory.filePath(kind == "log-input" ? "run.runlog.txt" : "model.inp");
        QString rpt = directory.filePath("run.rpt"), out = directory.filePath("run.out");
        QByteArray original(kMinimal1D);
        if (kind == "quoted-h5-input") original += "\n[ 2D_OPTIONS ] ; header\nOUTPUT_FILE \"model.inp\" ; collision\n";
        if (kind == "last-h5-input") original += "\n[2D_OPTIONS]\nOUTPUT_FILE harmless.h5\nOUTPUT_FILE model.inp\n";
        if (kind == "h5-input" || kind == "h5-report")
            original += "\n[2D_OPTIONS]\nOUTPUT_FILE " + QFileInfo(kind == "h5-input" ? inp : rpt).fileName().toUtf8() + "\n";
        for (const auto &path : {inp, rpt, out}) QFile::remove(path);
        QFile file(inp); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(original), qint64(original.size())); file.close();
        if (kind == "report-input") rpt = inp;
        if (kind == "output-input") out = inp;
        if (kind == "report-output") out = rpt;
        if (kind == "symlink-input" || kind == "hardlink-input") {
            std::error_code error;
            if (kind == "symlink-input") std::filesystem::create_symlink(inp.toStdString(), out.toStdString(), error);
            else std::filesystem::create_hard_link(inp.toStdString(), out.toStdString(), error);
            QVERIFY2(!error, error.message().c_str());
        }
        SimulationRunner runner(104, kind, inp, rpt, out);
        QSignalSpy finished(&runner, &SimulationRunner::finished);
        runner.start(); QVERIFY(finished.count() || finished.wait(15000));
        QVERIFY(!finished.first()[1].toBool());
        QVERIFY(finished.first()[2].toInt() != 0);
        QVERIFY(finished.first()[3].toString().contains("path", Qt::CaseInsensitive)
            || finished.first()[3].toString().contains("file", Qt::CaseInsensitive));
        QFile unchanged(inp); QVERIFY(unchanged.open(QIODevice::ReadOnly)); QCOMPARE(unchanged.readAll(), original);
        if (rpt != inp && kind != "log-input") QVERIFY(!QFileInfo::exists(rpt));
    }



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
