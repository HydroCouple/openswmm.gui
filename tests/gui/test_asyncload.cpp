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
#include "map/spatialreferencesystem.h"
#include "selection/selectionmanager.h"
#include "ui/panels/objectbrowserpanel.h"

#include <QTreeView>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QObject>
#include <QSignalSpy>
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
