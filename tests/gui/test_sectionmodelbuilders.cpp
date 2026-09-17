/*!
 * \file   test_sectionmodelbuilders.cpp
 * \brief  Elevation annotations produced by the Slice SP section builders.
 *
 * The builders turn engine state into a `SectionDiagramModel`; these tests
 * assert on the leader labels, which are the part a user reads numbers off.
 * Specifically:
 *   - `buildLinkSection` reports BOTH end elevations on a sloping run and
 *     collapses to a single value on a flat one.
 *   - `buildLinkProfile` crowns are the true barrel-end soffits, not an
 *     interpolated mid-run value.
 *   - `buildNodeProfile` labels a crown only for connections that have a real
 *     cross-section — a pump's stub height is a drawing minimum, and reporting
 *     it as an elevation would be a fabricated number.
 *   - `buildNodeProfile` draws each node TYPE as a different object: a storage
 *     unit's shell is brown, wider than a manhole's and follows its own storage
 *     shape; an outfall discharges into drawn receiving water, or into air when
 *     the boundary condition supplies no stage.
 *   - connecting links are drawn per link TYPE: a pump gets a symbol rather
 *     than a barrel, and an orifice gets a wall with fill above and below the
 *     opening rather than a pipe.
 *   - a stage that VARIES (tidal, time series) is never labelled with a number.
 *
 * Fixtures are built in memory (`swmm_engine_new` + the object/setter API), so
 * nothing here needs an .inp on disk. Mirrors the pattern in
 * `test_linkpropertyadapter.cpp`.
 */

#include "ui/sectionview/sectionmodelbuilders.h"
#include "ui/sectionview/xsectsampler.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QObject>
#include <QTest>

#include <openswmm/engine/openswmm_spatial.h>

#include <cmath>

#include <QStringList>

using namespace openswmmvis::sectionview;

namespace {

//! The builders format with the label the caller supplies; US keeps the
//! fixture numbers readable as feet.
const DiagramUnits kUnits{ QStringLiteral("ft"), false };

//! First leader whose text starts with \p prefix, or an empty string.
QString leaderStarting(const SectionDiagramModel &m, const QString &prefix)
{
    for (const DiagramLeader &l : m.leaders)
        if (l.text.startsWith(prefix)) return l.text;
    return {};
}

int countLeadersContaining(const SectionDiagramModel &m, const QString &needle)
{
    int n = 0;
    for (const DiagramLeader &l : m.leaders)
        if (l.text.contains(needle)) ++n;
    return n;
}

/*!
 * Two junctions joined by a 1.0-diameter circular conduit, 1000 long.
 * \param invUp / \p invDn  node invert elevations.
 */
SWMM_Engine buildConduitFixture(double invUp, double invDn)
{
    SWMM_Engine e = swmm_engine_new();
    if (!e) return nullptr;

    swmm_node_add(e, "J1", SWMM_NODE_JUNCTION);
    swmm_node_add(e, "J2", SWMM_NODE_JUNCTION);
    const int j1 = swmm_node_index(e, "J1");
    const int j2 = swmm_node_index(e, "J2");
    swmm_node_set_invert_elev(e, j1, invUp);
    swmm_node_set_invert_elev(e, j2, invDn);
    swmm_node_set_max_depth(e, j1, 10.0);
    swmm_node_set_max_depth(e, j2, 10.0);

    swmm_link_add(e, "C1", SWMM_LINK_CONDUIT);
    const int c1 = swmm_link_index(e, "C1");
    swmm_link_set_nodes(e, c1, j1, j2);
    swmm_link_set_length(e, c1, 1000.0);
    swmm_link_set_xsect(e, c1, SWMM_XSECT_CIRCULAR, 1.0, 0.0, 0.0, 0.0);
    return e;
}

//! Widest half-width of the polys carrying \p role, 0 when none are present.
double widestHalfWidth(const SectionDiagramModel &m, DiagramRole role)
{
    double w = 0.0;
    for (const DiagramPoly &p : m.polys) {
        if (p.role != role) continue;
        for (const QPointF &pt : p.pts) w = std::max(w, std::abs(pt.x()));
    }
    return w;
}

int countPolys(const SectionDiagramModel &m, DiagramRole role)
{
    int n = 0;
    for (const DiagramPoly &p : m.polys)
        if (p.role == role) ++n;
    return n;
}

bool hasSymbol(const SectionDiagramModel &m, DiagramSymbolKind kind)
{
    for (const DiagramSymbol &s : m.symbols)
        if (s.kind == kind) return true;
    return false;
}

/*! A single node of \p type with one inbound conduit, so every node case has a
 *  pipe to attach and a shell to draw. Returns the node's index. */
int buildNodeFixture(SWMM_Engine e, const char *id, int type,
                     double invert, double maxDepth)
{
    swmm_node_add(e, id, type);
    const int n = swmm_node_index(e, id);
    swmm_node_set_invert_elev(e, n, invert);
    swmm_node_set_max_depth(e, n, maxDepth);

    swmm_node_add(e, "FEED", SWMM_NODE_JUNCTION);
    const int up = swmm_node_index(e, "FEED");
    swmm_node_set_invert_elev(e, up, invert + 4.0);
    swmm_node_set_max_depth(e, up, 8.0);

    swmm_link_add(e, "CF", SWMM_LINK_CONDUIT);
    const int c = swmm_link_index(e, "CF");
    swmm_link_set_nodes(e, c, up, n);
    swmm_link_set_length(e, c, 300.0);
    swmm_link_set_xsect(e, c, SWMM_XSECT_CIRCULAR, 2.0, 0, 0, 0);
    return n;
}

} // namespace

class TestSectionModelBuilders : public QObject
{
    Q_OBJECT

private slots:
    void linkSectionReportsBothEndsWhenSloping();
    void linkSectionCollapsesToOneValueWhenFlat();
    void linkProfileCrownsAreTrueBarrelEnds();
    void nodeProfileCrownsOnlyForRealSections();
    void storageShellIsWiderAndBrownerThanAJunction();
    void storageSilhouetteFollowsItsShape();
    void outfallDrawsReceivingWaterOnlyWhenItHasAStage();
    void varyingOutfallStageIsNeverLabelledWithANumber();
    void pumpConnectionIsASymbolNotABarrel();
    void orificeConnectionIsAWallWithFillAboveTheOpening();
    void linkProfileStubsTheOtherPipesAtBothEnds();
    void linkProfileGivesEachEndItsOwnPlanInset();
    void linkProfileStubsDoNotWidenTheDrawing();
};

void TestSectionModelBuilders::linkSectionReportsBothEndsWhenSloping()
{
    SWMM_Engine e = buildConduitFixture(100.0, 98.0);
    QVERIFY(e);

    const SectionDiagramModel m =
        buildLinkSection(e, swmm_link_index(e, "C1"), kUnits);

    // A section is one shape but the run has two ends; both belong on the
    // drawing or the number silently describes the upstream end only.
    QCOMPARE(leaderStarting(m, QStringLiteral("Invert El.")),
             QStringLiteral("Invert El. 100.00 / 98.00 ft"));
    QCOMPARE(leaderStarting(m, QStringLiteral("Crown El.")),
             QStringLiteral("Crown El. 101.00 / 99.00 ft"));

    swmm_engine_destroy(e);
}

void TestSectionModelBuilders::linkSectionCollapsesToOneValueWhenFlat()
{
    SWMM_Engine e = buildConduitFixture(100.0, 100.0);
    QVERIFY(e);

    const SectionDiagramModel m =
        buildLinkSection(e, swmm_link_index(e, "C1"), kUnits);

    const QString invert = leaderStarting(m, QStringLiteral("Invert El."));
    const QString crown  = leaderStarting(m, QStringLiteral("Crown El."));
    QCOMPARE(invert, QStringLiteral("Invert El. 100.00 ft"));
    QCOMPARE(crown,  QStringLiteral("Crown El. 101.00 ft"));
    QVERIFY2(!invert.contains(QLatin1Char('/')),
             "a flat run should not pay for the two-ended form");
    QVERIFY(!crown.contains(QLatin1Char('/')));

    swmm_engine_destroy(e);
}

void TestSectionModelBuilders::linkProfileCrownsAreTrueBarrelEnds()
{
    SWMM_Engine e = buildConduitFixture(100.0, 98.0);
    QVERIFY(e);

    const SectionDiagramModel m =
        buildLinkProfile(e, swmm_link_index(e, "C1"), kUnits);

    QVector<QString> crowns;
    for (const DiagramLeader &l : m.leaders)
        if (l.text.startsWith(QStringLiteral("Crown "))) crowns << l.text;

    // Exactly one per barrel end, each carrying that end's invert + yFull.
    // The values this replaced were interpolated at a quarter point, which
    // matched no station on a plan set.
    QCOMPARE(crowns.size(), 2);
    QCOMPARE(crowns.at(0), QStringLiteral("Crown 101.00"));
    QCOMPARE(crowns.at(1), QStringLiteral("Crown 99.00"));

    swmm_engine_destroy(e);
}

void TestSectionModelBuilders::nodeProfileCrownsOnlyForRealSections()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e);

    swmm_node_add(e, "UP", SWMM_NODE_JUNCTION);
    swmm_node_add(e, "J3", SWMM_NODE_JUNCTION);
    swmm_node_add(e, "O1", SWMM_NODE_JUNCTION);
    const int up = swmm_node_index(e, "UP");
    const int j3 = swmm_node_index(e, "J3");
    const int o1 = swmm_node_index(e, "O1");
    swmm_node_set_invert_elev(e, up, 100.0);
    swmm_node_set_invert_elev(e, j3, 93.0);
    swmm_node_set_invert_elev(e, o1, 90.0);
    swmm_node_set_max_depth(e, j3, 10.0);

    swmm_link_add(e, "C2", SWMM_LINK_CONDUIT);
    const int c2 = swmm_link_index(e, "C2");
    swmm_link_set_nodes(e, c2, up, j3);
    swmm_link_set_length(e, c2, 1000.0);
    swmm_link_set_xsect(e, c2, SWMM_XSECT_CIRCULAR, 1.0, 0.0, 0.0, 0.0);

    // A pump has no cross-section: `buildNodeProfile` still draws it a stub,
    // using a fraction of the chamber depth so it stays visible.
    swmm_link_add(e, "P1", SWMM_LINK_PUMP);
    swmm_link_set_nodes(e, swmm_link_index(e, "P1"), j3, o1);

    const SectionDiagramModel m = buildNodeProfile(e, j3, kUnits);

    // The conduit's crown is its invert at this node plus the full depth.
    QCOMPARE(leaderStarting(m, QStringLiteral("C2  Crown")),
             QStringLiteral("C2  Crown 94.00"));
    // Both connections keep their invert label.
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Inv 93.00")), 2);
    // ...but the pump's stub height must never be reported as an elevation.
    QCOMPARE(countLeadersContaining(m, QStringLiteral("Crown")), 1);
    QVERIFY(leaderStarting(m, QStringLiteral("P1  Crown")).isEmpty());

    swmm_engine_destroy(e);
}

void TestSectionModelBuilders::storageShellIsWiderAndBrownerThanAJunction()
{
    SWMM_Engine ej = swmm_engine_new();
    QVERIFY(ej);
    const int j = buildNodeFixture(ej, "J1", SWMM_NODE_JUNCTION, 100.0, 10.0);
    const SectionDiagramModel mj = buildNodeProfile(ej, j, kUnits);

    SWMM_Engine es = swmm_engine_new();
    QVERIFY(es);
    const int s = buildNodeFixture(es, "ST1", SWMM_NODE_STORAGE, 100.0, 10.0);
    swmm_node_set_storage_shape(es, s, SWMM_STORAGE_CYLINDRICAL);
    const SectionDiagramModel ms = buildNodeProfile(es, s, kUnits);

    // The whole point of the type styling: a tank must not be able to be
    // mistaken for a manhole. Brown shell (its own role), and materially wider.
    QCOMPARE(countPolys(mj, DiagramRole::Storage), 0);
    QCOMPARE(countPolys(ms, DiagramRole::Storage), 1);
    QCOMPARE(countPolys(ms, DiagramRole::Structure), 0);

    const double junctionW = widestHalfWidth(mj, DiagramRole::Structure);
    const double storageW  = widestHalfWidth(ms, DiagramRole::Storage);
    QVERIFY2(storageW > junctionW * 1.8,
             qPrintable(QStringLiteral("storage %1 vs junction %2")
                            .arg(storageW).arg(junctionW)));

    // A manhole gets a frame and cover; a tank does not.
    QVERIFY(hasSymbol(mj, DiagramSymbolKind::ManholeCover));
    QVERIFY(!hasSymbol(ms, DiagramSymbolKind::ManholeCover));

    swmm_engine_destroy(ej);
    swmm_engine_destroy(es);
}

void TestSectionModelBuilders::storageSilhouetteFollowsItsShape()
{
    // A cone with a 3:1 side slope over 10 ft of depth is four times wider at
    // its rim than at its invert; a cylinder is a box. If the shell ignored the
    // shape, both would be rectangles and the drawing would be telling the
    // reader something the model does not say.
    const auto shellSpan = [](int shape, double p3) {
        SWMM_Engine e = swmm_engine_new();
        const int s = buildNodeFixture(e, "ST", SWMM_NODE_STORAGE, 100.0, 10.0);
        swmm_node_set_storage_shape(e, s, shape);
        swmm_node_set_storage_geometry(e, s, 20.0, 16.0, p3);
        const SectionDiagramModel m = buildNodeProfile(e, s, kUnits);

        double atInvert = 0.0, atRim = 0.0;
        for (const DiagramPoly &p : m.polys) {
            if (p.role != DiagramRole::Storage) continue;
            for (const QPointF &pt : p.pts) {
                if (std::abs(pt.y() - 100.0) < 1.0e-6)
                    atInvert = std::max(atInvert, std::abs(pt.x()));
                if (std::abs(pt.y() - 110.0) < 1.0e-6)
                    atRim = std::max(atRim, std::abs(pt.x()));
            }
        }
        swmm_engine_destroy(e);
        return QPointF(atInvert, atRim);
    };

    const QPointF cone = shellSpan(SWMM_STORAGE_CONICAL, 3.0);
    QVERIFY(cone.x() > 0.0 && cone.y() > 0.0);
    QVERIFY2(cone.y() > cone.x() * 1.5,
             "a conical tank must be drawn wider at its rim than at its invert");

    const QPointF cyl = shellSpan(SWMM_STORAGE_CYLINDRICAL, 0.0);
    QCOMPARE(cyl.x(), cyl.y());
}

void TestSectionModelBuilders::outfallDrawsReceivingWaterOnlyWhenItHasAStage()
{
    // FIXED supplies a tailwater elevation, so water is drawn and labelled with
    // it. FREE supplies none — the pipe discharges to air, and drawing a water
    // body there would invent a boundary the model does not have.
    SWMM_Engine ef = swmm_engine_new();
    QVERIFY(ef);
    const int of = buildNodeFixture(ef, "OF", SWMM_NODE_OUTFALL, 90.0, 10.0);
    swmm_node_set_outfall_type(ef, of, 2 /* FIXED */);
    swmm_node_set_outfall_stage(ef, of, 95.0);
    const SectionDiagramModel mf = buildNodeProfile(ef, of, kUnits);

    QCOMPARE(countPolys(mf, DiagramRole::Water), 1);
    bool labelled = false;
    for (const DiagramPolyline &pl : mf.polylines)
        if (pl.label.contains(QStringLiteral("95.00"))) labelled = pl.wavy;
    QVERIFY2(labelled, "a fixed tailwater must be a labelled, wavy surface");

    SWMM_Engine ee = swmm_engine_new();
    QVERIFY(ee);
    const int free = buildNodeFixture(ee, "OF", SWMM_NODE_OUTFALL, 90.0, 10.0);
    swmm_node_set_outfall_type(ee, free, 0 /* FREE */);
    const SectionDiagramModel me = buildNodeProfile(ee, free, kUnits);

    QCOMPARE(countPolys(me, DiagramRole::Water), 0);
    QCOMPARE(leaderStarting(me, QStringLiteral("free discharge")),
             QStringLiteral("free discharge"));

    swmm_engine_destroy(ef);
    swmm_engine_destroy(ee);
}

void TestSectionModelBuilders::varyingOutfallStageIsNeverLabelledWithANumber()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e);
    const int of = buildNodeFixture(e, "OF", SWMM_NODE_OUTFALL, 90.0, 10.0);
    swmm_node_set_outfall_type(e, of, 3 /* TIDAL */);
    const SectionDiagramModel m = buildNodeProfile(e, of, kUnits);

    // The surface drawn for a tidal outfall is schematic: it is dashed, it says
    // "varies", and it must never carry an elevation, because the one drawn is
    // not a stage the model predicts.
    bool found = false;
    for (const DiagramPolyline &pl : m.polylines) {
        if (pl.role != DiagramRole::Water) continue;
        found = true;
        QVERIFY2(pl.dashed, "a varying stage must be drawn dashed");
        QCOMPARE(pl.label, QStringLiteral("stage varies"));
        QVERIFY(!pl.label.contains(QLatin1Char('.')));
    }
    QVERIFY(found);

    swmm_engine_destroy(e);
}

void TestSectionModelBuilders::pumpConnectionIsASymbolNotABarrel()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e);
    const int ww = buildNodeFixture(e, "WW", SWMM_NODE_JUNCTION, 80.0, 14.0);

    swmm_node_add(e, "FM", SWMM_NODE_JUNCTION);
    swmm_link_add(e, "PMP", SWMM_LINK_PUMP);
    const int p = swmm_link_index(e, "PMP");
    swmm_link_set_nodes(e, p, ww, swmm_node_index(e, "FM"));

    const SectionDiagramModel m = buildNodeProfile(e, ww, kUnits);

    // A pump carries a curve and two depths, not a barrel: the drawing gets a
    // pressure line plus the symbol, and no conduit polygon beyond the inbound
    // conduit's own stub.
    QVERIFY(hasSymbol(m, DiagramSymbolKind::Pump));
    QCOMPARE(countPolys(m, DiagramRole::Conduit), 2);   // shell void + the feed

    swmm_engine_destroy(e);
}

void TestSectionModelBuilders::orificeConnectionIsAWallWithFillAboveTheOpening()
{
    SWMM_Engine e = swmm_engine_new();
    QVERIFY(e);
    const int n = buildNodeFixture(e, "N1", SWMM_NODE_JUNCTION, 80.0, 14.0);

    swmm_node_add(e, "DN", SWMM_NODE_JUNCTION);
    swmm_link_add(e, "ORF", SWMM_LINK_ORIFICE);
    const int o = swmm_link_index(e, "ORF");
    swmm_link_set_nodes(e, o, n, swmm_node_index(e, "DN"));
    swmm_link_set_xsect(e, o, SWMM_XSECT_RECT_CLOSED, 1.5, 2.0, 0, 0);
    swmm_link_set_offset_up(e, o, 3.0);
    swmm_link_set_orifice_type(e, o, SWMM_ORIFICE_SIDE);

    QVERIFY(o >= 0);
    const SectionDiagramModel m = buildNodeProfile(e, n, kUnits);

    // Opening 83.0 → 84.5. The wall is drawn as fill ABOVE the crown and BELOW
    // the invert, with the opening itself left void — that gap is the orifice.
    bool fillAbove = false, fillBelow = false, coversOpening = false;
    for (const DiagramPoly &p : m.polys) {
        if (p.texture != DiagramTexture::Hatch) continue;
        double lo = 1.0e9, hi = -1.0e9;
        for (const QPointF &pt : p.pts) {
            lo = std::min(lo, pt.y());
            hi = std::max(hi, pt.y());
        }
        if (lo >= 84.5 - 1.0e-6) fillAbove = true;
        if (hi <= 83.0 + 1.0e-6) fillBelow = true;
        // Any hatch straddling the opening would be a plate over the hole.
        if (lo < 84.5 - 1.0e-6 && hi > 83.0 + 1.0e-6) coversOpening = true;
    }
    QVERIFY2(fillAbove, "an orifice must show the wall it is cut through");
    QVERIFY(fillBelow);
    QVERIFY2(!coversOpening, "the opening itself must stay void");

    // The jet through it is what says the opening is an opening.
    QVERIFY(!m.arrows.isEmpty());

    swmm_engine_destroy(e);
}

QTEST_MAIN(TestSectionModelBuilders)

// ---------------------------------------------------------------------------
// Link profile — what else meets each end
// ---------------------------------------------------------------------------

namespace {

/*! J0 -> B1 -> J1 -> C1 -> J2 -> B2 -> J3, laid out west-to-east so every
 *  heading resolves. Selecting C1 must show B1 at its upstream node and B2 at
 *  its downstream node, and nothing of J0 / J3 (they are a reach away). */
SWMM_Engine buildBranchedFixture()
{
    SWMM_Engine e = swmm_engine_new();
    if (!e) return nullptr;

    struct N { const char *id; double x, y, inv; };
    const N ns[4] = { { "J0", -100.0, 50.0, 101.0 },
                      { "J1",    0.0,  0.0, 100.0 },
                      { "J2",  100.0,  0.0,  98.0 },
                      { "J3",  200.0, -50.0, 97.0 } };
    for (const N &n : ns) {
        swmm_node_add(e, n.id, SWMM_NODE_JUNCTION);
        const int i = swmm_node_index(e, n.id);
        swmm_node_set_invert_elev(e, i, n.inv);
        swmm_node_set_max_depth(e, i, 10.0);
        swmm_spatial_set_node_coord(e, i, n.x, n.y);
    }

    struct L { const char *id; const char *from; const char *to; };
    const L ls[3] = { { "B1", "J0", "J1" },     // branch INTO the upstream node
                      { "C1", "J1", "J2" },     // the reach under inspection
                      { "B2", "J2", "J3" } };   // branch OUT of the downstream node
    for (const L &l : ls) {
        swmm_link_add(e, l.id, SWMM_LINK_CONDUIT);
        const int i = swmm_link_index(e, l.id);
        swmm_link_set_nodes(e, i, swmm_node_index(e, l.from),
                            swmm_node_index(e, l.to));
        swmm_link_set_length(e, i, 100.0);
        swmm_link_set_xsect(e, i, SWMM_XSECT_CIRCULAR, 1.0, 0.0, 0.0, 0.0);
    }
    return e;
}

//! Every leader whose text names \p link — either alone (a branch stub, which
//! carries only the name) or as the first word of a longer label.
int leadersNaming(const SectionDiagramModel &m, const QString &link)
{
    int n = 0;
    for (const DiagramLeader &l : m.leaders)
        if (l.text == link || l.text.startsWith(link + QLatin1Char(' '))) ++n;
    return n;
}

} // namespace

void TestSectionModelBuilders::linkProfileStubsTheOtherPipesAtBothEnds()
{
    SWMM_Engine e = buildBranchedFixture();
    QVERIFY(e);

    const SectionDiagramModel m =
        buildLinkProfile(e, swmm_link_index(e, "C1"), kUnits);

    // The branch at each end is drawn and labelled with its own invert.
    QVERIFY2(leadersNaming(m, QStringLiteral("B1")) > 0,
             "upstream node's other pipe was not stubbed");
    QVERIFY2(leadersNaming(m, QStringLiteral("B2")) > 0,
             "downstream node's other pipe was not stubbed");

    // The selected reach is drawn full length by the profile itself; stubbing
    // it as well would draw the same pipe twice at the same elevation.
    QCOMPARE(leadersNaming(m, QStringLiteral("C1")), 0);

    // Barrel + two stubs, so a plain reach's single barrel has grown.
    const SectionDiagramModel plain =
        buildLinkProfile(buildConduitFixture(100.0, 98.0),
                         0, kUnits);
    QVERIFY2(countPolys(m, DiagramRole::Conduit)
                 > countPolys(plain, DiagramRole::Conduit),
             "no extra conduit bodies were added for the branches");

    swmm_engine_destroy(e);
}

void TestSectionModelBuilders::linkProfileGivesEachEndItsOwnPlanInset()
{
    SWMM_Engine e = buildBranchedFixture();
    QVERIFY(e);

    const SectionDiagramModel m =
        buildLinkProfile(e, swmm_link_index(e, "C1"), kUnits);

    // Two dials, titled and ordered upstream-then-downstream — an untitled or
    // single dial cannot say which end it describes.
    QCOMPARE(m.planInsets.size(), 2);
    // Titled by ROLE as well as by name, and anchored to opposite margins, so
    // the pair cannot be read the wrong way round.
    QCOMPARE(m.planInsets.at(0).title, QStringLiteral("Inlet · J1"));
    QCOMPARE(m.planInsets.at(1).title, QStringLiteral("Outlet · J2"));
    QCOMPARE(m.planInsets.at(0).side, PlanInset::Side::Left);
    QCOMPARE(m.planInsets.at(1).side, PlanInset::Side::Right);

    auto spokeNames = [](const PlanInset &pi) {
        QStringList out;
        for (const PlanSpoke &s : pi.spokes) out << s.label;
        out.sort();
        return out;
    };
    // Upstream dial: the branch arriving plus the reach leaving.
    QCOMPARE(spokeNames(m.planInsets.at(0)),
             (QStringList{ QStringLiteral("B1"), QStringLiteral("C1") }));
    // Downstream dial: the reach arriving plus the branch leaving.
    QCOMPARE(spokeNames(m.planInsets.at(1)),
             (QStringList{ QStringLiteral("B2"), QStringLiteral("C1") }));

    // The shared reach must point OPPOSITE ways on the two dials: it leaves
    // one node and arrives at the other. Getting this wrong is the classic
    // way a pair of compasses ends up lying about direction.
    auto reach = [](const PlanInset &pi) {
        for (const PlanSpoke &s : pi.spokes)
            if (s.label == QStringLiteral("C1")) return s;
        return PlanSpoke{};
    };
    const PlanSpoke up = reach(m.planInsets.at(0));
    const PlanSpoke dn = reach(m.planInsets.at(1));
    QVERIFY2(!up.inbound, "the reach should LEAVE its upstream node");
    QVERIFY2(dn.inbound,  "the reach should ARRIVE at its downstream node");
    const double delta =
        std::fmod(std::abs(up.angleDeg - dn.angleDeg) + 360.0, 360.0);
    QVERIFY2(std::abs(delta - 180.0) < 1.0e-6,
             qPrintable(QStringLiteral("reach bearings differ by %1 deg, not 180")
                            .arg(delta)));

    swmm_engine_destroy(e);
}


void TestSectionModelBuilders::linkProfileStubsDoNotWidenTheDrawing()
{
    // The pane fits the model's bounds while HOLDING a capped vertical
    // exaggeration, so anything that widens the model without deepening it
    // costs horizontal fill. The stubs are deliberately kept inside the
    // ground-line reach the profile already drew, so they are free: this
    // pins that, and fails if a future tweak lengthens them past it.
    SWMM_Engine plainE = buildConduitFixture(100.0, 98.0);
    QVERIFY(plainE);
    const QRectF plain =
        buildLinkProfile(plainE, 0, kUnits).computeBounds();

    SWMM_Engine e = buildBranchedFixture();
    QVERIFY(e);
    const QRectF withStubs =
        buildLinkProfile(e, swmm_link_index(e, "C1"), kUnits).computeBounds();

    // Same reach length in both fixtures? No — normalise by length instead.
    const double plainRatio  = plain.width()      / 1000.0;   // C1 is 1000 long
    const double stubbedRatio = withStubs.width() /  100.0;   // C1 is 100 long
    QVERIFY2(stubbedRatio <= plainRatio + 1.0e-9,
             qPrintable(QStringLiteral("stubs widened the drawing: %1 vs %2 "
                                       "reach-lengths across")
                            .arg(stubbedRatio).arg(plainRatio)));

    swmm_engine_destroy(plainE);
    swmm_engine_destroy(e);
}

#include "test_sectionmodelbuilders.moc"
