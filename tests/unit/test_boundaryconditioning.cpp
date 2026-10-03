// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * \file   test_boundaryconditioning.cpp
 * \brief  Footprint conditioning (MESH_REGIONAL_TRIQUAD_PLAN D-R3): holes and
 *         gaps narrower than the minimum cell size go, redundant vertices go,
 *         square corners stay square. SWMMVIS_BOUNDARY_CONDITION_FILE=<shp>
 *         additionally times a real boundary.
 */
#include <gtest/gtest.h>

#include "mesh/boundaryconditioning.h"

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <gdal_priv.h>
#include <ogr_geometry.h>
#include <ogrsf_frmts.h>

#include <cmath>
#include <memory>

using namespace mesh;

namespace {

std::unique_ptr<OGRGeometry> wkt(const char *text)
{
    OGRGeometry *g = nullptr;
    EXPECT_EQ(OGRGeometryFactory::createFromWkt(text, nullptr, &g), OGRERR_NONE);
    return std::unique_ptr<OGRGeometry>(g);
}

std::unique_ptr<OGRGeometry> condition(const OGRGeometry *g, double gap, BoundaryConditionReport *r)
{
    return std::unique_ptr<OGRGeometry>(conditionMeshRegion(g, gap, 0.25 * gap, r));
}

/*! Holes as (minX, minY, maxX, maxY) envelopes, for assertions. */
std::vector<OGREnvelope> holeEnvelopes(const OGRGeometry *g)
{
    std::vector<OGREnvelope> out;
    const OGRPolygon *poly = g->toPolygon();
    for (int i = 0; i < poly->getNumInteriorRings(); ++i) {
        OGREnvelope e; poly->getInteriorRing(i)->getEnvelope(&e); out.push_back(e);
    }
    return out;
}

} // namespace

TEST(BoundaryConditioning, NarrowHolesFillAndCloseHolesMerge)
{
    // 100 x 100 domain. A 1 x 1 hole (narrower than gap 2) must fill; two
    // 10 x 10 holes 1 apart must merge into one; a lone 10 x 10 hole stays.
    auto g = wkt("POLYGON((0 0,100 0,100 100,0 100,0 0),"
                 "(20 20,21 20,21 21,20 21,20 20),"
                 "(40 40,50 40,50 50,40 50,40 40),"
                 "(51 40,61 40,61 50,51 50,51 40),"
                 "(70 70,80 70,80 80,70 80,70 70))");
    BoundaryConditionReport r;
    auto out = condition(g.get(), 2.0, &r);
    ASSERT_TRUE(out);
    ASSERT_EQ(wkbFlatten(out->getGeometryType()), wkbPolygon);
    EXPECT_EQ(r.holesIn, 4);
    EXPECT_EQ(r.holesOut, 2);
    const auto holes = holeEnvelopes(out.get());
    bool merged = false, lone = false;
    for (const auto &e : holes) {
        merged = merged || (std::abs(e.MinX - 40) < 1e-9 && std::abs(e.MaxX - 61) < 1e-9);
        lone = lone || (std::abs(e.MinX - 70) < 1e-9 && std::abs(e.MaxX - 80) < 1e-9
                        && std::abs(e.MinY - 70) < 1e-9 && std::abs(e.MaxY - 80) < 1e-9);
    }
    EXPECT_TRUE(merged);
    EXPECT_TRUE(lone);   // a wide hole keeps its exact square shape
}

TEST(BoundaryConditioning, GapToTheBoundaryClosesIntoANotch)
{
    // A building 0.5 from the outer edge (gap 2): the sliver between them is
    // removed, so the building becomes a notch in the boundary, not a hole.
    auto g = wkt("POLYGON((0 0,100 0,100 100,0 100,0 0),(0.5 40,10 40,10 50,0.5 50,0.5 40))");
    BoundaryConditionReport r;
    auto out = condition(g.get(), 2.0, &r);
    ASSERT_TRUE(out);
    EXPECT_EQ(r.holesOut, 0);
    // Only the 0.5 x 10 sliver leaves the meshable area: 10000 - (10 x 10).
    EXPECT_NEAR(r.areaIn, 10000.0 - 9.5 * 10.0, 1e-9);
    EXPECT_NEAR(r.areaOut, 10000.0 - 10.0 * 10.0, 1e-6);
}

TEST(BoundaryConditioning, ThinProtrusionsAndRedundantVerticesGo)
{
    // A 20 x 20 building with a 0.5-wide, 6-long spike and extra collinear
    // vertices along its edges: the spike is cut back and the collinear
    // vertices dropped, leaving the four square corners.
    auto g = wkt("POLYGON((0 0,100 0,100 100,0 100,0 0),"
                 "(40 40,45 40,50 40,55 40,60 40,60 50,60 60,50 60,50.25 60,50.25 66,49.75 66,49.75 60,40 60,40 50,40 40))");
    BoundaryConditionReport r;
    auto out = condition(g.get(), 2.0, &r);
    ASSERT_TRUE(out);
    ASSERT_EQ(r.holesOut, 1);
    const OGRLinearRing *hole = out->toPolygon()->getInteriorRing(0);
    EXPECT_EQ(hole->getNumPoints(), 5);   // closed square: 4 corners + repeat
    OGREnvelope e; hole->getEnvelope(&e);
    EXPECT_NEAR(e.MinX, 40, 1e-9); EXPECT_NEAR(e.MaxX, 60, 1e-9);
    EXPECT_NEAR(e.MinY, 40, 1e-9); EXPECT_NEAR(e.MaxY, 60, 1e-9);
}

TEST(BoundaryConditioning, NonPositiveGapOrNonPolygonIsRefused)
{
    auto g = wkt("POLYGON((0 0,1 0,1 1,0 1,0 0))");
    EXPECT_EQ(conditionMeshRegion(g.get(), 0.0, 0.0), nullptr);
    auto line = wkt("LINESTRING(0 0,1 1)");
    EXPECT_EQ(conditionMeshRegion(line.get(), 1.0, 0.25), nullptr);
}

// Opt-in: SWMMVIS_BOUNDARY_CONDITION_FILE=<polygon file>,
// SWMMVIS_BOUNDARY_CONDITION_GAP=<map units>. Dissolves the layer, conditions
// it and prints the report.
TEST(BoundaryConditioning, RealBoundaryTiming)
{
    const QString path = qEnvironmentVariable("SWMMVIS_BOUNDARY_CONDITION_FILE");
    if (path.isEmpty()) GTEST_SKIP() << "opt-in";
    const double gap = qEnvironmentVariableIsSet("SWMMVIS_BOUNDARY_CONDITION_GAP")
        ? qEnvironmentVariable("SWMMVIS_BOUNDARY_CONDITION_GAP").toDouble() : 6.56;
    GDALAllRegister();
    std::unique_ptr<GDALDataset> ds(GDALDataset::Open(path.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY));
    ASSERT_TRUE(ds);
    OGRMultiPolygon mp;
    for (auto &f : *ds->GetLayer(0)) {
        const OGRGeometry *geom = f->GetGeometryRef();
        if (!geom) continue;
        if (wkbFlatten(geom->getGeometryType()) == wkbPolygon) mp.addGeometry(geom);
        else if (wkbFlatten(geom->getGeometryType()) == wkbMultiPolygon)
            for (const OGRPolygon *p : *geom->toMultiPolygon()) mp.addGeometry(p);
    }
    QElapsedTimer clock; clock.start();
    std::unique_ptr<OGRGeometry> dissolved(mp.UnaryUnion());
    ASSERT_TRUE(dissolved);
    const qint64 unionMs = clock.elapsed();
    BoundaryConditionReport r;
    auto out = condition(dissolved.get(), gap, &r);
    ASSERT_TRUE(out);
    std::printf("tiles %d\n", r.tiles);
    std::printf("union %lld ms; conditioning %lld ms at gap %g: polygons %d -> %d, holes %d -> %d, "
                "vertices %lld -> %lld, edges < gap %lld -> %lld, area %.1f -> %.1f\n",
                (long long)unionMs, (long long)r.milliseconds, gap, r.polygonsIn, r.polygonsOut,
                r.holesIn, r.holesOut, (long long)r.verticesIn, (long long)r.verticesOut,
                (long long)r.shortEdgesIn, (long long)r.shortEdgesOut, r.areaIn, r.areaOut);
    EXPECT_TRUE(out->IsValid());
    EXPECT_LE(r.holesOut, r.holesIn);
    // Optional probe: SWMMVIS_BOUNDARY_CONDITION_PROBE="x,y,radius" prints
    // every conditioned ring vertex near a point (diagnostics).
    // SWMMVIS_BOUNDARY_CONDITION_SEEDS="x1,y1;x2,y2": which ring holds each point.
    for (const QString &pt : qEnvironmentVariable("SWMMVIS_BOUNDARY_CONDITION_SEEDS").split(';', Qt::SkipEmptyParts)) {
        const QStringList xy = pt.split(',');
        OGRPoint q(xy[0].toDouble(), xy[1].toDouble());
        OGRMultiPolygon polys;
        if (wkbFlatten(out->getGeometryType()) == wkbPolygon) polys.addGeometry(out.get());
        else for (const OGRPolygon *p : *out->toMultiPolygon()) polys.addGeometry(p);
        QString where = QStringLiteral("outside every polygon");
        int pi = 0;
        for (const OGRPolygon *poly : polys) {
            OGRPolygon shell; shell.addRing(poly->getExteriorRing());
            if (shell.Contains(&q)) {
                where = QStringLiteral("inside polygon %1 (meshed area)").arg(pi);
                for (int h = 0; h < poly->getNumInteriorRings(); ++h) {
                    OGRPolygon hole; hole.addRing(poly->getInteriorRing(h));
                    if (hole.Contains(&q)) {
                        where = QStringLiteral("inside polygon %1 hole %2 (%3 vertices, area %4)").arg(pi).arg(h)
                                    .arg(poly->getInteriorRing(h)->getNumPoints()).arg(hole.get_Area(), 0, 'f', 0);
                        break;
                    }
                }
            }
            ++pi;
        }
        std::printf("seed %s: %s\n", qPrintable(pt), qPrintable(where));
    }
    const QStringList probe = qEnvironmentVariable("SWMMVIS_BOUNDARY_CONDITION_PROBE").split(',');
    if (probe.size() == 3) {
        const double px = probe[0].toDouble(), py = probe[1].toDouble(), pr = probe[2].toDouble();
        OGRMultiPolygon polys;
        if (wkbFlatten(out->getGeometryType()) == wkbPolygon) polys.addGeometry(out.get());
        else for (const OGRPolygon *p : *out->toMultiPolygon()) polys.addGeometry(p);
        int pi = 0;
        for (const OGRPolygon *poly : polys) {
            for (int ri = -1; ri < poly->getNumInteriorRings(); ++ri) {
                const OGRLinearRing *ring = ri < 0 ? poly->getExteriorRing() : poly->getInteriorRing(ri);
                for (int i = 0; i < ring->getNumPoints(); ++i)
                    if (std::hypot(ring->getX(i) - px, ring->getY(i) - py) < pr)
                        std::printf("poly %d ring %d vertex %d: %.4f %.4f\n", pi, ri, i, ring->getX(i), ring->getY(i));
            }
            ++pi;
        }
    }
}
