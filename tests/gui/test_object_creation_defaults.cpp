/**
 * @file      test_object_creation_defaults.cpp
 * @brief     Creation-path tests for object defaults: a drawn object must land
 *         in the engine with the configured ObjectDefaults for the active
 *         unit system, geometry-derived overrides must win, and undo must
 *         roll the whole object back.
 *         Plan: workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md
 *
 *  Uses the Add*Commands directly (canvas = nullptr) — the same choke point
 *  the draw tools and the GIS importer push through. Fixtures:
 *  data/object_defaults_cfs.inp (US) and data/object_defaults_cms.inp (SI).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <memory>
#include <cmath>

#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"
#include "simulation/simulationrunner.h"
#include <QFile>
#include <QSignalSpy>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>

namespace
{

/** @brief Resolve creation-default fixtures.
 * @return SWMMVIS_GUI_TEST_DATA or "." when unset. */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/** @brief Load an independent owned model fixture.
 * @param[in] fixture Path appended to the configured data directory.
 * @return Unique loaded layer or nullptr on failure; local diagnostics are discarded and failed allocations are released by unique ownership. */
std::unique_ptr<SWMMModelLayer> openLayer(const QString &fixture)
{
    auto layer = std::make_unique<SWMMModelLayer>(QDir(dataDir()).filePath(fixture), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

//! Binds the UnitSystem facade to the layer's engine for the test scope and
//! unbinds on destruction so cases stay independent.
struct ScopedUnits {
    UnitSystem units;
    /** @brief Bind a scope-owned UnitSystem as the process-wide active project facade.
     * @param[in] layer Required loaded layer whose engine supplies the units.
     * The prior active facade is not saved; tests must serialize scopes and keep this object alive while its facade is active. */
    explicit ScopedUnits(SWMMModelLayer *layer)
    {
        units.syncFromEngine(layer->engine());
        UnitSystem::setActiveProject(&units);
    }
    /** @brief Clear the process-wide active UnitSystem before this scope-owned facade is destroyed. */
    ~ScopedUnits() { UnitSystem::setActiveProject(nullptr); }
};

} // anonymous

class TestObjectCreationDefaults : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void usDefaults_appliedToDrawnObjects();
    void siDefaults_selectedBySiProject();
    void curveNumberProject_getsCurveNumberFamily();
    void terrainInvert_winsOverDefaults();
    void editedPreference_flowsThroughToCreation();
    void undo_rollsTheWholeObjectBack();
    void junctionStorageDefaultsAndDeleteUndo();
    void junctionStorageRunsThroughTheGuiRunner();

private:
    void clearStoredDefaults();
};

/** @brief Select an isolated settings namespace and clear stored object-default overrides. */
void TestObjectCreationDefaults::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("objectcreation-test"));
    clearStoredDefaults();
}

/** @brief Clear stored defaults before each test case. */
void TestObjectCreationDefaults::init()
{
    clearStoredDefaults();
}

/** @brief Remove and synchronize the ObjectDefaults preference subtree in the current test settings namespace. */
void TestObjectCreationDefaults::clearStoredDefaults()
{
    QSettings settings;
    settings.remove(QStringLiteral("SWMMVis/Preferences/ObjectDefaults"));
    settings.sync();
}

/** @brief Verify direct creation commands apply US storage, conduit, subcatchment and gage defaults.
 * Checks feet-based dimensions, Horton family/rates, percentages and a 900-second gage interval; no map-tool interaction occurs and a null canvas disables automatic conduit length. */
void TestObjectCreationDefaults::usDefaults_appliedToDrawnObjects()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cfs.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    QVERIFY(!scope.units.isSI());
    SWMM_Engine eng = layer->engine();

    // Storage node → nonzero depth + functional constant area.
    AddNodeCommand addStorage(layer.get(), QStringLiteral("ST1"),
                              /*SWMM_NODE_STORAGE*/ 2, 100.0, 100.0, nullptr,
                              /*invertElev*/ 0.0);
    addStorage.redo();
    const int st = swmm_node_index(eng, "ST1");
    QVERIFY(st >= 0);
    double depth = -1.0;
    QCOMPARE(swmm_node_get_max_depth(eng, st, &depth), 0);
    QCOMPARE(depth, 15.0);
    double a = -1, b = -1, c = -1;
    QCOMPARE(swmm_node_get_storage_functional(eng, st, &a, &b, &c), 0);
    QCOMPARE(c, 1000.0);

    // Conduit → circular 1.0 ft, n = 0.013, length 400 (no auto-length:
    // canvas is null, so the flag reads false).
    AddLinkCommand addConduit(layer.get(), QStringLiteral("C_NEW"),
                              /*SWMM_LINK_CONDUIT*/ 0, QStringLiteral("J1"), QStringLiteral("J2"),
                              {}, nullptr);
    addConduit.redo();
    const int li = swmm_link_index(eng, "C_NEW");
    QVERIFY(li >= 0);
    int shape = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    QCOMPARE(swmm_link_get_xsect(eng, li, &shape, &g1, &g2, &g3, &g4), 0);
    QCOMPARE(shape, 0); // CIRCULAR
    QCOMPARE(g1, 1.0);
    double n = 0;
    QCOMPARE(swmm_link_get_roughness(eng, li, &n), 0);
    QCOMPARE(n, 0.013);
    double len = 0;
    QCOMPARE(swmm_link_get_length(eng, li, &len), 0);
    QCOMPARE(len, 400.0);

    // Subcatchment → width/slope/Horton family (fixture INFILTRATION HORTON).
    AddSubcatchmentCommand addSub(layer.get(), QStringLiteral("S_NEW"),
                                  {QPointF(0, 0), QPointF(100, 0), QPointF(50, 100)}, nullptr);
    addSub.redo();
    const int si = swmm_subcatch_index(eng, "S_NEW");
    QVERIFY(si >= 0);
    double w = 0, s = 0;
    QCOMPARE(swmm_subcatch_get_width(eng, si, &w), 0);
    QCOMPARE(w, 500.0);
    QCOMPARE(swmm_subcatch_get_slope(eng, si, &s), 0);
    QCOMPARE(s, 0.5);
    // Infiltration family follows the project option (fixture = HORTON).
    int model = -1;
    QCOMPARE(swmm_subcatch_get_infil_model(eng, si, &model), 0);
    QCOMPARE(model, 0); // HORTON
    double f0 = 0, fmin = 0, decay = 0, dry = 0;
    QCOMPARE(swmm_subcatch_get_infil_horton(eng, si, &f0, &fmin, &decay, &dry), 0);
    QCOMPARE(f0, 3.0);
    QCOMPARE(fmin, 0.5);
    // Zero-depression-storage impervious fraction ([SUBAREAS] PctZero).
    double pctZero = -1.0;
    QCOMPARE(swmm_subcatch_get_zero_imperv_pct(eng, si, &pctZero), 0);
    QCOMPARE(pctZero, 25.0);

    // Rain gage → 15-min intensity, catch factor 1.
    AddGageCommand addGage(layer.get(), QStringLiteral("RG_NEW"), 200.0, 200.0, nullptr);
    addGage.redo();
    const int gi = swmm_gage_index(eng, "RG_NEW");
    QVERIFY(gi >= 0);
    double interval = 0;
    QCOMPARE(swmm_gage_get_rain_interval(eng, gi, &interval), 0);
    QCOMPARE(interval, 900.0); // 15 min in seconds
    double scf = 0;
    QCOMPARE(swmm_gage_get_snow_factor(eng, gi, &scf), 0);
    QCOMPARE(scf, 1.0);
}

/** @brief Verify an SI project receives metre-based conduit diameter/length and storage depth defaults. */
void TestObjectCreationDefaults::siDefaults_selectedBySiProject()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cms.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    QVERIFY(scope.units.isSI());
    SWMM_Engine eng = layer->engine();

    AddLinkCommand addConduit(layer.get(), QStringLiteral("C_NEW"), 0, QStringLiteral("J1"),
                              QStringLiteral("J2"), {}, nullptr);
    addConduit.redo();
    const int li = swmm_link_index(eng, "C_NEW");
    QVERIFY(li >= 0);
    int shape = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    QCOMPARE(swmm_link_get_xsect(eng, li, &shape, &g1, &g2, &g3, &g4), 0);
    QCOMPARE(g1, 0.3); // SI diameter
    double len = 0;
    QCOMPARE(swmm_link_get_length(eng, li, &len), 0);
    QCOMPARE(len, 120.0); // SI length

    AddNodeCommand addStorage(layer.get(), QStringLiteral("ST1"), 2, 100.0, 100.0, nullptr, 0.0);
    addStorage.redo();
    const int st = swmm_node_index(eng, "ST1");
    QVERIFY(st >= 0);
    double depth = -1.0;
    QCOMPARE(swmm_node_get_max_depth(eng, st, &depth), 0);
    QCOMPARE(depth, 4.5);
}

/** @brief Verify new subcatchments use the project Curve Number infiltration family and its defaults rather than zero-initialized Horton metadata. */
void TestObjectCreationDefaults::curveNumberProject_getsCurveNumberFamily()
{
    // swmm_subcatch_add zero-initialises infil_model, so a fresh subcatchment
    // always reads HORTON. The applier must key off the project's
    // INFILTRATION option instead, or the CN/Green-Ampt defaults are dead.
    auto layer = openLayer(QStringLiteral("object_defaults_cn.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    SWMM_Engine eng = layer->engine();

    AddSubcatchmentCommand addSub(layer.get(), QStringLiteral("S_CN"),
                                  {QPointF(0, 0), QPointF(100, 0), QPointF(50, 100)}, nullptr);
    addSub.redo();
    const int si = swmm_subcatch_index(eng, "S_CN");
    QVERIFY(si >= 0);

    int model = -1;
    QCOMPARE(swmm_subcatch_get_infil_model(eng, si, &model), 0);
    QCOMPARE(model, 4); // CURVE_NUMBER
    double cn = 0, cnDry = 0;
    QCOMPARE(swmm_subcatch_get_infil_curve_number(eng, si, &cn, &cnDry), 0);
    QCOMPARE(cn, 80.0);
    QCOMPARE(cnDry, 7.0);

    // Non-infiltration defaults still land.
    double w = 0;
    QCOMPARE(swmm_subcatch_get_width(eng, si, &w), 0);
    QCOMPARE(w, 500.0);
}

/** @brief Verify the explicit terrain-derived 42.25-foot invert survives application of creation defaults. */
void TestObjectCreationDefaults::terrainInvert_winsOverDefaults()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cfs.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    SWMM_Engine eng = layer->engine();

    // The terrain-derived invert passed to the command must survive the
    // defaults application (defaults first, specific value wins).
    AddNodeCommand add(layer.get(), QStringLiteral("JT1"),
                       /*junction*/ 0, 10.0, 10.0, nullptr,
                       /*invertElev*/ 42.25);
    add.redo();
    const int idx = swmm_node_index(eng, "JT1");
    QVERIFY(idx >= 0);
    double invert = 0;
    QCOMPARE(swmm_node_get_invert_elev(eng, idx, &invert), 0);
    QCOMPARE(invert, 42.25);
}

/** @brief Verify a changed US roughness preference reaches a newly created conduit. */
void TestObjectCreationDefaults::editedPreference_flowsThroughToCreation()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cfs.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    SWMM_Engine eng = layer->engine();

    auto *p = PreferencesManager::instance();
    auto d = PreferencesManager::ObjectDefaults::usSeed();
    d.conduitRoughness = 0.017; // user picks corrugated metal
    p->setObjectDefaults(d, /*si=*/false);

    AddLinkCommand addConduit(layer.get(), QStringLiteral("C_EDIT"), 0, QStringLiteral("J1"),
                              QStringLiteral("J2"), {}, nullptr);
    addConduit.redo();
    const int li = swmm_link_index(eng, "C_EDIT");
    QVERIFY(li >= 0);
    double n = 0;
    QCOMPARE(swmm_link_get_roughness(eng, li, &n), 0);
    QCOMPARE(n, 0.017);
}

/** @brief Verify undo of a completed conduit creation removes the engine object. */
void TestObjectCreationDefaults::undo_rollsTheWholeObjectBack()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cfs.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    SWMM_Engine eng = layer->engine();

    AddLinkCommand addConduit(layer.get(), QStringLiteral("C_UNDO"), 0, QStringLiteral("J1"),
                              QStringLiteral("J2"), {}, nullptr);
    addConduit.redo();
    QVERIFY(swmm_link_index(eng, "C_UNDO") >= 0);
    addConduit.undo();
    QVERIFY(swmm_link_index(eng, "C_UNDO") < 0);
}

void TestObjectCreationDefaults::junctionStorageDefaultsAndDeleteUndo()
{
    for (bool si : {false, true}) {
        auto layer = openLayer(si ? QStringLiteral("object_defaults_cms.inp")
                                  : QStringLiteral("object_defaults_cfs.inp"));
        QVERIFY(layer);
        ScopedUnits scope(layer.get());
        auto *prefs = PreferencesManager::instance();
        auto defaults = prefs->objectDefaults(si);
        QCOMPARE(defaults.junctionStorageArea, 0.0);
        defaults.junctionStorageArea = 0.125;
        prefs->setObjectDefaults(defaults, si);
        QCOMPARE(prefs->objectDefaults(si).junctionStorageArea, 0.125);
        AddNodeCommand add(layer.get(), QStringLiteral("CHAMBER"), 0, 10, 10, nullptr, 0);
        add.redo();
        const auto eng = layer->engine();
        int idx = swmm_node_index(eng, "CHAMBER");
        QVERIFY(idx >= 0);
        double area = 0;
        QCOMPARE(swmm_node_get_storage_area(eng, idx, &area), 0);
        QCOMPARE(area, 0.125);
        DeleteObjectCommand remove(layer.get(), QStringLiteral("CHAMBER"), DeleteObjectCommand::DeleteNode, nullptr);
        remove.redo();
        QVERIFY(swmm_node_index(eng, "CHAMBER") < 0);
        remove.undo();
        idx = swmm_node_index(eng, "CHAMBER");
        QVERIFY(idx >= 0);
        QCOMPARE(swmm_node_get_storage_area(eng, idx, &area), 0);
        QCOMPARE(area, 0.125);
        int type = -1;
        QCOMPARE(swmm_node_get_type(eng, idx, &type), 0);
        QCOMPARE(type, 0);
        ConvertNodeTypeCommand convert(layer.get(), QStringLiteral("CHAMBER"), 2, {}, nullptr);
        convert.redo();
        QVERIFY(convert.converted());
        QCOMPARE(swmm_node_get_storage_area(eng, idx, &area), 0);
        QCOMPARE(area, 0.0);
        convert.undo();
        QCOMPARE(swmm_node_get_storage_area(eng, idx, &area), 0);
        QCOMPARE(area, 0.125);
    }
}

void TestObjectCreationDefaults::junctionStorageRunsThroughTheGuiRunner()
{
    const QString folder = QDir(dataDir()).absoluteFilePath(QStringLiteral("junction_storage_issue10"));
    QVERIFY(QDir().mkpath(folder));
    const QString input = QDir(folder).filePath(QStringLiteral("gui-chamber.inp"));
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[OPTIONS]\nFLOW_UNITS CFS\nFLOW_ROUTING DYNWAVE\nSTART_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 00:10:00\nROUTING_STEP 1\nVARIABLE_STEP 0\nREPORT_STEP 00:00:10\n[JUNCTIONS]\nJ1 0 10 0 0 0 100\n[OUTFALLS]\nO1 0 FREE NO\n[INFLOWS]\nJ1 FLOW \"\" FLOW 1 1 1\n");
    file.close();
    QStringList versions{QStringLiteral("6.0.0")};
    // Installed test workers exercise the same capability probe as real runs.
    const QString workers = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../bin"));
    if (QFile::exists(QDir(workers).filePath(QStringLiteral("openswmm-legacy-worker"))))
        versions << QStringLiteral("5.3.0");
    if (QFile::exists(QDir(workers).filePath(QStringLiteral("openswmm-legacy-worker-5.2.4"))))
        versions << QStringLiteral("5.2.4");
    for (const auto &version : versions) {
        const QString stem = QDir(folder).filePath(QStringLiteral("runner-") + version);
        SimulationRunner runner(10, QStringLiteral("physical chamber"), input, stem + ".rpt", stem + ".out", version);
        QSignalSpy finished(&runner, &SimulationRunner::finished);
        runner.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        const auto result = finished.takeFirst();
        if (version == QStringLiteral("5.2.4")) {
            QVERIFY(!result[1].toBool());
            QVERIFY(result[3].toString().contains(QStringLiteral("does not support junction")));
        } else {
            QVERIFY2(result[1].toBool(), qPrintable(result[3].toString()));
            QVERIFY(std::abs(result[5].toDouble()) < .001);
        }
    }
}

QTEST_MAIN(TestObjectCreationDefaults)
#include "test_object_creation_defaults.moc"
