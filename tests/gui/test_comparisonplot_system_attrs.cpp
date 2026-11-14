/**
 * @file      test_comparisonplot_system_attrs.cpp
 * @brief     Tests for comparisonplot system attrs.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AT.2 — pins the AT.2 system-attribute additions:
 *   - 15 plottable PlotAttribute::System* enumerators carry labels + units.
 *   - ObjectRef::Kind::System is valid without a name; forSystem() factory.
 *   - SwmmOutRunLayer::variableCodeFor() maps each System* to the slot the
 *     engine's .out writer fills (DefaultOutputPlugin, legacy SysResults
 *     order). Literal slot numbers, not enum names: the enum once disagreed
 *     with the writer and every system plot from slot 3 on read the wrong
 *     column (fixed 2026-10-02).
 *   - Subcatchment groundwater attributes map to SWMM_OUT_SUBCATCH_GW_*.
 *
 * Self-contained: links plotattribute.cpp + the engine's output header only
 * (no .out fixture, no SWMMResultsLayer — we exercise the pure mapping).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/plotattribute.h"
#include "plot/irunlayer.h"
#include "plot/swmmoutrunlayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <QObject>
#include <QTest>

#include <iterator>

using namespace openswmmvis::plot;

class TestComparisonPlotSystemAttrs : public QObject
{
    Q_OBJECT
private slots:
    void objectRef_forSystem_isValid();
    void objectRef_forSystem_equality();
    void isSystemAttribute_classifiesAll();
    void isSystemAttribute_rejectsNonSystemKinds();
    void labelFor_isPopulatedForAllSystemAttrs();
    void unitsFor_followsUnitSystemAndAttribute();
    void variableCode_mapsToEngineSwmmOutSysConstants();
    void variableCode_rejectsNonSystemKindForSystemAttr();
    void variableCode_rejectsSystemKindForNonSystemAttr();
    void variableCode_retiredEvapTotalHasNoSlot();
    void subcatchGroundwater_mapsToEngineSlots();
};

// Convenience list — keep in lockstep with PlotAttribute::System*.
namespace
{
const std::pair<PlotAttribute, int> kSystemMap[] = {
    {PlotAttribute::SystemTemperature, 0},
    {PlotAttribute::SystemRainfall, 1},
    {PlotAttribute::SystemSnowDepth, 2},
    {PlotAttribute::SystemInfil, 3},
    {PlotAttribute::SystemRunoff, 4},
    {PlotAttribute::SystemDwInflow, 5},
    {PlotAttribute::SystemGwInflow, 6},
    {PlotAttribute::SystemRdiiInflow, 7},
    {PlotAttribute::SystemExtInflow, 8},
    {PlotAttribute::SystemLatInflow, 9}, // total lateral inflow
    {PlotAttribute::SystemFlooding, 10},
    {PlotAttribute::SystemOutflow, 11},
    {PlotAttribute::SystemStorage, 12},
    {PlotAttribute::SystemEvap, 13},
    {PlotAttribute::SystemPET, 14},
};
} // namespace

/**
 *
 * @brief Require forSystem to produce a valid nameless System reference with triangle index minus
 * one.
 * No run file, native index or selected feature is needed.
 *
 */
void TestComparisonPlotSystemAttrs::objectRef_forSystem_isValid()
{
    ObjectRef r = ObjectRef::forSystem();
    QCOMPARE(r.kind, ObjectRef::Kind::System);
    QVERIFY(r.name.isEmpty());
    QCOMPARE(r.triIdx, -1);
    QVERIFY(r.isValid()); // System is valid even without a name
}

/**
 *
 * @brief Require independent system refs to compare equal and differ from node J1.
 * Does not check every reference field or other kinds.
 *
 */
void TestComparisonPlotSystemAttrs::objectRef_forSystem_equality()
{
    QCOMPARE(ObjectRef::forSystem(), ObjectRef::forSystem());
    QVERIFY(ObjectRef::forSystem() != ObjectRef::forNode("J1"));
}

/**
 *
 * @brief Require every entry in the fifteen-attribute local mapping to classify as system-wide.
 * Does not assert classification of retired/unlisted enumerators or file availability.
 *
 */
void TestComparisonPlotSystemAttrs::isSystemAttribute_classifiesAll()
{
    for (const auto &[attr, _] : kSystemMap) {
        QVERIFY2(isSystemAttribute(attr), QString("attr enum value %1 should be a system attribute")
                                              .arg(static_cast<int>(attr))
                                              .toUtf8()
                                              .constData());
    }
}

/**
 *
 * @brief Require the representative Unknown/node/link/subcatch/mesh attributes to classify as
 * nonsystem.
 * This bounded list is not a full enumeration of every per-object variable.
 *
 */
void TestComparisonPlotSystemAttrs::isSystemAttribute_rejectsNonSystemKinds()
{
    const PlotAttribute nonSystem[] = {
        PlotAttribute::Unknown,           PlotAttribute::NodeDepth,
        PlotAttribute::NodeHead,          PlotAttribute::LinkFlow,
        PlotAttribute::LinkVelocity,      PlotAttribute::SubcatchRainfall,
        PlotAttribute::SubcatchRunoff,    PlotAttribute::Mesh2DDepth,
        PlotAttribute::Mesh2DVelocityMag,
    };
    for (auto a : nonSystem)
        QVERIFY(!isSystemAttribute(a));
}

/**
 *
 * @brief Require each mapped system attribute to have a nonempty non-Unknown label.
 * Pins exact runoff/flooding/temperature labels only; no translation context or runtime
 * plot rendering is inspected.
 *
 */
void TestComparisonPlotSystemAttrs::labelFor_isPopulatedForAllSystemAttrs()
{
    for (const auto &[attr, _] : kSystemMap) {
        const QString label = labelFor(attr);
        QVERIFY2(
            !label.isEmpty() && label != QStringLiteral("Unknown"),
            QString("missing label for attr %1").arg(static_cast<int>(attr)).toUtf8().constData());
    }
    // Spot-check a couple to lock the human-facing strings.
    QCOMPARE(labelFor(PlotAttribute::SystemRunoff), QStringLiteral("Total runoff"));
    QCOMPARE(labelFor(PlotAttribute::SystemFlooding), QStringLiteral("Total flooding"));
    QCOMPARE(labelFor(PlotAttribute::SystemTemperature), QStringLiteral("Air temperature"));
}

/**
 *
 * @brief Pin selected system flow/storage/rainfall/infiltration/evaporation/temperature unit
 * labels under SI/US.
 * Checks unit strings, including daily evaporation rates; no numerical values are
 * converted and not every attribute/system combination is enumerated.
 *
 */
void TestComparisonPlotSystemAttrs::unitsFor_followsUnitSystemAndAttribute()
{
    // Flow-style totals — m³/s vs ft³/s.
    QCOMPARE(unitsFor(PlotAttribute::SystemFlooding, UnitSystem::SI), QStringLiteral("m³/s"));
    QCOMPARE(unitsFor(PlotAttribute::SystemFlooding, UnitSystem::US), QStringLiteral("ft³/s"));
    QCOMPARE(unitsFor(PlotAttribute::SystemRunoff, UnitSystem::SI), QStringLiteral("m³/s"));

    // Volume — m³ vs ft³.
    QCOMPARE(unitsFor(PlotAttribute::SystemStorage, UnitSystem::SI), QStringLiteral("m³"));
    QCOMPARE(unitsFor(PlotAttribute::SystemStorage, UnitSystem::US), QStringLiteral("ft³"));

    // Rate — mm/hr vs in/hr.
    QCOMPARE(unitsFor(PlotAttribute::SystemRainfall, UnitSystem::SI), QStringLiteral("mm/hr"));
    QCOMPARE(unitsFor(PlotAttribute::SystemInfil, UnitSystem::US), QStringLiteral("in/hr"));

    // Evaporation daily rate — mm/d vs in/d.
    QCOMPARE(unitsFor(PlotAttribute::SystemPET, UnitSystem::SI), QStringLiteral("mm/d"));
    QCOMPARE(unitsFor(PlotAttribute::SystemEvap, UnitSystem::US), QStringLiteral("in/d"));

    // Temperature — °C vs °F.
    QCOMPARE(unitsFor(PlotAttribute::SystemTemperature, UnitSystem::SI), QStringLiteral("°C"));
    QCOMPARE(unitsFor(PlotAttribute::SystemTemperature, UnitSystem::US), QStringLiteral("°F"));
}

/**
 *
 * @brief Require all fifteen system attributes to map to literal writer slots zero through
 * fourteen.
 * Uses pure mapping with System kind; no output file is read or independently generated.
 *
 */
void TestComparisonPlotSystemAttrs::variableCode_mapsToEngineSwmmOutSysConstants()
{
    for (const auto &[attr, engineCode] : kSystemMap) {
        const int got = SwmmOutRunLayer::variableCodeFor(attr, ObjectRef::Kind::System);
        QCOMPARE(got, engineCode);
    }
}

/**
 *
 * @brief Require SystemRunoff with Node/Link/Subcatch/Unknown kind to return minus one.
 * Does not enumerate every system attribute or every object family.
 *
 */
void TestComparisonPlotSystemAttrs::variableCode_rejectsNonSystemKindForSystemAttr()
{
    // SystemRunoff against Node/Link/Subcatch must not resolve.
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::SystemRunoff, ObjectRef::Kind::Node),
             -1);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::SystemRunoff, ObjectRef::Kind::Link),
             -1);
    QCOMPARE(
        SwmmOutRunLayer::variableCodeFor(PlotAttribute::SystemRunoff, ObjectRef::Kind::Subcatch),
        -1);
    QCOMPARE(
        SwmmOutRunLayer::variableCodeFor(PlotAttribute::SystemRunoff, ObjectRef::Kind::Unknown),
        -1);
}

/**
 *
 * @brief Require node-depth/link-flow/subcatch-runoff attributes with System kind to return minus
 * one.
 * Checks representative family mismatches without querying runtime availability.
 *
 */
void TestComparisonPlotSystemAttrs::variableCode_rejectsSystemKindForNonSystemAttr()
{
    // Per-object attributes on a System kind must not resolve.
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::NodeDepth, ObjectRef::Kind::System),
             -1);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::LinkFlow, ObjectRef::Kind::System),
             -1);
    QCOMPARE(
        SwmmOutRunLayer::variableCodeFor(PlotAttribute::SubcatchRunoff, ObjectRef::Kind::System),
        -1);
}

/**
 *
 * @brief Require retired total-evaporation to have no native slot or canonical system-list entry.
 * Checks the canonical count equals the local fifteen-entry mapping; no legacy file
 * compatibility path is loaded.
 *
 */
void TestComparisonPlotSystemAttrs::variableCode_retiredEvapTotalHasNoSlot()
{
    QCOMPARE(
        SwmmOutRunLayer::variableCodeFor(PlotAttribute::SystemEvapTotal, ObjectRef::Kind::System),
        -1);
    QVERIFY(!systemPlotAttributes().contains(PlotAttribute::SystemEvapTotal));
    QCOMPARE(systemPlotAttributes().size(), int(std::size(kSystemMap)));
}

/**
 *
 * @brief Pin groundwater flow/elevation/moisture to subcatchment output slots5/6/7 and reject
 * flow on Node.
 * Checks canonical groundwater elevation membership and representative SI/US/unitless
 * labels; no native numerical groundwater run or sample conversion.
 *
 */
void TestComparisonPlotSystemAttrs::subcatchGroundwater_mapsToEngineSlots()
{
    using K = ObjectRef::Kind;
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::SubcatchGwFlow, K::Subcatch), 5);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::SubcatchGwElev, K::Subcatch), 6);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::SubcatchSoilMoisture, K::Subcatch), 7);
    QCOMPARE(SwmmOutRunLayer::variableCodeFor(PlotAttribute::SubcatchGwFlow, K::Node), -1);
    QVERIFY(subcatchPlotAttributes().contains(PlotAttribute::SubcatchGwElev));
    QCOMPARE(unitsFor(PlotAttribute::SubcatchGwElev, UnitSystem::SI), QStringLiteral("m"));
    QCOMPARE(unitsFor(PlotAttribute::SubcatchGwFlow, UnitSystem::US), QStringLiteral("ft³/s"));
    QVERIFY(unitsFor(PlotAttribute::SubcatchSoilMoisture, UnitSystem::SI).isEmpty());
}

QTEST_MAIN(TestComparisonPlotSystemAttrs)
#include "test_comparisonplot_system_attrs.moc"
