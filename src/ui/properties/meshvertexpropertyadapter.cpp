/*!
 * \file   meshvertexpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/properties/meshvertexpropertyadapter.h"

#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshobjectref.h"

#include <QCoreApplication>

#include <cmath>
#include <limits>

MeshVertexPropertyAdapter::MeshVertexPropertyAdapter(SWMM2DMeshLayer *layer, int vertexIdx,
                                                     QObject *parent)
    : QObject(parent), m_layer(layer), m_idx(vertexIdx)
{
    if (m_layer) {
        m_refName = mesh::MeshObjectRef::vertex(m_layer->sourcePath(), m_idx).name;
        connect(m_layer, &SWMM2DMeshLayer::attributeChanged,
                this, &MeshVertexPropertyAdapter::onLayerAttributeChanged);
    }
}

double MeshVertexPropertyAdapter::x() const
{
    if (!m_layer) return std::numeric_limits<double>::quiet_NaN();
    if (m_idx < 0 || m_idx >= m_layer->mesh().vertices.size())
        return std::numeric_limits<double>::quiet_NaN();
    return m_layer->mesh().vertices[m_idx].xy.x();
}

double MeshVertexPropertyAdapter::y() const
{
    if (!m_layer) return std::numeric_limits<double>::quiet_NaN();
    if (m_idx < 0 || m_idx >= m_layer->mesh().vertices.size())
        return std::numeric_limits<double>::quiet_NaN();
    return m_layer->mesh().vertices[m_idx].xy.y();
}

double MeshVertexPropertyAdapter::z() const
{
    if (!m_layer) return std::numeric_limits<double>::quiet_NaN();
    if (m_idx < 0 || m_idx >= m_layer->mesh().vertices.size())
        return std::numeric_limits<double>::quiet_NaN();
    return m_layer->mesh().vertices[m_idx].z;
}

QString MeshVertexPropertyAdapter::coupledNodeId() const
{
    if (!m_layer) return {};
    if (m_idx < 0 || m_idx >= m_layer->mesh().vertices.size()) return {};
    return m_layer->mesh().vertices[m_idx].coupledNode;
}

QString MeshVertexPropertyAdapter::tag() const
{
    if (!m_layer) return {};
    if (m_idx < 0 || m_idx >= m_layer->mesh().vertices.size()) return {};
    return m_layer->mesh().vertices[m_idx].tag;
}

void MeshVertexPropertyAdapter::setZ(double v)
{
    if (!m_layer) return;
    m_layer->applyMeshVertexZ(m_idx, v);
    // attributeChanged → onLayerAttributeChanged → changed() — no need to
    // emit here, the layer's signal round-trips back to us.
}

void MeshVertexPropertyAdapter::setCoupledNodeId(const QString &id)
{
    if (!m_layer) return;
    m_layer->applyMeshVertexCoupledNode(m_idx, id);
}

void MeshVertexPropertyAdapter::setTag(const QString &tag)
{
    if (!m_layer) return;
    m_layer->applyMeshVertexTag(m_idx, tag);
}

void MeshVertexPropertyAdapter::onLayerAttributeChanged(const QString &refName)
{
    if (refName == m_refName) emit changed();
}

QString MeshVertexPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QStringLiteral("index"))         return QCoreApplication::translate("MeshVertex", "Vertex #");
    if (property == QStringLiteral("x"))             return QCoreApplication::translate("MeshVertex", "X");
    if (property == QStringLiteral("y"))             return QCoreApplication::translate("MeshVertex", "Y");
    if (property == QStringLiteral("z"))             return QCoreApplication::translate("MeshVertex", "Elevation (Z)");
    if (property == QStringLiteral("coupledNodeId")) return QCoreApplication::translate("MeshVertex", "Coupled SWMM node");
    if (property == QStringLiteral("tag"))           return QCoreApplication::translate("MeshVertex", "Tag");
    return property;
}
