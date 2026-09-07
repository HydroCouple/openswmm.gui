/*!
 * \file   test_featuregeometry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Unit tests for the editable-feature geometry value type and its OGR
 *         bridge (MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07 §3.1;
 *         HANDOFF_FEATURE_LAYERS_VALIDATE_2026-09-07 §4).
 *
 * A leaf test: featuregeometry.cpp depends only on Qt, GDAL's OGR geometry API
 * and the ring predicates in mesh/meshquadregion.cpp, so no widget or layer
 * closure is linked.
 *
 * The contract under test:
 *
 *   - rings keep their parallel Z array index-aligned through every edit
 *     (densify, reverse) — a Z that slides off its vertex is silent terrain
 *     corruption;
 *   - vertices inserted by densify are NaN, never zero: the mesh pipeline
 *     distinguishes "unsampled" from "at datum" via SteinerPoint::hasZ;
 *   - a geometry survives a round trip through OGR unchanged, including holes,
 *     multiple parts and Z;
 *   - validate() rejects exactly the shapes a quad region would reject
 *     (self-intersection, a hole outside its exterior, overlapping holes) and
 *     accepts a clean polygon with two holes.
 */
#include <gtest/gtest.h>

#include "feature/featuregeometry.h"

#include <ogr_api.h>
#include <ogr_geometry.h>

#include <QLineF>
#include <QPointF>
#include <QString>

#include <cmath>
#include <limits>
#include <memory>

using namespace openswmmvis::feature;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

//! Owning handle for a geometry toOGR() hands back.
struct OgrDeleter
{
    void operator()(OGRGeometry *g) const { OGRGeometryFactory::destroyGeometry(g); }
};
using OgrPtr = std::unique_ptr<OGRGeometry, OgrDeleter>;

Ring ringOf(std::initializer_list<QPointF> pts)
{
    Ring r;
    for (const QPointF &p : pts) r.pts.append(p);
    return r;
}

//! A CCW unit-ish square, open (no repeated closing vertex).
Ring square(double x, double y, double w)
{
    return ringOf({QPointF(x, y), QPointF(x + w, y),
                   QPointF(x + w, y + w), QPointF(x, y + w)});
}

FeatureGeometry polygonWith(const Ring &ext, const QVector<Ring> &holes = {})
{
    Part p;
    p.exterior = ext;
    p.holes    = holes;
    FeatureGeometry g(GeometryType::Polygon);
    g.addPart(p);
    return g;
}

} // namespace

// ---------------------------------------------------------------------------
// Ring editing keeps Z aligned
// ---------------------------------------------------------------------------

TEST(FeatureGeometryRing, DensifyInsertsAndKeepsZAligned)
{
    // A single 100-unit segment at spacing 30 needs ceil(100/30) = 4
    // sub-segments, i.e. 3 interior vertices.
    Ring r = ringOf({QPointF(0.0, 0.0), QPointF(100.0, 0.0)});
    r.z = {10.0, 20.0};
    ASSERT_TRUE(r.hasZ());

    EXPECT_EQ(r.densify(30.0), 3);
    EXPECT_EQ(r.pts.size(), 5);
    EXPECT_EQ(r.z.size(), r.pts.size());

    // Endpoints keep their sampled Z; every inserted vertex is NaN, not zero.
    EXPECT_DOUBLE_EQ(r.z.first(), 10.0);
    EXPECT_DOUBLE_EQ(r.z.last(), 20.0);
    for (int i = 1; i < r.z.size() - 1; ++i)
        EXPECT_TRUE(std::isnan(r.z.at(i))) << "inserted Z " << i << " is not NaN";

    // No sub-segment longer than the spacing.
    for (int i = 1; i < r.pts.size(); ++i)
        EXPECT_LE(QLineF(r.pts.at(i - 1), r.pts.at(i)).length(), 30.0 + 1e-9);

    // A 2D ring stays 2D.
    Ring flat = ringOf({QPointF(0.0, 0.0), QPointF(100.0, 0.0)});
    EXPECT_EQ(flat.densify(30.0), 3);
    EXPECT_TRUE(flat.z.isEmpty());

    // Non-positive spacing is a no-op.
    Ring untouched = ringOf({QPointF(0.0, 0.0), QPointF(100.0, 0.0)});
    EXPECT_EQ(untouched.densify(0.0), 0);
    EXPECT_EQ(untouched.pts.size(), 2);
}

TEST(FeatureGeometryRing, NormalizeCCWReversesZToo)
{
    // Clockwise square, each vertex carrying a distinguishable Z.
    Ring r = ringOf({QPointF(0.0, 0.0), QPointF(0.0, 10.0),
                     QPointF(10.0, 10.0), QPointF(10.0, 0.0)});
    r.z = {1.0, 2.0, 3.0, 4.0};
    ASSERT_LT(r.signedArea(), 0.0) << "fixture is not clockwise";

    r.normalizeCCW();
    EXPECT_GT(r.signedArea(), 0.0);

    // Every vertex kept ITS OWN Z through the reversal.
    ASSERT_EQ(r.pts.size(), 4);
    ASSERT_EQ(r.z.size(), 4);
    EXPECT_EQ(r.pts.at(0), QPointF(10.0, 0.0));
    EXPECT_DOUBLE_EQ(r.z.at(0), 4.0);
    EXPECT_EQ(r.pts.at(3), QPointF(0.0, 0.0));
    EXPECT_DOUBLE_EQ(r.z.at(3), 1.0);

    // Already CCW: untouched.
    Ring ccw = square(0.0, 0.0, 5.0);
    ccw.z = {1.0, 2.0, 3.0, 4.0};
    const Ring before = ccw;
    ccw.normalizeCCW();
    EXPECT_EQ(ccw, before);
}

// ---------------------------------------------------------------------------
// OGR round trips
// ---------------------------------------------------------------------------

TEST(FeatureGeometryOgr, PolygonWithHoleRoundTrips)
{
    const FeatureGeometry g =
        polygonWith(square(0.0, 0.0, 100.0), {square(20.0, 20.0, 20.0)});

    const OgrPtr ogr(g.toOGR());
    ASSERT_NE(ogr.get(), nullptr);
    EXPECT_EQ(wkbFlatten(ogr->getGeometryType()), wkbPolygon);

    const auto *poly = static_cast<const OGRPolygon *>(ogr.get());
    ASSERT_NE(poly->getExteriorRing(), nullptr);
    EXPECT_EQ(poly->getNumInteriorRings(), 1);
    // OGR stores rings CLOSED: 5 points for a 4-corner ring.
    EXPECT_EQ(poly->getExteriorRing()->getNumPoints(), 5);

    const FeatureGeometry back = FeatureGeometry::fromOGR(ogr.get());
    EXPECT_EQ(back.type(), GeometryType::Polygon);
    ASSERT_EQ(back.partCount(), 1);
    // The closing vertex was dropped again — rings are stored OPEN.
    EXPECT_EQ(back.parts().at(0).exterior.size(), 4);
    ASSERT_EQ(back.parts().at(0).holes.size(), 1);
    EXPECT_EQ(back.parts().at(0).holes.at(0).size(), 4);
    EXPECT_EQ(back, g);
}

/*! HANDOFF §3.1 R8. qFuzzyCompare is a RELATIVE test and returns false for
 *  (0.0, 0.0), so a ring whose first vertex sits exactly on the origin used to
 *  keep its duplicate closing vertex and grow by one on every save/load cycle. */
TEST(FeatureGeometryOgr, ClosingVertexDroppedEvenAtTheOrigin)
{
    const FeatureGeometry g = polygonWith(square(0.0, 0.0, 10.0));
    ASSERT_EQ(g.parts().at(0).exterior.pts.first(), QPointF(0.0, 0.0));

    const OgrPtr ogr(g.toOGR());
    ASSERT_NE(ogr.get(), nullptr);
    const FeatureGeometry back = FeatureGeometry::fromOGR(ogr.get());
    ASSERT_EQ(back.partCount(), 1);
    EXPECT_EQ(back.parts().at(0).exterior.size(), 4)
        << "closing vertex survived a ring anchored at (0,0)";
    EXPECT_EQ(back, g);

    // And it does not grow across repeated round trips.
    const OgrPtr again(back.toOGR());
    ASSERT_NE(again.get(), nullptr);
    EXPECT_EQ(FeatureGeometry::fromOGR(again.get()), g);
}

TEST(FeatureGeometryOgr, MultiPolygonRoundTrips)
{
    FeatureGeometry g(GeometryType::MultiPolygon);
    Part a;
    a.exterior = square(0.0, 0.0, 10.0);
    Part b;
    b.exterior = square(50.0, 50.0, 10.0);
    b.holes.append(square(52.0, 52.0, 3.0));
    g.addPart(a);
    g.addPart(b);

    const OgrPtr ogr(g.toOGR());
    ASSERT_NE(ogr.get(), nullptr);
    EXPECT_EQ(wkbFlatten(ogr->getGeometryType()), wkbMultiPolygon);
    EXPECT_EQ(static_cast<const OGRMultiPolygon *>(ogr.get())->getNumGeometries(), 2);

    const FeatureGeometry back = FeatureGeometry::fromOGR(ogr.get());
    EXPECT_EQ(back.type(), GeometryType::MultiPolygon);
    ASSERT_EQ(back.partCount(), 2);
    EXPECT_EQ(back.parts().at(1).holes.size(), 1);
    EXPECT_EQ(back, g);
}

TEST(FeatureGeometryOgr, ThreeDLineRoundTripsZ)
{
    // Finite Z only: NaN is written to OGR as 0.0 (HANDOFF §3.1 R7 — there is
    // no NaN convention in GeoPackage), so an unsampled vertex does NOT survive
    // the file boundary as unsampled. That is a documented design decision.
    Ring r = ringOf({QPointF(0.0, 0.0), QPointF(10.0, 0.0), QPointF(10.0, 10.0)});
    r.z = {1.5, -2.25, 300.125};

    FeatureGeometry g(GeometryType::LineString);
    Part p;
    p.exterior = r;
    g.addPart(p);
    ASSERT_TRUE(g.hasZ());

    const OgrPtr ogr(g.toOGR());
    ASSERT_NE(ogr.get(), nullptr);
    EXPECT_TRUE(ogr->Is3D());

    const FeatureGeometry back = FeatureGeometry::fromOGR(ogr.get());
    ASSERT_EQ(back.partCount(), 1);
    ASSERT_TRUE(back.hasZ());
    const Ring &br = back.parts().at(0).exterior;
    ASSERT_EQ(br.z.size(), 3);
    EXPECT_DOUBLE_EQ(br.z.at(0), 1.5);
    EXPECT_DOUBLE_EQ(br.z.at(1), -2.25);
    EXPECT_DOUBLE_EQ(br.z.at(2), 300.125);
    EXPECT_EQ(back, g);

    // ogrTypeFor mirrors the 2.5D flag.
    EXPECT_EQ(FeatureGeometry::ogrTypeFor(GeometryType::LineString, false),
              static_cast<int>(wkbLineString));
    EXPECT_EQ(FeatureGeometry::ogrTypeFor(GeometryType::LineString, true),
              static_cast<int>(wkbLineString25D));
}

// ---------------------------------------------------------------------------
// validate()
// ---------------------------------------------------------------------------

TEST(FeatureGeometryValidate, AcceptsCleanPolygonWithTwoHoles)
{
    const FeatureGeometry g = polygonWith(
        square(0.0, 0.0, 100.0),
        {square(10.0, 10.0, 20.0), square(60.0, 60.0, 20.0)});

    QString reason = QStringLiteral("untouched");
    EXPECT_TRUE(g.validate(GeometryType::Polygon, &reason)) << reason.toStdString();
    EXPECT_TRUE(reason.isEmpty());
}

TEST(FeatureGeometryValidate, RejectsHoleOutsideExterior)
{
    const FeatureGeometry g =
        polygonWith(square(0.0, 0.0, 100.0), {square(200.0, 200.0, 10.0)});

    QString reason;
    EXPECT_FALSE(g.validate(GeometryType::Polygon, &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("not inside")))
        << reason.toStdString();
}

TEST(FeatureGeometryValidate, RejectsSelfIntersectingRing)
{
    // Bow tie.
    const FeatureGeometry g = polygonWith(
        ringOf({QPointF(0.0, 0.0), QPointF(10.0, 10.0),
                QPointF(10.0, 0.0), QPointF(0.0, 10.0)}));

    QString reason;
    EXPECT_FALSE(g.validate(GeometryType::Polygon, &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("self-intersects")))
        << reason.toStdString();
}

TEST(FeatureGeometryValidate, RejectsOverlappingHoles)
{
    const FeatureGeometry g = polygonWith(
        square(0.0, 0.0, 100.0),
        {square(10.0, 10.0, 30.0), square(20.0, 20.0, 30.0)});

    QString reason;
    EXPECT_FALSE(g.validate(GeometryType::Polygon, &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("overlap"))) << reason.toStdString();
}

/*! validate() is a STRUCTURAL check against the LAYER's declared type — it
 *  never consults the geometry's own type(). That is deliberate: the drawing
 *  tools build a geometry with the target layer's type, so the question worth
 *  asking is "is this shape admissible in a layer of that type", not "do two
 *  enums match". These are the structural rejections it does make. */
TEST(FeatureGeometryValidate, RejectsEmptyMultiPartAndStrayHoles)
{
    QString reason;

    // No parts at all.
    const FeatureGeometry empty(GeometryType::Polygon);
    EXPECT_FALSE(empty.validate(GeometryType::Polygon, &reason));
    EXPECT_FALSE(reason.isEmpty());

    // Two parts in a single-part layer; the same geometry is fine as a multi.
    FeatureGeometry two(GeometryType::Polygon);
    Part a;
    a.exterior = square(0.0, 0.0, 10.0);
    Part b;
    b.exterior = square(50.0, 50.0, 10.0);
    two.addPart(a);
    two.addPart(b);
    EXPECT_FALSE(two.validate(GeometryType::Polygon, &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("exactly one part"))) << reason.toStdString();
    EXPECT_TRUE(two.validate(GeometryType::MultiPolygon, &reason)) << reason.toStdString();

    // Holes are polygon-only.
    FeatureGeometry line(GeometryType::LineString);
    Part l;
    l.exterior = square(0.0, 0.0, 10.0);
    l.holes.append(square(2.0, 2.0, 2.0));
    line.addPart(l);
    EXPECT_FALSE(line.validate(GeometryType::LineString, &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("Only polygons"))) << reason.toStdString();

    // Too few vertices for the declared type.
    FeatureGeometry stub(GeometryType::Polygon);
    Part s;
    s.exterior = ringOf({QPointF(0.0, 0.0), QPointF(1.0, 0.0)});
    stub.addPart(s);
    EXPECT_FALSE(stub.validate(GeometryType::Polygon, &reason));
    EXPECT_TRUE(reason.contains(QStringLiteral("at least 3"))) << reason.toStdString();
    // …but two vertices are enough for a line.
    EXPECT_TRUE(stub.validate(GeometryType::LineString, &reason)) << reason.toStdString();

    // A layer with no geometry type accepts nothing.
    EXPECT_FALSE(polygonWith(square(0.0, 0.0, 10.0)).validate(GeometryType::None, &reason));
}

// ---------------------------------------------------------------------------
// Z bookkeeping
// ---------------------------------------------------------------------------

TEST(FeatureGeometryZ, UnsampledZCountCountsNaNOnly)
{
    Ring r = ringOf({QPointF(0.0, 0.0), QPointF(10.0, 0.0),
                     QPointF(10.0, 10.0), QPointF(0.0, 10.0)});
    r.z = {1.0, kNaN, 3.0, kNaN};
    FeatureGeometry g = polygonWith(r);

    EXPECT_EQ(g.unsampledZCount(), 2);
    EXPECT_EQ(g.vertexCount(), 4);

    double lo = 0.0, hi = 0.0;
    ASSERT_TRUE(g.zRange(lo, hi));           // NaN entries are skipped
    EXPECT_DOUBLE_EQ(lo, 1.0);
    EXPECT_DOUBLE_EQ(hi, 3.0);

    // A 2D geometry has no unsampled vertices — it has no Z at all.
    FeatureGeometry flat = polygonWith(square(0.0, 0.0, 10.0));
    EXPECT_FALSE(flat.hasZ());
    EXPECT_EQ(flat.unsampledZCount(), 0);
    double a = 0.0, b = 0.0;
    EXPECT_FALSE(flat.zRange(a, b));

    // setHasZ(true) makes every vertex unsampled, not zero.
    flat.setHasZ(true);
    EXPECT_TRUE(flat.hasZ());
    EXPECT_EQ(flat.unsampledZCount(), 4);
}
