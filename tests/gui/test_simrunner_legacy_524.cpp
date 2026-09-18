/*!
 * \file   test_simrunner_legacy_524.cpp
 * \brief  SimulationRunner drives a model on EPA SWMM 5.2.4 through the
 *         bundled openswmm-legacy-worker-5.2.4 (built by
 *         cmake/EngineVersions.cmake from the pinned build-v5.2.4 tag):
 *         the worker is found by version, the JSON protocol arrives
 *         (dates, progress, warning, continuity), the .out is a 5.2.4 file,
 *         and a version with no worker fails loudly instead of silently
 *         running some other engine.
 *
 * Decks and artefacts land in the reviewable output_simrunner_legacy_524/
 * dir (under SWMMVIS_GUI_TEST_DATA, else the working directory).
 */
#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QTimeZone>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_output.h>

#include "simulation/simulationrunner.h"
#include "simulation/simulationstatusmodel.h"

#include <cmath>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString outputDir()
{
    const QString dir = QDir(dataDir()).filePath(QStringLiteral("output_simrunner_legacy_524"));
    QDir().mkpath(dir);
    return dir;
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

/// Copy the stock-SWMM-5 fixture into the output dir under `name`, optionally
/// appending `extra` (used to inject v6-only content the backports must skip).
QString stageDeck(const QString &name, const QString &extra = QString())
{
    const QString src = QDir(dataDir()).filePath(QStringLiteral("selection_trace_fixture.inp"));
    QString text = readAll(src);
    if (text.isEmpty()) return {};
    if (!extra.isEmpty()) text += QStringLiteral("\n") + extra;
    const QString inp = QDir(outputDir()).filePath(name + QStringLiteral(".inp"));
    QFile f(inp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return {};
    f.write(text.toUtf8());
    return inp;
}

struct RunResult {
    bool     success       = false;
    int      errorCode     = 0;
    QString  message;
    double   runoffErrFrac = std::nan("");
    double   routingErrFrac= std::nan("");
    int      datesCount    = 0;
    QDateTime start, end;
    int      progressCount = 0;
    QStringList warnings;
    QString  outPath;
};

/// Run `inp` on `engineVersion` exactly as SWMMVis wires a runner; returns
/// everything the signals carried (fails the calling test on timeout).
RunResult runToEnd(QObject *parent, SimulationStatusModel &model, const QString &name,
                   const QString &inp, const QString &engineVersion)
{
    const QString rpt = QDir(outputDir()).filePath(name + QStringLiteral(".rpt"));
    const QString out = QDir(outputDir()).filePath(name + QStringLiteral(".out"));
    QFile::remove(rpt); QFile::remove(out);

    const int jobId = model.addJob(name + QStringLiteral(".inp"), inp);
    auto *runner = new SimulationRunner(jobId, name + QStringLiteral(".inp"), inp, rpt, out,
                                        engineVersion, parent);
    QObject::connect(runner, &SimulationRunner::finished,
                     &model, &SimulationStatusModel::finishJob);
    QSignalSpy finishedSpy(runner, &SimulationRunner::finished);
    QSignalSpy datesSpy(runner, &SimulationRunner::simulationDatesKnown);
    QSignalSpy progressSpy(runner, &SimulationRunner::progressChanged);
    QSignalSpy warningSpy(runner, &SimulationRunner::warningReceived);
    runner->start();

    RunResult r;
    r.outPath = out;
    if (finishedSpy.count() == 0 && !finishedSpy.wait(60000)) {
        r.message = QStringLiteral("no finished signal within 60 s");
        return r;
    }
    const QList<QVariant> a = finishedSpy.last();
    r.success        = a.at(1).toBool();
    r.errorCode      = a.at(2).toInt();
    r.message        = a.at(3).toString();
    r.runoffErrFrac  = a.at(4).toDouble();
    r.routingErrFrac = a.at(5).toDouble();
    r.datesCount     = datesSpy.count();
    if (r.datesCount > 0) {
        r.start = datesSpy.first().at(1).toDateTime();
        r.end   = datesSpy.first().at(2).toDateTime();
    }
    r.progressCount = progressSpy.count();
    for (const QList<QVariant> &w : warningSpy)
        r.warnings << w.at(2).toString();
    runner->deleteLater();
    return r;
}

} // namespace

class TestSimRunnerLegacy524 : public QObject
{
    Q_OBJECT

private slots:
    // The full protocol arrives and the engine that ran was 5.2.4.
    void runsToCompletion()
    {
        const QString inp = stageDeck(QStringLiteral("run524"));
        QVERIFY2(!inp.isEmpty(), "fixture selection_trace_fixture.inp not found in SWMMVIS_GUI_TEST_DATA");

        SimulationStatusModel model;
        const RunResult r = runToEnd(this, model, QStringLiteral("run524"), inp,
                                     QStringLiteral("5.2.4"));
        QVERIFY2(r.success, qPrintable(QStringLiteral("run failed: [%1] %2")
                                           .arg(r.errorCode).arg(r.message)));
        QCOMPARE(r.errorCode, 0);

        // dates: one line, the deck's own window (2 h from 2026-01-01 00:00).
        QCOMPARE(r.datesCount, 1);
        QVERIFY(r.start.isValid() && r.end.isValid());
        QCOMPARE(r.start.date(), QDate(2026, 1, 1));
        QCOMPARE(r.start.secsTo(r.end), 2 * 3600);

        // progress: the step-0 line plus at least the first-step line.
        QVERIFY2(r.progressCount >= 1, "no progress lines parsed from the worker");

        // continuity: parsed (finite), and small for a stock deck.
        QVERIFY(!std::isnan(r.runoffErrFrac) && !std::isnan(r.routingErrFrac));
        QVERIFY(std::fabs(r.runoffErrFrac) < 1.0 && std::fabs(r.routingErrFrac) < 1.0);

        // The .out was written by 5.2.4 (52004), not by the 5.3.0 worker.
        QVERIFY(QFile::exists(r.outPath));
        SWMM_Output h = swmm_output_open(r.outPath.toUtf8().constData());
        QVERIFY2(h != nullptr, "swmm_output_open failed on the 5.2.4 .out");
        QCOMPARE(swmm_output_get_version(h), 52004);
        QCOMPARE(swmm_output_get_period_count(h), 120);              // 2 h / 1 min
        swmm_output_close(h);
    }

    // v6-only content is skipped with a warning (the backports), not fatal.
    void unknownSectionAndOptionWarnButRun()
    {
        const QString inp = stageDeck(QStringLiteral("run524_v6content"),
            QStringLiteral("[OPTIONS]\nNODE_CONTINUITY  YES\n\n"
                           "[USER_FLAGS]\n;; v6-only section\nJ1  review\n"));
        QVERIFY(!inp.isEmpty());

        SimulationStatusModel model;
        const RunResult r = runToEnd(this, model, QStringLiteral("run524_v6content"), inp,
                                     QStringLiteral("5.2.4"));
        QVERIFY2(r.success, qPrintable(QStringLiteral("run failed: [%1] %2")
                                           .arg(r.errorCode).arg(r.message)));

        const QString all = r.warnings.join(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("Unknown section")),
                 qPrintable(QStringLiteral("no unknown-section warning in: ") + all));
        QVERIFY2(all.contains(QStringLiteral("Unknown option")),
                 qPrintable(QStringLiteral("no unknown-option warning in: ") + all));
    }

    // A 5.x version with no bundled worker must fail naming the missing
    // file — never fall back to a different engine. (This is the automated
    // "gate bites" case: the lookup is keyed on the version.)
    void missingVersionedWorkerFailsLoudly()
    {
        const QString inp = stageDeck(QStringLiteral("run599"));
        QVERIFY(!inp.isEmpty());

        SimulationStatusModel model;
        const RunResult r = runToEnd(this, model, QStringLiteral("run599"), inp,
                                     QStringLiteral("5.9.9"));
        QVERIFY(!r.success);
        QVERIFY2(r.message.contains(QStringLiteral("openswmm-legacy-worker-5.9.9")),
                 qPrintable(QStringLiteral("message does not name the worker: ") + r.message));
        QVERIFY(!QFile::exists(r.outPath));
    }
};

QTEST_MAIN(TestSimRunnerLegacy524)
#include "test_simrunner_legacy_524.moc"
