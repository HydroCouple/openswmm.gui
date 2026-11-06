/**
 * @file      groundwatermapmarkers.h
 * @brief     Map locations of [GW_SOURCES] rows and [2D_AQUIFER_NODE] beds (R7),
 *            read from the engine model on demand.
 *
 * Pure engine query, like gwsourcesummary: the mesh layer re-reads it when the
 * model changes and draws the result as its Groundwater Markers sublayer, so
 * the map never keeps an independent copy of the rows.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_LAYERS_GROUNDWATERMAPMARKERS_H
#define OPENSWMM_LAYERS_GROUNDWATERMAPMARKERS_H

#include <QPointF>
#include <QString>
#include <QVector>

#include <openswmm/engine/openswmm_engine.h>

namespace OpenSWMMVis::Groundwater
{

struct GroundwaterMapMarker {
    enum class Kind { Source, NodeBed, NodeBedNoExchange };
    Kind kind = Kind::Source;
    QString name;         ///< source name, or the bed's 1D node id
    QPointF point;        ///< project map coordinates when hasPoint
    bool hasPoint = false;
    int cell = -1;        ///< zero-based mesh cell drawn at its centroid when !hasPoint
};

/*! Markers for every located [GW_SOURCES] row and [2D_AQUIFER_NODE] bed, in row order.
 * @param[in] engine Borrowed engine handle; null returns empty. Read only.
 * @return Sources: an XY row at its authored coordinates, a CELL row at that cell; scoped
 * (* / TAG) rows have no single location and are omitted. Beds: at the node's coordinates
 * (or its authored cell when the node has none), NodeBedNoExchange for `EXCHANGE NO` rows.
 * Beds enrolled automatically at initialize (NODE_ENROLMENT AUTO) are not authored rows and
 * are not listed.
 */
QVector<GroundwaterMapMarker> groundwaterMapMarkers(SWMM_Engine engine);

} // namespace OpenSWMMVis::Groundwater

#endif // OPENSWMM_LAYERS_GROUNDWATERMAPMARKERS_H
