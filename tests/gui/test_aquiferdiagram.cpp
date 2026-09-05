/*!
 * \file   test_aquiferdiagram.cpp
 * \brief  Phase G2 — the two-zone groundwater (aquifer) illustration builder.
 *
 * `buildAquiferDiagram` is a pure value → value builder, so the assertions are
 * on the model: every [AQUIFERS] parameter gets exactly one callout, the water
 * table sits Egw − Ebot above the aquifer bottom, an Egw at or below Ebot
 * collapses the saturated zone to an "unknown" hatch, and the focused field's
 * callout is the one emphasised. No engine, no widget.
 */

#include <QtTest>

#include "ui/sectionview/aquiferdiagram.h"
#include "ui/sectionview/sectiondiagram.h"

using namespace openswmmvis::sectionview;

namespace {

//! A fully-entered aquifer in feet / in/hr (the SWMM manual's example).
AquiferDiagramInput sampleInput()
{
    AquiferDiagramInput in;
    in.name           = QStringLiteral("AQ1");
    in.porosity       = 0.5;
    in.wiltingPoint   = 0.15;
    in.fieldCapacity  = 0.30;
    in.conductivity   = 5.0;
    in.conductSlope   = 10.0;
    in.tensionSlope   = 15.0;
    in.upperEvapFrac  = 0.35;
    in.lowerEvapDepth = 2.0;
    in.lowerLossCoeff = 0.002;
    in.bottomElev     = 0.0;
    in.waterTableElev = 10.0;
    in.upperMoisture  = 0.35;
    return in;
}

int countLeadersContaining(const SectionDiagramModel &m, const QString &needle)
{
    int n = 0;
    for (const DiagramLeader &l : m.leaders)
        if (l.text.contains(needle)) ++n;
    return n;
}

int countDimsContaining(const SectionDiagramModel &m, const QString &needle)
{
    int n = 0;
    for (const DiagramDim &d : m.dims)
        if (d.text.contains(needle)) ++n;
    return n;
}

const DiagramPolyline *waterTableLine(const SectionDiagramModel &m)
{
    for (const DiagramPolyline &pl : m.polylines)
        if (pl.wavy) return &pl;
    return nullptr;
}

} // namespace

class TestAquiferDiagram : public QObject
{
    Q_OBJECT

private slots:
    void everyParameterHasOneCallout();
    void fluxArrowsAndSchematicNodePresent();
    void waterTablePlacedAtEgwAboveEbot();
    void unknownHatchWhenWaterTableNotAboveBottom();
    void activeParamEmphasisesItsCallout();
    void warningsBecomeCallouts();
    void unitLabelsFlowThrough();
};

void TestAquiferDiagram::everyParameterHasOneCallout()
{
    const SectionDiagramModel m = buildAquiferDiagram(sampleInput());
    QCOMPARE(m.title, QStringLiteral("AQ1"));
    QVERIFY(!m.isEmpty());

    // Leader callouts: Por / WP / FC / Umc, Ksat / Kslope / Tslope, ETu, Seep, Ebot.
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Porosity 0.500")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Wilting Point 0.150")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Field Capacity 0.300")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Umc")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Ksat")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Kslope")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Tslope")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("ETu")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Seep")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Ebot")), 1);
    // Dimension lines: ETs from the surface, Egw from the bottom.
    QCOMPARE(countDimsContaining(m, QStringLiteral("ETs")), 1);
    QCOMPARE(countDimsContaining(m, QStringLiteral("Egw")), 1);
    QCOMPARE(m.dims.size(), 2);
    // Schematic ground surface and receiving node are labelled as the
    // subcatchment's, never given a number.
    QCOMPARE(countLeadersContaining(m, QStringLiteral("per subcatchment")), 2);
    // Nothing is emphasised or warned about by default.
    QCOMPARE(countLeadersContaining(m, aquiferActiveCalloutPrefix()), 0);
    QCOMPARE(countLeadersContaining(m, aquiferWarningCalloutPrefix()), 0);
    for (const DiagramDim &d : m.dims) QVERIFY(!d.accent);

    // Pattern name rides on the ETu callout only when set.
    AquiferDiagramInput withPat = sampleInput();
    withPat.evapPattern = QStringLiteral("EVAP_MONTHLY");
    QCOMPARE(countLeadersContaining(buildAquiferDiagram(withPat),
                                    QStringLiteral("EVAP_MONTHLY")), 1);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("pattern")), 0);
}

void TestAquiferDiagram::fluxArrowsAndSchematicNodePresent()
{
    const SectionDiagramModel m = buildAquiferDiagram(sampleInput());

    QStringList labels;
    for (const DiagramArrow &a : m.arrows) labels << a.label;
    QVERIFY(labels.filter(QStringLiteral("FI")).size() == 1);
    QVERIFY(labels.filter(QStringLiteral("ETu")).size() == 1);
    QVERIFY(labels.filter(QStringLiteral("ETs")).size() == 1);
    QVERIFY(labels.filter(QStringLiteral("percolation")).size() == 2);   // upper→lower + deep
    QVERIFY(labels.filter(QStringLiteral("lateral")).size() == 1);

    // Ground surface with planting, and a manhole-cover glyph on the node.
    QCOMPARE(m.grounds.size(), 1);
    QCOMPARE(m.vegetation.size(), 1);
    QCOMPARE(m.symbols.size(), 1);
    QCOMPARE(m.symbols.first().kind, DiagramSymbolKind::ManholeCover);

    // Upper zone, lower zone, deep ground, node shaft.
    QCOMPARE(m.polys.size(), 4);

    // With ETs = 0 the lower-zone ET arrow is dropped but its dim remains.
    AquiferDiagramInput noEts = sampleInput();
    noEts.lowerEvapDepth = 0.0;
    const SectionDiagramModel m2 = buildAquiferDiagram(noEts);
    QStringList labels2;
    for (const DiagramArrow &a : m2.arrows) labels2 << a.label;
    QCOMPARE(labels2.filter(QStringLiteral("ETs")).size(), 0);
    QCOMPARE(countDimsContaining(m2, QStringLiteral("ETs")), 1);
}

void TestAquiferDiagram::waterTablePlacedAtEgwAboveEbot()
{
    // Ebot 0, Egw 10, ETs 2 → saturated zone 10 thick; the unsaturated zone
    // above is max(ETs, 35 % of that) = 3.5, so the surface sits at 13.5.
    const SectionDiagramModel m = buildAquiferDiagram(sampleInput());

    const DiagramPolyline *wt = waterTableLine(m);
    QVERIFY(wt);
    QCOMPARE(wt->pts.size(), 2);
    QCOMPARE(wt->pts.first().y(), 10.0);
    QCOMPARE(wt->pts.last().y(), 10.0);
    QCOMPARE(m.grounds.first().y, 13.5);

    // The Egw dimension runs from the water table down to the bottom (y = 0).
    for (const DiagramDim &d : m.dims) {
        if (!d.text.contains(QStringLiteral("Egw"))) continue;
        QCOMPARE(d.from.y(), 10.0);
        QCOMPARE(d.to.y(), 0.0);
        QVERIFY(d.text.contains(QStringLiteral("10.00 ft")));
    }
    // Lower zone is a real, known slab.
    QVERIFY(m.polys.size() >= 2);
    QVERIFY(!m.polys[1].unknown);
    QCOMPARE(m.polys[1].role, DiagramRole::Water);
    QVERIFY(m.footer.contains(QStringLiteral("10.00 ft")));

    // A non-zero bottom shifts nothing: the drawing is relative to Ebot.
    AquiferDiagramInput raised = sampleInput();
    raised.bottomElev = 100.0;
    raised.waterTableElev = 110.0;
    // Keep the model alive: waterTableLine() returns a pointer into it.
    const SectionDiagramModel raisedModel = buildAquiferDiagram(raised);
    const DiagramPolyline *wt2 = waterTableLine(raisedModel);
    QVERIFY(wt2);
    QCOMPARE(wt2->pts.first().y(), 10.0);
}

void TestAquiferDiagram::unknownHatchWhenWaterTableNotAboveBottom()
{
    for (double egw : { 0.0, -3.0 }) {
        AquiferDiagramInput in = sampleInput();
        in.waterTableElev = egw;     // Ebot = 0
        const SectionDiagramModel m = buildAquiferDiagram(in);

        QVERIFY(m.polys.size() >= 2);
        QVERIFY2(m.polys[1].unknown, "saturated zone must be hatched as unknown");
        QVERIFY(m.polys[1].insetLabel.contains(QStringLiteral("not above")));
        // The water table line collapses onto the bottom slab, never below it.
        const DiagramPolyline *wt = waterTableLine(m);
        QVERIFY(wt);
        QVERIFY(wt->pts.first().y() > 0.0);
        QVERIFY(wt->pts.first().y() < m.grounds.first().y);
        QVERIFY(countDimsContaining(m, QStringLiteral("not above Ebot")) == 1);
        QVERIFY(m.footer.contains(QStringLiteral("not above bottom")));
    }

    // The normal case draws no such hint.
    const SectionDiagramModel ok = buildAquiferDiagram(sampleInput());
    QCOMPARE(countDimsContaining(ok, QStringLiteral("not above Ebot")), 0);
}

void TestAquiferDiagram::activeParamEmphasisesItsCallout()
{
    // Leader-style params (0..6, 8, 9, 11) get the prefix on exactly one leader.
    for (int param : { 0, 1, 2, 3, 4, 5, 6, 8, 9, 11 }) {
        AquiferDiagramInput in = sampleInput();
        in.activeParam = param;
        const SectionDiagramModel m = buildAquiferDiagram(in);
        QCOMPARE(countLeadersContaining(m, aquiferActiveCalloutPrefix()), 1);
        for (const DiagramDim &d : m.dims) QVERIFY(!d.accent);
    }
    // Dimension-style params (7 = ETs, 10 = Egw) accent their dim instead.
    for (int param : { 7, 10 }) {
        AquiferDiagramInput in = sampleInput();
        in.activeParam = param;
        const SectionDiagramModel m = buildAquiferDiagram(in);
        QCOMPARE(countLeadersContaining(m, aquiferActiveCalloutPrefix()), 0);
        int accented = 0;
        for (const DiagramDim &d : m.dims) if (d.accent) ++accented;
        QCOMPARE(accented, 1);
        const QString key = (param == 7) ? QStringLiteral("ETs") : QStringLiteral("Egw");
        for (const DiagramDim &d : m.dims)
            QCOMPARE(d.accent, d.text.contains(key));
    }
    // Porosity specifically.
    AquiferDiagramInput in = sampleInput();
    in.activeParam = 0;
    bool found = false;
    for (const DiagramLeader &l : buildAquiferDiagram(in).leaders)
        if (l.text.startsWith(aquiferActiveCalloutPrefix()))
            found = l.text.contains(QStringLiteral("Porosity"));
    QVERIFY(found);
}

void TestAquiferDiagram::warningsBecomeCallouts()
{
    AquiferDiagramInput in = sampleInput();
    in.warnings << QStringLiteral("Wilting Point exceeds Field Capacity.")
                << QStringLiteral("Field Capacity exceeds Porosity.");
    const SectionDiagramModel m = buildAquiferDiagram(in);
    QCOMPARE(countLeadersContaining(m, aquiferWarningCalloutPrefix()), 2);
    QCOMPARE(countLeadersContaining(m, QStringLiteral("exceeds Porosity")), 1);
    // Warnings stack; they must not land on the same pixel offset.
    QPointF first, second;
    int seen = 0;
    for (const DiagramLeader &l : m.leaders) {
        if (!l.text.startsWith(aquiferWarningCalloutPrefix())) continue;
        (seen++ == 0 ? first : second) = l.pixelOffset;
    }
    QVERIFY(first != second);
}

void TestAquiferDiagram::unitLabelsFlowThrough()
{
    AquiferDiagramInput in = sampleInput();
    in.lengthLabel = QStringLiteral("m");
    in.rateLabel   = QStringLiteral("mm/hr");
    const SectionDiagramModel m = buildAquiferDiagram(in);
    QVERIFY(countLeadersContaining(m, QStringLiteral("5.00 mm/hr")) == 1);   // Ksat
    QVERIFY(countDimsContaining(m, QStringLiteral("10.00 m")) == 1);         // Egw
    QVERIFY(countDimsContaining(m, QStringLiteral("2.00 m")) == 1);          // ETs
    QVERIFY(countLeadersContaining(m, QStringLiteral("ft")) == 0);
}

QTEST_MAIN(TestAquiferDiagram)
#include "test_aquiferdiagram.moc"
