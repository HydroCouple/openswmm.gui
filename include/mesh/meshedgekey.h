/*!
 * \file   meshedgekey.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Unordered vertex-pair key shared by the quad pairing, cleanup and
 * generator code (formerly in meshquadmerge.h, retired with the tri-pair
 * merge — MESH_OVERHAUL_PLAN_2026-09-29.md Phase 5).
 */
#ifndef OPENSWMMVIS_MESH_MESHEDGEKEY_H
#define OPENSWMMVIS_MESH_MESHEDGEKEY_H

#include <QPair>

namespace mesh {

/*! \brief Unordered vertex-pair key used for locked edges: (min, max). */
inline QPair<int, int> edgeKey(int a, int b) noexcept
{
    return a < b ? qMakePair(a, b) : qMakePair(b, a);
}

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHEDGEKEY_H
