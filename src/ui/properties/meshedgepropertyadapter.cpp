/*!
 * \file   meshedgepropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/properties/meshedgepropertyadapter.h"

#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshobjectref.h"

#include <QCoreApplication>

#include <cmath>
#include <limits>

namespace {
mesh::MeshEdgeBC currentBC(SWMM2DMeshLayer *layer, int tri, int e)
{
    if (!layer) return {};
    const int flat = tri * 3 + e;
    const auto &bcs = layer->edgeBCs();
    if (flat < 0 || flat >= bcs.size()) return {};
    return bcs[flat];
}
} // namespace

MeshEdgePropertyAdapter::MeshEdgePropertyAdapter(SWMM2DMeshLayer *layer,
                                                 int triIdx, int edgeLocal,
                                                 QObject *parent)
    : QObject(parent), m_layer(layer), m_tri(triIdx), m_e(edgeLocal)
{
    if (m_layer) {
        m_refName = mesh::MeshObjectRef::edge(m_layer->sourcePath(), m_tri, m_e).name;
        connect(m_layer, &SWMM2DMeshLayer::attributeChanged,
                this, &MeshEdgePropertyAdapter::onLayerAttributeChanged);
    }
}

bool MeshEdgePropertyAdapter::isBoundary() const
{
    if (!m_layer) return false;
    return m_layer->isBoundaryEdge(m_tri, m_e);
}

double MeshEdgePropertyAdapter::length() const
{
    if (!m_layer) return std::numeric_limits<double>::quiet_NaN();
    if (m_tri < 0 || m_tri >= m_layer->mesh().triangles.size())
        return std::numeric_limits<double>::quiet_NaN();
    const auto &tri = m_layer->mesh().triangles[m_tri];
    int va = -1, vb = -1;
    switch (m_e) {
    case 0: va = tri.v1; vb = tri.v2; break;
    case 1: va = tri.v2; vb = tri.v0; break;
    case 2: va = tri.v0; vb = tri.v1; break;
    default: return std::numeric_limits<double>::quiet_NaN();
    }
    const auto &verts = m_layer->mesh().vertices;
    if (va < 0 || vb < 0 || va >= verts.size() || vb >= verts.size())
        return std::numeric_limits<double>::quiet_NaN();
    const double dx = verts[vb].xy.x() - verts[va].xy.x();
    const double dy = verts[vb].xy.y() - verts[va].xy.y();
    return std::sqrt(dx * dx + dy * dy);
}

int     MeshEdgePropertyAdapter::bcType()    const { return static_cast<int>(currentBC(m_layer, m_tri, m_e).type); }
double  MeshEdgePropertyAdapter::bcHead()    const { return currentBC(m_layer, m_tri, m_e).head; }
double  MeshEdgePropertyAdapter::bcSlope()   const { return currentBC(m_layer, m_tri, m_e).slope; }
double  MeshEdgePropertyAdapter::bcFlow()    const { return currentBC(m_layer, m_tri, m_e).flow; }
QString MeshEdgePropertyAdapter::bcTseries() const { return currentBC(m_layer, m_tri, m_e).tseries; }
QString MeshEdgePropertyAdapter::bcCurve()   const { return currentBC(m_layer, m_tri, m_e).curve; }
QString MeshEdgePropertyAdapter::bcGroup()   const { return currentBC(m_layer, m_tri, m_e).group; }
double  MeshEdgePropertyAdapter::bcConveyance() const { return currentBC(m_layer, m_tri, m_e).conveyance; }

template <typename F>
void MeshEdgePropertyAdapter::mutate(F &&fn)
{
    if (!m_layer) return;
    mesh::MeshEdgeBC bc = currentBC(m_layer, m_tri, m_e);
    fn(bc);
    m_layer->applyMeshEdgeBC(m_tri, m_e, bc);
}

void MeshEdgePropertyAdapter::setBCType(int newType)
{
    mutate([newType](mesh::MeshEdgeBC &bc) {
        bc.type = static_cast<mesh::MeshBCTypes::Type>(newType);
    });
}

void MeshEdgePropertyAdapter::setBCHead(double v)
{
    mutate([v](mesh::MeshEdgeBC &bc) { bc.head = v; });
}

void MeshEdgePropertyAdapter::setBCSlope(double v)
{
    mutate([v](mesh::MeshEdgeBC &bc) { bc.slope = v; });
}

void MeshEdgePropertyAdapter::setBCFlow(double v)
{
    mutate([v](mesh::MeshEdgeBC &bc) { bc.flow = v; });
}

void MeshEdgePropertyAdapter::setBCTseries(const QString &name)
{
    mutate([&](mesh::MeshEdgeBC &bc) { bc.tseries = name; });
}

void MeshEdgePropertyAdapter::setBCCurve(const QString &name)
{
    mutate([&](mesh::MeshEdgeBC &bc) { bc.curve = name; });
}

void MeshEdgePropertyAdapter::setBCGroup(const QString &name)
{
    mutate([&](mesh::MeshEdgeBC &bc) { bc.group = name; });
}

void MeshEdgePropertyAdapter::setBCConveyance(double v)
{
    // ψ goes through the dedicated layer helper rather than the BC mutate
    // template — the helper enforces the [0, 1] range and mirrors interior
    // edges (the BC template uses applyMeshEdgeBC which would skip both).
    if (!m_layer) return;
    m_layer->applyMeshEdgeConveyance(m_tri, m_e, v);
}

void MeshEdgePropertyAdapter::onLayerAttributeChanged(const QString &refName)
{
    if (refName == m_refName) emit changed();
}

QString MeshEdgePropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QStringLiteral("triIdx"))     return QCoreApplication::translate("MeshEdge", "Triangle");
    if (property == QStringLiteral("edgeLocal"))  return QCoreApplication::translate("MeshEdge", "Edge (0..2)");
    if (property == QStringLiteral("isBoundary")) return QCoreApplication::translate("MeshEdge", "Boundary?");
    if (property == QStringLiteral("length"))     return QCoreApplication::translate("MeshEdge", "Length");
    if (property == QStringLiteral("bcType"))     return QCoreApplication::translate("MeshEdge", "BC type");
    if (property == QStringLiteral("bcHead"))     return QCoreApplication::translate("MeshEdge", "BC stage");
    if (property == QStringLiteral("bcSlope"))    return QCoreApplication::translate("MeshEdge", "BC bed slope");
    if (property == QStringLiteral("bcFlow"))     return QCoreApplication::translate("MeshEdge", "BC flow / m");
    if (property == QStringLiteral("bcTseries"))  return QCoreApplication::translate("MeshEdge", "BC timeseries");
    if (property == QStringLiteral("bcCurve"))    return QCoreApplication::translate("MeshEdge", "BC rating curve");
    if (property == QStringLiteral("bcGroup"))    return QCoreApplication::translate("MeshEdge", "BC group");
    if (property == QStringLiteral("bcConveyance")) return QCoreApplication::translate("MeshEdge", "Conveyance (ψ)");
    return property;
}
