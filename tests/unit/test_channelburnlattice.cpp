/*!
 * \file   test_channelburnlattice.cpp
 * \brief  Gates V5, V6 and V7 for the channel burn-in, phases P2 and P3
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §8, §16.2).
 *
 * V5 — every lattice vertex carries the exact burned z, and the strings are
 *      conformal (string k vertex i and string k+1 vertex i are quad corners).
 * V6 — the corridor is 100 % quads, every cell convex/CCW with a healthy
 *      scaled Jacobian, and streamwise-aligned.
 * V7 — the densification guard reports a lattice finer than the mesh floor.
 */
#include <gtest/gtest.h>

#include <QPointF>
#include <QPolygonF>
#include <QSet>
#include <QVector>

#include "mesh/channelburnlattice.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshquadquality.h"

#include <cmath>

using namespace mesh;

namespace {

BurnOptions options()
{
    BurnOptions o;
    o.forceHalfWidth = 2.0;
    o.clipToBanks    = false;
    o.maxHalfWidth   = 6.0;
    o.stringCount    = 1;
    o.chainageStep   = 1.0;
    return o;
}

/*! Trapezoidal section: bottom width 4, side slope 2H:1V, 2 deep. */
SectionGeometry trapezoid()
{
    const QVector<double> d = {0.0, 1.0, 2.0};
    QVector<double> w;
    for (const double y : d) w.append(4.0 + 4.0 * y);
    return sectionFromWidths(d, w);
}

BurnProfile straight(const QString &id = QStringLiteral("C1"), double len = 40.0)
{
    ChannelInput in;
    in.conduitId  = id;
    in.centerline = {QPointF(0, 0), QPointF(len, 0)};
    in.zUp = 10.0;
    in.zDn = 10.0 - len * 0.01;
    in.section = trapezoid();
    return buildBurnProfile(in, options());
}

/*! An arc — a real bend, but not so tight that the corridor must fold. */
BurnProfile curved(double radius)
{
    ChannelInput in;
    in.conduitId = QStringLiteral("BEND");
    for (int i = 0; i <= 24; ++i)
    {
        const double a = (M_PI / 2.0) * double(i) / 24.0;
        in.centerline.append(QPointF(radius * std::sin(a), radius * (1.0 - std::cos(a))));
    }
    in.zUp = 10.0;
    in.zDn = 9.0;
    in.section = trapezoid();

    BurnOptions o = options();
    o.chainageStep = 0.5;
    return buildBurnProfile(in, o);
}

QVector<MeshVertex> vertsOf(const PatchMesh &pm)
{
    QVector<MeshVertex> v;
    v.reserve(pm.xy.size());
    for (const QPointF &p : pm.xy) { MeshVertex mv; mv.xy = p; v.append(mv); }
    return v;
}

} // namespace

// ─────────────────────── V5: conformal lattice, exact z ───────────────────

TEST(ChannelBurnLattice, LatticeIsConformalAndCarriesTheExactBurnedZ)
{
    const BurnProfile p = straight();
    ASSERT_TRUE(p.isValid());

    QString err;
    const BurnLattice lat = buildCorridorLattice(p, 2.0, 0.0, nullptr, &err);
    ASSERT_TRUE(lat.isValid()) << err.toStdString();
    EXPECT_EQ(lat.nAcross, p.offsets.size());
    EXPECT_EQ(lat.nAlong, 21);                       // 40 / 2 + 1

    for (int i = 0; i < lat.nAlong; ++i)
        for (int k = 0; k < lat.nAcross; ++k)
        {
            bool inExtent = false;
            const double want = sectionZAt(p, lat.chainage[i], lat.offsets[k], &inExtent);
            ASSERT_TRUE(inExtent) << "station " << i << " offset " << k;
            EXPECT_DOUBLE_EQ(lat.z[lat.at(i, k)], want);
        }

    // Conformality: every string shares the lattice's chainage stations, so the
    // four corners of a cell come from two consecutive stations on two
    // consecutive strings — which is what makes the quads fall out directly.
    const auto strings = corridorStrings(lat, 500);
    ASSERT_EQ(strings.size(), lat.nAcross);
    for (int k = 0; k < strings.size(); ++k)
    {
        ASSERT_EQ(strings[k].path.size(), lat.nAlong);
        for (int i = 0; i < lat.nAlong; ++i)
            EXPECT_EQ(strings[k].path[i], lat.xy[lat.at(i, k)]);
    }
}

TEST(ChannelBurnLattice, StringsCarryDistinctMarkersAndTags)
{
    const BurnLattice lat = buildCorridorLattice(straight(), 4.0, 0.0);
    ASSERT_TRUE(lat.isValid());

    QHash<int, QString> markerToTag;
    const auto strings = corridorStrings(lat, 900, &markerToTag);
    QSet<int> markers;
    for (const ConstraintSegment &cs : strings)
    {
        EXPECT_FALSE(markers.contains(cs.marker));
        markers.insert(cs.marker);
        EXPECT_TRUE(cs.tag.startsWith(QStringLiteral("burn:C1:")));
        EXPECT_EQ(markerToTag.value(cs.marker), cs.tag);
    }
    EXPECT_EQ(markers.size(), lat.nAcross);
}

TEST(ChannelBurnLattice, SteinerPointsPinTheExactElevation)
{
    const BurnLattice lat = buildCorridorLattice(straight(), 4.0, 0.0);
    ASSERT_TRUE(lat.isValid());

    const auto pts = corridorPoints(lat, 777);
    ASSERT_EQ(pts.size(), lat.xy.size());
    for (int i = 0; i < pts.size(); ++i)
    {
        EXPECT_TRUE(pts[i].hasZ);                    // never re-sampled downstream
        EXPECT_EQ(pts[i].marker, 777);               // non-zero: survives the Free-ring drop
        EXPECT_DOUBLE_EQ(pts[i].z, lat.z[i]);
        EXPECT_EQ(pts[i].xy, lat.xy[i]);
    }

    QVector<QPointF> xy;
    QVector<double>  z;
    corridorZSeeds(lat, &xy, &z);
    EXPECT_EQ(xy.size(), lat.xy.size());
    EXPECT_EQ(z.size(),  lat.z.size());
}

TEST(ChannelBurnLattice, OffsetsRunLeftToRightOfTheFlowDirection)
{
    // Flow west→east, so ascending offset must move south (-y), matching the
    // projection convention and [TRANSECTS] station order.
    const BurnLattice lat = buildCorridorLattice(straight(), 10.0, 0.0);
    ASSERT_TRUE(lat.isValid());
    const int mid = lat.nAlong / 2;
    for (int k = 1; k < lat.nAcross; ++k)
        EXPECT_LT(lat.xy[lat.at(mid, k)].y(), lat.xy[lat.at(mid, k - 1)].y());
}

// ────────────────────────── V6: the quad corridor ─────────────────────────

TEST(ChannelBurnLattice, CorridorIsAllQuadsAndValidates)
{
    const BurnProfile p = straight();
    const BurnLattice lat = buildCorridorLattice(p, 2.0, 0.0);
    ASSERT_TRUE(lat.isValid());

    QString err;
    const PatchMesh pm = corridorPatch(lat, p, options(), &err);
    ASSERT_FALSE(pm.quads.isEmpty()) << err.toStdString();
    EXPECT_TRUE(validate(pm).isEmpty()) << validate(pm).toStdString();
    EXPECT_EQ(pm.quads.size(), (lat.nAlong - 1) * (lat.nAcross - 1));

    const auto verts = vertsOf(pm);
    for (const MeshTriangle &q : pm.quads)
    {
        EXPECT_TRUE(q.isQuad());                            // 100 %, by construction
        EXPECT_GT(cellSignedArea(verts, q), 0.0);           // CCW
        EXPECT_TRUE(cellIsConvex(verts, q));
        EXPECT_GE(quadQuality(verts, q).scaledJacobian, 0.866);
    }
}

TEST(ChannelBurnLattice, CurvedCorridorStaysValidAndStreamwise)
{
    const BurnProfile p = curved(30.0);
    ASSERT_TRUE(p.isValid());
    const BurnLattice lat = buildCorridorLattice(p, 1.5, 0.0);
    ASSERT_TRUE(lat.isValid());

    QString err;
    const PatchMesh pm = corridorPatch(lat, p, options(), &err);
    ASSERT_FALSE(pm.quads.isEmpty()) << err.toStdString();
    EXPECT_TRUE(validate(pm).isEmpty());

    // Streamwise alignment: each cell's along-channel edge must follow the
    // local centreline tangent. Compare the (i → i+1) edge at the cell's own
    // station against the centreline's own direction there.
    double worstDeg = 0.0;
    for (int i = 0; i + 1 < lat.nAlong; ++i)
    {
        const QPointF a = lat.xy[lat.at(i,     lat.nAcross / 2)];
        const QPointF b = lat.xy[lat.at(i + 1, lat.nAcross / 2)];
        const double  ang = std::atan2(b.y() - a.y(), b.x() - a.x());

        // Centreline tangent at the same station, from the profile itself.
        const double t0 = lat.chainage[i], t1 = lat.chainage[i + 1];
        const auto ptAt = [&](double t) {
            const auto it = std::upper_bound(p.chainage.cbegin(), p.chainage.cend(), t);
            const int hi = std::min(int(it - p.chainage.cbegin()), int(p.chainage.size()) - 1);
            const int lo = std::max(0, hi - 1);
            const double d = p.chainage[hi] - p.chainage[lo];
            const double f = (d > 0.0) ? (t - p.chainage[lo]) / d : 0.0;
            return p.centerline[lo] + (p.centerline[hi] - p.centerline[lo]) * f;
        };
        const QPointF c0 = ptAt(t0), c1 = ptAt(t1);
        const double cang = std::atan2(c1.y() - c0.y(), c1.x() - c0.x());
        double diff = std::abs(ang - cang) * 180.0 / M_PI;
        if (diff > 180.0) diff = 360.0 - diff;
        worstDeg = std::max(worstDeg, diff);
    }
    EXPECT_LE(worstDeg, 15.0);
}

TEST(ChannelBurnLattice, AHairpinIsReportedRatherThanEmittedFolded)
{
    // Radius well inside the corridor half-width: the inner offset row must
    // fold. The gate is that this is REPORTED, not handed to Triangle.
    const BurnProfile p = curved(1.5);
    ASSERT_TRUE(p.isValid());
    const BurnLattice lat = buildCorridorLattice(p, 0.5, 0.0);
    ASSERT_TRUE(lat.isValid());

    QString err;
    const PatchMesh pm = corridorPatch(lat, p, options(), &err);
    EXPECT_TRUE(pm.quads.isEmpty());
    EXPECT_FALSE(err.isEmpty());
    EXPECT_TRUE(err.contains(QStringLiteral("folds")));
}

TEST(ChannelBurnLattice, RoughnessTripleReachesTheCellsAsPerCellManningsN)
{
    // A transect with banks and a roughness triple: overbank cells must carry
    // the overbank n, channel cells the channel n.
    SectionGeometry g;
    g.station   = {0.0, 20.0, 30.0, 40.0, 60.0};
    g.elevation = {106.0, 102.0, 100.0, 102.0, 106.0};
    g.leftBank  = 20.0;
    g.rightBank = 40.0;
    g.nLeft = 0.06; g.nChannel = 0.035; g.nRight = 0.07;

    ChannelInput in;
    in.conduitId  = QStringLiteral("CREEK");
    in.centerline = {QPointF(0, 0), QPointF(40, 0)};
    in.zUp = 10.0; in.zDn = 9.0;
    in.section = g;

    BurnOptions o = options();
    o.clipToBanks  = false;
    o.maxHalfWidth = 0.0;
    o.stringCount  = 2;
    o.roughnessFromTransect = true;

    const BurnProfile p = buildBurnProfile(in, o);
    ASSERT_TRUE(p.isValid());
    const BurnLattice lat = buildCorridorLattice(p, 4.0, 0.0);
    ASSERT_TRUE(lat.isValid());

    QString err;
    const PatchMesh pm = corridorPatch(lat, p, o, &err);
    ASSERT_FALSE(pm.quads.isEmpty()) << err.toStdString();

    int left = 0, chan = 0, right = 0;
    int c = 0;
    for (int i = 0; i + 1 < lat.nAlong; ++i)
        for (int k = 0; k + 1 < lat.nAcross; ++k, ++c)
        {
            const double m = 0.5 * (lat.offsets[k] + lat.offsets[k + 1]);
            const MeshTriangle &q = pm.quads[c];
            if (m < p.section.leftBank)
            { EXPECT_DOUBLE_EQ(q.mannings, 0.06); EXPECT_TRUE(q.tag.endsWith(":left"));  ++left; }
            else if (m > p.section.rightBank)
            { EXPECT_DOUBLE_EQ(q.mannings, 0.07); EXPECT_TRUE(q.tag.endsWith(":right")); ++right; }
            else
            { EXPECT_DOUBLE_EQ(q.mannings, 0.035); EXPECT_TRUE(q.tag.endsWith(":chan"));  ++chan; }
        }
    EXPECT_GT(left, 0);
    EXPECT_GT(chan, 0);
    EXPECT_GT(right, 0);
}

TEST(ChannelBurnLattice, WithoutATransectTripleCellsKeepTheDefaultRoughness)
{
    const BurnProfile p = straight();
    const BurnLattice lat = buildCorridorLattice(p, 4.0, 0.0);
    const PatchMesh pm = corridorPatch(lat, p, options());
    ASSERT_FALSE(pm.quads.isEmpty());
    for (const MeshTriangle &q : pm.quads)
        EXPECT_TRUE(std::isnan(q.mannings));   // NaN = the writer's default
}

TEST(ChannelBurnLattice, BoundaryLoopIsClosedAndCoversTheOutline)
{
    const BurnProfile p = straight();
    const BurnLattice lat = buildCorridorLattice(p, 5.0, 0.0);
    const PatchMesh pm = corridorPatch(lat, p, options());
    ASSERT_FALSE(pm.quads.isEmpty());

    // Every boundary vertex appears exactly twice — once as each segment's end.
    QHash<int, int> degree;
    for (const auto &s : pm.boundarySegments) { ++degree[s.first]; ++degree[s.second]; }
    const int perimeter = 2 * (lat.nAlong + lat.nAcross) - 4;
    EXPECT_EQ(degree.size(), perimeter);
    for (auto it = degree.constBegin(); it != degree.constEnd(); ++it)
        EXPECT_EQ(it.value(), 2) << "vertex " << it.key() << " is not on a closed loop";
}

TEST(ChannelBurnLattice, CorridorRingIsSimpleCcwAndHoldsTheCentreline)
{
    const BurnProfile p = straight();
    const BurnLattice lat = buildCorridorLattice(p, 5.0, 0.0);
    const QPolygonF ring = corridorRing(lat);

    ASSERT_GE(ring.size(), 4);
    EXPECT_TRUE(ringIsSimple(ring));
    EXPECT_GT(ringSignedArea(ring), 0.0);              // CCW, as documented
    EXPECT_TRUE(pointInRing(ring, QPointF(p.length() * 0.5, 0.0)));
    EXPECT_FALSE(pointInRing(ring, QPointF(p.length() * 0.5, 50.0)));
}

// ─────────────────────── V7: the densification guard ──────────────────────

TEST(ChannelBurnLattice, LatticeFinerThanTheMeshFloorIsReported)
{
    const BurnProfile p = straight();
    QStringList warnings;
    const BurnLattice lat = buildCorridorLattice(p, 0.25, /*minCellSize*/ 1.0, &warnings);
    ASSERT_TRUE(lat.isValid());
    ASSERT_EQ(warnings.size(), 1);
    EXPECT_TRUE(warnings.first().contains(QStringLiteral("minimum cell size")));

    warnings.clear();
    buildCorridorLattice(p, 5.0, 0.5, &warnings);
    EXPECT_TRUE(warnings.isEmpty());
}

TEST(ChannelBurnLattice, ZeroAlongStepFallsBackToTheProfileStations)
{
    const BurnProfile p = straight();
    const BurnLattice lat = buildCorridorLattice(p, 0.0, 0.0);
    ASSERT_TRUE(lat.isValid());
    EXPECT_EQ(lat.chainage, p.chainage);
}
