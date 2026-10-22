/**
 * @file      test_resultdescriptor.cpp
 * @brief     Y2b-1 (amendment D-Y4) — ResultDescriptor + the kind-keyed list.
 *
 * @details The amendment's verify line: "descriptor list for a run with
 *          2 pollutants + age = hydraulic set + 3; legacy `.out` =
 *          hydraulic set only." The list-building logic lives in a free
 *          function (`resultDescriptorsForKind`) precisely so this test
 *          can drive it without linking `SWMMResultsLayer` — the Y2a
 *          extraction precedent. What this test therefore CANNOT observe
 *          is `SwmmOutRunLayer::resultDescriptorsForKind` itself (four
 *          lines: `m_layer->speciesNames()` into the tested function);
 *          that wiring is covered by the same closure limit every layer
 *          override has.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "plot/irunlayer.h"
#include "plot/resultdescriptor.h"

#include "layers/speciesattributes.h"

#include <QObject>
#include <QTest>

using namespace openswmmvis::plot;

namespace
{

const QStringList kRunSpecies = {QStringLiteral("TSS"), QStringLiteral("Lead"),
                                 QStringLiteral("__WATER_AGE__")};

/*! A minimal IRunLayer to observe the base-class default. */
class FixedOnlyRun final : public IRunLayer
{
public:
    /**
     *
     * @brief Return the fixed fixture scenario label.
     * @return Owned fixed string; no dataset or mutable state.
     *
     */
    QString scenarioName() const override { return QStringLiteral("fixed"); }
    /**
     *
     * @brief Return the fixture reporting-unit convention.
     * @return US unit system; no numeric conversion is performed.
     *
     */
    UnitSystem unitSystem() const override { return UnitSystem::US; }
    /**
     *
     * @brief Return the no-data fixture start-time placeholder.
     * @return Zero Julian/day value; this fixture supplies no real time series.
     *
     */
    double startDateJulian() const override { return 0.0; }
    /**
     *
     * @brief Report that the metadata-only fixture has no samples.
     * @return Zero periods, without opening output data.
     *
     */
    int periodCount() const override { return 0; }
    /**
     *
     * @brief Return the metadata-only fixture report-step placeholder.
     * @return Zero seconds; not a usable positive simulation cadence.
     *
     */
    int reportStepSeconds() const override { return 0; }
    /**
     *
     * @brief Refuse all series requests in this fixed-descriptor-only fixture.
     * @param object Borrowed object reference, unused and not retained.
     * @param attr Requested attribute, unused.
     * @param out Borrowed mutable output; sets only ok=false, retaining any old arrays,
     * errors and other metadata. No dataset access, allocation or signal emission.
     *
     */
    void getSeriesAt(const ObjectRef &object, PlotAttribute attr, SeriesData &out) const override
    {
        out.ok = false;
    }
};

} // namespace

class TestResultDescriptor : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require species-free kinds to retain the exact ordered fixed hydraulic descriptor
     * lists.
     *
     */
    void legacyRunServesTheFixedSetOnly();
    /**
     *
     * @brief Require node/link/subcatch species to append after fixed descriptors in run order,
     * skipping empty names.
     *
     */
    void speciesAppendAfterTheFixedSetInRunOrder();
    /**
     *
     * @brief Require system/mesh descriptors to ignore species and observed kind to return no
     * descriptors.
     *
     */
    void systemAndMeshKindsCarryNoSpecies();
    /**
     *
     * @brief Check fixed labels/units, pollutant concentration passthrough and reserved water-age
     * display in hours.
     *
     */
    void labelsAndUnitsComeFromTheRightAuthority();
    /**
     *
     * @brief Require the base IRunLayer descriptor implementation to serve fixed node attributes
     * without species.
     *
     */
    void baseIRunLayerDefaultIsFixedOnly();
    /**
     *
     * @brief Check species/fixed identity distinctions and validity of default versus populated
     * descriptors.
     *
     */
    void descriptorIdentityAndValidity();
};

void TestResultDescriptor::legacyRunServesTheFixedSetOnly()
{
    // A legacy .out (no quality) — every kind serves exactly its fixed
    // enum list, wrapped, in presentation order.
    struct Case {
        ObjectRef::Kind kind;
        const QVector<PlotAttribute> *fixed;
    };
    const Case cases[] = {
        {ObjectRef::Kind::Node, &nodePlotAttributes()},
        {ObjectRef::Kind::Link, &linkPlotAttributes()},
        {ObjectRef::Kind::Subcatch, &subcatchPlotAttributes()},
        {ObjectRef::Kind::System, &systemPlotAttributes()},
    };
    for (const auto &c : cases) {
        const auto ds = resultDescriptorsForKind(c.kind, QStringList());
        QCOMPARE(ds.size(), c.fixed->size());
        for (int i = 0; i < ds.size(); ++i) {
            QCOMPARE(ds[i].attr, (*c.fixed)[i]);
            QVERIFY(!ds[i].isSpecies());
        }
    }
}

void TestResultDescriptor::speciesAppendAfterTheFixedSetInRunOrder()
{
    // The amendment's razor: 2 pollutants + age = hydraulic set + 3.
    const auto ds = resultDescriptorsForKind(ObjectRef::Kind::Node, kRunSpecies);
    const auto &fixed = nodePlotAttributes();
    QCOMPARE(ds.size(), fixed.size() + 3);
    // Fixed set first, untouched — species must EXTEND the surface, never
    // displace an enumerator another picker indexes into.
    for (int i = 0; i < fixed.size(); ++i)
        QCOMPARE(ds[i].attr, fixed[i]);
    // Then the species, in the run's .out order (the engine's order —
    // pollutants first, reserved trailing), each carried BY NAME.
    for (int i = 0; i < kRunSpecies.size(); ++i) {
        const auto &d = ds[fixed.size() + i];
        QVERIFY(d.isSpecies());
        QCOMPARE(d.species, kRunSpecies[i]);
        QCOMPARE(d.attr, PlotAttribute::Unknown);
    }

    // Link and subcatch kinds carry species too (Y2a's rule).
    QCOMPARE(resultDescriptorsForKind(ObjectRef::Kind::Link, kRunSpecies).size(),
             linkPlotAttributes().size() + 3);
    QCOMPARE(resultDescriptorsForKind(ObjectRef::Kind::Subcatch, kRunSpecies).size(),
             subcatchPlotAttributes().size() + 3);

    // An empty name is not a species (the engine never reports one; a
    // defensive skip beats a blank picker row).
    const auto blank = resultDescriptorsForKind(ObjectRef::Kind::Node, QStringList{QString()});
    QCOMPARE(blank.size(), fixed.size());
}

void TestResultDescriptor::systemAndMeshKindsCarryNoSpecies()
{
    // System series are .out system variables — there is no per-species
    // system column, so the list must NOT grow with the run's species.
    QCOMPARE(resultDescriptorsForKind(ObjectRef::Kind::System, kRunSpecies).size(),
             systemPlotAttributes().size());
    // Mesh kinds carry their fixed list (per-layer availability is gated by
    // supportsAttribute) but no species columns.
    QCOMPARE(resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell, kRunSpecies).size(),
             mesh2DCellPlotAttributes().size());
    QVERIFY(resultDescriptorsForKind(ObjectRef::Kind::Observed, kRunSpecies).isEmpty());
}

void TestResultDescriptor::labelsAndUnitsComeFromTheRightAuthority()
{
    // Fixed attributes keep their existing label/unit authorities.
    const auto depth = ResultDescriptor::forAttribute(PlotAttribute::NodeDepth);
    QCOMPARE(depth.label(), labelFor(PlotAttribute::NodeDepth));
    QCOMPARE(depth.unitLabel(UnitSystem::US), unitsFor(PlotAttribute::NodeDepth, UnitSystem::US));

    // An ordinary pollutant: bare name, concentration unit passthrough.
    const auto tss = ResultDescriptor::forSpecies(QStringLiteral("TSS"));
    QCOMPARE(tss.label(), QStringLiteral("TSS"));
    QCOMPARE(tss.unitLabel(UnitSystem::US, QStringLiteral("mg/L")), QStringLiteral("mg/L"));

    // The reserved age species: the Y2a display label and the fixed unit
    // the .out enum cannot express (engine A2b) — NOT mg/L. A picker that
    // showed age in mg/L would have the user typing hours into a
    // concentration axis (amendment §6's unit warning).
    const auto age =
        ResultDescriptor::forSpecies(QString::fromLatin1(OpenSWMMVis::Species::kWaterAgeName));
    QCOMPARE(age.label(), OpenSWMMVis::Species::speciesDisplayLabel(
                              QString::fromLatin1(OpenSWMMVis::Species::kWaterAgeName)));
    QCOMPARE(age.unitLabel(UnitSystem::SI, QStringLiteral("mg/L")), QStringLiteral("h"));
}

void TestResultDescriptor::baseIRunLayerDefaultIsFixedOnly()
{
    // Sources with no species (observed CSV, mesh layers) inherit a
    // default that serves exactly the fixed set — no override needed.
    FixedOnlyRun run;
    const auto ds = run.resultDescriptorsForKind(ObjectRef::Kind::Node);
    QCOMPARE(ds.size(), nodePlotAttributes().size());
    for (const auto &d : ds)
        QVERIFY(!d.isSpecies());
}

void TestResultDescriptor::descriptorIdentityAndValidity()
{
    QVERIFY(ResultDescriptor::forSpecies(QStringLiteral("TSS")) ==
            ResultDescriptor::forSpecies(QStringLiteral("TSS")));
    QVERIFY(ResultDescriptor::forSpecies(QStringLiteral("TSS")) !=
            ResultDescriptor::forSpecies(QStringLiteral("Lead")));
    QVERIFY(ResultDescriptor::forSpecies(QStringLiteral("TSS")) !=
            ResultDescriptor::forAttribute(PlotAttribute::NodeDepth));

    QVERIFY(!ResultDescriptor().isValid());
    QVERIFY(!ResultDescriptor().isSpecies());
    QVERIFY(ResultDescriptor::forAttribute(PlotAttribute::LinkFlow).isValid());
    QVERIFY(ResultDescriptor::forSpecies(QStringLiteral("X")).isValid());
}

QTEST_MAIN(TestResultDescriptor)
#include "test_resultdescriptor.moc"
