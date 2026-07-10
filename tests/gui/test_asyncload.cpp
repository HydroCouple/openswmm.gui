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
#include <QObject>
#include <QSignalSpy>
#include <QTest>

namespace {

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

    // 3. openEngineForPath() + adoptOpenEngine() on a fresh layer must
    //    yield the same node/link/subcatchment/gage counts as a plain
    //    loadModel() on another layer for the same fixture.
    void openAndAdoptEqualsSyncLoad()
    {
        QString detail;
        qint64 openMs = -1;
        SWMM_Engine eng = SWMMModelLayer::openEngineForPath(fixturePath(), &detail, &openMs);
        QVERIFY(eng != nullptr);
        QVERIFY(openMs >= 0);

        SWMMModelLayer asyncLayer(fixturePath(), nullptr);
        QList<QString> warningsA, errorsA;
        QVERIFY(asyncLayer.adoptOpenEngine(eng, warningsA, errorsA, openMs));

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
    //    reports the load-timing line in `warnings`.
    void loadModelRegression()
    {
        SWMMModelLayer layer(fixturePath(), nullptr);
        QList<QString> warnings, errors;
        QVERIFY(layer.loadModel(warnings, errors));
        QVERIFY(errors.isEmpty());

        bool foundTiming = false;
        for (const QString &w : warnings)
            if (w.contains(QStringLiteral("load timing (ms)")))
                foundTiming = true;
        QVERIFY2(foundTiming, "expected a '... load timing (ms): ...' line in warnings");
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
};

QTEST_MAIN(TestAsyncLoad)
#include "test_asyncload.moc"
