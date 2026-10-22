/**
 * @file      meshboundarygraph.h
 * @brief     Boundary-edge connectivity graph for a generated / loaded 2D mesh, used by the mesh
 *            edge-select tool to Ctrl-click two boundary edges and select the whole chain between
 *            them (shortest = geometric length).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Boundary-edge connectivity graph for a generated / loaded 2D mesh, used
 * by the mesh edge-select tool to Ctrl-click two boundary edges and select
 * the whole chain between them (shortest = geometric length).
 *
 * Pure value type over mesh::MeshResult — no Qt-GUI deps, so it is
 * unit-testable headless.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHBOUNDARYGRAPH_H
#define OPENSWMMVIS_MESH_MESHBOUNDARYGRAPH_H

#include <QHash>
#include <QVector>

namespace mesh
{

struct MeshResult;

/*! \brief Undirected graph whose nodes are mesh vertices and whose arcs
 *  are boundary edge slots (`mesh::edgeSlot(cell, edgeLocal)`).
 *
 * Boundary status is supplied by the caller as the same flat flag vector
 * the layer keeps (`buildBoundaryFlags`), so marker-tagged internal
 * boundaries participate exactly as they do everywhere else in the app.
 * A marker-tagged internal edge is shared by two triangles and therefore
 * contributes two parallel slots of equal length; either may be returned.
 */
class MeshBoundaryGraph
{
public:
    /**
     * @brief Copy flagged cell-local mesh edges into an owned compact undirected connectivity
     * graph.
     * @param[in] mesh Borrowed stable geometry, unchanged; planar coordinates must be finite and
     * lengths representable for meaningful shortest-path weights. Cell/edge slot arithmetic and
     * sizes must fit int.
     * @param[in] isBoundary Borrowed stride-four flags indexed by edgeSlot(cell,local),
     * unchanged; short vectors are allowed and missing slots skipped. Triangle padding is never
     * consulted.
     * @return Owned graph, empty when inputs contain no usable flagged edges. Out-of-range
     * endpoint IDs and equal endpoint IDs are skipped; distinct IDs at coincident coordinates
     * remain zero-length edges. Marker-defined shared edges can appear as parallel slots.
     * Coordinates are not retained, graph storage allocates independently and no engine/layer
     * state is modified.
     *
     * @details Build from \p mesh and the flat per-slot boundary flags
     * (`isBoundary[mesh::edgeSlot(cell, e)]`). Slots outside the flag vector, slots with
     * out-of-range or degenerate vertex indices, are skipped.
     */
    static MeshBoundaryGraph build(const MeshResult &mesh, const QVector<bool> &isBoundary);

    /**
     * @brief Find a minimum-length chain connecting two boundary slots using graph vertex
     * adjacency.
     * @param[in] startSlot Flat stride-four boundary slot present in this graph; arbitrary
     * values fail empty.
     * @param[in] endSlot Other flat boundary slot; arbitrary values fail empty.
     * @return Owned ordered slot vector containing both terminals, one slot if equal, empty if
     * either is absent or the components are disconnected. Intermediate edge lengths weight
     * Dijkstra; terminal lengths do not affect route choice. Duplicate slots are removed,
     * zero-length edges are allowed and equal-length alternatives are not a unique-route
     * guarantee. Allocates temporary search storage; read-only graph query, safe concurrent
     * reads with stable graph storage.
     *
     * @details Shortest chain of boundary edge slots from \p startSlot to
     * \p endSlot, inclusive of both terminal slots.
     *
     * Weight is the 2D segment length in mesh (project-CRS) units of the intermediate edges; the
     * terminal edges are always included and so do not influence the choice of route. Returns an
     * empty vector when either slot is not a boundary edge of this graph, or when the two lie on
     * boundary loops that are not connected to each other. `startSlot == endSlot` returns that
     * one slot.
     */
    [[nodiscard]] QVector<int> shortestPath(int startSlot, int endSlot) const;

    /**
     * @brief Check whether the graph contains any copied boundary edge slots.
     * @return True for default construction or an empty build, false otherwise. Constant
     * read-only query; does not reconsult mesh geometry or boundary flags.
     *
     * @details True when the mesh contributed no boundary edges (also the state of a
     * default-constructed graph).
     */
    [[nodiscard]] bool isEmpty() const { return m_edges.isEmpty(); }

    /**
     * @brief Report the number of copied boundary slots, including parallel shared-edge slots.
     * @return Stored edge count as int; graph sizes must fit int. Constant read-only query, no
     * allocation.
     *
     * @details Number of boundary edge slots in the graph.
     */
    [[nodiscard]] int edgeCount() const { return int(m_edges.size()); }

    /**
     * @brief Check membership of one flat boundary slot in the stored graph.
     * @param[in] slot Flat stride-four slot identifier; negative or absent values simply do not
     * match.
     * @return True when build() retained this slot, false otherwise. Hash lookup only, no mesh
     * bounds validation or current layer boundary-state lookup; graph is unchanged.
     *
     * @details True when \p slot (`mesh::edgeSlot(cell, edgeLocal)`) is one of them.
     */
    [[nodiscard]] bool contains(int slot) const { return m_slotToEdge.contains(slot); }

private:
    struct Edge {
        int slot = -1;    ///< mesh::edgeSlot(cell, edgeLocal)
        int a = -1;       ///< compact vertex id
        int b = -1;       ///< compact vertex id
        double len = 0.0; ///< 2D segment length, mesh units
    };

    QVector<Edge> m_edges;
    QHash<int, int> m_slotToEdge; ///< flat slot -> index into m_edges
    // CSR adjacency over compact vertex ids: the boundary touches only a
    // small subset of a large mesh's vertices, so compacting keeps the
    // arrays proportional to the boundary rather than to the whole mesh.
    QVector<int> m_vertPtr;  ///< size = nCompactVerts + 1
    QVector<int> m_vertEdge; ///< edge indices, grouped by vertex
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHBOUNDARYGRAPH_H
