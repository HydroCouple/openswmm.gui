/*!
 * \file   meshboundarygraph.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Boundary-edge connectivity graph for a generated / loaded 2D mesh, used
 * by the mesh edge-select tool to Ctrl-click two boundary edges and select
 * the whole chain between them (shortest = geometric length).
 *
 * Pure value type over mesh::MeshResult — no Qt-GUI deps, so it is
 * unit-testable headless.
 */
#ifndef OPENSWMMVIS_MESH_MESHBOUNDARYGRAPH_H
#define OPENSWMMVIS_MESH_MESHBOUNDARYGRAPH_H

#include <QHash>
#include <QVector>

namespace mesh {

struct MeshResult;

/*! \brief Undirected graph whose nodes are mesh vertices and whose arcs
 *  are boundary edge slots (`triIdx * 3 + edgeLocal`).
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
    /*! \brief Build from \p mesh and the flat per-slot boundary flags
     *  (`isBoundary[tri*3 + e]`). Slots outside the flag vector, slots
     *  with out-of-range or degenerate vertex indices, are skipped. */
    static MeshBoundaryGraph build(const MeshResult &mesh,
                                   const QVector<bool> &isBoundary);

    /*! \brief Shortest chain of boundary edge slots from \p startSlot to
     *  \p endSlot, inclusive of both terminal slots.
     *
     *  Weight is the 2D segment length in mesh (project-CRS) units of the
     *  intermediate edges; the terminal edges are always included and so
     *  do not influence the choice of route. Returns an empty vector when
     *  either slot is not a boundary edge of this graph, or when the two
     *  lie on boundary loops that are not connected to each other.
     *  `startSlot == endSlot` returns that one slot. */
    [[nodiscard]] QVector<int> shortestPath(int startSlot, int endSlot) const;

    /*! \brief True when the mesh contributed no boundary edges (also the
     *  state of a default-constructed graph). */
    [[nodiscard]] bool isEmpty() const { return m_edges.isEmpty(); }

    /*! \brief Number of boundary edge slots in the graph. */
    [[nodiscard]] int edgeCount() const { return int(m_edges.size()); }

    /*! \brief True when \p slot (`tri*3 + edgeLocal`) is one of them. */
    [[nodiscard]] bool contains(int slot) const
    { return m_slotToEdge.contains(slot); }

private:
    struct Edge
    {
        int    slot = -1;   ///< tri * 3 + edgeLocal
        int    a    = -1;   ///< compact vertex id
        int    b    = -1;   ///< compact vertex id
        double len  = 0.0;  ///< 2D segment length, mesh units
    };

    QVector<Edge>  m_edges;
    QHash<int,int> m_slotToEdge;  ///< flat slot -> index into m_edges
    // CSR adjacency over compact vertex ids: the boundary touches only a
    // small subset of a large mesh's vertices, so compacting keeps the
    // arrays proportional to the boundary rather than to the whole mesh.
    QVector<int>   m_vertPtr;     ///< size = nCompactVerts + 1
    QVector<int>   m_vertEdge;    ///< edge indices, grouped by vertex
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHBOUNDARYGRAPH_H
