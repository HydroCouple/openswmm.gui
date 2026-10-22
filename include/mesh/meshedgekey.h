/**
 * @file      meshedgekey.h
 * @brief     Unordered vertex-pair key shared by the quad pairing, cleanup and generator code
 *            (formerly in meshquadmerge.h, retired with the tri-pair merge —
 *            MESH_OVERHAUL_PLAN_2026-09-29.md Phase 5).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Unordered vertex-pair key shared by the quad pairing, cleanup and
 * generator code (formerly in meshquadmerge.h, retired with the tri-pair
 * merge — MESH_OVERHAUL_PLAN_2026-09-29.md Phase 5).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHEDGEKEY_H
#define OPENSWMMVIS_MESH_MESHEDGEKEY_H

#include <QPair>

namespace mesh
{

/**
 * @brief Normalize two endpoint identifiers into an undirected ordered key.
 * @param[in] a First integer vertex identifier, any value including negative sentinel accepted.
 * @param[in] b Second integer identifier, equal values allowed.
 * @return Owning (min(a,b),max(a,b)) pair, independent of orientation. Does not validate mesh
 * bounds, self-edge geometry or connectivity; noexcept constant work, no allocation or shared
 * state.
 *
 * @details Unordered vertex-pair key used for locked edges: (min, max).
 */
inline QPair<int, int> edgeKey(int a, int b) noexcept
{
    return a < b ? qMakePair(a, b) : qMakePair(b, a);
}

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHEDGEKEY_H
