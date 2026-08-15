/*!
 * \file   test_object_creation_defaults.cpp
 * \brief  Creation-path tests for object defaults: a drawn object must land
 *         in the engine with the configured ObjectDefaults for the active
 *         unit system, geometry-derived overrides must win, and undo must
 *         roll the whole object back.
 *         Plan: workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md
 *
 *  Uses the Add*Commands directly (canvas = nullptr) — the same choke point
 *  the draw tools and the GIS importer push through. Fixtures:
 *  data/object_defaults_cfs.inp (US) and data/object_defaults_cms.inp (SI).
 */

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <memory>

#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"
#include "map/mapundostack.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

std::unique_ptr<SWMMModelLayer> openLayer(const QString &fixture)
{
    auto layer = std::make_unique<SWMMModelLayer>(
        QDir(dataDir()).filePath(fixture), nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors)) return nullptr;
    return layer;
}

//! Binds the UnitSystem facade to the layer's engine for the test scope and
//! unbinds on destruction so cases stay independent.
struct ScopedUnits
{
    UnitSystem units;
    explicit ScopedUnits(SWMMModelLayer *layer)
    {
        units.syncFromEngine(layer->engine());
        UnitSystem::setActiveProject(&units);
    }
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

private:
    void clearStoredDefaults();
};

void TestObjectCreationDefaults::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("objectcreation-test"));
    clearStoredDefaults();
}

void TestObjectCreationDefaults::init()
{
    clearStoredDefaults();
}

void TestObjectCreationDefaults::clearStoredDefaults()
{
    QSettings settings;
    settings.remove(QStringLiteral("SWMMVis/Preferences/ObjectDefaults"));
    settings.sync();
}

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
                              /*SWMM_LINK_CONDUIT*/ 0,
                              QStringLiteral("J1"), QStringLiteral("J2"),
                              {}, nullptr);
    addConduit.redo();
    const int li = swmm_link_index(eng, "C_NEW");
    QVERIFY(li >= 0);
    int shape = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    QCOMPARE(swmm_link_get_xsect(eng, li, &shape, &g1, &g2, &g3, &g4), 0);
    QCOMPARE(shape, 0);          // CIRCULAR
    QCOMPARE(g1, 1.0);
    double n = 0;
    QCOMPARE(swmm_link_get_roughness(eng, li, &n), 0);
    QCOMPARE(n, 0.013);
    double len = 0;
    QCOMPARE(swmm_link_get_length(eng, li, &len), 0);
    QCOMPARE(len, 400.0);

    // Subcatchment → width/slope/Horton family (fixture INFILTRATION HORTON).
    AddSubcatchmentCommand addSub(layer.get(), QStringLiteral("S_NEW"),
                                  {QPointF(0, 0), QPointF(100, 0),
                                   QPointF(50, 100)},
                                  nullptr);
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
    QCOMPARE(model, 0);          // HORTON
    double f0 = 0, fmin = 0, decay = 0, dry = 0;
    QCOMPARE(swmm_subcatch_get_infil_horton(eng, si, &f0, &fmin, &decay, &dry), 0);
    QCOMPARE(f0, 3.0);
    QCOMPARE(fmin, 0.5);
    // Zero-depression-storage impervious fraction ([SUBAREAS] PctZero).
    double pctZero = -1.0;
    QCOMPARE(swmm_subcatch_get_zero_imperv_pct(eng, si, &pctZero), 0);
    QCOMPARE(pctZero, 25.0);

    // Rain gage → 15-min intensity, catch factor 1.
    AddGageCommand addGage(layer.get(), QStringLiteral("RG_NEW"),
                           200.0, 200.0, nullptr);
    addGage.redo();
    const int gi = swmm_gage_index(eng, "RG_NEW");
    QVERIFY(gi >= 0);
    double interval = 0;
    QCOMPARE(swmm_gage_get_rain_interval(eng, gi, &interval), 0);
    QCOMPARE(interval, 900.0);   // 15 min in seconds
    double scf = 0;
    QCOMPARE(swmm_gage_get_snow_factor(eng, gi, &scf), 0);
    QCOMPARE(scf, 1.0);
}

void TestObjectCreationDefaults::siDefaults_selectedBySiProject()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cms.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    QVERIFY(scope.units.isSI());
    SWMM_Engine eng = layer->engine();

    AddLinkCommand addConduit(layer.get(), QStringLiteral("C_NEW"),
                              0, QStringLiteral("J1"), QStringLiteral("J2"),
                              {}, nullptr);
    addConduit.redo();
    const int li = swmm_link_index(eng, "C_NEW");
    QVERIFY(li >= 0);
    int shape = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    QCOMPARE(swmm_link_get_xsect(eng, li, &shape, &g1, &g2, &g3, &g4), 0);
    QCOMPARE(g1, 0.3);           // SI diameter
    double len = 0;
    QCOMPARE(swmm_link_get_length(eng, li, &len), 0);
    QCOMPARE(len, 120.0);        // SI length

    AddNodeCommand addStorage(layer.get(), QStringLiteral("ST1"),
                              2, 100.0, 100.0, nullptr, 0.0);
    addStorage.redo();
    const int st = swmm_node_index(eng, "ST1");
    QVERIFY(st >= 0);
    double depth = -1.0;
    QCOMPARE(swmm_node_get_max_depth(eng, st, &depth), 0);
    QCOMPARE(depth, 4.5);
}

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
                                  {QPointF(0, 0), QPointF(100, 0),
                                   QPointF(50, 100)},
                                  nullptr);
    addSub.redo();
    const int si = swmm_subcatch_index(eng, "S_CN");
    QVERIFY(si >= 0);

    int model = -1;
    QCOMPARE(swmm_subcatch_get_infil_model(eng, si, &model), 0);
    QCOMPARE(model, 4);          // CURVE_NUMBER
    double cn = 0, cnDry = 0;
    QCOMPARE(swmm_subcatch_get_infil_curve_number(eng, si, &cn, &cnDry), 0);
    QCOMPARE(cn, 80.0);
    QCOMPARE(cnDry, 7.0);

    // Non-infiltration defaults still land.
    double w = 0;
    QCOMPARE(swmm_subcatch_get_width(eng, si, &w), 0);
    QCOMPARE(w, 500.0);
}

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

void TestObjectCreationDefaults::editedPreference_flowsThroughToCreation()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cfs.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    SWMM_Engine eng = layer->engine();

    auto *p = PreferencesManager::instance();
    auto d = PreferencesManager::ObjectDefaults::usSeed();
    d.conduitRoughness = 0.017;   // user picks corrugated metal
    p->setObjectDefaults(d, /*si=*/false);

    AddLinkCommand addConduit(layer.get(), QStringLiteral("C_EDIT"),
                              0, QStringLiteral("J1"), QStringLiteral("J2"),
                              {}, nullptr);
    addConduit.redo();
    const int li = swmm_link_index(eng, "C_EDIT");
    QVERIFY(li >= 0);
    double n = 0;
    QCOMPARE(swmm_link_get_roughness(eng, li, &n), 0);
    QCOMPARE(n, 0.017);
}

void TestObjectCreationDefaults::undo_rollsTheWholeObjectBack()
{
    auto layer = openLayer(QStringLiteral("object_defaults_cfs.inp"));
    QVERIFY(layer);
    ScopedUnits scope(layer.get());
    SWMM_Engine eng = layer->engine();

    AddLinkCommand addConduit(layer.get(), QStringLiteral("C_UNDO"),
                              0, QStringLiteral("J1"), QStringLiteral("J2"),
                              {}, nullptr);
    addConduit.redo();
    QVERIFY(swmm_link_index(eng, "C_UNDO") >= 0);
    addConduit.undo();
    QVERIFY(swmm_link_index(eng, "C_UNDO") < 0);
}

QTEST_MAIN(TestObjectCreationDefaults)
#include "test_object_creation_defaults.moc"
