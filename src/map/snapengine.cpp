/*!
 * \file   snapengine.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/snapengine.h"
#include "map/tools/maptool.h"
#include "layers/swmmmodellayer.h"
#include "core/preferencesmanager.h"

#include <QPainter>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace {

inline double dist2(double ax, double ay, double bx, double by)
{
    const double dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

} // namespace

// ---------------------------------------------------------------------------

SnapEngine::Result SnapEngine::snap(const OpenSWMMVisMapTool *tool,
                                     SWMMModelLayer           *layer,
                                     double mapX, double mapY)
{
    Result out;
    out.x = mapX;
    out.y = mapY;

    auto *prefs = PreferencesManager::instance();
    if (!prefs->snapEnabled() || !tool || !layer)
        return out;

    // Convert pixel tolerance to map units (same pattern as snapToNode()).
    const int tolPx = prefs->snapTolerancePx();
    double mx2, my2, mx0, my0;
    tool->toMapCoords(tolPx, tolPx, mx2, my2);
    tool->toMapCoords(0, 0, mx0, my0);
    const double tol  = std::max(std::abs(mx2 - mx0), std::abs(my2 - my0));
    const double tol2 = tol * tol;

    double bestD2 = tol2 + 1.0; // sentinel: anything > tol2 means no winner yet

    // ── 1. Nodes + Gages (KD-tree path via identifyAt — fastest) ─────────
    {
        const QVariantMap hit = layer->identifyAt(mapX, mapY, nullptr, tol);
        const QString type = hit.value(QStringLiteral("elementType")).toString();
        if (type == QStringLiteral("Node")) {
            const QString name = hit.value(QStringLiteral("elementName")).toString();
            double nx = mapX, ny = mapY;
            const int idx = layer->nodeIndex(name);
            if (idx >= 0) layer->cachedNodeCoord(idx, &nx, &ny);
            const double d2 = dist2(mapX, mapY, nx, ny);
            if (d2 <= tol2 && d2 < bestD2) {
                bestD2 = d2;
                out = {true, nx, ny, Kind::Node, name};
            }
        } else if (type == QStringLiteral("RainGage")) {
            const QString name = hit.value(QStringLiteral("elementName")).toString();
            const double gx = hit.value(QStringLiteral("x"), mapX).toDouble();
            const double gy = hit.value(QStringLiteral("y"), mapY).toDouble();
            const double d2 = dist2(mapX, mapY, gx, gy);
            if (d2 <= tol2 && d2 < bestD2) {
                bestD2 = d2;
                out = {true, gx, gy, Kind::Gage, name};
            }
        }
    }

    if (!prefs->snapToVertices())
        return out;

    // ── 2. Link polyline vertices (bbox pre-filter then vertex scan) ──────
    {
        const QStringList links = layer->linksInRect(mapX - tol, mapY - tol,
                                                      mapX + tol, mapY + tol);
        for (const QString &name : links) {
            const int idx = layer->linkIndex(name);
            if (idx < 0) continue;
            for (const QPointF &v : layer->cachedLinkPolyline(idx)) {
                const double d2 = dist2(mapX, mapY, v.x(), v.y());
                if (d2 <= tol2 && d2 < bestD2) {
                    bestD2 = d2;
                    out = {true, v.x(), v.y(), Kind::LinkVertex, name};
                }
            }
        }
    }

    // ── 3. Subcatchment polygon vertices (full scan — count is small) ─────
    {
        const int n = layer->cachedSubcatchCount();
        for (int idx = 0; idx < n; ++idx) {
            for (const QPointF &v : layer->cachedSubcatchVertices(idx)) {
                const double d2 = dist2(mapX, mapY, v.x(), v.y());
                if (d2 <= tol2 && d2 < bestD2) {
                    bestD2 = d2;
                    out = {true, v.x(), v.y(), Kind::SubcatchVertex, {}};
                }
            }
        }
    }

    return out;
}

// ---------------------------------------------------------------------------

void SnapEngine::paintSnapRing(QPainter                 *painter,
                                const OpenSWMMVisMapTool *tool,
                                const Result             &result,
                                const QColor             &color)
{
    if (!result.snapped || !painter || !tool)
        return;

    int px = 0, py = 0;
    tool->toPixelCoords(result.x, result.y, px, py);

    painter->save();
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(color, 2));
    painter->drawEllipse(QPoint(px, py), 9, 9);
    painter->restore();
}
