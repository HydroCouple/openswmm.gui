/*!
 * \file   test_dirtytracking_run.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The pre-run auto-save gate: engine mutations must mark the project
 *         dirty, and view-only actions must not.
 *
 *         SWMMVis::onRunSimulation only saves the project when
 *         SWMMVisProjectWindow::hasChanges() is true, and SimulationRunner
 *         then re-opens the .inp from disk in a fresh engine. So any edit that
 *         mutates the in-memory engine without setting that flag is silently
 *         dropped from the run — the user sees results for the last saved
 *         state. Conversely a false positive costs a full .inp rewrite before
 *         every run, which on a large model is minutes.
 *
 *         SWMMModelLayer::modelEdited() is the dirty channel. These slots pin
 *         that it fires for one representative edit from each family that
 *         previously had no path to the flag, and that it stays silent for
 *         selection changes.
 *
 *         Uses QTEST_MAIN because the last slot constructs a real
 *         SWMMVisProjectWindow (a QWidget).
 */
#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"

#include <QDir>
#include <QObject>
#include <QPointF>
#include <QSignalSpy>
#include <QTest>
#include <QVector>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString fixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

} // namespace

class TestDirtyTrackingRun : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void nodeMoveMarksEdited();
    void linkInteriorVerticesMarkEdited();
    void subcatchVerticesMarkEdited();
    void curvePointEditMarksEdited();
    void patternFactorEditMarksEdited();
    void aquiferParamEditMarksEdited();
    void selectionDoesNotMarkEdited();
    void registryFlushWithoutEditDoesNotMarkEdited();
    void projectIsCleanAfterLoad();

private:
    /*! Loads the fixture into a standalone layer. Returns nullptr on failure. */
    SWMMModelLayer *loadLayer();

    SWMMModelLayer *mLayer = nullptr;
};

void TestDirtyTrackingRun::init()
{
    mLayer = nullptr;
}

void TestDirtyTrackingRun::cleanup()
{
    delete mLayer;
    mLayer = nullptr;
}

SWMMModelLayer *TestDirtyTrackingRun::loadLayer()
{
    auto *layer = new SWMMModelLayer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) {
        delete layer;
        return nullptr;
    }
    return layer;
}

// ---------------------------------------------------------------------------
// Geometry edits — these emit only repaintRequested(), so before the
// modelEdited() channel they never reached the dirty flag.
// ---------------------------------------------------------------------------

void TestDirtyTrackingRun::nodeMoveMarksEdited()
{
    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    QVERIFY(mLayer->applyNodeMove(0, 123.0, 456.0));
    QVERIFY2(spy.count() >= 1,
             "moving a node writes [COORDINATES]; the project must be dirty");
}

void TestDirtyTrackingRun::linkInteriorVerticesMarkEdited()
{
    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    const QVector<QPointF> interior{ QPointF(500.0, 75.0) };
    QVERIFY(mLayer->applyLinkInteriorVertices(0, interior));
    QVERIFY2(spy.count() >= 1,
             "link interior vertices are [VERTICES] data; the project must be dirty");
}

void TestDirtyTrackingRun::subcatchVerticesMarkEdited()
{
    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    const QVector<QPointF> ring{ QPointF(-2100.0, 1900.0), QPointF(-1800.0, 1900.0),
                                 QPointF(-1800.0, 2200.0), QPointF(-2100.0, 2200.0) };
    QVERIFY(mLayer->applySubcatchVertices(0, ring));
    QVERIFY2(spy.count() >= 1,
             "subcatchment polygons are [Polygons] data; the project must be dirty");
}

// ---------------------------------------------------------------------------
// Data-object content edits — these only reach the provider, never the
// registry, so the registry's add/remove/rename signals do not cover them.
// ---------------------------------------------------------------------------

void TestDirtyTrackingRun::curvePointEditMarksEdited()
{
    using openswmmvis::curve::CurveProvider;
    using openswmmvis::curve::CurveRegistry;

    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    auto *reg = qobject_cast<CurveRegistry *>(mLayer->ensureCurveRegistry());
    QVERIFY2(reg, "curve registry unavailable");

    // Creating the provider is itself a mutation (providerAdded); take the spy
    // afterwards so the assertion is about the POINT edit alone.
    CurveProvider *p = reg->create(QStringLiteral("dirty_probe_curve"),
                                   openswmmvis::curve::CurveType::Storage);
    QVERIFY2(p, "could not create probe curve");
    QVERIFY(p->insertPoint(0.0, 1.0) >= 0);

    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    QVERIFY(p->setPointAt(0, 0.0, 2.0));
    QVERIFY2(spy.count() >= 1,
             "editing a curve point changes [CURVES]; the project must be dirty");
}

void TestDirtyTrackingRun::patternFactorEditMarksEdited()
{
    using openswmmvis::pattern::PatternProvider;
    using openswmmvis::pattern::PatternRegistry;

    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    auto *reg = qobject_cast<PatternRegistry *>(mLayer->ensurePatternRegistry());
    QVERIFY2(reg, "pattern registry unavailable");

    PatternProvider *p = reg->create(QStringLiteral("dirty_probe_pattern"),
                                     openswmmvis::pattern::PatternType::Hourly);
    QVERIFY2(p, "could not create probe pattern");

    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    p->setFactor(0, 1.25);
    QVERIFY2(spy.count() >= 1,
             "editing a pattern factor changes [PATTERNS]; the project must be dirty");
}

// ---------------------------------------------------------------------------
// Comprehensive-editor parameter edits — these live on the provider and reach
// neither the registry's add/remove/rename signals nor any layer signal.
// ---------------------------------------------------------------------------

void TestDirtyTrackingRun::aquiferParamEditMarksEdited()
{
    using openswmmvis::aquifer::AquiferProvider;
    using openswmmvis::aquifer::AquiferRegistry;

    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    auto *reg = qobject_cast<AquiferRegistry *>(mLayer->ensureAquiferRegistry());
    QVERIFY2(reg, "aquifer registry unavailable");

    AquiferProvider *p = reg->create(QStringLiteral("dirty_probe_aquifer"));
    QVERIFY2(p, "could not create probe aquifer");

    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    p->setParam(AquiferProvider::Porosity, 0.42);
    QVERIFY2(spy.count() >= 1,
             "editing an aquifer parameter changes [AQUIFERS]; the project must be dirty");
}

// ---------------------------------------------------------------------------
// The other half of the contract: view-only actions must stay clean, or a
// large model pays a needless .inp rewrite before every run.
// ---------------------------------------------------------------------------

void TestDirtyTrackingRun::selectionDoesNotMarkEdited()
{
    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    mLayer->setSelectedElements({ { QStringLiteral("J1"), SWMMModelLayer::kKindNode } });
    mLayer->setSelectedElements({});
    QCOMPARE(spy.count(), 0);
}

void TestDirtyTrackingRun::registryFlushWithoutEditDoesNotMarkEdited()
{
    using openswmmvis::aquifer::AquiferRegistry;

    mLayer = loadLayer();
    QVERIFY2(mLayer, "fixture failed to load");

    auto *reg = qobject_cast<AquiferRegistry *>(mLayer->ensureAquiferRegistry());
    QVERIFY2(reg, "aquifer registry unavailable");
    QVERIFY2(reg->create(QStringLiteral("flush_probe_aquifer")) != nullptr,
             "could not create probe aquifer");

    // AquiferEditorDialog::pickAquifer runs `dlg.exec(); registry->saveToEngine();`
    // — the flush happens whether the user accepted or cancelled, and it writes
    // every provider unconditionally. Dirty tracking must therefore key off the
    // provider edit, not off the flush, or opening a data editor and pressing
    // Cancel would force a full .inp rewrite before the next run.
    QSignalSpy spy(mLayer, &SWMMModelLayer::modelEdited);
    reg->saveToEngine();
    QCOMPARE(spy.count(), 0);
}

void TestDirtyTrackingRun::projectIsCleanAfterLoad()
{
    auto *workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    QVERIFY(workspace != nullptr);
    auto *window = new SWMMVisProjectWindow(workspace, fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY2(window->loadModel(warnings, errors),
             "fixture failed to load into the project window");

    // Opening a project must not arm the pre-run auto-save. This is the
    // regression that would make every run of a large model rewrite the .inp.
    QVERIFY2(!window->hasChanges(),
             "a freshly loaded project must be clean");

    QVERIFY2(window->modelLayer(), "project window has no model layer");
    window->modelLayer()->markEdited();
    QVERIFY2(window->hasChanges(),
             "modelEdited() must reach SWMMVisProjectWindow::setHasChanges");

    delete window;
}

QTEST_MAIN(TestDirtyTrackingRun)
#include "test_dirtytracking_run.moc"
