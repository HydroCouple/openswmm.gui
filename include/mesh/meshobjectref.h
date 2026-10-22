/**
 * @file      meshobjectref.h
 * @brief     Slice §V.VA — helpers to build / parse SWMMObjectRef names for 2D mesh elements
 *            (vertices and edges).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice §V.VA — helpers to build / parse SWMMObjectRef names for 2D mesh
 * elements (vertices and edges).
 *
 * The encoded form is:
 * @code
 *     mesh::<layerKey>#v<vertexIdx>
 *     mesh::<layerKey>#e<triIdx>:<edgeLocal>
 *     mesh::<layerKey>#c<triIdx>
 * @endcode
 *
 * Where `layerKey` is derived from the mesh layer's source-file basename
 * (equal basenames in different directories still share the same key). Callers pass the layer's source path; this
 * keeps MeshObjectRef decoupled from the heavy SWMM2DMeshLayer class so
 * it can be unit-tested without widget deps.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHOBJECTREF_H
#define OPENSWMMVIS_MESH_MESHOBJECTREF_H

#include "selection/selectionmanager.h"

#include <QString>

namespace mesh
{

class MeshObjectRef
{
public:
    /**
     * @brief Derive the selection-bus layer key from a source filename.
     * @param[in] sourcePath Borrowed path, inspected lexically without filesystem access;
     * unchanged.
     * @return Owned `mesh::` prefix followed by QFileInfo::completeBaseName(), or
     * `mesh::<unsaved>` for an empty path. Directories are omitted, so equal basenames in
     * different directories share a key; no uniqueness registry, path canonicalization or mesh
     * existence check is performed.
     *
     * @details "mesh::<basename>" for the given source path (extension stripped). Empty path →
     * "`mesh::<unsaved>`".
     */
    static QString layerKey(const QString &sourcePath);

    /**
     * @brief Encode a vertex selection reference using its layer filename and index.
     * @param[in] sourcePath Borrowed source path used by layerKey(), unchanged.
     * @param[in] vertexIdx Vertex index to encode, unchecked; negative values are serialized but
     * not parseable.
     * @return Owned SWMMObjectRef of MeshVertex type, name layerKey#v followed by the decimal
     * index. No mesh lookup, index validation, ownership retention or selection mutation.
     *
     * @details Build a SelectionManager ref naming a vertex on the layer whose source file is at
     * \p sourcePath.
     */
    static SWMMObjectRef vertex(const QString &sourcePath, int vertexIdx);

    /**
     * @brief Encode a cell-local edge selection reference.
     * @param[in] sourcePath Borrowed source path used by layerKey(), unchanged.
     * @param[in] triIdx Cell index to encode, unchecked against mesh storage.
     * @param[in] edgeLocal Local edge index to encode, unchecked; valid parsing requires 0..3
     * and callers must further exclude slot 3 for triangles.
     * @return Owned MeshEdge reference named layerKey#e followed by cell:local decimal
     * identifiers. No mesh lookup, topology update or selection mutation.
     *
     * @details Build a SelectionManager ref naming an edge on the layer whose source file is at
     * \p sourcePath.
     */
    static SWMMObjectRef edge(const QString &sourcePath, int triIdx, int edgeLocal);

    /**
     * @brief Encode a triangle-or-quad cell selection reference.
     * @param[in] sourcePath Borrowed source path used by layerKey(), unchanged.
     * @param[in] triIdx Cell index to encode, unchecked; negative values serialize but are
     * rejected by parsing.
     * @return Owned MeshCell reference named layerKey#c followed by the decimal cell index. No
     * mesh lookup or selection mutation.
     *
     * @details Build a SelectionManager ref naming a cell (triangle) on the layer whose source
     * file is at \p sourcePath.
     */
    static SWMMObjectRef cell(const QString &sourcePath, int triIdx);

    /**
     * @brief Decode a vertex-typed reference after validating its marker and nonnegative integer
     * suffix.
     * @param[in] ref Borrowed reference, unchanged; requires MeshVertex object type and a `#v`
     * marker.
     * @param[out] outLayerKey Optional borrowed string output, filled only on success with text
     * before the first `#v`.
     * @param[out] outVertexIdx Optional borrowed int output, filled only on success with the
     * nonnegative index.
     * @return True on successful QString::toInt conversion, false for wrong type, missing
     * marker, overflow or negative index. All outputs remain untouched on failure, null outputs
     * are allowed. The layer prefix need not begin with mesh:: or identify a real layer, and
     * index bounds are not checked.
     *
     * @details Parse a vertex ref name. Returns true and fills the outs on success; false (and
     * leaves outs untouched) if the name is malformed or names a different kind.
     */
    static bool parseVertex(const SWMMObjectRef &ref, QString *outLayerKey, int *outVertexIdx);

    /**
     * @brief Decode an edge-typed reference into a layer key and stride-four cell/local indices.
     * @param[in] ref Borrowed reference, unchanged; requires MeshEdge type, `#e` marker and
     * colon separator.
     * @param[out] outLayerKey Optional borrowed string output, filled only on success with
     * prefix before first `#e`.
     * @param[out] outTriIdx Optional borrowed int output, filled only on success with a
     * nonnegative cell index.
     * @param[out] outEdgeLocal Optional borrowed int output, filled only on success with local
     * index 0..3; use distinct integer outputs when both values are required.
     * @return True when both suffixes convert to int and satisfy the stated ranges, false
     * otherwise; all outputs untouched on failure. Does not validate layer identity, mesh bounds
     * or triangle padding. No ownership retained, mesh update or selection mutation.
     *
     * @details Parse an edge ref name.
     */
    static bool parseEdge(const SWMMObjectRef &ref, QString *outLayerKey, int *outTriIdx,
                          int *outEdgeLocal);

    /**
     * @brief Decode a cell-typed selection reference without consulting a mesh.
     * @param[in] ref Borrowed reference, unchanged; requires MeshCell type and a `#c` marker.
     * @param[out] outLayerKey Optional borrowed string output, filled only on success with text
     * before first `#c`.
     * @param[out] outTriIdx Optional borrowed int output, filled only on success with the
     * nonnegative cell index.
     * @return True for a convertible nonnegative int suffix, false for wrong type, missing
     * marker, overflow or negative value. Outputs untouched on failure; null outputs are
     * accepted. No mesh:: prefix, layer existence or index-bound validation, and no ownership
     * retention or selection mutation.
     *
     * @details Parse a cell ref name.
     */
    static bool parseCell(const SWMMObjectRef &ref, QString *outLayerKey, int *outTriIdx);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHOBJECTREF_H
