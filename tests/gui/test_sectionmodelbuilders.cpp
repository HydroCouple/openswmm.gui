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

} // namespace

class TestSectionModelBuilders : public QObject
{
    Q_OBJECT

private slots:
    void linkSectionReportsBothEndsWhenSloping();
    void linkSectionCollapsesToOneValueWhenFlat();
    void linkProfileCrownsAreTrueBarrelEnds();
    void nodeProfileCrownsOnlyForRealSections();
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

QTEST_MAIN(TestSectionModelBuilders)
#include "test_sectionmodelbuilders.moc"
