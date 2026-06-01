/*!
 * \file   meshobjectref.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshobjectref.h"

#include <QFileInfo>

namespace mesh {

QString MeshObjectRef::layerKey(const QString &sourcePath)
{
    if (sourcePath.isEmpty())
        return QStringLiteral("mesh::<unsaved>");
    return QStringLiteral("mesh::") + QFileInfo(sourcePath).completeBaseName();
}

SWMMObjectRef MeshObjectRef::vertex(const QString &sourcePath, int vertexIdx)
{
    const QString name = layerKey(sourcePath) + QStringLiteral("#v")
                       + QString::number(vertexIdx);
    return SWMMObjectRef(SWMMObjectRef::MeshVertex, name);
}

SWMMObjectRef MeshObjectRef::edge(const QString &sourcePath, int triIdx, int edgeLocal)
{
    const QString name = layerKey(sourcePath) + QStringLiteral("#e")
                       + QString::number(triIdx)
                       + QChar(':')
                       + QString::number(edgeLocal);
    return SWMMObjectRef(SWMMObjectRef::MeshEdge, name);
}

SWMMObjectRef MeshObjectRef::cell(const QString &sourcePath, int triIdx)
{
    const QString name = layerKey(sourcePath) + QStringLiteral("#c")
                       + QString::number(triIdx);
    return SWMMObjectRef(SWMMObjectRef::MeshCell, name);
}

bool MeshObjectRef::parseCell(const SWMMObjectRef &ref,
                              QString *outLayerKey,
                              int *outTriIdx)
{
    if (ref.objectType != SWMMObjectRef::MeshCell) return false;
    const int hashIdx = ref.name.indexOf(QStringLiteral("#c"));
    if (hashIdx < 0) return false;
    const QString layerStr = ref.name.left(hashIdx);
    const QString idxStr   = ref.name.mid(hashIdx + 2);
    bool ok = false;
    const int tri = idxStr.toInt(&ok);
    if (!ok || tri < 0) return false;
    if (outLayerKey) *outLayerKey = layerStr;
    if (outTriIdx)   *outTriIdx   = tri;
    return true;
}

bool MeshObjectRef::parseVertex(const SWMMObjectRef &ref,
                                QString *outLayerKey,
                                int *outVertexIdx)
{
    if (ref.objectType != SWMMObjectRef::MeshVertex) return false;
    const int hashIdx = ref.name.indexOf(QStringLiteral("#v"));
    if (hashIdx < 0) return false;
    const QString layerStr = ref.name.left(hashIdx);
    const QString idxStr   = ref.name.mid(hashIdx + 2);
    bool ok = false;
    const int vi = idxStr.toInt(&ok);
    if (!ok || vi < 0) return false;
    if (outLayerKey)  *outLayerKey  = layerStr;
    if (outVertexIdx) *outVertexIdx = vi;
    return true;
}

bool MeshObjectRef::parseEdge(const SWMMObjectRef &ref,
                              QString *outLayerKey,
                              int *outTriIdx,
                              int *outEdgeLocal)
{
    if (ref.objectType != SWMMObjectRef::MeshEdge) return false;
    const int hashIdx = ref.name.indexOf(QStringLiteral("#e"));
    if (hashIdx < 0) return false;
    const QString layerStr = ref.name.left(hashIdx);
    const QString tail     = ref.name.mid(hashIdx + 2);
    const int colonIdx = tail.indexOf(QChar(':'));
    if (colonIdx < 0) return false;
    bool ok1 = false, ok2 = false;
    const int tri = tail.left(colonIdx).toInt(&ok1);
    const int e   = tail.mid(colonIdx + 1).toInt(&ok2);
    if (!ok1 || !ok2 || tri < 0 || e < 0 || e > 2) return false;
    if (outLayerKey)   *outLayerKey   = layerStr;
    if (outTriIdx)     *outTriIdx     = tri;
    if (outEdgeLocal)  *outEdgeLocal  = e;
    return true;
}

} // namespace mesh
