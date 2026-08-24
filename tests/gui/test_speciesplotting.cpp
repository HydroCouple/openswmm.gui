/*!
 * \file test_speciesplotting.cpp
 * \brief Y2b-2 (amendment D-Y4) — species series resolution + row keying.
 *
 * \details Link-light on purpose: swmmoutrunlayer_codes.cpp carries the
 *          pure static mappings (no SWMMResultsLayer), and the model is
 *          engine-free. What no test here can observe is
 *          `SwmmOutRunLayer::getSeriesAt(descriptor)` against a real
 *          `.out` (it needs the full layer closure) — its species branch
 *          is `speciesVariableCodeFor` (gated here) + the same
 *          fetch-by-code tail the enum path uses.
 *
 *          The claims:
 *          1. Species codes resolve NAME → index → POLLUT_BASE + index
 *             per kind, against the list given AT CALL TIME — a reorder
 *             moves the code with the column, never with any storage.
 *          2. The comparison model keys chart rows on (attribute,
 *             species): TSS and Lead get separate rows with their own
 *             unit labels; age rows read hours.
 *          3. resolveSeries dispatches by DESCRIPTOR: a species spec
 *             reaches a species-aware source, and cleanly refuses on a
 *             source without species (the base default).
 *          4. 1v1 pairs require the same descriptor — TSS vs Lead is
 *             refused exactly like depth vs flow.
 */

#include "layers/speciesattributes.h"
#include "plot/comparisonplotmodel.h"
#include "plot/irunlayer.h"
#include "plot/resultdescriptor.h"
#include "plot/swmmoutrunlayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"

#include <openswmm/engine/openswmm_output.h>

#include <QObject>
#include <QTest>

using namespace openswmmvis::plot;

namespace {

const QStringList kSpecies = {QStringLiteral("TSS"), QStringLiteral("Lead"),
                              QStringLiteral("__WATER_AGE__")};

/*! A source with NO species — exercises the base-class descriptor
 *  default (precise refusal). */
class FixedOnlySource : public IRunLayer
{
public:
    QString    scenarioName()      const override { return QStringLiteral("fixed"); }
    UnitSystem unitSystem()        const override { return UnitSystem::US; }
    double     startDateJulian()   const override { return 0.0; }
    int        periodCount()       const override { return 1; }
    int        reportStepSeconds() const override { return 60; }
    void getSeriesAt(const ObjectRef&, PlotAttribute,
                     SeriesData& out) const override
    {
        out.timesJulian = {1.0};
        out.values      = {42.0};
        out.ok          = true;
    }
};

/*! A species-aware source: marks which pathway served the request. */
class SpeciesSource final : public FixedOnlySource
{
public:
    void getSeriesAt(const ObjectRef& ref, const ResultDescriptor& d,
                     SeriesData& out) const override
    {
        if (!d.isSpecies()) {
            FixedOnlySource::getSeriesAt(ref, d.attr, out);
            return;
        }
        const int idx = kSpecies.indexOf(d.species);
        if (idx < 0) {
            out.ok = false;
            out.errorMessage = QStringLiteral("no such species");
            return;
        }
        out.timesJulian = {1.0};
        out.values      = {100.0 + idx};   // marker: which column served
        out.ok          = true;
    }
};

} // namespace

class TestSpeciesPlotting : public QObject
{
    Q_OBJECT
private slots:
    void speciesCodesResolveByNameAndFollowReorder();
    void modelRowsKeyPerSpecies();
    void resolveSeriesDispatchesByDescriptor();
    void pairsRequireTheSameDescriptor();
    void savedSpeciesTokenSurvivesReopenAgainstAPoorerRun();
};

void TestSpeciesPlotting::speciesCodesResolveByNameAndFollowReorder()
{
    using K = ObjectRef::Kind;
    // NAME → index → per-kind base + index.
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("TSS"), kSpecies, K::Node),
             SWMM_OUT_NODE_POLLUT_BASE + 0);
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("__WATER_AGE__"), kSpecies, K::Node),
             SWMM_OUT_NODE_POLLUT_BASE + 2);
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("__WATER_AGE__"), kSpecies, K::Link),
             SWMM_OUT_LINK_POLLUT_BASE + 2);
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("Lead"), kSpecies, K::Subcatch),
             SWMM_OUT_SUBCATCH_POLLUT_BASE + 1);

    // D-G1's whole point: after a reorder the NAME follows its column.
    const QStringList reordered = {QStringLiteral("__WATER_AGE__"),
                                   QStringLiteral("TSS"),
                                   QStringLiteral("Lead")};
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("__WATER_AGE__"), reordered, K::Node),
             SWMM_OUT_NODE_POLLUT_BASE + 0);
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("TSS"), reordered, K::Node),
             SWMM_OUT_NODE_POLLUT_BASE + 1);

    // Misses are -1, never a wrong column: unknown name, kinds without
    // species columns, the empty name.
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("Zinc"), kSpecies, K::Node), -1);
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("TSS"), kSpecies, K::System), -1);
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QStringLiteral("TSS"), kSpecies, K::Mesh2DCell), -1);
    QCOMPARE(SwmmOutRunLayer::speciesVariableCodeFor(
                 QString(), kSpecies, K::Node), -1);
}

void TestSpeciesPlotting::modelRowsKeyPerSpecies()
{
    ComparisonPlotModel model;
    RunSource rs;
    rs.layer = std::make_shared<SpeciesSource>();
    const int run = model.addRunSource(std::move(rs));

    auto addSpec = [&](const QString& node, PlotAttribute a,
                       const QString& sp) {
        SeriesSpec spec;
        spec.runIndex  = run;
        spec.objectRef = ObjectRef::forNode(node);
        spec.attribute = a;
        spec.species   = sp;
        return model.addSeries(std::move(spec));
    };
    addSpec(QStringLiteral("J1"), PlotAttribute::NodeDepth, {});
    addSpec(QStringLiteral("J1"), PlotAttribute::Unknown, QStringLiteral("TSS"));
    addSpec(QStringLiteral("J2"), PlotAttribute::Unknown, QStringLiteral("TSS"));
    addSpec(QStringLiteral("J1"), PlotAttribute::Unknown, QStringLiteral("Lead"));
    addSpec(QStringLiteral("J1"), PlotAttribute::Unknown,
            QStringLiteral("__WATER_AGE__"));

    // Four rows: depth, TSS (2 series), Lead, age — TSS and Lead share
    // units but are different quantities; one row would overlay them.
    const auto& rows = model.rows();
    QCOMPARE(rows.size(), 4);
    QCOMPARE(rows[0].attribute, PlotAttribute::NodeDepth);
    QVERIFY(rows[0].species.isEmpty());
    QCOMPARE(rows[1].species, QStringLiteral("TSS"));
    QCOMPARE(rows[1].seriesIndices.size(), 2);
    QCOMPARE(rows[1].unitsLabel, QStringLiteral("mg/L"));
    QCOMPARE(rows[2].species, QStringLiteral("Lead"));
    QCOMPARE(rows[2].seriesIndices.size(), 1);
    // The age row reads HOURS (engine A2b via the Y2a authorities).
    QCOMPARE(rows[3].species, QStringLiteral("__WATER_AGE__"));
    QCOMPARE(rows[3].unitsLabel, QStringLiteral("h"));
}

void TestSpeciesPlotting::resolveSeriesDispatchesByDescriptor()
{
    ComparisonPlotModel model;
    RunSource withSpecies;
    withSpecies.layer = std::make_shared<SpeciesSource>();
    RunSource without;
    without.layer = std::make_shared<FixedOnlySource>();
    const int runS = model.addRunSource(std::move(withSpecies));
    const int runF = model.addRunSource(std::move(without));

    SeriesSpec lead;
    lead.runIndex  = runS;
    lead.objectRef = ObjectRef::forNode(QStringLiteral("J1"));
    lead.species   = QStringLiteral("Lead");
    const int sLead = model.addSeries(lead);

    SeriesSpec leadOnFixed = lead;
    leadOnFixed.runIndex = runF;
    const int sMiss = model.addSeries(leadOnFixed);

    SeriesData out;
    model.resolveSeries(sLead, out);
    QVERIFY(out.ok);
    QCOMPARE(out.values.size(), std::size_t(1));
    QCOMPARE(out.values[0], 101.0);   // Lead's marker — the species column

    // The same spec against a species-less source refuses with words —
    // the base default, not a crash and not a silently-wrong column.
    SeriesData miss;
    model.resolveSeries(sMiss, miss);
    QVERIFY(!miss.ok);
    QVERIFY(miss.errorMessage.contains(QStringLiteral("no species")));
}

void TestSpeciesPlotting::pairsRequireTheSameDescriptor()
{
    ComparisonPlotModel model;
    RunSource rs;
    rs.layer = std::make_shared<SpeciesSource>();
    const int run = model.addRunSource(std::move(rs));

    auto addSpec = [&](const QString& node, PlotAttribute a,
                       const QString& sp) {
        SeriesSpec spec;
        spec.runIndex  = run;
        spec.objectRef = ObjectRef::forNode(node);
        spec.attribute = a;
        spec.species   = sp;
        return model.addSeries(std::move(spec));
    };
    const int tss1  = addSpec(QStringLiteral("J1"), PlotAttribute::Unknown,
                              QStringLiteral("TSS"));
    const int tss2  = addSpec(QStringLiteral("J2"), PlotAttribute::Unknown,
                              QStringLiteral("TSS"));
    const int lead1 = addSpec(QStringLiteral("J1"), PlotAttribute::Unknown,
                              QStringLiteral("Lead"));
    const int depth = addSpec(QStringLiteral("J1"), PlotAttribute::NodeDepth,
                              {});

    QVERIFY(model.addPair({tss1, tss2}) >= 0);   // same species — legal
    QCOMPARE(model.addPair({tss1, lead1}), -1);  // TSS vs Lead — refused
    QCOMPARE(model.addPair({tss1, depth}), -1);  // species vs fixed — refused
}

void TestSpeciesPlotting::savedSpeciesTokenSurvivesReopenAgainstAPoorerRun()
{
    // The amendment's Y2b-3 razor: a project saved against a 3-species
    // run, reopened against a 1-species run, warns and degrades rather
    // than mis-plotting. Leg 1 — the .oswp carrier: the style stores the
    // token VERBATIM through toJson/fromJson (the NAME is the identity).
    OpenSWMM::Render::FeatureSublayerStyle style;
    style.setAttribute(QStringLiteral("qual:Lead"));
    const QJsonObject j = style.toJson();
    OpenSWMM::Render::FeatureSublayerStyle back;
    back.fromJson(j);
    QCOMPARE(back.attribute(), QStringLiteral("qual:Lead"));

    // Leg 2 — resolution against the poorer run: a precise miss (−1),
    // never a wrong column; and against a run that HAS the species but
    // in a different position, the right column.
    const QStringList poorer = {QStringLiteral("TSS")};
    QCOMPARE(OpenSWMMVis::Species::speciesOutCode(
                 back.attribute(), poorer, SWMM_OUT_NODE_POLLUT_BASE), -1);
    const QStringList reordered = {QStringLiteral("Lead"),
                                   QStringLiteral("TSS")};
    QCOMPARE(OpenSWMMVis::Species::speciesOutCode(
                 back.attribute(), reordered, SWMM_OUT_NODE_POLLUT_BASE),
             SWMM_OUT_NODE_POLLUT_BASE + 0);

    // Leg 3 — the miss has WORDS, exactly once (the layer logs what this
    // returns; per-frame resolution must not spam).
    OpenSWMMVis::Species::SpeciesMissWarner warner;
    const QString msg =
        warner.noteMiss(back.attribute(), QStringLiteral("poorer.out"));
    QVERIFY(msg.contains(QStringLiteral("Lead")));
    QVERIFY(warner.noteMiss(back.attribute(),
                            QStringLiteral("poorer.out")).isEmpty());
}

QTEST_MAIN(TestSpeciesPlotting)
#include "test_speciesplotting.moc"
