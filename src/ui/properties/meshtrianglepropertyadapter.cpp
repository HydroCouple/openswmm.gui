/*!
 * \file   meshtrianglepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/properties/meshtrianglepropertyadapter.h"

#include "layers/swmm2dmeshlayer.h"
#include "map/meshcommands.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshobjectref.h"

#include <QCoreApplication>

#include <cmath>
#include <limits>

MeshTrianglePropertyAdapter::MeshTrianglePropertyAdapter(SWMM2DMeshLayer *layer, int triIdx,
                                                         QObject *parent)
    : QObject(parent), m_layer(layer), m_idx(triIdx)
{
    if (m_layer) {
        m_refName = mesh::MeshObjectRef::cell(m_layer->sourcePath(), m_idx).name;
        connect(m_layer, &SWMM2DMeshLayer::attributeChanged,
                this, &MeshTrianglePropertyAdapter::onLayerAttributeChanged);
    }
}

namespace {
bool valid(const QPointer<SWMM2DMeshLayer> &layer, int idx)
{
    return layer && idx >= 0 && idx < layer->mesh().triangles.size();
}
} // namespace

int MeshTrianglePropertyAdapter::v0() const
{
    return valid(m_layer, m_idx) ? m_layer->mesh().triangles[m_idx].v0 : -1;
}

int MeshTrianglePropertyAdapter::v1() const
{
    return valid(m_layer, m_idx) ? m_layer->mesh().triangles[m_idx].v1 : -1;
}

int MeshTrianglePropertyAdapter::v2() const
{
    return valid(m_layer, m_idx) ? m_layer->mesh().triangles[m_idx].v2 : -1;
}

double MeshTrianglePropertyAdapter::mannings() const
{
    return valid(m_layer, m_idx) ? m_layer->mesh().triangles[m_idx].mannings
                                 : std::numeric_limits<double>::quiet_NaN();
}

QString MeshTrianglePropertyAdapter::tag() const
{
    return valid(m_layer, m_idx) ? m_layer->mesh().triangles[m_idx].tag : QString();
}

void MeshTrianglePropertyAdapter::setMannings(double n)
{
    if (!m_layer) return;
    mesh::pushCellParamEdit(m_layer, {m_idx}, "mannings", n, m_canvas);
}

double MeshTrianglePropertyAdapter::initDepth() const
{
    if (!valid(m_layer, m_idx)) return std::numeric_limits<double>::quiet_NaN();
    const double d = m_layer->mesh().triangles[m_idx].initDepth;
    return std::isfinite(d) ? d : 0.0;   // unset column = engine default 0 (dry)
}

void MeshTrianglePropertyAdapter::setInitDepth(double d)
{
    if (!m_layer) return;
    mesh::pushCellParamEdit(m_layer, {m_idx}, "initDepth", d, m_canvas);
}

void MeshTrianglePropertyAdapter::setTag(const QString &tag)
{
    if (!m_layer) return;
    m_layer->applyMeshTriangleTag(m_idx, tag);
}

void MeshTrianglePropertyAdapter::onLayerAttributeChanged(const QString &refName)
{
    if (refName == m_refName) emit changed();
}

QString MeshTrianglePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QStringLiteral("index"))    return QCoreApplication::translate("MeshTriangle", "Cell #");
    if (property == QStringLiteral("v0"))       return QCoreApplication::translate("MeshTriangle", "Vertex 1");
    if (property == QStringLiteral("v1"))       return QCoreApplication::translate("MeshTriangle", "Vertex 2");
    if (property == QStringLiteral("v2"))       return QCoreApplication::translate("MeshTriangle", "Vertex 3");
    if (property == QStringLiteral("mannings"))  return mesh::cellParamLabel("mannings", m_depthUnit);
    if (property == QStringLiteral("initDepth")) return mesh::cellParamLabel("initDepth", m_depthUnit);
    if (property == QStringLiteral("tag"))      return QCoreApplication::translate("MeshTriangle", "Tag");
    return property;
}
