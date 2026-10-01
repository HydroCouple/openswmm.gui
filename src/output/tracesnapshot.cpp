// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/swmmmodellayer.h"
#include "map/spatialreferencesystem.h"
#include "output/tracedata.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_climate.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

namespace openswmmvis::trace
{
Snapshot Snapshot::capture(SWMMModelLayer *model, QString *error)
{
    Snapshot s;
    if (!model || !model->engine())
    {
        if (error)
            *error = QStringLiteral("Open a model first.");
        return s;
    }
    auto e = model->engine();
    int units = 0;
    swmm_get_flow_units(e, &units);
    double lengthFactor = units < 3 ? 0.3048 : 1;
    for (int i = 0; i < swmm_node_count(e); ++i)
    {
        const char *id = swmm_node_id(e, i);
        if (!id)
        {
            if (error)
                *error = QStringLiteral("Cannot read node ID.");
            return {};
        }
        Node n;
        n.id = QString::fromUtf8(id);
        swmm_node_get_type(e, i, &n.type);
        double pond = 0;
        swmm_node_get_ponded_area(e, i, &pond);
        if (pond > 0)
            n.flags |= SWMM_TRACE_PONDING;
        int routed = -1;
        if (n.type == 1 && swmm_node_get_outfall_route_to(e, i, &routed) == 0 && routed >= 0)
            n.flags |= SWMM_TRACE_ROUTED_OUTFALL;
        if (n.type == 2)
        {
            double suction = 0, ksat = 0, deficit = 0;
            swmm_node_get_exfil_params(e, i, &suction, &ksat, &deficit);
            int evaporation = 0;
            swmm_climate_get_evap_type(e, &evaporation);
            if (ksat > 0 || evaporation > 0)
                n.flags |= SWMM_TRACE_UNKNOWN_LOSSES;
        }
        double x = 0, y = 0;
        n.hasGeometry = model->cachedNodeCoord(model->nodeIndex(n.id), &x, &y);
        n.point = {x, y};
        s.nodes.append(n);
    }
    for (int i = 0; i < swmm_link_count(e); ++i)
    {
        const char *id = swmm_link_id(e, i);
        if (!id)
        {
            if (error)
                *error = QStringLiteral("Cannot read link ID.");
            return {};
        }
        Link l;
        l.id = QString::fromUtf8(id);
        l.from = model->linkFromNodeIdx(i);
        l.to = model->linkToNodeIdx(i);
        swmm_link_get_type(e, i, &l.type);
        double length = 0;
        swmm_link_get_length(e, i, &length);
        l.length = length * lengthFactor;
        l.points = model->cachedLinkPolyline(model->linkIndex(l.id));
        if (l.points.size() < 2 && l.from >= 0 && l.to >= 0 && l.from < s.nodes.size() &&
            l.to < s.nodes.size() && s.nodes[l.from].hasGeometry && s.nodes[l.to].hasGeometry)
            l.points = {s.nodes[l.from].point, s.nodes[l.to].point};
        double seep = 0;
        if (swmm_link_get_seep_rate(e, i, &seep) == 0 && seep > 0)
        {
            if (l.from >= 0 && l.from < s.nodes.size())
                s.nodes[l.from].flags |= SWMM_TRACE_UNKNOWN_LOSSES;
            if (l.to >= 0 && l.to < s.nodes.size())
                s.nodes[l.to].flags |= SWMM_TRACE_UNKNOWN_LOSSES;
        }
        s.links.append(l);
    }
    int count = 0;
    swmm_2d_vertex_count(e, &count);
    for (int i = 0; i < count; ++i)
    {
        int node = -1;
        swmm_2d_vertex_get_coupled_node(e, i, &node);
        if (node >= 0 && node < s.nodes.size())
            s.nodes[node].flags |= SWMM_TRACE_EXTERNAL_EXCHANGE;
    }
    count = 0;
    swmm_2d_triangle_count(e, &count);
    for (int i = 0; i < count; ++i)
    {
        int node = -1;
        swmm_2d_triangle_get_coupled_node(e, i, &node);
        if (node >= 0 && node < s.nodes.size())
            s.nodes[node].flags |= SWMM_TRACE_EXTERNAL_EXCHANGE;
    }
    if (model->srs())
        s.wkt = model->srs()->toWkt();
    QJsonObject topology = s.toJson();
    topology.remove("wkt");
    // Coordinates belong to the saved geometry, not the hydraulic cache identity.
    for (const QString &key : {QStringLiteral("nodes"), QStringLiteral("links")})
    {
        QJsonArray objects;
        for (auto value : topology.value(key).toArray())
        {
            auto object = value.toObject();
            object.remove("x");
            object.remove("y");
            object.remove("geometry");
            object.remove("points");
            objects.append(object);
        }
        topology[key] = objects;
    }
    s.modelFingerprint = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(topology).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256)
            .toHex());
    if (!s.valid(error))
        return {};
    return s;
}
} // namespace openswmmvis::trace
