/*!
 * \file   boundaryconditioning.cpp
 * \license GPL-3.0-or-later
 */
#include "mesh/boundaryconditioning.h"

#include <QElapsedTimer>
#include <ogr_geometry.h>

#include <cmath>
#include <memory>

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

OGRPolygon *collapsePolygon(const OGRPolygon *poly, double minEdge)
{
    auto out = std::make_unique<OGRPolygon>();
    auto ext = std::unique_ptr<OGRLinearRing>(poly->getExteriorRing()->clone());
    if (!collapseShortEdges(ext.get(), minEdge)) return nullptr;
    out->addRingDirectly(ext.release());
    for (int i = 0; i < poly->getNumInteriorRings(); ++i) {
        auto hole = std::unique_ptr<OGRLinearRing>(poly->getInteriorRing(i)->clone());
        if (collapseShortEdges(hole.get(), minEdge)) out->addRingDirectly(hole.release());
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

OGRGeometry *conditionMeshRegion(const OGRGeometry *region, double gap,
                                 double simplifyTolerance, BoundaryConditionReport *report)
{
    if (!region || !(gap > 0.0) || !polygonal(region)) return nullptr;
    QElapsedTimer clock;
    clock.start();
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

    OGRGeometry *result = opened.release();
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
    report && (report->buffersMs = clock.elapsed());
    if (OGRGeometry *collapsed = collapseAll(result, gap)) {
        report && (report->collapseMs = clock.elapsed());
        if (!collapsed->IsValid()) {
            report && (report->repaired = true);
            OGRGeometry *valid = collapsed->MakeValid();
            OGRGeometryFactory::destroyGeometry(collapsed);
            collapsed = valid && polygonal(valid) ? valid : (valid ? (OGRGeometryFactory::destroyGeometry(valid), nullptr) : nullptr);
        }
        if (collapsed) { OGRGeometryFactory::destroyGeometry(result); result = collapsed; }
    }
    if (report) {
        measurePolygonal(region, &report->polygonsIn, &report->holesIn, &report->verticesIn, &report->areaIn,
                         gap, &report->shortEdgesIn);
        measurePolygonal(result, &report->polygonsOut, &report->holesOut, &report->verticesOut, &report->areaOut,
                         gap, &report->shortEdgesOut);
        report->milliseconds = clock.elapsed();
    }
    return result;
}

} // namespace mesh
