/*!
 * \file   meshattributetablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/meshattributetablemodel.h"

#include "core/unitsystem.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/meshcommands.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshobjectref.h"
#include "ui/properties/dataobjectref.h"

#include <QCoreApplication>
#include <QLineF>

#include <cmath>
#include <limits>

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;
using openswmmvis::UnitKind;

namespace {

/*! Resolve the semantic unit to a display string for the active flow-units
 *  system. Mirrors the private helper in swmmattributetablemodel.cpp; only the
 *  handful of kinds this model actually uses are distinguished. */
QString unitLabel(UnitKind kind)
{
    auto *us = UnitSystem::instance();
    switch (kind) {
    case UnitKind::None:   return {};
    case UnitKind::Length: return us ? us->lengthLabel() : QStringLiteral("ft");
    default:               return {};
    }
}

ColumnSpec ro(const QString &key, const QString &label,
              UnitKind unit = UnitKind::None, const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key; c.label = label; c.unit = unit; c.tooltip = tooltip;
    c.editor = EditorKind::ReadOnly;
    return c;
}

ColumnSpec num(const QString &key, const QString &label,
               double minV, double maxV, int decimals,
               UnitKind unit = UnitKind::None, const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key; c.label = label; c.tooltip = tooltip;
    c.editor   = EditorKind::Numeric;
    c.setter   = key;          // non-empty ⇒ editable; dispatch is by key
    c.minValue = minV; c.maxValue = maxV; c.decimals = decimals;
    c.unit     = unit;
    return c;
}

ColumnSpec text(const QString &key, const QString &label,
                const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key; c.label = label; c.tooltip = tooltip;
    c.editor = EditorKind::Text;
    c.setter = key;
    return c;
}

/*! A cell that names another model object. Rendered by CompoundEditDelegate →
 *  DataObjectPickerEditor: a non-editable combo of the objects that actually
 *  exist, plus a "…" button onto that family's editor. Free text cannot reach
 *  the model through it, which is the referential-integrity guarantee — a mesh
 *  edge cannot cite a time series nobody defined, and a vertex cannot couple to
 *  a node that isn't there. */
ColumnSpec picker(const QString &key, const QString &label,
                  const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key; c.label = label; c.tooltip = tooltip;
    c.editor = EditorKind::Compound;
    c.setter = key;
    return c;
}

/*! A column naming another object: a closed picker when a model is bound,
 *  plain text when one is not (a mesh can be opened without its .inp, and a
 *  picker with nothing to offer would be a dead cell). */
ColumnSpec refCol(bool canPick, const QString &key, const QString &label,
                  const QString &tooltip = {})
{
    return canPick ? picker(key, label, tooltip) : text(key, label, tooltip);
}

/*! The BC-type combo, in the same order the mesh-editing toolbar shows. */
ColumnSpec bcTypeCol()
{
    using mesh::MeshBCTypes;
    ColumnSpec c;
    c.key    = QStringLiteral("bcType");
    c.label  = QCoreApplication::translate("MeshAttributeTableModel", "BC Type");
    c.editor = EditorKind::Enum;
    c.setter = c.key;
    for (int t = int(MeshBCTypes::Type::Wall);
         t <= int(MeshBCTypes::Type::RatingCurve); ++t) {
        c.enumValues.append(QVariantList{
            MeshBCTypes::label(static_cast<MeshBCTypes::Type>(t)), t});
    }
    return c;
}

/*! Does boundary-condition field \p key carry a value for type \p t?
 *
 *  Each BC type reads exactly one parameter, so the others are noise: a Wall
 *  has none, a Normal Flow reads only its bed slope, a Rating Curve only its
 *  curve. Mirrors the mesh toolbar, which shows one parameter widget per type
 *  and hides the rest — here the inapplicable cells render "—" and refuse
 *  edits, so a boundary can't be left carrying a stage the engine will ignore
 *  or a curve reference nothing reads. `group` is orthogonal to the type and
 *  applies throughout. */
bool bcFieldApplies(mesh::MeshBCTypes::Type t, const QString &key)
{
    using T = mesh::MeshBCTypes::Type;
    if (key == QLatin1String("group")) return true;
    switch (t) {
    case T::Wall:                return false;
    case T::NormalFlow:          return key == QLatin1String("slope");
    case T::SpecifiedStageConst: return key == QLatin1String("head");
    case T::SpecifiedStageTS:    return key == QLatin1String("tseries");
    case T::SpecifiedFlowConst:  return key == QLatin1String("flow");
    case T::SpecifiedFlowTS:     return key == QLatin1String("tseries");
    case T::RatingCurve:         return key == QLatin1String("curve");
    }
    return false;
}

/*! Endpoint vertex indices of local edge \p e on triangle \p tri.
 *  Convention matches the layer's: e0 = (v1,v2), e1 = (v2,v0), e2 = (v0,v1). */
bool edgeEndpoints(const mesh::MeshResult &m, int tri, int e, int *va, int *vb)
{
    if (tri < 0 || tri >= m.triangles.size() || e < 0 || e > 2) return false;
    const mesh::MeshTriangle &t = m.triangles[tri];
    switch (e) {
    case 0: *va = t.v1; *vb = t.v2; break;
    case 1: *va = t.v2; *vb = t.v0; break;
    default: *va = t.v0; *vb = t.v1; break;
    }
    return *va >= 0 && *va < m.vertices.size()
        && *vb >= 0 && *vb < m.vertices.size();
}

/*! Placeholder shown in a cell that does not apply to this row (a BC field on
 *  an interior edge, a per-cell parameter awaiting engine support). */
QString notApplicable() { return QStringLiteral("—"); }

} // namespace

// ===========================================================================
// Construction / binding
// ===========================================================================

MeshAttributeTableModel::MeshAttributeTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    rebuildColumns();
}

void MeshAttributeTableModel::setCanvas(MapCanvas *canvas)
{
    m_canvas = canvas;
}

void MeshAttributeTableModel::setModelLayer(SWMMModelLayer *layer)
{
    // Compare pick-ability, not just the pointer: the panel binds the layer
    // before the engine is open, so the SAME layer goes from "nothing to offer"
    // to "here are the nodes" and the columns have to follow.
    if (m_modelLayer == layer && canPick() == (layer && layer->engine()))
        return;
    // The reference columns switch editor kind on this (picker ⇄ plain text),
    // so the schema itself changes — a reset, not a dataChanged.
    beginResetModel();
    m_modelLayer = layer;
    rebuildColumns();
    endResetModel();
}

bool MeshAttributeTableModel::canPick() const
{
    return m_modelLayer && m_modelLayer->engine();
}

QVariant MeshAttributeTableModel::pickerRef(const QString &key,
                                            const QString &current) const
{
    if (!m_modelLayer || !m_modelLayer->engine()) return {};
    DataObjectRef ref;
    ref.engine      = m_modelLayer->engine();
    ref.layer       = m_modelLayer;
    ref.currentName = current;
    if (key == QLatin1String("coupledNode"))   ref.kind = DataObjectRef::Node;
    else if (key == QLatin1String("tseries"))  ref.kind = DataObjectRef::TimeSeries;
    // The engine resolves a 2D rating curve against the whole table namespace
    // (SurfaceRouter2D's ctx.table_names lookup), so the picker offers every
    // curve rather than only those typed RATING — narrowing it here would hide
    // choices the engine accepts.
    else if (key == QLatin1String("curve"))    ref.kind = DataObjectRef::AnyCurve;
    else return {};
    return QVariant::fromValue(ref);
}

void MeshAttributeTableModel::setSource(SWMM2DMeshLayer *layer, Kind kind)
{
    beginResetModel();
    // Unconditional: connectLayer() below re-establishes everything, and one of
    // the connections is a lambda, which Qt::UniqueConnection cannot de-dup —
    // so re-binding the SAME layer would otherwise stack a second copy.
    if (m_layer)
        disconnect(m_layer, nullptr, this, nullptr);
    m_layer = layer;
    m_kind  = kind;
    connectLayer(layer);
    rebuildColumns();
    rebuildEdgeRows();
    endResetModel();
}

void MeshAttributeTableModel::connectLayer(SWMM2DMeshLayer *layer)
{
    if (!layer) return;
    connect(layer, &SWMM2DMeshLayer::attributeChanged,
            this,  &MeshAttributeTableModel::onLayerAttributeChanged,
            Qt::UniqueConnection);
    // Bulk mutations carry no single element ref — repaint every row rather
    // than trying to work out which ones moved.
    connect(layer, &SWMM2DMeshLayer::meshEditsChanged, this, [this]() {
        const int rows = rowCount(), cols = columnCount();
        if (rows > 0 && cols > 0)
            emit dataChanged(index(0, 0), index(rows - 1, cols - 1));
    });
    // A progressive load only gains its boundary flags + vertex adjacency here,
    // and the Edge rows are built from both.
    connect(layer, &SWMM2DMeshLayer::sceneGeometryReady,
            this,  &MeshAttributeTableModel::reload, Qt::UniqueConnection);
    connect(layer, &SWMM2DMeshLayer::selectionInvalidated,
            this,  &MeshAttributeTableModel::reload, Qt::UniqueConnection);
}

void MeshAttributeTableModel::reload()
{
    beginResetModel();
    rebuildEdgeRows();
    endResetModel();
}

// ===========================================================================
// Schema
// ===========================================================================

void MeshAttributeTableModel::rebuildColumns()
{
    m_columnSpecs.clear();
    m_firstCellParamCol = -1;

    switch (m_kind) {
    case Kind::Vertex:
        m_columnSpecs
            << ro(QStringLiteral("Index"), tr("Index"))
            << ro(QStringLiteral("X"), tr("X"), UnitKind::None,
                  tr("Vertex easting in project map units. Move a vertex on "
                     "the map — coordinates are not editable here."))
            << ro(QStringLiteral("Y"), tr("Y"), UnitKind::None,
                  tr("Vertex northing in project map units. Move a vertex on "
                     "the map — coordinates are not editable here."))
            << ro(QStringLiteral("Marker"), tr("Marker"), UnitKind::None,
                  tr("Triangle input/output point marker carried through "
                     "mesh generation."))
            << num(QStringLiteral("z"), tr("Elevation"), -1.0e6, 1.0e6, 3,
                   UnitKind::Length,
                   tr("Bed elevation in project vertical units "
                      "([2D_VERTICES] Z column)."))
            << text(QStringLiteral("tag"), tr("Tag"),
                    tr("Descriptive vertex tag ([2D_VERTICES] TAG column)."))
            << refCol(canPick(), QStringLiteral("coupledNode"), tr("Coupled Node"),
                      tr("Coupled SWMM node ([2D_VERTEX_NODE_MAP]). Pick from "
                         "the model's nodes; blank = uncoupled."))
            << num(QStringLiteral("couplingCd"), tr("Coupling Cd"),
                   0.001, 1.0, 3, UnitKind::None,
                   tr("Coupling discharge coefficient ([2D_VERTEX_NODE_MAP] CD "
                      "column). Coupled vertices only; default 0.65."))
            // The AREA column is metres² regardless of the project's flow
            // units, so the unit is spelled into the label rather than
            // resolved through UnitKind (which would read ft² in US units).
            << num(QStringLiteral("couplingArea"), tr("Coupling Area (m²)"),
                   0.0001, 1.0e6, 3, UnitKind::None,
                   tr("Coupling exchange area in m² ([2D_VERTEX_NODE_MAP] AREA "
                      "column). Coupled vertices only; default 1.0."));
        break;

    case Kind::Edge:
        m_columnSpecs
            << ro(QStringLiteral("Edge"), tr("Edge"), UnitKind::None,
                  tr("Owning triangle and local edge, as triangle:edge. An "
                     "interior edge is listed once, under its lower slot."))
            << ro(QStringLiteral("Boundary"), tr("Boundary"), UnitKind::None,
                  tr("Whether the edge lies on the mesh outline (or a hole). "
                     "Boundary conditions apply to these edges only."))
            << ro(QStringLiteral("Length"), tr("Length (map units)"))
            << num(QStringLiteral("conveyance"), tr("Conveyance"), 0.0, 1.0, 4,
                   UnitKind::None,
                   tr("Flux attenuation multiplier in [0, 1]; 1 = "
                      "unrestricted. Applies to interior edges too, and is "
                      "mirrored onto the neighbouring half."))
            << bcTypeCol()
            << num(QStringLiteral("head"), tr("Stage"), -1.0e9, 1.0e9, 4,
                   UnitKind::Length,
                   tr("Fixed water-surface elevation for a Specified Stage "
                      "(constant) boundary."))
            << num(QStringLiteral("slope"), tr("Bed Slope"), 0.0, 1.0e6, 6,
                   UnitKind::None,
                   tr("Bed slope for a Normal Flow boundary. Must be > 0 — the "
                      "engine treats 0 as a wall."))
            << num(QStringLiteral("flow"), tr("Flow"), -1.0e12, 1.0e12, 4,
                   UnitKind::None,
                   tr("Discharge per metre of edge for a Specified Flow "
                      "(constant) boundary."))
            << refCol(canPick(), QStringLiteral("tseries"), tr("Time Series"),
                      tr("Time series driving a Specified Stage / Flow (TS) "
                         "boundary. Pick from the project's time series."))
            << refCol(canPick(), QStringLiteral("curve"), tr("Rating Curve"),
                      tr("Stage → flow curve for a Rating Curve boundary. Pick "
                         "from the project's curves."))
            << text(QStringLiteral("group"), tr("Group"),
                    tr("Optional named boundary group."));
        break;

    case Kind::Cell:
        m_columnSpecs
            << ro(QStringLiteral("Index"), tr("Index"))
            << ro(QStringLiteral("Area"), tr("Area (map units²)"))
            << ro(QStringLiteral("Centroid X"), tr("Centroid X"))
            << ro(QStringLiteral("Centroid Y"), tr("Centroid Y"))
            << text(QStringLiteral("tag"), tr("Tag"),
                    tr("Descriptive cell tag ([2D_TRIANGLES] TAG column)."));
        m_firstCellParamCol = int(m_columnSpecs.size());
        // One column per registry entry, so a new per-cell parameter shows up
        // here the moment it is added to mesh::cellParamSpecs().
        for (const mesh::CellParamSpec &s : mesh::cellParamSpecs()) {
            const QString key = QString::fromUtf8(s.key);
            if (!s.enabled) {
                m_columnSpecs << ro(key, s.label,
                                    s.lengthUnit ? UnitKind::Length
                                                 : UnitKind::None,
                                    s.tooltip);
            } else {
                m_columnSpecs << num(key, s.label, s.min, s.max, s.decimals,
                                     s.lengthUnit ? UnitKind::Length
                                                  : UnitKind::None,
                                     s.tooltip);
            }
        }
        break;
    }
}

void MeshAttributeTableModel::rebuildEdgeRows()
{
    m_edgeSlots.clear();
    m_slotRow.clear();
    if (!m_layer || m_kind != Kind::Edge) return;
    // findEdgeNeighbour needs the vertex adjacency, which a progressive load
    // only builds in its background pass. Listing 3T rows before then would
    // double-count every interior edge, so stay empty until it is ready — the
    // sceneGeometryReady connection re-runs this.
    if (!m_layer->sceneGeometryComplete()) return;

    const int nTri = m_layer->triangleCount();
    m_edgeSlots.reserve((3 * nTri + 1) / 2);
    m_slotRow.reserve(3 * nTri);
    for (int t = 0; t < nTri; ++t) {
        for (int e = 0; e < 3; ++e) {
            const int flat = t * 3 + e;
            // Scanning in increasing flat order means the first slot reached
            // is always the lower one, which becomes the canonical row.
            if (m_slotRow.contains(flat)) continue;
            const int row = int(m_edgeSlots.size());
            m_edgeSlots.append(flat);
            m_slotRow.insert(flat, row);
            const QPair<int,int> nbr = m_layer->findEdgeNeighbour(t, e);
            if (nbr.first >= 0 && nbr.second >= 0)
                m_slotRow.insert(nbr.first * 3 + nbr.second, row);
        }
    }
}

// ===========================================================================
// Row identity
// ===========================================================================

int MeshAttributeTableModel::slotForRow(int row) const
{
    if (m_kind != Kind::Edge) return -1;
    if (row < 0 || row >= m_edgeSlots.size()) return -1;
    return m_edgeSlots[row];
}

SWMMObjectRef MeshAttributeTableModel::refForRow(int row) const
{
    if (!m_layer || row < 0 || row >= rowCount()) return {};
    const QString path = m_layer->sourcePath();
    switch (m_kind) {
    case Kind::Vertex: return mesh::MeshObjectRef::vertex(path, row);
    case Kind::Cell:   return mesh::MeshObjectRef::cell(path, row);
    case Kind::Edge: {
        const int flat = slotForRow(row);
        if (flat < 0) return {};
        return mesh::MeshObjectRef::edge(path, flat / 3, flat % 3);
    }
    }
    return {};
}

int MeshAttributeTableModel::rowForRef(const SWMMObjectRef &ref) const
{
    if (!m_layer) return -1;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_layer->sourcePath());
    QString lk;
    switch (m_kind) {
    case Kind::Vertex: {
        int v = -1;
        if (!mesh::MeshObjectRef::parseVertex(ref, &lk, &v)) return -1;
        if (lk != wantKey || v < 0 || v >= m_layer->vertexCount()) return -1;
        return v;
    }
    case Kind::Cell: {
        int t = -1;
        if (!mesh::MeshObjectRef::parseCell(ref, &lk, &t)) return -1;
        if (lk != wantKey || t < 0 || t >= m_layer->triangleCount()) return -1;
        return t;
    }
    case Kind::Edge: {
        int t = -1, e = -1;
        if (!mesh::MeshObjectRef::parseEdge(ref, &lk, &t, &e)) return -1;
        if (lk != wantKey) return -1;
        // Either half of an interior pair maps to the one canonical row.
        return m_slotRow.value(t * 3 + e, -1);
    }
    }
    return -1;
}

QRectF MeshAttributeTableModel::elementExtent(int row, bool *ok) const
{
    if (ok) *ok = false;
    if (!m_layer || row < 0 || row >= rowCount()) return {};
    const mesh::MeshResult &m = m_layer->mesh();
    switch (m_kind) {
    case Kind::Vertex: {
        if (row >= m.vertices.size()) return {};
        const QPointF p = m.vertices[row].xy;
        if (ok) *ok = true;
        return QRectF(p, p);
    }
    case Kind::Edge: {
        const int flat = slotForRow(row);
        int va = -1, vb = -1;
        if (flat < 0 || !edgeEndpoints(m, flat / 3, flat % 3, &va, &vb)) return {};
        if (ok) *ok = true;
        return QRectF(m.vertices[va].xy, m.vertices[vb].xy).normalized();
    }
    case Kind::Cell: {
        if (row >= m.triangles.size()) return {};
        const mesh::MeshTriangle &t = m.triangles[row];
        if (t.v0 < 0 || t.v0 >= m.vertices.size()
            || t.v1 < 0 || t.v1 >= m.vertices.size()
            || t.v2 < 0 || t.v2 >= m.vertices.size())
            return {};
        QRectF r = QRectF(m.vertices[t.v0].xy, m.vertices[t.v1].xy).normalized();
        const QPointF c = m.vertices[t.v2].xy;
        if (ok) *ok = true;
        return r.united(QRectF(c, c));
    }
    }
    return {};
}

// ===========================================================================
// QAbstractTableModel
// ===========================================================================

int MeshAttributeTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_layer) return 0;
    switch (m_kind) {
    case Kind::Vertex: return m_layer->vertexCount();
    case Kind::Cell:   return m_layer->triangleCount();
    case Kind::Edge:   return int(m_edgeSlots.size());
    }
    return 0;
}

int MeshAttributeTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_columnSpecs.size());
}

bool MeshAttributeTableModel::rowIsBoundaryEdge(int row) const
{
    if (!m_layer || m_kind != Kind::Edge) return false;
    const int flat = slotForRow(row);
    if (flat < 0) return false;
    return m_layer->isBoundaryEdge(flat / 3, flat % 3);
}

QVariant MeshAttributeTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_layer) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole
        && role != Qt::ToolTipRole)
        return {};

    const int row = index.row(), col = index.column();
    if (row < 0 || row >= rowCount() || col < 0 || col >= m_columnSpecs.size())
        return {};
    const ColumnSpec &spec = m_columnSpecs[col];
    const mesh::MeshResult &m = m_layer->mesh();

    switch (m_kind) {
    case Kind::Vertex: {
        if (row >= m.vertices.size()) return {};
        const mesh::MeshVertex &v = m.vertices[row];
        if (spec.key == QLatin1String("Index"))        return row;
        if (spec.key == QLatin1String("X"))            return v.xy.x();
        if (spec.key == QLatin1String("Y"))            return v.xy.y();
        if (spec.key == QLatin1String("Marker"))       return v.marker;
        if (spec.key == QLatin1String("z"))            return v.z;
        if (spec.key == QLatin1String("tag"))          return v.tag;
        if (spec.key == QLatin1String("coupledNode")) {
            // EditRole hands the delegate a picker over the model's nodes;
            // DisplayRole stays a plain string so the query bar, Copy and CSV
            // export see the id and nothing else.
            if (role == Qt::EditRole) {
                const QVariant ref = pickerRef(spec.key, v.coupledNode);
                if (ref.isValid()) return ref;
            }
            return v.coupledNode;
        }
        // Cd / Area are meaningless without a coupling, and the layer refuses
        // to write them there — render the same "not applicable" dash the BC
        // columns use rather than a default that looks like real data.
        if (spec.key == QLatin1String("couplingCd"))
            return v.coupledNode.isEmpty()
                       ? QVariant(role == Qt::EditRole ? QVariant()
                                                       : QVariant(notApplicable()))
                       : QVariant(v.couplingCd);
        if (spec.key == QLatin1String("couplingArea"))
            return v.coupledNode.isEmpty()
                       ? QVariant(role == Qt::EditRole ? QVariant()
                                                       : QVariant(notApplicable()))
                       : QVariant(v.couplingArea);
        return {};
    }

    case Kind::Edge: {
        const int flat = slotForRow(row);
        if (flat < 0) return {};
        const int tri = flat / 3, e = flat % 3;
        if (spec.key == QLatin1String("Edge"))
            return QStringLiteral("%1:%2").arg(tri).arg(e);
        if (spec.key == QLatin1String("Boundary"))
            return m_layer->isBoundaryEdge(tri, e) ? tr("Yes") : tr("No");
        if (spec.key == QLatin1String("Length")) {
            int va = -1, vb = -1;
            if (!edgeEndpoints(m, tri, e, &va, &vb)) return {};
            return QLineF(m.vertices[va].xy, m.vertices[vb].xy).length();
        }
        const QVector<mesh::MeshEdgeBC> &bcs = m_layer->edgeBCs();
        if (flat >= bcs.size()) return {};
        const mesh::MeshEdgeBC &bc = bcs[flat];
        if (spec.key == QLatin1String("conveyance")) return bc.conveyance;
        // Every remaining column is a boundary-condition field.
        if (!m_layer->isBoundaryEdge(tri, e))
            return role == Qt::EditRole ? QVariant() : QVariant(notApplicable());
        if (spec.key == QLatin1String("bcType")) {
            if (role == Qt::EditRole) return int(bc.type);
            return mesh::MeshBCTypes::label(bc.type);
        }
        // A parameter this BC type does not read carries no meaning — show it
        // as inapplicable rather than as a stale number the engine ignores.
        if (!bcFieldApplies(bc.type, spec.key))
            return role == Qt::EditRole ? QVariant() : QVariant(notApplicable());
        if (spec.key == QLatin1String("head"))    return bc.head;
        if (spec.key == QLatin1String("slope"))   return bc.slope;
        if (spec.key == QLatin1String("flow"))    return bc.flow;
        if (spec.key == QLatin1String("tseries")
            || spec.key == QLatin1String("curve")) {
            const QString cur = (spec.key == QLatin1String("tseries"))
                                    ? bc.tseries : bc.curve;
            if (role == Qt::EditRole) {
                const QVariant ref = pickerRef(spec.key, cur);
                if (ref.isValid()) return ref;
            }
            return cur;
        }
        if (spec.key == QLatin1String("group"))   return bc.group;
        return {};
    }

    case Kind::Cell: {
        if (row >= m.triangles.size()) return {};
        const mesh::MeshTriangle &t = m.triangles[row];
        if (spec.key == QLatin1String("Index")) return row;
        if (spec.key == QLatin1String("Area"))  return mesh::triangleArea(m, row);
        if (spec.key == QLatin1String("Centroid X")
            || spec.key == QLatin1String("Centroid Y")) {
            if (t.v0 < 0 || t.v0 >= m.vertices.size()
                || t.v1 < 0 || t.v1 >= m.vertices.size()
                || t.v2 < 0 || t.v2 >= m.vertices.size())
                return {};
            const QPointF c = (m.vertices[t.v0].xy + m.vertices[t.v1].xy
                               + m.vertices[t.v2].xy) / 3.0;
            return spec.key == QLatin1String("Centroid X") ? c.x() : c.y();
        }
        if (spec.key == QLatin1String("tag")) return t.tag;
        // Per-cell parameter: unset reads back as the registry default, which
        // is what the engine would use; engine-pending keys have no value.
        const QByteArray key = spec.key.toUtf8();
        const mesh::CellParamSpec *cs = mesh::cellParamSpec(key);
        if (!cs) return {};
        if (!cs->enabled)
            return role == Qt::EditRole ? QVariant() : QVariant(notApplicable());
        const double raw = mesh::cellParamValue(m, row, key);
        return std::isfinite(raw) ? raw : cs->defaultValue;
    }
    }
    return {};
}

Qt::ItemFlags MeshAttributeTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid() || !m_layer) return f;
    const int col = index.column();
    if (col < 0 || col >= m_columnSpecs.size()) return f;
    const ColumnSpec &spec = m_columnSpecs[col];
    if (spec.editor == EditorKind::ReadOnly) return f;

    switch (m_kind) {
    case Kind::Vertex:
        // Coupling coefficients only exist once the vertex carries a node.
        if (spec.key == QLatin1String("couplingCd")
            || spec.key == QLatin1String("couplingArea")) {
            const mesh::MeshResult &m = m_layer->mesh();
            const int row = index.row();
            if (row < 0 || row >= m.vertices.size()
                || m.vertices[row].coupledNode.isEmpty())
                return f;
        }
        break;
    case Kind::Edge: {
        // Conveyance applies to every edge; the BC fields to boundary edges
        // only — same gating the toolbar applies.
        if (spec.key == QLatin1String("conveyance")) break;
        if (!rowIsBoundaryEdge(index.row())) return f;
        // And within a boundary edge, only the parameter its type actually
        // reads. Set the type first, then its value.
        if (spec.key != QLatin1String("bcType")) {
            const int flat = slotForRow(index.row());
            const QVector<mesh::MeshEdgeBC> &bcs = m_layer->edgeBCs();
            if (flat < 0 || flat >= bcs.size()) return f;
            if (!bcFieldApplies(bcs[flat].type, spec.key)) return f;
        }
        break;
    }
    case Kind::Cell:
        break;
    }
    return f | Qt::ItemIsEditable;
}

bool MeshAttributeTableModel::setData(const QModelIndex &index,
                                      const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || !m_layer) return false;
    if (!(flags(index) & Qt::ItemIsEditable)) return false;
    const int row = index.row(), col = index.column();
    if (row < 0 || row >= rowCount() || col < 0 || col >= m_columnSpecs.size())
        return false;
    const QByteArray key = m_columnSpecs[col].key.toUtf8();

    // A picker cell round-trips as a DataObjectRef; the id it settled on is
    // the only part the mesh stores. The combo is closed, so that id always
    // names an object that exists (or is empty, clearing the reference).
    QVariant v = value;
    if (v.userType() == qMetaTypeId<DataObjectRef>())
        v = v.value<DataObjectRef>().currentName;

    int changed = 0;
    switch (m_kind) {
    case Kind::Vertex:
        changed = mesh::pushVertexParamEdit(m_layer, {row}, key, v, m_canvas);
        break;
    case Kind::Edge: {
        const int flat = slotForRow(row);
        if (flat < 0) return false;
        changed = mesh::pushEdgeParamEdit(
            m_layer, {qMakePair(flat / 3, flat % 3)}, key, v, m_canvas);
        break;
    }
    case Kind::Cell:
        if (key == "tag")
            changed = mesh::pushCellTagEdit(m_layer, {row}, v.toString(),
                                            m_canvas);
        else
            changed = mesh::pushCellParamEdit(m_layer, {row}, key,
                                              v.toDouble(), m_canvas);
        break;
    }
    if (changed <= 0) return false;

    // The layer's attributeChanged already drove dataChanged for the row;
    // objectEdited is the user-edit-only signal other views key off.
    emit objectEdited(refForRow(row).name);
    return true;
}

QVariant MeshAttributeTableModel::headerData(int section,
                                             Qt::Orientation orientation,
                                             int role) const
{
    if (orientation == Qt::Horizontal && section >= 0
        && section < m_columnSpecs.size()) {
        const ColumnSpec &spec = m_columnSpecs[section];
        const QString u = unitLabel(spec.unit);
        if (role == Qt::ToolTipRole) {
            if (!spec.tooltip.isEmpty()) return spec.tooltip;
            return u.isEmpty() ? QVariant() : QVariant(tr("Units: %1").arg(u));
        }
        if (role == Qt::DisplayRole)
            return u.isEmpty() ? spec.label : tr("%1 (%2)").arg(spec.label, u);
    }
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return section + 1;
    return {};
}

// ===========================================================================
// Layer signals
// ===========================================================================

void MeshAttributeTableModel::onLayerAttributeChanged(const QString &refName)
{
    if (!m_layer || m_columnSpecs.isEmpty()) return;
    // The signal carries only the encoded name; rebuild the ref with the kind
    // this view lists and let rowForRef reject the ones that belong elsewhere.
    static const SWMMObjectRef::ObjectType kTypes[] = {
        SWMMObjectRef::MeshVertex, SWMMObjectRef::MeshEdge,
        SWMMObjectRef::MeshCell};
    const SWMMObjectRef ref(kTypes[int(m_kind)], refName);
    const int row = rowForRef(ref);
    if (row < 0) return;
    emit dataChanged(index(row, 0), index(row, int(m_columnSpecs.size()) - 1));
}
