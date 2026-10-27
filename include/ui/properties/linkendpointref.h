/**
 * @file linkendpointref.h
 * @brief Shared endpoint identity and eligibility rules for link editors.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#ifndef LINKENDPOINTREF_H
#define LINKENDPOINTREF_H

#include <QMetaType>
#include <QString>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

struct LinkEndpointRef {
    SWMM_Engine engine = nullptr; ///< Borrowed editing engine.
    QString linkName;
    bool upstream = true;
    QString nodeName;
    bool operator==(const LinkEndpointRef &) const = default;
};
Q_DECLARE_METATYPE(LinkEndpointRef)

namespace linkendpoint
{
/** Validate typed IDs as well as picker choices before any engine write.
 * Virtual/inlet junctions belong to fixed conduit pairs, so neither adding
 * a connection nor detaching an existing connection is permitted here.
 * Ordinary SWMM node types are endpoints for all five link types.
 */
inline bool allowed(SWMM_Engine engine, int link, bool upstream, int node)
{
    if (!engine || node < 0 || !swmm_node_id(engine, node))
        return false;
    int state = -1, from = -1, to = -1, kind = -1;
    if (swmm_engine_get_state(engine, &state) != SWMM_OK ||
        (state != SWMM_STATE_BUILDING && state != SWMM_STATE_OPENED) ||
        swmm_link_get_type(engine, link, &kind) != SWMM_OK || kind < SWMM_LINK_CONDUIT ||
        kind > SWMM_LINK_OUTLET || swmm_link_get_from_node(engine, link, &from) != SWMM_OK ||
        swmm_link_get_to_node(engine, link, &to) != SWMM_OK || node == (upstream ? to : from))
        return false;
    const int current = upstream ? from : to;
    if (node == current)
        return true;
    int isVirtual = 0;
    if (current >= 0 && (swmm_node_is_virtual(engine, current, &isVirtual) != SWMM_OK || isVirtual))
        return false;
    return swmm_node_is_virtual(engine, node, &isVirtual) == SWMM_OK && !isVirtual;
}
} // namespace linkendpoint

inline void registerLinkEndpointRefConverter()
{
    static const bool registered = QMetaType::registerConverter<LinkEndpointRef, QString>(
        [](const LinkEndpointRef &ref) { return ref.nodeName; });
    Q_UNUSED(registered);
}

#endif // LINKENDPOINTREF_H
