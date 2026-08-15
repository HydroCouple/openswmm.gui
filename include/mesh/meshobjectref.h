/*!
 * \file   meshobjectref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice §V.VA — helpers to build / parse SWMMObjectRef names for 2D mesh
 * elements (vertices and edges).
 *
 * The encoded form is:
 *     mesh::<layerKey>#v<vertexIdx>
 *     mesh::<layerKey>#e<triIdx>:<edgeLocal>
 *     mesh::<layerKey>#c<triIdx>
 *
 * Where <layerKey> is derived from the mesh layer's source-file basename
 * (so two meshes loaded from different files don't collide in a single
 * project's selection bus). Callers pass the layer's source path; this
 * keeps MeshObjectRef decoupled from the heavy SWMM2DMeshLayer class so
 * it can be unit-tested without widget deps.
 */
#ifndef OPENSWMMVIS_MESH_MESHOBJECTREF_H
#define OPENSWMMVIS_MESH_MESHOBJECTREF_H

#include "selection/selectionmanager.h"

#include <QString>

namespace mesh {

class MeshObjectRef
{
public:
    /*! \brief "mesh::<basename>" for the given source path (extension
     *  stripped). Empty path → "mesh::<unsaved>". */
    static QString layerKey(const QString &sourcePath);

    /*! \brief Build a SelectionManager ref naming a vertex on the layer
     *  whose source file is at \p sourcePath. */
    static SWMMObjectRef vertex(const QString &sourcePath, int vertexIdx);

    /*! \brief Build a SelectionManager ref naming an edge on the layer
     *  whose source file is at \p sourcePath. */
    static SWMMObjectRef edge(const QString &sourcePath, int triIdx, int edgeLocal);

    /*! \brief Build a SelectionManager ref naming a cell (triangle) on the
     *  layer whose source file is at \p sourcePath. */
    static SWMMObjectRef cell(const QString &sourcePath, int triIdx);

    /*! \brief Parse a vertex ref name. Returns true and fills the outs
     *  on success; false (and leaves outs untouched) if the name is
     *  malformed or names a different kind. */
    static bool parseVertex(const SWMMObjectRef &ref,
                            QString *outLayerKey,
                            int *outVertexIdx);

    /*! \brief Parse an edge ref name. */
    static bool parseEdge(const SWMMObjectRef &ref,
                          QString *outLayerKey,
                          int *outTriIdx,
                          int *outEdgeLocal);

    /*! \brief Parse a cell ref name. */
    static bool parseCell(const SWMMObjectRef &ref,
                          QString *outLayerKey,
                          int *outTriIdx);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHOBJECTREF_H
