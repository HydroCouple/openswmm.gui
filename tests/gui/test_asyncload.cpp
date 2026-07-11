/*!
 * \file   test_asyncload.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Coverage for commit 82fc82b (feat(load): async model load) —
 *         SWMMModelLayer::openEngineForPath()/adoptOpenEngine() and
 *         SWMMVisProjectWindow::loadModelAsync().
 *
 *         Uses QTEST_MAIN (not QTEST_APPLESS_MAIN) because the last slot
 *         constructs a real SWMMVisProjectWindow (a QWidget), which needs
 *         a QApplication event loop even under the offscreen QPA.
 */
#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/engine/openswmm_engine.h>

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

namespace {

// Capture openswmm.load.* telemetry emitted during a scoped block so the
// timing regression guard can assert on the category channel (load timing
// now goes to openswmm.load.model, not the layer's `warnings` list).
QStringList     g_loadLog;
QtMessageHandler g_prevHandler = nullptr;
void loadLogHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (ctx.category
        && QString::fromLatin1(ctx.category).startsWith(QStringLiteral("openswmm.load")))
        g_loadLog << msg;
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

QString malformedFixturePath()
{
    return QDir(dataDir()).filePath(
        QStringLiteral("typed_selection_malformed_fixture.inp"));
}

QString nonexistentPath()
{
    return QDir(dataDir()).filePath(
        QStringLiteral("typed_selection_does_not_exist.inp"));
}

int totalNodeCount(const SWMMModelLayer &l)
{
    return l.categoryCount(SWMMModelLayer::CatJunctions)
         + l.categoryCount(SWMMModelLayer::CatOutfalls)
         + l.categoryCount(SWMMModelLayer::CatStorage)
         + l.categoryCount(SWMMModelLayer::CatDividers);
}

int totalLinkCount(const SWMMModelLayer &l)
{
    return l.categoryCount(SWMMModelLayer::CatConduits)
         + l.categoryCount(SWMMModelLayer::CatPumps)
         + l.categoryCount(SWMMModelLayer::CatOrifices)
         + l.categoryCount(SWMMModelLayer::CatWeirs)
         + l.categoryCount(SWMMModelLayer::CatOutlets);
}

} // namespace

class TestAsyncLoad : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "typed_selection_fixture.inp missing from the gui-test data dir");
        QVERIFY2(QFile::exists(malformedFixturePath()),
                 "typed_selection_malformed_fixture.inp missing from the gui-test data dir");
        QVERIFY2(!QFile::exists(nonexistentPath()),
                 "nonexistent-path fixture name unexpectedly exists on disk");
    }

    // 1. openEngineForPath error paths: empty path and a nonexistent path.
    //    Both are pure engine C-API calls — no GUI thread required.
    void openEngineForPathErrorPaths()
    {
        QString detail;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(QString(), &detail);
        QVERIFY(eng == nullptr);
        QCOMPARE(detail, QStringLiteral("No model file path specified."));

        QString detail2;
        SWMM_Engine eng2 = SWMMModelLayer::openEngineForPath(nonexistentPath(), &detail2);
        QVERIFY(eng2 == nullptr);
        QVERIFY2(detail2.contains(QStringLiteral("Model file not found")),
                 qPrintable(detail2));
    }

    // 2. Parse-error diagnostics preserved: a malformed fixture (see
    //    typed_selection_malformed_fixture.inp for why a [DWF] line naming
    //    an undefined node is the reliable failure — unknown section
    //    headers and garbled numeric fields are NOT fatal in this engine)
    //    must surface the engine's real diagnostic, not the generic
    //    "Out of memory" string documented as a regression in
    //    SWMMModelLayer::openEngineForPath()'s comments.
    void parseErrorDiagnosticsPreserved()
    {
        QString detail;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(malformedFixturePath(), &detail);
        QVERIFY(eng == nullptr);
        QVERIFY(!detail.isEmpty());
        QVERIFY2(!detail.contains(QStringLiteral("Out of memory")), qPrintable(detail));
        // The real cross-reference-resolution diagnostic names the
        // offending node.
        QVERIFY2(detail.contains(QStringLiteral("GHOST")), qPrintable(detail));
    }

    // 3. openEngineForPath() + buildFromEngine() + adoptOpenEngine() on a
    //    fresh layer (the async open sequence) must yield the same
    //    node/link/subcatchment/gage counts as a plain loadModel() on another
    //    layer for the same fixture.
    void openAndAdoptEqualsSyncLoad()
    {
        QString detail;
        qint64 openMs = -1;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(fixturePath(), &detail, &openMs);
        QVERIFY(eng != nullptr);
        QVERIFY(openMs >= 0);

        SWMMModelLayer asyncLayer(fixturePath(), nullptr);
        QList<QString> warningsA, errorsA;
        // buildFromEngine() is the worker half in the real async path; run it
        // here before adoption exactly as SWMMVisProjectWindow::loadModelAsync
        // does.
        qint64 soaMs = 0, geomMs = 0;
        asyncLayer.buildFromEngine(eng, &soaMs, &geomMs);
        QVERIFY(asyncLayer.adoptOpenEngine(eng, warningsA, errorsA, openMs, soaMs, geomMs));

        SWMMModelLayer syncLayer(fixturePath(), nullptr);
        QList<QString> warningsB, errorsB;
        QVERIFY(syncLayer.loadModel(warningsB, errorsB));

        QCOMPARE(totalNodeCount(asyncLayer), totalNodeCount(syncLayer));
        QCOMPARE(totalLinkCount(asyncLayer), totalLinkCount(syncLayer));
        QCOMPARE(asyncLayer.categoryCount(SWMMModelLayer::CatSubcatchments),
                 syncLayer.categoryCount(SWMMModelLayer::CatSubcatchments));
        QCOMPARE(asyncLayer.categoryCount(SWMMModelLayer::CatRainGages),
                 syncLayer.categoryCount(SWMMModelLayer::CatRainGages));
    }

    // 4. Plain loadModel() still succeeds (sync-path regression guard) and
    //    emits the load-timing line to the openswmm.load.model category.
    //    (Timing moved out of `warnings` — which is now real-warnings-only —
    //    into opt-in telemetry; the GUI shows one clean success summary.)
    void loadModelRegression()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("openswmm.load.*=true"));
        g_loadLog.clear();
        g_prevHandler = qInstallMessageHandler(loadLogHandler);

        SWMMModelLayer layer(fixturePath(), nullptr);
        QList<QString> warnings, errors;
        const bool ok = layer.loadModel(warnings, errors);

        qInstallMessageHandler(g_prevHandler);
        g_prevHandler = nullptr;
        QLoggingCategory::setFilterRules(QString());

        QVERIFY(ok);
        QVERIFY(errors.isEmpty());

        bool foundTiming = false;
        for (const QString &m : g_loadLog)
            if (m.contains(QStringLiteral("load timing (ms)")))
                foundTiming = true;
        QVERIFY2(foundTiming,
                 "expected a '... load timing (ms): ...' line in the "
                 "openswmm.load.model category");
    }

    // 5. Async completion: SWMMVisProjectWindow::loadModelAsync() must emit
    //    modelLoadFinished(ok, warnings, errors) exactly once, ok==true for
    //    the fixture and ok==false (with a non-empty error) for a bad path.
    //
    //    The fixture carries an explicit [MAP] Units line so the model's
    //    CRS resolves to a local projected CRS (not "Untitled (Local)") —
    //    finishModelLoad() opens a MODAL CRS-picker dialog for an untitled
    //    CRS, which would hang this unattended test.
    void asyncCompletionViaProjectWindow()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(workspace != nullptr);

        // ---- Success case: fixture opens, ok == true --------------------
        {
            auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
            QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
            QVERIFY(spy.isValid());

            window->loadModelAsync();
            QVERIFY2(spy.wait(20000), "modelLoadFinished did not fire within 20s");
            QCOMPARE(spy.count(), 1);

            const QList<QVariant> args = spy.takeFirst();
            QVERIFY(args.at(0).toBool());

            delete window;
        }

        // ---- Failure case: nonexistent path, ok == false + non-empty error
        {
            auto *window = new SWMMVisProjectWindow(workspace, nonexistentPath(), nullptr);
            QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
            QVERIFY(spy.isValid());

            window->loadModelAsync();
            QVERIFY2(spy.wait(20000), "modelLoadFinished did not fire within 20s");
            QCOMPARE(spy.count(), 1);

            const QList<QVariant> args = spy.takeFirst();
            QVERIFY(!args.at(0).toBool());
            const QList<QString> errs = args.at(2).value<QList<QString>>();
            QVERIFY(!errs.isEmpty());

            delete window;
        }

        delete workspace;
    }

    // 6. Optional profiling harness (Phase 0 of the file-open plan). Loads an
    //    arbitrary model named by SWMM_PROFILE_INP and dumps the GUI-thread
    //    load breakdown (engine_open vs SoA copy vs CRS vs geometry cache,
    //    plus sub-splits) captured from the openswmm.load.model category.
    //    Skips when the env var is unset, so it is a no-op in CI.
    void profileExternalModel()
    {
        const QString inp = qEnvironmentVariable("SWMM_PROFILE_INP");
        if (inp.isEmpty())
            QSKIP("set SWMM_PROFILE_INP=<path.inp> to run the load profiler");
        QVERIFY2(QFile::exists(inp), qPrintable("SWMM_PROFILE_INP not found: " + inp));

        QLoggingCategory::setFilterRules(QStringLiteral("openswmm.load.*=true"));
        g_loadLog.clear();
        g_prevHandler = qInstallMessageHandler(loadLogHandler);

        SWMMModelLayer layer(inp, nullptr);
        QList<QString> warnings, errors;
        const bool ok = layer.loadModel(warnings, errors);

        qInstallMessageHandler(g_prevHandler);
        g_prevHandler = nullptr;
        QLoggingCategory::setFilterRules(QString());

        QVERIFY2(ok, qPrintable(errors.join(QStringLiteral("; "))));
        qInfo().noquote() << "=== PROFILE" << inp
                          << QStringLiteral("(nodes=%1 links=%2 subcatch=%3 gages=%4) ===")
                                 .arg(layer.cachedNodeCount()).arg(layer.cachedLinkCount())
                                 .arg(layer.cachedSubcatchCount()).arg(layer.cachedGageCount());
        for (const QString &m : g_loadLog)
            qInfo().noquote() << "  " << m;
    }
};

QTEST_MAIN(TestAsyncLoad)
#include "test_asyncload.moc"
