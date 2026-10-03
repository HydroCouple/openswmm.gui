/*!
 * \file   boundaryconditioning.cpp
 * \license GPL-3.0-or-later
 */
#include "mesh/boundaryconditioning.h"

#include <QElapsedTimer>
#include <QVector>
#include <QtConcurrent/QtConcurrentMap>
#include <ogr_geometry.h>

#include <cmath>
#include <algorithm>
#include <memory>
#include <numeric>

namespace mesh {

namespace {

using GeomPtr = std::unique_ptr<OGRGeometry, void (*)(OGRGeometry *)>;
GeomPtr own(OGRGeometry *g) { return GeomPtr(g, [](OGRGeometry *p) { OGRGeometryFactory::destroyGeometry(p); }); }

OGRGeometry *mitredBuffer(const OGRGeometry *g, double distance)
{
    // A mitre limit of 2 keeps 90-degree corners square (ratio sqrt(2)) and
    // bevels only acute spikes.
    const char *const options[] = {"JOIN_STYLE=MITRE", "MITRE_LIMIT=2", "QUADRANT_SEGMENTS=2", nullptr};
    return g->BufferEx(distance, const_cast<char **>(options));
}

bool polygonal(const OGRGeometry *g)
{
    const auto t = wkbFlatten(g->getGeometryType());
    return t == wkbPolygon || t == wkbMultiPolygon;
}

// Drop ring vertices closer than minEdge to the previous kept vertex (the
// short bevel and junction segments buffering leaves behind). A ring left
// with fewer than three distinct points is reported as collapsed.
bool collapseShortEdges(OGRLinearRing *ring, double minEdge)
{
    const int n = ring->getNumPoints();
    if (n < 4) return false;
    std::vector<OGRRawPoint> kept;
    kept.reserve(n);
    kept.push_back({ring->getX(0), ring->getY(0)});
    for (int i = 1; i < n - 1; ++i) {
        const OGRRawPoint q{ring->getX(i), ring->getY(i)};
        if (std::hypot(q.x - kept.back().x, q.y - kept.back().y) >= minEdge) kept.push_back(q);
    }
    // The closing edge back to the first vertex obeys the same rule.
    while (kept.size() > 3 && std::hypot(kept.back().x - kept.front().x, kept.back().y - kept.front().y) < minEdge)
        kept.pop_back();
    if (kept.size() < 3) return false;
    kept.push_back(kept.front());
    ring->setPoints(int(kept.size()), kept.data());
    return true;
}

// A ring is simple when the polygon it alone bounds is valid.
bool simpleRing(const OGRLinearRing *ring)
{
    OGRPolygon alone;
    alone.addRing(ring);
    return alone.IsValid();
}

// Collapse each ring on its own, keeping the original ring wherever the
// collapsed one is not simple. After opening, rings are at least a gap
// apart, so a ring that stays simple cannot reach its neighbours.
OGRPolygon *collapsePolygon(const OGRPolygon *poly, double minEdge)
{
    auto out = std::make_unique<OGRPolygon>();
    auto ext = std::unique_ptr<OGRLinearRing>(poly->getExteriorRing()->clone());
    if (!collapseShortEdges(ext.get(), minEdge)) return nullptr;
    if (!simpleRing(ext.get())) ext.reset(poly->getExteriorRing()->clone());
    out->addRingDirectly(ext.release());
    for (int i = 0; i < poly->getNumInteriorRings(); ++i) {
        auto hole = std::unique_ptr<OGRLinearRing>(poly->getInteriorRing(i)->clone());
        if (!collapseShortEdges(hole.get(), minEdge)) continue;   // collapsed to nothing: dropped
        if (!simpleRing(hole.get())) hole.reset(poly->getInteriorRing(i)->clone());
        out->addRingDirectly(hole.release());
    }
    return out.release();
}

OGRGeometry *collapseAll(const OGRGeometry *g, double minEdge)
{
    if (wkbFlatten(g->getGeometryType()) == wkbPolygon) return collapsePolygon(g->toPolygon(), minEdge);
    auto out = std::make_unique<OGRMultiPolygon>();
    for (const OGRPolygon *poly : *g->toMultiPolygon())
        if (OGRPolygon *c = collapsePolygon(poly, minEdge)) out->addGeometryDirectly(c);
    return out->IsEmpty() ? nullptr : out.release();
}

} // namespace

void measurePolygonal(const OGRGeometry *g, int *polygons, int *holes, qint64 *vertices, double *area,
                      double shortEdge, qint64 *shortEdges)
{
    int p = 0, h = 0; qint64 v = 0, s = 0; double a = 0.0;
    auto ring = [&](const OGRLinearRing *r) {
        if (!r) return;
        v += r->getNumPoints();
        for (int i = 1; i < r->getNumPoints(); ++i)
            if (std::hypot(r->getX(i) - r->getX(i - 1), r->getY(i) - r->getY(i - 1)) < shortEdge) ++s;
    };
    auto onePolygon = [&](const OGRPolygon *poly) {
        if (!poly || poly->IsEmpty()) return;
        ++p;
        a += poly->get_Area();
        ring(poly->getExteriorRing());
        for (int i = 0; i < poly->getNumInteriorRings(); ++i) {
            ++h;
            ring(poly->getInteriorRing(i));
        }
    };
    if (g) {
        const auto t = wkbFlatten(g->getGeometryType());
        if (t == wkbPolygon) onePolygon(g->toPolygon());
        else if (t == wkbMultiPolygon)
            for (const OGRPolygon *poly : *g->toMultiPolygon()) onePolygon(poly);
    }
    if (polygons) *polygons = p;
    if (holes) *holes = h;
    if (vertices) *vertices = v;
    if (area) *area = a;
    if (shortEdges) *shortEdges = s;
}

namespace {

// Closing then opening at gap/2: exact and local (reach about 2 gaps), so
// it can run on tiles with a margin and agree across their seams.
OGRGeometry *morphology(const OGRGeometry *region, double gap)
{
    const double r = 0.5 * gap;
    // Closing: thin holes and building protrusions disappear.
    GeomPtr grown = own(mitredBuffer(region, r));
    if (!grown) return nullptr;
    GeomPtr closed = own(mitredBuffer(grown.get(), -r));
    grown.reset();
    if (!closed) return nullptr;
    // Opening: narrow passages between obstacles close up.
    GeomPtr shrunk = own(mitredBuffer(closed.get(), -r));
    closed.reset();
    if (!shrunk) return nullptr;
    GeomPtr opened = own(mitredBuffer(shrunk.get(), r));
    shrunk.reset();
    if (!opened || opened->IsEmpty() || !polygonal(opened.get())) return nullptr;
    return opened.release();
}

// Simplify and collapse short edges: ring-wide, so run once on the whole
// region (tiles would disagree at their seams). Takes ownership of result.
OGRGeometry *finish(OGRGeometry *result, double gap, double simplifyTolerance)
{
    if (simplifyTolerance > 0.0) {
        if (OGRGeometry *simple = result->SimplifyPreserveTopology(simplifyTolerance);
            simple && !simple->IsEmpty() && polygonal(simple)) {
            OGRGeometryFactory::destroyGeometry(result);
            result = simple;
        } else if (simple) {
            OGRGeometryFactory::destroyGeometry(simple);
        }
    }
    // Buffering leaves short bevel and junction segments; drop them so the
    // conditioned outline does not force the small cells it set out to remove.
    if (OGRGeometry *collapsed = collapseAll(result, gap)) { OGRGeometryFactory::destroyGeometry(result); result = collapsed; }
    return result;
}

OGRPolygon rectangle(double x0, double y0, double x1, double y1)
{
    OGRLinearRing ring;
    ring.addPoint(x0, y0); ring.addPoint(x1, y0); ring.addPoint(x1, y1); ring.addPoint(x0, y1); ring.addPoint(x0, y0);
    OGRPolygon poly;
    poly.addRing(&ring);
    return poly;
}

void appendPolygons(const OGRGeometry *g, OGRMultiPolygon *out)
{
    if (!g) return;
    const auto t = wkbFlatten(g->getGeometryType());
    if (t == wkbPolygon) out->addGeometry(g);
    else if (t == wkbMultiPolygon || t == wkbGeometryCollection)
        for (const OGRGeometry *part : *g->toGeometryCollection()) appendPolygons(part, out);
}

} // namespace

OGRGeometry *conditionMeshRegion(const OGRGeometry *region, double gap,
                                 double simplifyTolerance, BoundaryConditionReport *report)
{
    if (!region || !(gap > 0.0) || !polygonal(region)) return nullptr;
    QElapsedTimer clock;
    clock.start();
    int polygonsIn = 0, holesIn = 0; qint64 verticesIn = 0; double areaIn = 0;
    measurePolygonal(region, &polygonsIn, &holesIn, &verticesIn, &areaIn);

    // The operations reach at most about 2 gaps, so a large region is cut
    // into tiles with a 3-gap margin, conditioned in parallel, clipped back
    // to each tile's core and unioned. Tiles are gathered in order, so the
    // result does not depend on thread timing.
    const int perSide = std::clamp(int(std::ceil(std::sqrt(double(verticesIn) / 20000.0))), 1, 16);
    OGRGeometry *result = nullptr;
    if (perSide == 1) {
        result = morphology(region, gap);
    } else {
        OGREnvelope env;
        region->getEnvelope(&env);
        const double w = (env.MaxX - env.MinX) / perSide, h = (env.MaxY - env.MinY) / perSide;
        const double margin = 3.0 * gap;
        const double precision = 1e-4 * gap;
        QVector<int> tiles(perSide * perSide);
        std::iota(tiles.begin(), tiles.end(), 0);
        QVector<OGRGeometry *> parts(tiles.size(), nullptr);
        QVector<char> failed(tiles.size(), 0);
        QtConcurrent::blockingMap(tiles, [&](int i) {
            const int ix = i % perSide, iy = i / perSide;
            const double x0 = env.MinX + ix * w, y0 = env.MinY + iy * h;
            // Outer tiles extend past the envelope so nothing falls between.
            const double cx0 = ix == 0 ? x0 - margin : x0, cy0 = iy == 0 ? y0 - margin : y0;
            const double cx1 = ix == perSide - 1 ? x0 + w + margin : x0 + w;
            const double cy1 = iy == perSide - 1 ? y0 + h + margin : y0 + h;
            const OGRPolygon outer = rectangle(cx0 - margin, cy0 - margin, cx1 + margin, cy1 + margin);
            GeomPtr piece = own(region->Intersection(&outer));
            if (!piece || piece->IsEmpty()) return;
            OGRMultiPolygon polys;
            appendPolygons(piece.get(), &polys);
            if (polys.IsEmpty()) return;
            GeomPtr conditioned = own(morphology(&polys, gap));
            if (!conditioned) { failed[i] = 1; return; }
            const OGRPolygon core = rectangle(cx0, cy0, cx1, cy1);
            GeomPtr clipped = own(conditioned->Intersection(&core));
            // Neighbouring tiles buffer different inputs, so their seam
            // vertices agree only to rounding. Snapping every piece to one
            // grid makes them coincide, so the union fuses the seam instead
            // of leaving near-duplicate rings that cross in the mesh.
            if (clipped) parts[i] = clipped->SetPrecision(precision, 0);
            if (clipped && !parts[i]) failed[i] = 1;
        });
        OGRMultiPolygon all;
        bool ok = !failed.contains(1);
        for (OGRGeometry *p : std::as_const(parts)) { appendPolygons(p, &all); OGRGeometryFactory::destroyGeometry(p); }
        if (ok && !all.IsEmpty()) {
            OGRGeometry *joined = all.UnaryUnion();
            if (joined && polygonal(joined) && !joined->IsEmpty()) result = joined;
            else if (joined) OGRGeometryFactory::destroyGeometry(joined);
        }
    }
    if (!result) return nullptr;
    result = finish(result, gap, simplifyTolerance);
    if (!result) return nullptr;
    if (report) {
        report->polygonsIn = polygonsIn; report->holesIn = holesIn;
        report->verticesIn = verticesIn; report->areaIn = areaIn;
        measurePolygonal(region, nullptr, nullptr, nullptr, nullptr, gap, &report->shortEdgesIn);
        measurePolygonal(result, &report->polygonsOut, &report->holesOut, &report->verticesOut, &report->areaOut,
                         gap, &report->shortEdgesOut);
        report->tiles = perSide * perSide;
        report->milliseconds = clock.elapsed();
    }
    return result;
}

} // namespace mesh
