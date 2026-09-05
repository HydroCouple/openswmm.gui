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
#include <QQuickWindow>
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
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

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

        // main.cpp registers these before any MapCanvas exists; the test
        // binary has no main.cpp, and without them swmmlayer.qml's
        // `import OpenSWMM 1.0` fails and the canvas silently has no GPU
        // renderers — blankProjectAddedObjectsRender's QSG row would then
        // fail for a harness reason rather than the one it pins.
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0,
                                               "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0,
                                                "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0,
                                                   "SWMM2DResultsQSGRenderer");
        // main.cpp §QSG-5 sets a 6-sample default surface format before any
        // overlay QQuickWidget exists; MapCanvas inherits it. Mirror that so
        // the overlay's FBO matches the app's (SWMMVIS_TEST_QSG_SAMPLES
        // overrides for diagnostics).
        {
            bool ok = false;
            int samples = qEnvironmentVariableIntValue("SWMMVIS_TEST_QSG_SAMPLES", &ok);
            if (!ok) samples = 6;
            QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
            fmt.setSamples(samples);
            QSurfaceFormat::setDefaultFormat(fmt);
        }
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
    //    headers and garbled numeric fields are NOT fatal in this engine).
    //    openEngineForPath opens leniently, so a post-parse validation
    //    error no longer aborts the open — the model stays loadable for
    //    editing and the engine's real diagnostic (naming the offending
    //    node, not a generic "Out of memory") is queryable on the handle
    //    for adoptOpenEngine to surface as a warning.
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
            if (msg.contains(QStringLiteral("GHOST"))) namesGhost = true;
        }
        QVERIFY(namesGhost);
        swmm_engine_destroy(eng);
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

    // 5b. LINK_OFFSETS ELEVATION must survive the async load into the project
    //     window's offset-mode flag — the status bar's Offset Mode toggle
    //     reads exactly SWMMVisProjectWindow::isElevationOffsetMode(). DEPTH
    //     cannot detect a failure here (it is SimulationOptions' default, so
    //     the assertion passes even if the value never arrives), so this
    //     fixture declares ELEVATION.
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
        QCOMPARE(swmm_options_get(window->modelLayer()->engine(),
                                  "LINK_OFFSETS", buf, sizeof(buf)), 0);
        QCOMPARE(QString::fromLatin1(buf).trimmed().toUpper(),
                 QStringLiteral("ELEVATION"));

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
    void blankEngineDefaultsRoundTrip()
    {
        SWMMModelLayer::NewProjectSpec spec;
        spec.name          = QStringLiteral("Untitled");
        spec.forNewEngine  = true;
        spec.startDateTime = QDateTime(QDate(2026, 8, 6), QTime(0, 0));
        spec.endDateTime   = spec.startDateTime.addSecs(24 * 3600);
        spec.sim           = PreferencesManager::SimulationDefaults{};
        spec.sim.threads   = 4;
        spec.twoD          = PreferencesManager::TwoDDefaults{};

        QString detail;
        SWMM_Engine eng = SWMMModelLayer::createBlankEngine(spec, &detail);
        QVERIFY2(eng != nullptr, qPrintable(detail));

        auto get = [eng](const char *key) {
            char buf[128] = {};
            const int rc = swmm_options_get(eng, key, buf, sizeof(buf));
            return rc == 0 ? QString::fromUtf8(buf).trimmed() : QString();
        };

        const auto &d = spec.sim;
        QCOMPARE(get("FLOW_UNITS"),   d.flowUnits);
        QCOMPARE(get("FLOW_ROUTING"), d.flowRouting);
        QCOMPARE(get("INFILTRATION").left(6), d.infiltrationModel.left(6));
        QCOMPARE(get("LINK_OFFSETS"), QStringLiteral("DEPTH"));
        QCOMPARE(get("START_DATE"),   QStringLiteral("08/06/2026"));
        QCOMPARE(get("END_DATE"),     QStringLiteral("08/07/2026"));
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
        const QString outPath = QDir::current().filePath(
            QStringLiteral("blank_new_engine_roundtrip.inp"));
        QFile::remove(outPath);
        const QByteArray outUtf8 = outPath.toUtf8();
        QCOMPARE(swmm_model_write(eng, outUtf8.constData()), 0);
        QVERIFY(QFile::exists(outPath));
        swmm_engine_destroy(eng);

        // The written .inp reopens with the same options.
        QString openDetail;
        SWMM_Engine reopened =
            SWMMModelLayer::openEngineForPath(outPath, &openDetail);
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
    void adoptNewEngineKeepsNameAndDerivesLocalCrs()
    {
        SWMMModelLayer::NewProjectSpec spec;
        spec.name          = QStringLiteral("Untitled");
        spec.startDateTime = QDateTime(QDate(2026, 8, 6), QTime(0, 0));
        spec.endDateTime   = spec.startDateTime.addSecs(3600);
        spec.sim           = PreferencesManager::SimulationDefaults{};

        struct Case { QString flowUnits; QString wantCrs; };
        const Case cases[] = {
            { QStringLiteral("CFS"), QStringLiteral("Local (ft)") },
            { QStringLiteral("CMS"), QStringLiteral("Local (m)")  },
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
    void blankProjectWindowFlags()
    {
        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(workspace != nullptr);

        SWMMModelLayer::NewProjectSpec spec;
        spec.name          = QStringLiteral("Untitled");
        spec.startDateTime = QDateTime(QDate(2026, 8, 6), QTime(0, 0));
        spec.endDateTime   = spec.startDateTime.addSecs(3600);
        spec.sim           = PreferencesManager::SimulationDefaults{};

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
        auto clickButton = [](QMessageBox::StandardButton std,
                              const char *fallbackText) {
            QTimer::singleShot(0, [std, fallbackText]() {
                auto *box = qobject_cast<QMessageBox *>(
                    QApplication::activeModalWidget());
                if (!box) return;
                if (QAbstractButton *b = box->button(std)) { b->click(); return; }
                const auto buttons = box->buttons();
                for (QAbstractButton *b : buttons)
                    if (b->text().contains(QLatin1String(fallbackText)))
                        { b->click(); return; }
            });
        };

        clickButton(QMessageBox::Cancel, "Cancel");
        QVERIFY(!window->close());        // Cancel → close refused
        QVERIFY(window->isUntitled());

        clickButton(QMessageBox::Discard, "Discard");
        QVERIFY(window->close());         // Discard → closes

        delete window;
        delete workspace;
    }

    // 8b. File → New, then draw with the real map tools. The junctions and
    //     the conduit must reach PIXELS on the canvas, on both render paths
    //     (CPU SWMMLayerItem and the QSG overlay the Preferences default
    //     selects). Reported 2026-09-05: on a fresh Local-CRS project, added
    //     nodes and links "do not render". Baseline/after grabs are written
    //     next to the binary (CLAUDE.md §4.1) so a failure can be eyeballed.
    void blankProjectAddedObjectsRender_data()
    {
        QTest::addColumn<bool>("qsg");
        QTest::newRow("cpu-layeritem") << false;
        QTest::newRow("qsg-overlay")   << true;
    }
    void blankProjectAddedObjectsRender()
    {
        QFETCH(bool, qsg);
        const QString tag = QString::fromLatin1(QTest::currentDataTag());

        // Pixels that differ from the corner background inside the central
        // 80% of a grab — decorations (scale bar, coordinates) live at the
        // edges, so this counts drawn network geometry only.
        auto nonBackground = [](const QImage &img) {
            if (img.isNull()) return -1;
            const QRgb bg = img.pixel(5, 5);
            const int x0 = img.width()  / 10, x1 = img.width()  - x0;
            const int y0 = img.height() / 10, y1 = img.height() - y0;
            int n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    if (img.pixel(x, y) != bg) ++n;
            return n;
        };

        // Control for the GPU row: the offscreen QPA has no guarantee of a
        // working QQuickWidget scene graph. Render a LOADED model through the
        // same QSG overlay first; if that draws nothing while the CPU path
        // draws the network, the harness cannot see the overlay and the row is
        // skipped instead of reporting a bug it cannot observe.
        if (qsg) {
            auto *ws2 = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
            auto *w2  = new SWMMVisProjectWindow(ws2, fixturePath(), nullptr);
            QList<QString> lw, le;
            QVERIFY2(w2->loadModel(lw, le), qPrintable(le.join(QStringLiteral("; "))));
            w2->resize(900, 700);
            w2->show();
            QVERIFY(QTest::qWaitForWindowExposed(w2));
            QTest::qWait(300);
            MapCanvas *c2 = w2->canvas();
            c2->zoomToFullExtent();
            using K2 = SWMMModelLayer;
            w2->modelLayer()->setQsgRenderKinds(K2::QsgKinds(K2::QsgNone));
            QTest::qWait(150);
            const int cpuPx = nonBackground(c2->grab().toImage());
            w2->modelLayer()->setQsgRenderKinds(K2::QsgKinds(
                K2::QsgNodes | K2::QsgLinks | K2::QsgCatch | K2::QsgGages));
            QTest::qWait(150);
            const QImage qsgImg = c2->grab().toImage();
            qsgImg.save(QDir::current().filePath(
                QStringLiteral("newproject_render_control_loaded_qsg.png")));
            const int qsgPx = nonBackground(qsgImg);
            qInfo().noquote() << QStringLiteral(
                "[control] loaded fixture non-background pixels: cpu=%1 qsg=%2")
                .arg(cpuPx).arg(qsgPx);
            // Why the overlay is (in)visible here: MapCanvas parks its
            // QQuickWidget as an off-screen top-level, so it is reachable
            // through the top-level list. Its scene-graph state says whether
            // a grab can ever contain pixels in this process.
            for (QWidget *tl : QApplication::topLevelWidgets()) {
                auto *qw = qobject_cast<QQuickWidget *>(tl);
                if (!qw) continue;
                const QImage fb = qw->grabFramebuffer();
                qInfo().noquote() << QStringLiteral(
                    "[control] QQuickWidget status=%1 root=%2 sgInit=%3 size=%4x%5 "
                    "grab=%6x%7 grabNonBg=%8")
                    .arg(int(qw->status()))
                    .arg(qw->rootObject() ? "yes" : "no")
                    .arg(qw->quickWindow() && qw->quickWindow()->isSceneGraphInitialized()
                             ? "yes" : "no")
                    .arg(qw->width()).arg(qw->height())
                    .arg(fb.width()).arg(fb.height())
                    .arg(nonBackground(fb));
                qInfo().noquote() << QStringLiteral(
                    "[control] screens=%1 overlayScreen=%2 canvasScreen=%3 overlayDpr=%4 canvasDpr=%5")
                    .arg(QGuiApplication::screens().size())
                    .arg(qw->windowHandle() ? qw->windowHandle()->screen()->name() : QStringLiteral("?"))
                    .arg(c2->window()->windowHandle()
                             ? c2->window()->windowHandle()->screen()->name()
                             : QStringLiteral("?"))
                    .arg(qw->devicePixelRatioF()).arg(c2->devicePixelRatioF());
                if (QQuickItem *root = qw->rootObject()) {
                    auto *r = root->findChild<SWMMLayerQSGRenderer *>(
                        QStringLiteral("swmmRenderer"));
                    // Drive MapCanvas's OWN overlay widget exactly like the
                    // stand-alone probe below — same layer, same extent, same
                    // repaint+grab — bypassing MapCanvas::paintEvent.
                    if (r) {
                        r->setLayer(w2->modelLayer());
                        r->setMapExtent(c2->extent().scaled(1.05));
                        qw->repaint();
                        const QImage g = qw->grabFramebuffer();
                        g.save(QDir::current().filePath(
                            QStringLiteral("newproject_render_control_canvaswidget_direct.png")));
                        qInfo().noquote() << QStringLiteral(
                            "[control] MapCanvas-owned overlay driven directly: grab=%1x%2 nonBg=%3")
                            .arg(g.width()).arg(g.height()).arg(nonBackground(g));
                    }
                    qInfo().noquote() << QStringLiteral(
                        "[control] root %1x%2 visible=%3; swmmRenderer %4 %5x%6 "
                        "visible=%7 opacity=%8 rev=%9")
                        .arg(root->width()).arg(root->height())
                        .arg(root->isVisible() ? "yes" : "no")
                        .arg(r ? "found" : "MISSING")
                        .arg(r ? r->width() : -1.0).arg(r ? r->height() : -1.0)
                        .arg(r && r->isVisible() ? "yes" : "no")
                        .arg(r ? r->opacity() : -1.0)
                        .arg(r ? r->contentRevision() : 0ULL);
                }
            }
            // Can this process grab ANY off-screen QQuickWidget? A plain red
            // rectangle in the same WA_DontShowOnScreen top-level setup as
            // MapCanvas's overlay separates "the SWMM renderer drew nothing"
            // from "no scene-graph readback works here". Then the real overlay
            // QML + SWMMLayerQSGRenderer, driven directly with the loaded
            // layer, separates the renderer from MapCanvas's paint plumbing.
            auto makeProbe = [](QQuickWidget &probe, bool msaa) {
                probe.setAttribute(Qt::WA_DontShowOnScreen);
                probe.setAttribute(Qt::WA_QuitOnClose, false);
                probe.setClearColor(Qt::transparent);
                probe.setResizeMode(QQuickWidget::SizeRootObjectToView);
                if (msaa) {
                    QSurfaceFormat f = probe.format();
                    if (f.samples() < 4) f.setSamples(4);
                    probe.setFormat(f);
                }
            };
            for (int msaa = 0; msaa <= 1; ++msaa) {
                const QString qmlPath = QDir::current().filePath(
                    QStringLiteral("newproject_render_probe.qml"));
                QFile qf(qmlPath);
                QVERIFY(qf.open(QIODevice::WriteOnly | QIODevice::Text));
                qf.write("import QtQuick\nRectangle { color: \"red\" }\n");
                qf.close();
                QQuickWidget probe(nullptr);
                makeProbe(probe, msaa == 1);
                probe.setSource(QUrl::fromLocalFile(qmlPath));
                probe.resize(200, 150);
                probe.show();
                QTest::qWait(100);
                probe.repaint();
                const QImage g = probe.grabFramebuffer();
                int red = 0;
                for (int y = 0; y < g.height(); ++y)
                    for (int x = 0; x < g.width(); ++x)
                        if (qRed(g.pixel(x, y)) > 200 && qGreen(g.pixel(x, y)) < 50) ++red;
                qInfo().noquote() << QStringLiteral(
                    "[control] plain QQuickWidget probe msaa=%1: status=%2 grab=%3x%4 redPixels=%5")
                    .arg(msaa).arg(int(probe.status())).arg(g.width()).arg(g.height()).arg(red);
            }
            {
                QQuickWidget probe(nullptr);
                makeProbe(probe, true);
                probe.setSource(QUrl(QStringLiteral("qrc:/openswmm/qml/swmmlayer.qml")));
                probe.resize(900, 700);
                probe.show();
                QTest::qWait(100);
                auto *r = probe.rootObject()
                    ? probe.rootObject()->findChild<SWMMLayerQSGRenderer *>(
                          QStringLiteral("swmmRenderer"))
                    : nullptr;
                int px = -1;
                if (r) {
                    r->setLayer(w2->modelLayer());
                    r->setMapExtent(c2->extent());
                    QTest::qWait(100);
                    probe.repaint();
                    const QImage g = probe.grabFramebuffer();
                    g.save(QDir::current().filePath(
                        QStringLiteral("newproject_render_control_direct_qsg.png")));
                    px = nonBackground(g);
                }
                qInfo().noquote() << QStringLiteral(
                    "[control] direct swmmlayer.qml probe: status=%1 renderer=%2 nonBg=%3")
                    .arg(int(probe.status())).arg(r ? "found" : "MISSING").arg(px);
                // MapCanvas's exact regrab sequence: (re)size the overlay to a
                // NEW size, synchronous repaint(), immediate grabFramebuffer()
                // — no event-loop turn in between.
                if (r) {
                    r->setMapExtent(c2->extent().scaled(1.1));   // content dirty
                    probe.resize(901, 701);
                    probe.repaint();
                    const QImage g1 = probe.grabFramebuffer();
                    const int px1 = nonBackground(g1);
                    // Same again, but let one event-loop turn pass before grabbing.
                    r->setMapExtent(c2->extent().scaled(1.2));
                    probe.resize(902, 702);
                    probe.repaint();
                    QTest::qWait(50);
                    const QImage g2 = probe.grabFramebuffer();
                    const int px2 = nonBackground(g2);
                    // And: no resize at all, content dirty, immediate grab.
                    r->setMapExtent(c2->extent().scaled(1.3));
                    probe.repaint();
                    const QImage g3 = probe.grabFramebuffer();
                    const int px3 = nonBackground(g3);
                    qInfo().noquote() << QStringLiteral(
                        "[control] canvas-sequence probe: resize+repaint+grab=%1 "
                        "resize+repaint+wait+grab=%2 repaint+grab(no resize)=%3")
                        .arg(px1).arg(px2).arg(px3);
                    r->setLayer(nullptr);
                }
            }
            delete w2;
            delete ws2;
            QVERIFY2(cpuPx > 0, "control: CPU path drew nothing for a loaded model");
            if (qsgPx * 10 < cpuPx)
                QSKIP("QSG overlay renders nothing under this platform (offscreen "
                      "QQuickWidget) — GPU row cannot be observed here");
        }

        auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(workspace != nullptr);

        SWMMModelLayer::NewProjectSpec spec;
        spec.name          = QStringLiteral("Untitled");
        spec.forNewEngine  = true;
        spec.startDateTime = QDateTime(QDate(2026, 9, 5), QTime(0, 0));
        spec.endDateTime   = spec.startDateTime.addSecs(3600);
        spec.sim           = PreferencesManager::SimulationDefaults{};
        spec.sim.flowUnits = QStringLiteral("CFS");     // → Local (ft)
        spec.twoD          = PreferencesManager::TwoDDefaults{};

        auto *window = new SWMMVisProjectWindow(workspace, QString(), nullptr);
        window->markUntitled();
        QList<QString> warnings, errors;
        QVERIFY2(window->initializeBlankModel(spec, warnings, errors),
                 qPrintable(errors.join(QStringLiteral("; "))));

        window->resize(900, 700);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(150);   // finishModelLoad's canvas-sized retry + refresh timers

        MapCanvas       *canvas = window->canvas();
        SWMMModelLayer  *layer  = window->modelLayer();
        QVERIFY(canvas && layer);
        QVERIFY2(canvas->width() > 200 && canvas->height() > 200,
                 qPrintable(QStringLiteral("canvas %1x%2")
                                .arg(canvas->width()).arg(canvas->height())));
        QVERIFY(canvas->extent().isValid());
        QVERIFY(canvas->canvasSRS() != nullptr);
        QCOMPARE(canvas->canvasSRS()->description(), QStringLiteral("Local (ft)"));
        QVERIFY(layer->isVisible());

        using K = SWMMModelLayer;
        layer->setQsgRenderKinds(qsg ? K::QsgKinds(K::QsgNodes | K::QsgLinks
                                                   | K::QsgCatch | K::QsgGages)
                                     : K::QsgKinds(K::QsgNone));
        QTest::qWait(100);
        const QImage before = canvas->grab().toImage();
        before.save(QDir::current().filePath(
            QStringLiteral("newproject_render_%1_before.png").arg(tag)));

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
            QVERIFY2(qAbs(nx - mx) < 1e-6 * qMax(1.0, qAbs(mx))
                     && qAbs(ny - my) < 1e-6 * qMax(1.0, qAbs(my)),
                     qPrintable(QStringLiteral("node (%1,%2) vs click (%3,%4)")
                                    .arg(nx).arg(ny).arg(mx).arg(my)));
        }
        {
            OpenSWMMVisMapToolAddLink addC(canvas, 0, QStringLiteral("conduit"));
            canvas->setActiveTool(&addC);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, pA);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, pB);
            canvas->setActiveTool(nullptr);
        }
        QCOMPARE(layer->cachedLinkCount(), 1);

        QTest::qWait(250);   // Scene-channel debounce + refresh timer
        const QImage after = canvas->grab().toImage();
        after.save(QDir::current().filePath(
            QStringLiteral("newproject_render_%1_after.png").arg(tag)));
        QCOMPARE(after.size(), before.size());

        // Count pixels that changed inside a box around each click and at the
        // conduit's midpoint. Decorations (scale bar, north arrow) are in both
        // grabs, so only the added objects can differ.
        auto changedAround = [&](const QPoint &c, int half) {
            int n = 0;
            for (int y = c.y() - half; y <= c.y() + half; ++y)
                for (int x = c.x() - half; x <= c.x() + half; ++x) {
                    if (x < 0 || y < 0 || x >= after.width() || y >= after.height())
                        continue;
                    if (after.pixel(x, y) != before.pixel(x, y)) ++n;
                }
            return n;
        };
        const int dpr = qMax(1, int(after.devicePixelRatio()));
        const QPoint dA = pA * dpr, dB = pB * dpr, dM = (pA + pB) / 2 * dpr;
        const int nA = changedAround(dA, 12 * dpr);
        const int nB = changedAround(dB, 12 * dpr);
        const int nM = changedAround(dM, 6 * dpr);
        const QString why = QStringLiteral(
            "[%1] changed pixels: nodeA=%2 nodeB=%3 conduitMid=%4 "
            "(canvas %5x%6, extent %7)")
            .arg(tag).arg(nA).arg(nB).arg(nM)
            .arg(canvas->width()).arg(canvas->height())
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
        const QString firstJunction =
            layer.objectNameAt(SWMMModelLayer::CatJunctions, 0);
        QVERIFY(!firstJunction.isEmpty());
        selMgr.select(SWMMObjectRef{SWMMObjectRef::Node, firstJunction},
                      SelectionManager::Replace);
        QCOMPARE(selMgr.selection().size(), 1);

        // Focus the Junctions category — bus selection must survive.
        panel.selectCategory(SWMMModelLayer::CatJunctions);
        QCOMPARE(selMgr.selection().size(), 1);
        QVERIFY(selMgr.contains(
            SWMMObjectRef{SWMMObjectRef::Node, firstJunction}));

        auto *view = panel.findChild<QTreeView *>();
        QVERIFY(view);
        QVERIFY(view->currentIndex().isValid());
        QVERIFY(view->currentIndex().data(Qt::DisplayRole).toString()
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
