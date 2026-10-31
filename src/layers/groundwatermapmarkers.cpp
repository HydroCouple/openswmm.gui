/**
 * @file      groundwatermapmarkers.cpp
 * @brief     Map locations of [GW_SOURCES] rows and [2D_AQUIFER_NODE] beds (R7).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/groundwatermapmarkers.h"

#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

namespace OpenSWMMVis::Groundwater
{

QVector<GroundwaterMapMarker> groundwaterMapMarkers(SWMM_Engine engine)
{
    QVector<GroundwaterMapMarker> out;
    if (!engine)
        return out;
    for (int i = 0, n = swmm_gw_source_count(engine); i < n; ++i) {
        char name[512]{};
        int scope = -1, cell = -1, byXy = 0;
        double x = 0, y = 0;
        if (swmm_gw_source_get(engine, i, name, sizeof name, &scope, nullptr, 0, &cell, nullptr,
                               nullptr, 0) != SWMM_OK)
            continue;
        GroundwaterMapMarker m;
        m.name = QString::fromUtf8(name);
        if (swmm_gw_source_xy_get(engine, i, &byXy, &x, &y) == SWMM_OK && byXy) {
            m.point = QPointF(x, y);
            m.hasPoint = true;
        } else if (scope == SWMM_GW_SCOPE_CELL && cell >= 0)
            m.cell = cell;
        else
            continue; // * or TAG: no single location
        out.append(m);
    }
    int beds = 0;
    if (swmm_gw2d_node_count(engine, &beds) != SWMM_OK)
        return out;
    for (int i = 0; i < beds; ++i) {
        char node[512]{};
        int cell = -1, exchange = 1;
        if (swmm_gw2d_node_get(engine, i, node, sizeof node, &cell, nullptr, nullptr, nullptr) !=
            SWMM_OK)
            continue;
        swmm_gw2d_node_get_flags(engine, i, nullptr, &exchange, nullptr);
        GroundwaterMapMarker m;
        m.kind = exchange ? GroundwaterMapMarker::Kind::NodeBed
                          : GroundwaterMapMarker::Kind::NodeBedNoExchange;
        m.name = QString::fromUtf8(node);
        const int index = swmm_node_index(engine, node);
        double x = 0, y = 0;
        int present = 0;
        if (index >= 0 && swmm_spatial_node_has_coord(engine, index, &present) == SWMM_OK &&
            present && swmm_spatial_get_node_coord(engine, index, &x, &y) == SWMM_OK) {
            m.point = QPointF(x, y);
            m.hasPoint = true;
        } else if (cell >= 0)
            m.cell = cell;
        else
            continue;
        out.append(m);
    }
    return out;
}

} // namespace OpenSWMMVis::Groundwater
