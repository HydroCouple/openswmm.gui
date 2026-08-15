/*!
 * \file   test_comparisonplot_system_attrs.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AT.2 — pins the AT.2 system-attribute additions:
 *   - 14 PlotAttribute::System* enumerators carry sensible labels + units.
 *   - ObjectRef::Kind::System is valid without a name; forSystem() factory.
 *   - SwmmOutRunLayer::variableCodeFor() maps each System* to the matching
 *     engine SWMM_OUT_SYS_* code 1:1.
 *
 * Self-contained: links plotattribute.cpp + the engine's output header only
 * (no .out fixture, no SWMMResultsLayer — we exercise the pure mapping).
 */
#include "plot/plotattribute.h"
#include "plot/irunlayer.h"
#include "plot/swmmoutrunlayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <QObject>
#include <QTest>

using namespace openswmmvis::plot;

class TestComparisonPlotSystemAttrs : public QObject
{
    Q_OBJECT
private slots:
    void objectRef_forSystem_isValid();
    void objectRef_forSystem_equality();
    void isSystemAttribute_classifiesAllFourteen();
    void isSystemAttribute_rejectsNonSystemKinds();
    void labelFor_isPopulatedForAllSystemAttrs();
    void unitsFor_followsUnitSystemAndAttribute();
    void variableCode_mapsToEngineSwmmOutSysConstants();
    void variableCode_rejectsNonSystemKindForSystemAttr();
    void variableCode_rejectsSystemKindForNonSystemAttr();
};

// Convenience list — keep in lockstep with PlotAttribute::System*.
namespace {
const std::pair<PlotAttribute, int> kSystemMap[] = {
    {PlotAttribute::SystemTemperature, SWMM_OUT_SYS_TEMPERATURE},
    {PlotAttribute::SystemRainfall,    SWMM_OUT_SYS_RAINFALL},
    {PlotAttribute::SystemSnowDepth,   SWMM_OUT_SYS_SNOW_DEPTH},
    {PlotAttribute::SystemEvap,        SWMM_OUT_SYS_EVAP},
    {PlotAttribute::SystemInfil,       SWMM_OUT_SYS_INFIL},
    {PlotAttribute::SystemRunoff,      SWMM_OUT_SYS_RUNOFF},
    {PlotAttribute::SystemDwInflow,    SWMM_OUT_SYS_DW_INFLOW},
    {PlotAttribute::SystemGwInflow,    SWMM_OUT_SYS_GW_INFLOW},
    {PlotAttribute::SystemLatInflow,   SWMM_OUT_SYS_LAT_INFLOW},
    {PlotAttribute::SystemFlooding,    SWMM_OUT_SYS_FLOODING},
    {PlotAttribute::SystemOutflow,     SWMM_OUT_SYS_OUTFLOW},
    {PlotAttribute::SystemStorage,     SWMM_OUT_SYS_STORAGE},
    {PlotAttribute::SystemEvapTotal,   SWMM_OUT_SYS_EVAP_TOTAL},
    {PlotAttribute::SystemPET,         SWMM_OUT_SYS_PET},
};
} // namespace

void TestComparisonPlotSystemAttrs::objectRef_forSystem_isValid()
{
    ObjectRef r = ObjectRef::forSystem();
    QCOMPARE(r.kind, ObjectRef::Kind::System);
    QVERIFY(r.name.isEmpty());
    QCOMPARE(r.triIdx, -1);
    QVERIFY(r.isValid());   // System is valid even without a name
}

void TestComparisonPlotSystemAttrs::objectRef_forSystem_equality()
{
    QCOMPARE(ObjectRef::forSystem(), ObjectRef::forSystem());
    QVERIFY(ObjectRef::forSystem() != ObjectRef::forNode("J1"));
}

void TestComparisonPlotSystemAttrs::isSystemAttribute_classifiesAllFourteen()
{
    for (const auto& [attr, _] : kSystemMap) {
        QVERIFY2(isSystemAttribute(attr),
                 QString("attr enum value %1 should be a system attribute")
                     .arg(static_cast<int>(attr)).toUtf8().constData());
    }
}

void TestComparisonPlotSystemAttrs::isSystemAttribute_rejectsNonSystemKinds()
{
    const PlotAttribute nonSystem[] = {
        PlotAttribute::Unknown,
        PlotAttribute::NodeDepth, PlotAttribute::NodeHead,
        PlotAttribute::LinkFlow,  PlotAttribute::LinkVelocity,
        PlotAttribute::SubcatchRainfall, PlotAttribute::SubcatchRunoff,
        PlotAttribute::Mesh2DDepth, PlotAttribute::Mesh2DVelocityMag,
    };
    for (auto a : nonSystem)
        QVERIFY(!isSystemAttribute(a));
}

void TestComparisonPlotSystemAttrs::labelFor_isPopulatedForAllSystemAttrs()
{
    for (const auto& [attr, _] : kSystemMap) {
        const QString label = labelFor(attr);
        QVERIFY2(!label.isEmpty() && label != QStringLiteral("Unknown"),
                 QString("missing label for attr %1")
                     .arg(static_cast<int>(attr)).toUtf8().constData());
    }
    // Spot-check a couple to lock the human-facing strings.
    QCOMPARE(labelFor(PlotAttribute::SystemRunoff),   QStringLiteral("Total runoff"));
    QCOMPARE(labelFor(PlotAttribute::SystemFlooding), QStringLiteral("Total flooding"));
    QCOMPARE(labelFor(PlotAttribute::SystemTemperature),
             QStringLiteral("Air temperature"));
}

void TestComparisonPlotSystemAttrs::unitsFor_followsUnitSystemAndAttribute()
{
    // Flow-style totals — m³/s vs ft³/s.
    QCOMPARE(unitsFor(PlotAttribute::SystemFlooding, UnitSystem::SI),
             QStringLiteral("m³/s"));
    QCOMPARE(unitsFor(PlotAttribute::SystemFlooding, UnitSystem::US),
             QStringLiteral("ft³/s"));
    QCOMPARE(unitsFor(PlotAttribute::SystemRunoff,   UnitSystem::SI),
             QStringLiteral("m³/s"));

    // Volume — m³ vs ft³.
    QCOMPARE(unitsFor(PlotAttribute::SystemStorage,  UnitSystem::SI),
             QStringLiteral("m³"));
    QCOMPARE(unitsFor(PlotAttribute::SystemStorage,  UnitSystem::US),
             QStringLiteral("ft³"));

    // Rate — mm/hr vs in/hr.
    QCOMPARE(unitsFor(PlotAttribute::SystemRainfall, UnitSystem::SI),
             QStringLiteral("mm/hr"));
    QCOMPARE(unitsFor(PlotAttribute::SystemInfil,    UnitSystem::US),
             QStringLiteral("in/hr"));

    // Evaporation daily rate — mm/d vs in/d.
    QCOMPARE(unitsFor(PlotAttribute::SystemPET,      UnitSystem::SI),
             QStringLiteral("mm/d"));
    QCOMPARE(unitsFor(PlotAttribute::SystemEvapTotal, UnitSystem::US),
             QStringLiteral("in/d"));

    // Temperature — °C vs °F.
    QCOMPARE(unitsFor(PlotAttribute::SystemTemperature, UnitSystem::SI),
             QStringLiteral("°C"));
    QCOMPARE(unitsFor(PlotAttribute::SystemTemperature, UnitSystem::US),
             QStringLiteral("°F"));
}

void TestComparisonPlotSystemAttrs::variableCode_mapsToEngineSwmmOutSysConstants()
{
    for (const auto& [attr, engineCode] : kSystemMap) {
        const int got = SwmmOutRunLayer::variableCodeFor(
                            attr, ObjectRef::Kind::System);
        QCOMPARE(got, engineCode);
    }
}

void TestComparisonPlotSystemAttrs::variableCode_rejectsNonSystemKindForSystemAttr()
{
    // SystemRunoff against Node/Link/Subcatch must not resolve.
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(
                 PlotAttribute::SystemRunoff, ObjectRef::Kind::Node),     -1);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(
                 PlotAttribute::SystemRunoff, ObjectRef::Kind::Link),     -1);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(
                 PlotAttribute::SystemRunoff, ObjectRef::Kind::Subcatch), -1);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(
                 PlotAttribute::SystemRunoff, ObjectRef::Kind::Unknown),  -1);
}

void TestComparisonPlotSystemAttrs::variableCode_rejectsSystemKindForNonSystemAttr()
{
    // Per-object attributes on a System kind must not resolve.
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(
                 PlotAttribute::NodeDepth, ObjectRef::Kind::System), -1);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(
                 PlotAttribute::LinkFlow,  ObjectRef::Kind::System), -1);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(
                 PlotAttribute::SubcatchRunoff, ObjectRef::Kind::System), -1);
}

QTEST_MAIN(TestComparisonPlotSystemAttrs)
#include "test_comparisonplot_system_attrs.moc"
