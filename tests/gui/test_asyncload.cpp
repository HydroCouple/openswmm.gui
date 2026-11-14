/**
 * @file      test_asyncload.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Coverage for commit 82fc82b (feat(load): async model load) —
 *         SWMMModelLayer::openEngineForPath()/adoptOpenEngine() and
 *         SWMMVisProjectWindow::loadModelAsync().
 *
 *         Uses QTEST_MAIN (not QTEST_APPLESS_MAIN) because the last slot
 *         constructs a real SWMMVisProjectWindow (a QWidget), which needs
 *         a QApplication event loop even under the offscreen QPA.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include <openswmm/engine/openswmm_engine.h>

#include "core/preferencesmanager.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/tools/maptooladdlink.h"
#include "map/tools/maptooladdnode.h"
#include "selection/selectionmanager.h"
#include "ui/panels/objectbrowserpanel.h"

#include <QImage>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QSurfaceFormat>
#include <QTreeView>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QObject>
#include <QSignalSpy>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <stdexcept>

namespace
{

// Keep the actual open queued until the GUI-side ownership/path change has
// happened. This makes stale-context regressions independent of disk speed.
class QueuedOpenBarrier
{
public:
    /** @brief Drain the global pool, constrain it to one worker and occupy that worker until release.
     * Blocks until the occupying task starts, ensuring subsequent asynchronous opens remain queued. Construction and teardown must be serialized with other users of this process-global pool. */
    QueuedOpenBarrier() : oldMaximum(QThreadPool::globalInstance()->maxThreadCount())
    {
        QThreadPool::globalInstance()->waitForDone();
        QThreadPool::globalInstance()->setMaxThreadCount(1);
        blocker = QtConcurrent::run([this] {
            entered.release();
            resume.acquire();
        });
        entered.acquire();
    }
    /** @brief Idempotently unblock the occupying worker so queued model opens can proceed. */
    void release()
    {
        if (!released) {
            released = true;
            resume.release();
        }
    }
    /** @brief Release the blocker, wait for all global-pool work and restore its previous maximum thread count. */
    ~QueuedOpenBarrier()
    {
        release();
        QThreadPool::globalInstance()->waitForDone();
        QThreadPool::globalInstance()->setMaxThreadCount(oldMaximum);
    }

private:
    int oldMaximum;
    bool released = false;
    QSemaphore entered, resume;
    QFuture<void> blocker;
};

// Capture openswmm.load.* telemetry emitted during a scoped block so the
// timing regression guard can assert on the category channel (load timing
// now goes to openswmm.load.model, not the layer's `warnings` list).
QStringList g_loadLog;
QtMessageHandler g_prevHandler = nullptr;
/** @brief Collect load-category telemetry and forward every message to the saved handler.
 * @param[in] type Message severity forwarded unchanged.
 * @param[in] ctx Borrowed logging context; a non-null category starting with openswmm.load selects collection.
 * @param[in] msg Message appended to the process-global list for selected categories and forwarded unchanged.
 * The shared log and previous-handler globals are unsynchronized; use only while the test controls message emission and handler installation. */
void loadLogHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (ctx.category &&
        QString::fromLatin1(ctx.category).startsWith(QStringLiteral("openswmm.load")))
        g_loadLog << msg;
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

/** @brief Resolve the GUI test fixture directory.
 * @return SWMMVIS_GUI_TEST_DATA or the current-directory spelling "." when unset; does not create or validate it. */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Locate the valid typed-selection model under the fixture directory.
 * @return Joined path to typed_selection_fixture.inp; existence is checked by initTestCase. */
QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

/** @brief Locate the lenient-open fixture with an undefined-node diagnostic.
 * @return Joined path to typed_selection_malformed_fixture.inp without opening it. */
QString malformedFixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_malformed_fixture.inp"));
}

/** @brief Construct the deliberately absent model path used for open failures.
 * @return Joined typed_selection_does_not_exist.inp path; initTestCase requires it not to exist. */
QString nonexistentPath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_does_not_exist.inp"));
}

/** @brief Sum junction, outfall, storage and divider category counts.
 * @param[in] l Borrowed model layer whose cached categories are queried without mutation.
 * @return Total count across the four node categories. */
int totalNodeCount(const SWMMModelLayer &l)
{
    return l.categoryCount(SWMMModelLayer::CatJunctions) +
           l.categoryCount(SWMMModelLayer::CatOutfalls) +
           l.categoryCount(SWMMModelLayer::CatStorage) +
           l.categoryCount(SWMMModelLayer::CatDividers);
}

/** @brief Sum conduit, pump, orifice, weir and outlet category counts.
 * @param[in] l Borrowed model layer whose cached categories are queried without mutation.
 * @return Total count across the five link categories. */
int totalLinkCount(const SWMMModelLayer &l)
{
    return l.categoryCount(SWMMModelLayer::CatConduits) +
           l.categoryCount(SWMMModelLayer::CatPumps) +
           l.categoryCount(SWMMModelLayer::CatOrifices) +
           l.categoryCount(SWMMModelLayer::CatWeirs) + l.categoryCount(SWMMModelLayer::CatOutlets);
}

} // namespace

class TestAsyncLoad : public QObject
{
    Q_OBJECT

private slots:

    /** @brief Require valid/malformed/absent fixture paths and register the production canvas QML renderer types.
     * Sets the default surface format to six samples unless SWMMVIS_TEST_QSG_SAMPLES supplies an integer; these process-global registrations/settings support later widget render probes. */
    void initTestCase()
    {
        QVERIFY2(QFile::exists(fixturePath()),
                 "typed_selection_fixture.inp missing from the gui-test data dir");
        QVERIFY2(QFile::exists(malformedFixturePath()),
                 "typed_selection_malformed_fixture.inp missing from the gui-test data dir");
        QVERIFY2(!QFile::exists(nonexistentPath()),
                 "nonexistent-path fixture name unexpectedly exists on disk");

        // main.cpp registers these before any MapCanvas exists; the test
        // binary has no main.cpp, and without them swmmlayer.qml's
        // `import OpenSWMM 1.0` fails and the canvas silently has no GPU
        // renderers — blankProjectAddedObjectsRender's QSG row would then
        // fail for a harness reason rather than the one it pins.
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        // main.cpp §QSG-5 sets a 6-sample default surface format before any
        // overlay QQuickWidget exists; MapCanvas inherits it. Mirror that so
        // the overlay's FBO matches the app's (SWMMVIS_TEST_QSG_SAMPLES
        // overrides for diagnostics).
        {
            bool ok = false;
            int samples = qEnvironmentVariableIntValue("SWMMVIS_TEST_QSG_SAMPLES", &ok);
            if (!ok)
                samples = 6;
            QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
            fmt.setSamples(samples);
            QSurfaceFormat::setDefaultFormat(fmt);
        }
    }

    // 1. openEngineForPath error paths: empty path and a nonexistent path.
    //    Both are pure engine C-API calls — no GUI thread required.
    /** @brief Verify empty and missing paths return null handles with specific actionable diagnostics. */
    void openEngineForPathErrorPaths()
    {
        QString detail;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(QString(), &detail);
        QVERIFY(eng == nullptr);
        QCOMPARE(detail, QStringLiteral("No model file path specified."));

        QString detail2;
        SWMM_Engine eng2 = SWMMModelLayer::openEngineForPath(nonexistentPath(), &detail2);
        QVERIFY(eng2 == nullptr);
        QVERIFY2(detail2.contains(QStringLiteral("Model file not found")), qPrintable(detail2));
    }

    /** @brief Register NUL/control-byte corruptions, including CRLF text and corruption beyond the first one-megabyte read chunk. */
    void binaryInpRejected_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("byteDescription");
        QTest::newRow("utf16") << QByteArray("\xff\xfe[\0", 4) << 1 << QStringLiteral("NUL byte");
        QTest::newRow("timeseries-name")
            << QByteArray("[TIMESERIES]\r\nStorm") + QByteArray(1, '\0') + " 0:00 1\r\n"
            << 2 << QStringLiteral("NUL byte");
        QTest::newRow("comment") << QByteArray("[TITLE]\n; broken") + QByteArray(1, '\0') +
                                        "comment\n"
                                 << 2 << QStringLiteral("NUL byte");
        QTest::newRow("after-read-chunk")
            << QByteArray("; ") + QByteArray(1024 * 1024, 'x') + "\n" + QByteArray(1, '\0') << 2
            << QStringLiteral("NUL byte");
        QTest::newRow("name-control-character")
            << QByteArray("[TIMESERIES]\nVS_y1") + QByteArray(1, char(0x1d)) + "41 0:00 1\n"
            << 2 << QStringLiteral("Control byte 0x1d");
        QTest::newRow("delete-control-character")
            << QByteArray("; bad ") + QByteArray(1, char(0x7f)) << 1
            << QStringLiteral("Control byte 0x7f");
    }

    /** @brief Verify binary-corrupted decks fail lenient open with the offending byte, one-based line and path in the diagnostic.
     * Writes each payload into the fixture directory and destroys any unexpectedly returned engine before asserting failure. */
    void binaryInpRejected()
    {
        QFETCH(QByteArray, contents);
        QFETCH(int, line);
        QFETCH(QString, byteDescription);
        const QString path = QDir(dataDir()).filePath(
            QStringLiteral("binary_%1.inp").arg(QString::fromLatin1(QTest::currentDataTag())));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();
        QString detail;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(path, &detail);
        const bool opened = eng != nullptr;
        if (eng) {
            swmm_engine_close(eng);
            swmm_engine_destroy(eng);
        }
        QVERIFY2(!opened, "binary corruption must fail even with lenient open");
        QVERIFY2(detail.contains(QStringLiteral("%1 at line %2").arg(byteDescription).arg(line)),
                 qPrintable(detail));
        QVERIFY(detail.contains(path));
    }

    /** @brief Verify UTF-8 and legacy eight-bit comments remain loadable when prepended to a valid fixture.
     * Writes the combined deck into the fixture directory and explicitly closes/destroys the returned engine. */
    void textCommentsRemainLoadable()
    {
        QFile fixture(fixturePath());
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        // Valid UTF-8 and older eight-bit comment text are not binary corruption.
        const QByteArray contents =
            QByteArray("; Rivi\xc3\xa8re\n; legacy \xe9 comment\n") + fixture.readAll();
        const QString path = QDir(dataDir()).filePath(QStringLiteral("text_comments.inp"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();
        QString detail;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(path, &detail);
        QVERIFY2(eng != nullptr, qPrintable(detail));
        swmm_engine_close(eng);
        swmm_engine_destroy(eng);
    }

    // 2. Parse-error diagnostics preserved: a malformed fixture (see
    //    typed_selection_malformed_fixture.inp for why a [DWF] line naming
    //    an undefined node is the reliable failure — unknown section
    //    headers and garbled numeric fields are NOT fatal in this engine).
    //    openEngineForPath opens leniently, so a post-parse validation
    //    error no longer aborts the open — the model stays loadable for
    //    editing and the engine's real diagnostic (naming the offending
    //    node, not a generic "Out of memory") is queryable on the handle
    //    for adoptOpenEngine to surface as a warning.
    /** @brief Verify lenient open retains the undefined GHOST-node diagnostic on a usable engine handle.
     * Requires at least one real parse diagnostic and rejects generic out-of-memory substitutions before destroying the handle. */
    void parseErrorDiagnosticsPreserved()
    {
        QString detail;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(malformedFixturePath(), &detail);
        QVERIFY2(eng != nullptr, qPrintable(detail));
        const int n = swmm_get_error_count(eng);
        QVERIFY2(n >= 1, "the undefined-node diagnostic must be recorded");
        bool namesGhost = false;
        for (int i = 0; i < n; ++i) {
            const QString msg = QString::fromUtf8(swmm_get_error_at(eng, i));
            QVERIFY2(!msg.contains(QStringLiteral("Out of memory")), qPrintable(msg));
            if (msg.contains(QStringLiteral("GHOST")))
                namesGhost = true;
        }
        QVERIFY(namesGhost);
        swmm_engine_destroy(eng);
    }

    // 3. openEngineForPath() + buildFromEngine() + adoptOpenEngine() on a
    //    fresh layer (the async open sequence) must yield the same
    //    node/link/subcatchment/gage counts as a plain loadModel() on another
    //    layer for the same fixture.
    /** @brief Verify the open/build/adopt sequence yields the same node, link, subcatchment and gage counts as synchronous loading.
     * Checks nonnegative open milliseconds and transfers the opened engine to the asynchronous-path layer on adoption. */
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
    /** @brief Verify synchronous loading succeeds without errors and emits load timing through the logging category.
     * Temporarily installs the collecting message handler, then restores it and resets logging filter rules before checking the timing line. */
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
        QVERIFY2(foundTiming, "expected a '... load timing (ms): ...' line in the "
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
    /** @brief Verify real asynchronous project loading emits exactly one completion for both valid and missing model paths.
     * Waits at most twenty seconds per case, requires true for the valid fixture and false with errors for the missing file, and destroys windows before their shared workspace. */
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

    /** @brief Verify two opens queued on one window yield one success and one diagnostic failure within twenty seconds.
     * Uses the global-pool barrier to make overlap deterministic, then deletes window and workspace. */
    void duplicateAsyncOpenIsRefused()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
        QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
        {
            QueuedOpenBarrier barrier;
            window->loadModelAsync();
            window->loadModelAsync();
            barrier.release();
            QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 20000);
        }
        int successes = 0, failures = 0;
        for (const auto &args : spy) {
            if (args.at(0).toBool())
                ++successes;
            else {
                ++failures;
                QVERIFY(!args.at(2).value<QList<QString>>().isEmpty());
            }
        }
        QCOMPARE(successes, 1);
        QCOMPARE(failures, 1);
        delete window;
        delete workspace;
    }

    /** @brief Verify changing the model path while open is queued rejects the stale result.
     * Waits twenty seconds for failure and checks no engine/cache was adopted and the changed path remains. */
    void asyncOpenRejectsChangedPath()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
        QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
        {
            QueuedOpenBarrier barrier;
            window->loadModelAsync();
            window->modelLayer()->setModelFilePath(nonexistentPath());
            barrier.release();
            QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        }
        QVERIFY(!spy.at(0).at(0).toBool());
        QVERIFY(!spy.at(0).at(2).value<QList<QString>>().isEmpty());
        QVERIFY(window->modelLayer()->engine() == nullptr);
        QCOMPARE(window->modelLayer()->cachedNodeCount(), 0);
        QCOMPARE(window->modelLayer()->modelFilePath(), nonexistentPath());
        delete window;
        delete workspace;
    }

    /** @brief Verify asynchronous open refuses a layer already holding a synchronously loaded engine.
     * Requires one failure completion within twenty seconds and preserves the original engine identity and node cache. */
    void asyncOpenDoesNotReplaceLoadedEngine()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
        QList<QString> warnings, errors;
        QVERIFY(window->loadModel(warnings, errors));
        const SWMM_Engine original = window->modelLayer()->engine();
        const int count = window->modelLayer()->cachedNodeCount();
        QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
        window->loadModelAsync();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QVERIFY(!spy.at(0).at(0).toBool());
        QCOMPARE(window->modelLayer()->engine(), original);
        QCOMPARE(window->modelLayer()->cachedNodeCount(), count);
        delete window;
        delete workspace;
    }

    /** @brief Verify an edit revision emitted during queued open fences out the stale engine and node cache. */
    void asyncOpenRejectsChangedRevision()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
        QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
        {
            QueuedOpenBarrier barrier;
            window->loadModelAsync();
            emit window->modelLayer()->modelEdited();
            barrier.release();
            QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        }
        QVERIFY(!spy.at(0).at(0).toBool());
        QVERIFY(window->modelLayer()->engine() == nullptr);
        QCOMPARE(window->modelLayer()->cachedNodeCount(), 0);
        delete window;
        delete workspace;
    }

    /** @brief Verify deleting the project during its modal CRS selection makes finishing fail safely with errors.
     * Uses a queued owner deletion, checks the guarded pointer cleared, then deletes the remaining workspace. */
    void closingOwnerDuringCrsPromptIsSafe()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QPointer<SWMMVisProjectWindow> window =
            new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
        QList<QString> warnings, errors;
        QVERIFY(window->loadModel(warnings, errors));
        window->modelLayer()->setSRS(SpatialReferenceSystem::untitled(window->modelLayer()), true);
        window->mCanvasCRSAdopted = false;
        QTimer::singleShot(0, [window] { delete window.data(); });
        QVERIFY(!window->finishModelLoad(warnings, errors));
        QVERIFY(!window);
        QVERIFY(!errors.isEmpty());
        delete workspace;
    }

    /** @brief Verify an injected worker exception reaches one failure signal without adopting engine/cache state.
     * Clears the injection and requires a successful retry within twenty seconds before tearing down the owners. */
    void asyncWorkerExceptionIsReportedAndRetryWorks()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
        QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
        window->mAsyncLoadWorkerTestHook = [] {
            throw std::runtime_error("injected open failure");
        };
        window->loadModelAsync();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QVERIFY(!spy.at(0).at(0).toBool());
        QVERIFY(
            spy.at(0).at(2).value<QList<QString>>().join(' ').contains("injected open failure"));
        QVERIFY(window->modelLayer()->engine() == nullptr);
        QCOMPARE(window->modelLayer()->cachedNodeCount(), 0);
        window->mAsyncLoadWorkerTestHook = {};
        spy.clear();
        window->loadModelAsync();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 20000);
        QVERIFY(spy.at(0).at(0).toBool());
        QVERIFY(window->modelLayer()->engine() != nullptr);
        delete window;
        delete workspace;
    }

    /** @brief Register deletion of the window, detached layer, or window/workspace while an open is queued. */
    void asyncOwnerTeardown_data()
    {
        QTest::addColumn<QString>("owner");
        QTest::newRow("window") << QString("window");
        QTest::newRow("layer") << QString("layer");
        QTest::newRow("workspace") << QString("workspace");
    }

    /** @brief Verify queued open safely handles owner teardown in an isolated child process.
     * Parent waits thirty seconds then kills a hung child; the child deletes the selected owner behind a global-pool barrier, drains workers/events, and checks any surviving window fails or surviving layer remains empty. */
    void asyncOwnerTeardown()
    {
        QFETCH(QString, owner);
        if (qEnvironmentVariable("SWMMVIS_ASYNC_TEARDOWN_CHILD") != owner) {
            QProcess child;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("SWMMVIS_ASYNC_TEARDOWN_CHILD", owner);
            child.setProcessEnvironment(environment);
            child.start(QCoreApplication::applicationFilePath(),
                        {"asyncOwnerTeardown:" + owner, "-o", "-,txt"});
            QVERIFY(child.waitForStarted());
            const bool ended = child.waitForFinished(30000);
            if (!ended) {
                child.kill();
                child.waitForFinished();
            }
            const QByteArray details = child.readAllStandardOutput() + child.readAllStandardError();
            QVERIFY2(ended, details.constData());
            QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                     details.constData());
            return;
        }
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QPointer<SWMMVisProjectWindow> window =
            new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
        QPointer<SWMMModelLayer> layer = window->modelLayer();
        QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
        {
            QueuedOpenBarrier barrier;
            window->loadModelAsync();
            if (owner == "window") {
                delete window;
            } else if (owner == "layer") {
                const int index = window->canvas()->layers().indexOf(layer);
                QVERIFY(index >= 0);
                window->canvas()->takeLayer(index, false);
                delete layer;
            } else {
                delete window;
                delete workspace;
                workspace = nullptr;
            }
            barrier.release();
            QThreadPool::globalInstance()->waitForDone();
        }
        QCoreApplication::processEvents();
        if (window) {
            QCOMPARE(spy.count(), 1);
            QVERIFY(!spy.at(0).at(0).toBool());
        } else if (layer) {
            QVERIFY(layer->engine() == nullptr);
            QCOMPARE(layer->cachedNodeCount(), 0);
        }
        delete window;
        delete workspace;
    }

    // 5b. LINK_OFFSETS ELEVATION must survive the async load into the project
    //     window's offset-mode flag — the status bar's Offset Mode toggle
    //     reads exactly SWMMVisProjectWindow::isElevationOffsetMode(). DEPTH
    //     cannot detect a failure here (it is SimulationOptions' default, so
    //     the assertion passes even if the value never arrives), so this
    //     fixture declares ELEVATION.
    /** @brief Verify asynchronous loading hydrates both engine and project-window offset mode from an ELEVATION deck.
     * Creates a temporary token-modified fixture, waits twenty seconds, and checks the window agrees with the engine option. */
    void elevationOffsetModeReachesTheProjectWindow()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Same fixture, one token swapped — keeps the network identical to the
        // DEPTH case above so only the option under test differs.
        QFile src(fixturePath());
        QVERIFY(src.open(QIODevice::ReadOnly | QIODevice::Text));
        QString deck = QString::fromUtf8(src.readAll());
        src.close();
        QVERIFY2(deck.contains(QStringLiteral("LINK_OFFSETS         DEPTH")),
                 "fixture no longer declares LINK_OFFSETS DEPTH — update this swap");
        deck.replace(QStringLiteral("LINK_OFFSETS         DEPTH"),
                     QStringLiteral("LINK_OFFSETS         ELEVATION"));

        const QString elevPath = tmp.filePath(QStringLiteral("offsets_elevation.inp"));
        QFile out(elevPath);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Text));
        out.write(deck.toUtf8());
        out.close();

        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(workspace, elevPath, nullptr);
        QSignalSpy spy(window, &SWMMVisProjectWindow::modelLoadFinished);
        QVERIFY(spy.isValid());

        window->loadModelAsync();
        QVERIFY2(spy.wait(20000), "modelLoadFinished did not fire within 20s");
        QCOMPARE(spy.count(), 1);
        QVERIFY2(spy.takeFirst().at(0).toBool(), "ELEVATION deck failed to load");

        // The engine parsed it...
        char buf[32] = {};
        QVERIFY(window->modelLayer() != nullptr);
        QVERIFY(window->modelLayer()->engine() != nullptr);
        QCOMPARE(swmm_options_get(window->modelLayer()->engine(), "LINK_OFFSETS", buf, sizeof(buf)),
                 0);
        QCOMPARE(QString::fromLatin1(buf).trimmed().toUpper(), QStringLiteral("ELEVATION"));

        // ...and the project window's flag agrees, which is what the status
        // bar hydrates from.
        QVERIFY2(window->isElevationOffsetMode(),
                 "engine says ELEVATION but the project window still reports "
                 "depth offsets — the status-bar toggle will show Depth");

        delete window;
        delete workspace;
    }

    // 6. In-memory File → New: createBlankEngine stamps the preferences
    //    defaults onto a BUILDING-state engine and every key reads back
    //    through swmm_options_get — the contract readFromEngine-style
    //    consumers and the first Save As rely on. Also proves
    //    swmm_model_write works from BUILDING (no finalize).
    /** @brief Verify a new BUILDING engine contains configured simulation defaults and can write a first zero-object model.
     * Checks seconds, percentage tolerances, threads and optional CPU backend, reopens the written deck to check routing, destroys both handles and removes the working-directory output. */
    void blankEngineDefaultsRoundTrip()
    {
        SWMMModelLayer::NewProjectSpec spec;
        spec.name = QStringLiteral("Untitled");
        spec.forNewEngine = true;
        spec.startDateTime = QDateTime(QDate(2026, 8, 6), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addSecs(24 * 3600);
        spec.sim = PreferencesManager::SimulationDefaults{};
        spec.sim.threads = 4;
        spec.twoD = PreferencesManager::TwoDDefaults{};

        QString detail;
        SWMM_Engine eng = SWMMModelLayer::createBlankEngine(spec, &detail);
        QVERIFY2(eng != nullptr, qPrintable(detail));

        /** @brief Read a new-engine option into a fixed 128-byte buffer.
         * Argument `key`: required engine option name; the captured engine must remain live.
         * Returns: trimmed UTF-8 option text on success, or an empty string on any getter error. */
        auto get = [eng](const char *key) {
            char buf[128] = {};
            const int rc = swmm_options_get(eng, key, buf, sizeof(buf));
            return rc == 0 ? QString::fromUtf8(buf).trimmed() : QString();
        };

#ifdef OPENSWMM_HAS_2D
        char backend[32] = {};
        QCOMPARE(swmm_options_get_ext(eng, "BACKEND", backend, sizeof backend), 0);
        QCOMPARE(QByteArray(backend), QByteArray("CPU"));
#endif

        const auto &d = spec.sim;
        QCOMPARE(get("FLOW_UNITS"), d.flowUnits);
        QCOMPARE(get("FLOW_ROUTING"), d.flowRouting);
        QCOMPARE(get("INFILTRATION").left(6), d.infiltrationModel.left(6));
        QCOMPARE(get("LINK_OFFSETS"), QStringLiteral("DEPTH"));
        QCOMPARE(get("START_DATE"), QStringLiteral("08/06/2026"));
        QCOMPARE(get("END_DATE"), QStringLiteral("08/07/2026"));
        QCOMPARE(get("MAX_TRIALS").toInt(), d.maxTrials);
        QCOMPARE(get("THREADS").toInt(), 4);
        QCOMPARE(get("REPORT_STEP").toDouble(), double(d.reportStepSec));
        QCOMPARE(get("ROUTING_STEP").toDouble(), d.routingStepSec);
        // Percent both ways (the 2026-08-06 persistence-fix contract).
        QCOMPARE(get("SYS_FLOW_TOL").toDouble(), d.sysFlowTolPct);
        QCOMPARE(get("LAT_FLOW_TOL").toDouble(), d.latFlowTolPct);
        QCOMPARE(get("HEAD_TOLERANCE").toDouble(), d.headTolerance);
        QCOMPARE(get("SURCHARGE_METHOD"), d.surchargeMethod);

        // BUILDING-state write: the first Save As path with zero objects.
        // Written into the working directory (build tree) — reviewable on
        // failure, never in the tracked data dir.
        const QString outPath =
            QDir::current().filePath(QStringLiteral("blank_new_engine_roundtrip.inp"));
        QFile::remove(outPath);
        const QByteArray outUtf8 = outPath.toUtf8();
        QCOMPARE(swmm_model_write(eng, outUtf8.constData()), 0);
        QVERIFY(QFile::exists(outPath));
        swmm_engine_destroy(eng);

        // The written .inp reopens with the same options.
        QString openDetail;
        SWMM_Engine reopened = SWMMModelLayer::openEngineForPath(outPath, &openDetail);
        QVERIFY2(reopened != nullptr, qPrintable(openDetail));
        char buf[64] = {};
        QCOMPARE(swmm_options_get(reopened, "FLOW_ROUTING", buf, sizeof(buf)), 0);
        QCOMPARE(QString::fromUtf8(buf).trimmed(), d.flowRouting);
        swmm_engine_destroy(reopened);
        QFile::remove(outPath);
    }

    // 7. adoptNewEngine on a pathless layer: keeps the "Untitled" name, and
    //    the CRS derives Local (ft)/(m) from the flow units — proving the
    //    CRS-picker modal can never fire on File → New.
    /** @brief Verify new pathless CFS/CMS layers retain Untitled names and derive Local (ft)/Local (m) CRS descriptions. */
    void adoptNewEngineKeepsNameAndDerivesLocalCrs()
    {
        SWMMModelLayer::NewProjectSpec spec;
        spec.name = QStringLiteral("Untitled");
        spec.startDateTime = QDateTime(QDate(2026, 8, 6), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addSecs(3600);
        spec.sim = PreferencesManager::SimulationDefaults{};

        struct Case {
            QString flowUnits;
            QString wantCrs;
        };
        const Case cases[] = {
            {QStringLiteral("CFS"), QStringLiteral("Local (ft)")},
            {QStringLiteral("CMS"), QStringLiteral("Local (m)")},
        };
        for (const Case &c : cases) {
            spec.sim.flowUnits = c.flowUnits;
            SWMMModelLayer layer(QString(), nullptr);
            layer.setName(QStringLiteral("Untitled"));
            QList<QString> warnings, errors;
            QVERIFY2(layer.adoptNewEngine(spec, warnings, errors),
                     qPrintable(errors.join(QStringLiteral("; "))));
            QCOMPARE(layer.name(), QStringLiteral("Untitled"));
            QVERIFY(layer.modelFilePath().isEmpty());
            QVERIFY(layer.srs() != nullptr);
            QCOMPARE(layer.srs()->description(), c.wantCrs);
        }
    }

    // 8. initializeBlankModel via the project window: pathless, untitled,
    //    pristine — the state the always-prompt close guard keys on.
    /** @brief Verify a newly initialized untitled project is pathless and pristine but still prompts on close.
     * Drives the real close modal with queued Cancel then Discard clicks and checks the corresponding close refusal/acceptance. */
    void blankProjectWindowFlags()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(workspace != nullptr);

        SWMMModelLayer::NewProjectSpec spec;
        spec.name = QStringLiteral("Untitled");
        spec.startDateTime = QDateTime(QDate(2026, 8, 6), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addSecs(3600);
        spec.sim = PreferencesManager::SimulationDefaults{};

        auto *window = new SWMMVisProjectWindow(workspace, QString(), nullptr);
        window->markUntitled();
        QList<QString> warnings, errors;
        QVERIFY2(window->initializeBlankModel(spec, warnings, errors),
                 qPrintable(errors.join(QStringLiteral("; "))));

        QVERIFY(window->isUntitled());
        QVERIFY(!window->hasChanges());
        QCOMPARE(window->windowTitle(), QStringLiteral("Untitled"));
        QVERIFY(window->modelLayer() != nullptr);
        QVERIFY(window->modelLayer()->engine() != nullptr);
        QVERIFY(window->modelLayer()->modelFilePath().isEmpty());

        // Close prompt: Cancel keeps the window alive, Discard closes it —
        // drive the modal from a queued lambda since exec() blocks.
        /** @brief Queue one attempt to click a close-prompt button in the GUI event loop.
         * Argument `std`: preferred QMessageBox standard button.
         * Argument `fallbackText`: required C string whose storage must outlive the queued callback; used as a button-text substring if the standard button is absent.
         * Does nothing if no message box or matching button exists at the single queued attempt. */
        auto clickButton = [](QMessageBox::StandardButton std, const char *fallbackText) {
            QTimer::singleShot(0, [std, fallbackText]() {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box)
                    return;
                if (QAbstractButton *b = box->button(std)) {
                    b->click();
                    return;
                }
                const auto buttons = box->buttons();
                for (QAbstractButton *b : buttons)
                    if (b->text().contains(QLatin1String(fallbackText))) {
                        b->click();
                        return;
                    }
            });
        };

        clickButton(QMessageBox::Cancel, "Cancel");
        QVERIFY(!window->close()); // Cancel → close refused
        QVERIFY(window->isUntitled());

        clickButton(QMessageBox::Discard, "Discard");
        QVERIFY(window->close()); // Discard → closes

        delete window;
        delete workspace;
    }

    // 8b. File → New, then draw with the real map tools. The junctions and
    //     the conduit must reach PIXELS on the canvas, on both render paths
    //     (CPU SWMMLayerItem and the QSG overlay the Preferences default
    //     selects). Reported 2026-09-05: on a fresh Local-CRS project, added
    //     nodes and links "do not render". Baseline/after grabs are written
    //     next to the binary (CLAUDE.md §4.1) so a failure can be eyeballed.
    /** @brief Register CPU scene-item and QSG overlay rendering of objects drawn into an initially blank project. */
    void blankProjectAddedObjectsRender_data()
    {
        QTest::addColumn<bool>("qsg");
        QTest::newRow("cpu-layeritem") << false;
        QTest::newRow("qsg-overlay") << true;
    }
    /** @brief Verify map tools add two junctions and a conduit that visibly change canvas pixels in both render paths.
     * Checks Local (ft) click coordinates and caches, retains before/after captures in the working directory, and requires changed pixels near both nodes and the conduit midpoint. The QSG row first tests readback capability, skips unobservable platforms, and checks loaded-model CPU-to-QSG handoff. */
    void blankProjectAddedObjectsRender()
    {
        QFETCH(bool, qsg);
        const QString tag = QString::fromLatin1(QTest::currentDataTag());

        // Pixels that differ from the corner background inside the central
        // 80% of a grab — decorations (scale bar, coordinates) live at the
        // edges, so this counts drawn network geometry only.
        /** @brief Count nonbackground pixels in the central eighty percent of an image.
         * Argument `img`: image large enough to sample pixel (5,5) when non-null; no size check is performed.
         * Returns: -1 for a null image, otherwise pixels unequal to that sampled background. The central crop excludes canvas decorations. */
        auto nonBackground = [](const QImage &img) {
            if (img.isNull())
                return -1;
            const QRgb bg = img.pixel(5, 5);
            const int x0 = img.width() / 10, x1 = img.width() - x0;
            const int y0 = img.height() / 10, y1 = img.height() - y0;
            int n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    if (img.pixel(x, y) != bg)
                        ++n;
            return n;
        };

        // The GPU row needs two things this harness cannot take for granted.
        if (qsg) {
            auto *ws2 = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
            auto *w2 = new SWMMVisProjectWindow(ws2, fixturePath(), nullptr);
            QList<QString> lw, le;
            QVERIFY2(w2->loadModel(lw, le), qPrintable(le.join(QStringLiteral("; "))));
            w2->resize(900, 700);
            w2->show();
            QVERIFY(QTest::qWaitForWindowExposed(w2));
            QTest::qWait(300);
            MapCanvas *c2 = w2->canvas();
            c2->zoomToFullExtent();
            using K2 = SWMMModelLayer;

            // 1. Platform gate. MapCanvas parks its overlay QQuickWidget as a
            //    WA_DontShowOnScreen top-level. The same overlay QML and
            //    renderer, in a fresh widget of that kind, drawing the loaded
            //    model from a tree that STARTS populated: if this process
            //    cannot render/read that back (the offscreen QPA cannot), the
            //    row is skipped rather than reporting a bug it cannot observe.
            {
                QQuickWidget probe(nullptr);
                probe.setAttribute(Qt::WA_DontShowOnScreen);
                probe.setAttribute(Qt::WA_QuitOnClose, false);
                probe.setClearColor(Qt::transparent);
                probe.setResizeMode(QQuickWidget::SizeRootObjectToView);
                probe.setSource(QUrl(QStringLiteral("qrc:/openswmm/qml/swmmlayer.qml")));
                probe.resize(900, 700);
                probe.show();
                QTest::qWait(100);
                auto *r = probe.rootObject()
                              ? probe.rootObject()->findChild<SWMMLayerQSGRenderer *>(
                                    QStringLiteral("swmmRenderer"))
                              : nullptr;
                QVERIFY2(r, "swmmlayer.qml did not yield a SWMMLayerQSGRenderer "
                            "(QML types registered in initTestCase, resource in the target)");
                r->setLayer(w2->modelLayer());
                r->setMapExtent(c2->extent());
                QTest::qWait(100);
                const int px = nonBackground(probe.grabFramebuffer());
                r->setLayer(nullptr);
                qInfo().noquote()
                    << QStringLiteral(
                           "[control] fresh overlay widget drawing the loaded model: nonBg=%1")
                           .arg(px);
                if (px <= 0) {
                    delete w2;
                    delete ws2;
                    QSKIP("this platform cannot render/read back the off-screen QSG "
                          "overlay — the qsg-overlay row cannot be observed here");
                }
            }
            // 2. The loaded model through MapCanvas itself. Handing the kinds
            //    from the CPU painter to the overlay makes every overlay bucket
            //    go empty → populated — the same transition an empty project
            //    makes on its first added object — so this is asserted, not
            //    just logged: before the geometryDirtyFlags fix in
            //    SWMMLayerQSGRenderer the overlay stayed blank forever after
            //    an empty upload (Qt's batch renderer never batches an empty
            //    element and ignores DirtyGeometry on an unbatched one).
            {
                w2->modelLayer()->setQsgRenderKinds(K2::QsgKinds(K2::QsgNone));
                QTest::qWait(150);
                const int cpuPx = nonBackground(c2->grab().toImage());
                w2->modelLayer()->setQsgRenderKinds(
                    K2::QsgKinds(K2::QsgNodes | K2::QsgLinks | K2::QsgCatch | K2::QsgGages));
                QTest::qWait(150);
                const QImage qsgImg = c2->grab().toImage();
                qsgImg.save(QDir::current().filePath(
                    QStringLiteral("newproject_render_control_loaded_qsg.png")));
                const int qsgPx = nonBackground(qsgImg);
                qInfo().noquote()
                    << QStringLiteral(
                           "[control] loaded fixture non-background pixels: cpu=%1 qsg=%2")
                           .arg(cpuPx)
                           .arg(qsgPx);
                delete w2;
                delete ws2;
                QVERIFY2(cpuPx > 0, "control: CPU path drew nothing for a loaded model");
                QVERIFY2(qsgPx * 10 >= cpuPx,
                         qPrintable(QStringLiteral(
                                        "loaded model: the QSG overlay drew (almost) nothing after "
                                        "the CPU→QSG hand-off (cpu=%1 qsg=%2) — buckets that were "
                                        "empty once never render again")
                                        .arg(cpuPx)
                                        .arg(qsgPx)));
            }
        }

        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(workspace != nullptr);

        SWMMModelLayer::NewProjectSpec spec;
        spec.name = QStringLiteral("Untitled");
        spec.forNewEngine = true;
        spec.startDateTime = QDateTime(QDate(2026, 9, 5), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addSecs(3600);
        spec.sim = PreferencesManager::SimulationDefaults{};
        spec.sim.flowUnits = QStringLiteral("CFS"); // → Local (ft)
        spec.twoD = PreferencesManager::TwoDDefaults{};

        auto *window = new SWMMVisProjectWindow(workspace, QString(), nullptr);
        window->markUntitled();
        QList<QString> warnings, errors;
        QVERIFY2(window->initializeBlankModel(spec, warnings, errors),
                 qPrintable(errors.join(QStringLiteral("; "))));

        window->resize(900, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(150); // finishModelLoad's canvas-sized retry + refresh timers

        MapCanvas *canvas = window->canvas();
        SWMMModelLayer *layer = window->modelLayer();
        QVERIFY(canvas && layer);
        QVERIFY2(
            canvas->width() > 200 && canvas->height() > 200,
            qPrintable(QStringLiteral("canvas %1x%2").arg(canvas->width()).arg(canvas->height())));
        QVERIFY(canvas->extent().isValid());
        QVERIFY(canvas->canvasSRS() != nullptr);
        QCOMPARE(canvas->canvasSRS()->description(), QStringLiteral("Local (ft)"));
        QVERIFY(layer->isVisible());

        using K = SWMMModelLayer;
        layer->setQsgRenderKinds(
            qsg ? K::QsgKinds(K::QsgNodes | K::QsgLinks | K::QsgCatch | K::QsgGages)
                : K::QsgKinds(K::QsgNone));
        QTest::qWait(100);
        const QImage before = canvas->grab().toImage();
        before.save(
            QDir::current().filePath(QStringLiteral("newproject_render_%1_before.png").arg(tag)));

        // Two junctions + one conduit, exactly as the toolbar does it.
        const QPoint pA(int(canvas->width() * 0.30), int(canvas->height() * 0.40));
        const QPoint pB(int(canvas->width() * 0.70), int(canvas->height() * 0.60));
        {
            OpenSWMMVisMapToolAddNode addJ(canvas, 0, QStringLiteral("junction"));
            canvas->setActiveTool(&addJ);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, pA);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, pB);
            canvas->setActiveTool(nullptr);
        }
        QCOMPARE(layer->cachedNodeCount(), 2);
        {
            // The stored coordinate is the click, in the (Local) canvas CRS.
            double mx = 0, my = 0, nx = 0, ny = 0;
            canvas->toMapCoords(pA.x(), pA.y(), mx, my);
            QVERIFY(layer->cachedNodeCoord(0, &nx, &ny));
            QVERIFY2(qAbs(nx - mx) < 1e-6 * qMax(1.0, qAbs(mx)) &&
                         qAbs(ny - my) < 1e-6 * qMax(1.0, qAbs(my)),
                     qPrintable(QStringLiteral("node (%1,%2) vs click (%3,%4)")
                                    .arg(nx)
                                    .arg(ny)
                                    .arg(mx)
                                    .arg(my)));
        }
        {
            OpenSWMMVisMapToolAddLink addC(canvas, 0, QStringLiteral("conduit"));
            canvas->setActiveTool(&addC);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, pA);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, pB);
            canvas->setActiveTool(nullptr);
        }
        QCOMPARE(layer->cachedLinkCount(), 1);

        QTest::qWait(250); // Scene-channel debounce + refresh timer
        const QImage after = canvas->grab().toImage();
        after.save(
            QDir::current().filePath(QStringLiteral("newproject_render_%1_after.png").arg(tag)));
        QCOMPARE(after.size(), before.size());

        // Count pixels that changed inside a box around each click and at the
        // conduit's midpoint. Decorations (scale bar, north arrow) are in both
        // grabs, so only the added objects can differ.
        /** @brief Count before/after pixel differences in a clipped square around a device-pixel point.
         * Argument `c`: center in image pixel coordinates.
         * Argument `half`: nonnegative half-width/height, producing a (2*half+1)-pixel square before clipping.
         * Returns: differing pixel count; captured before/after images must have equal sizes, as asserted by the calling test. */
        auto changedAround = [&](const QPoint &c, int half) {
            int n = 0;
            for (int y = c.y() - half; y <= c.y() + half; ++y)
                for (int x = c.x() - half; x <= c.x() + half; ++x) {
                    if (x < 0 || y < 0 || x >= after.width() || y >= after.height())
                        continue;
                    if (after.pixel(x, y) != before.pixel(x, y))
                        ++n;
                }
            return n;
        };
        const int dpr = qMax(1, int(after.devicePixelRatio()));
        const QPoint dA = pA * dpr, dB = pB * dpr, dM = (pA + pB) / 2 * dpr;
        const int nA = changedAround(dA, 12 * dpr);
        const int nB = changedAround(dB, 12 * dpr);
        const int nM = changedAround(dM, 6 * dpr);
        const QString why = QStringLiteral("[%1] changed pixels: nodeA=%2 nodeB=%3 conduitMid=%4 "
                                           "(canvas %5x%6, extent %7)")
                                .arg(tag)
                                .arg(nA)
                                .arg(nB)
                                .arg(nM)
                                .arg(canvas->width())
                                .arg(canvas->height())
                                .arg(canvas->extent().toString());
        qInfo().noquote() << why;
        QVERIFY2(nA > 0, qPrintable("junction A never reached the canvas: " + why));
        QVERIFY2(nB > 0, qPrintable("junction B never reached the canvas: " + why));
        QVERIFY2(nM > 0, qPrintable("conduit never reached the canvas: " + why));

        // No close(): the edits made the project dirty and close() would park
        // on the save prompt. Deleting skips closeEvent, which is all we need.
        delete window;
        delete workspace;
    }

    // 9. Layers-panel → object-browser category sync: selectCategory picks
    //    the right category header WITHOUT wiping the SelectionManager —
    //    a category header maps to zero object refs, so an unguarded tree
    //    selection would push an empty Replace onto the bus.
    /** @brief Verify selecting a category focuses its browser header while preserving the existing map-selection bus.
     * Also checks selecting an empty storage category leaves the selected junction intact. */
    void selectCategoryFocusesHeaderWithoutWipingBus()
    {
        SWMMModelLayer layer(fixturePath(), nullptr);
        QList<QString> warnings, errors;
        QVERIFY(layer.loadModel(warnings, errors));
        QVERIFY(layer.categoryCount(SWMMModelLayer::CatJunctions) > 0);

        SelectionManager selMgr;
        ObjectBrowserPanel panel;
        panel.setProject(&layer, &selMgr, nullptr);

        // Seed a bus selection (first junction).
        const QString firstJunction = layer.objectNameAt(SWMMModelLayer::CatJunctions, 0);
        QVERIFY(!firstJunction.isEmpty());
        selMgr.select(SWMMObjectRef{SWMMObjectRef::Node, firstJunction}, SelectionManager::Replace);
        QCOMPARE(selMgr.selection().size(), 1);

        // Focus the Junctions category — bus selection must survive.
        panel.selectCategory(SWMMModelLayer::CatJunctions);
        QCOMPARE(selMgr.selection().size(), 1);
        QVERIFY(selMgr.contains(SWMMObjectRef{SWMMObjectRef::Node, firstJunction}));

        auto *view = panel.findChild<QTreeView *>();
        QVERIFY(view);
        QVERIFY(view->currentIndex().isValid());
        QVERIFY(view->currentIndex()
                    .data(Qt::DisplayRole)
                    .toString()
                    .contains(QStringLiteral("Junction")));

        // Empty/hidden category → graceful no-op (fixture has no LID rows).
        panel.selectCategory(SWMMModelLayer::CatStorage);
        QCOMPARE(selMgr.selection().size(), 1);
    }

    // 10. Optional profiling harness (Phase 0 of the file-open plan). Loads an
    //    arbitrary model named by SWMM_PROFILE_INP and dumps the GUI-thread
    //    load breakdown (engine_open vs SoA copy vs CRS vs geometry cache,
    //    plus sub-splits) captured from the openswmm.load.model category.
    //    Skips when the env var is unset, so it is a no-op in CI.
    /** @brief Optionally load SWMM_PROFILE_INP and print captured load timing plus object counts.
     * Skips when unset, checks existence and successful synchronous load, and restores the message handler and resets category filter rules; this profiler asserts no performance threshold. */
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
                                 .arg(layer.cachedNodeCount())
                                 .arg(layer.cachedLinkCount())
                                 .arg(layer.cachedSubcatchCount())
                                 .arg(layer.cachedGageCount());
        for (const QString &m : g_loadLog)
            qInfo().noquote() << "  " << m;
    }
};

QTEST_MAIN(TestAsyncLoad)
#include "test_asyncload.moc"
