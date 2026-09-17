/*!
 * \file   conduitsplitpick.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/tools/conduitsplitpick.h"

#include "core/editgeometry.h"
#include "core/preferencesmanager.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "ui/properties/xsectshapegeom.h"   // kXsectStreetId

#include <openswmm/engine/openswmm_links.h>

#include <algorithm>
#include <cmath>

namespace ConduitSplitPick
{

ConduitHit pickConduit(const MapCanvas *canvas, const QPoint &pixel)
{
    ConduitHit h;
    if (!canvas) return h;

    double mx = 0.0, my = 0.0;
    canvas->toMapCoords(pixel.x(), pixel.y(), mx, my);

    // 12-pixel pick tolerance in map units at the current zoom (same as the
    // vertex editor's link pick).
    double mx2 = 0.0, my2 = 0.0;
    canvas->toMapCoords(pixel.x() + 12, pixel.y() + 12, mx2, my2);
    const double tol = std::max(std::abs(mx2 - mx), std::abs(my2 - my));

    for (OpenSWMMVisLayer *l : canvas->layers()) {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;

        const auto r = sl->pickAt(mx, my, tol);
        if (!r.valid || r.cat != SWMMModelLayer::CatConduits) continue;

        // Closest point + normalized arclength position on the vertex-aware
        // polyline (layer CRS).
        const QVector<QPointF> poly = sl->cachedLinkPolyline(r.soaIndex);
        if (poly.size() < 2) continue;

        double px = mx, py = my;
        sl->transformCanvasToLayer(mx, my, px, py);

        int seg = -1;
        QPointF closest;
        EditGeometry::distanceToPolyline(poly, QPointF(px, py), &seg, &closest);
        if (seg < 0) continue;

        double total = 0.0, upto = 0.0;
        for (int s = 0; s + 1 < poly.size(); ++s) {
            const double len = std::hypot(poly[s + 1].x() - poly[s].x(),
                                          poly[s + 1].y() - poly[s].y());
            if (s < seg) upto += len;
            else if (s == seg)
                upto += std::hypot(closest.x() - poly[s].x(),
                                   closest.y() - poly[s].y());
            total += len;
        }
        if (total <= 0.0) continue;

        h.layer   = sl;
        h.linkIdx = r.soaIndex;
        h.name    = r.name;
        // Keep the break away from the ends: a sliver conduit is numerically
        // useless and the engine rejects t outside (0,1) anyway.
        h.t       = std::clamp(upto / total, 0.02, 0.98);
        h.point   = closest;

        // Cross section — the same accessor the section builders read
        // (swmm_link_get_xsect; shape codes in ui/properties/xsectshapegeom.h).
        if (SWMM_Engine eng = sl->engine()) {
            int shape = -1;
            double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
            if (swmm_link_get_xsect(eng, r.soaIndex, &shape, &g1, &g2, &g3, &g4)
                    == SWMM_OK)
                h.isStreet = (shape == openswmmvis::kXsectStreetId);
        }
        return h;
    }
    return h;
}

QString nextNodeName(const SWMMModelLayer *layer, const QString &elementKind)
{
    const QString prefix = PreferencesManager::instance()->elementNamePrefix(elementKind);
    if (!layer) return prefix + QStringLiteral("1");
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = prefix + QString::number(n);
        if (layer->nodeIndex(candidate) < 0)
            return candidate;
    }
    return prefix + QStringLiteral("_X");
}

QString nextSplitLinkName(const SWMMModelLayer *layer, const QString &baseName)
{
    if (!layer) return baseName + QStringLiteral("_B");
    for (int n = 0; n < 100000; ++n) {
        const QString candidate = (n == 0)
            ? baseName + QStringLiteral("_B")
            : baseName + QStringLiteral("_B") + QString::number(n);
        if (layer->linkIndex(candidate) < 0)
            return candidate;
    }
    return baseName + QStringLiteral("_BX");
}

} // namespace ConduitSplitPick
