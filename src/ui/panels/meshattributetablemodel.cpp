/**
 * @file      meshattributetablemodel.cpp
 * @brief     Tabular model over one 2D mesh layer's vertices, edges or cells, so the Attribute
 *            Table dock can list and edit mesh elements the same way it lists SWMM objects.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/attributevalue.h"
#include "ui/panels/meshattributetablemodel.h"

#include "core/unitsystem.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/meshcommands.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshinfil.h"
#include "mesh/meshobjectref.h"
#include "ui/properties/dataobjectref.h"

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QLineF>
#include <QPalette>

#include <cmath>
#include <limits>

using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;
using openswmmvis::UnitKind;

namespace
{

/*! Resolve the semantic unit to a display string for the active flow-units
 *  system. Mirrors the private helper in swmmattributetablemodel.cpp; only the
 *  handful of kinds this model actually uses are distinguished.
 * @param[in] kind Only Length distinguished, None/other kinds empty.
 * @return Live global length label, ft without singleton. No mesh-SI special case, conversion or
 *   source query.
 */
QString unitLabel(UnitKind kind)
{
    auto *us = UnitSystem::instance();
    switch (kind) {
    case UnitKind::None:
        return {};
    case UnitKind::Length:
        return us ? us->lengthLabel() : QStringLiteral("ft");
    default:
        return {};
    }
}

/**
 * @param[in] key Exact lookup/setter key copied.
 * @param[in] label Display label copied.
 * @param[in] tooltip Optional raw explanatory text copied.
 * @param[in] unit Copied semantic presentation kind, default None.
 * @return ReadOnly column with empty setter token. No provider/candidate lookup, bounds
 *   normalization, model notification or source write.
 */
ColumnSpec ro(const QString &key, const QString &label, UnitKind unit = UnitKind::None,
              const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key;
    c.label = label;
    c.unit = unit;
    c.tooltip = tooltip;
    c.editor = EditorKind::ReadOnly;
    return c;
}

/**
 * @param[in] key Exact lookup/setter key copied.
 * @param[in] label Display label copied.
 * @param[in] tooltip Optional raw explanatory text copied.
 * @param[in] minV Raw editor lower bound, not direct-write validation.
 * @param[in] maxV Raw editor upper bound, not direct-write validation.
 * @param[in] decimals Raw editor precision; does not round stored data.
 * @param[in] unit Copied presentation kind, default None.
 * @return Numeric column with setter token equal to key. No provider/candidate lookup, bounds
 *   normalization, model notification or source write.
 */
ColumnSpec num(const QString &key, const QString &label, double minV, double maxV, int decimals,
               UnitKind unit = UnitKind::None, const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key;
    c.label = label;
    c.tooltip = tooltip;
    c.editor = EditorKind::Numeric;
    c.setter = key; // non-empty ⇒ editable; dispatch is by key
    c.minValue = minV;
    c.maxValue = maxV;
    c.decimals = decimals;
    c.unit = unit;
    return c;
}

/**
 * @param[in] key Exact lookup/setter key copied.
 * @param[in] label Display label copied.
 * @param[in] tooltip Optional raw explanatory text copied.
 * @return Text column with setter token equal to key. No provider/candidate lookup, bounds
 *   normalization, model notification or source write.
 */
ColumnSpec text(const QString &key, const QString &label, const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key;
    c.label = label;
    c.tooltip = tooltip;
    c.editor = EditorKind::Text;
    c.setter = key;
    return c;
}

/*! A cell that names another model object. Rendered by CompoundEditDelegate →
 *  DataObjectPickerEditor: a non-editable combo of the objects that actually
 *  exist, plus a "…" button onto that family's editor. Free text cannot reach
 *  model through normal closed-combo selection. Direct setData callers and stale
 *  references still require validation by the underlying edit path.
 * @param[in] key Exact lookup/setter key copied.
 * @param[in] label Display label copied.
 * @param[in] tooltip Optional raw explanatory text copied.
 * @return Compound column with setter token equal to key; closed GUI picker is presentation, direct
 *   setData can still supply raw names. No provider/candidate lookup, bounds normalization, model
 *   notification or source write.
 */
ColumnSpec picker(const QString &key, const QString &label, const QString &tooltip = {})
{
    ColumnSpec c;
    c.key = key;
    c.label = label;
    c.tooltip = tooltip;
    c.editor = EditorKind::Compound;
    c.setter = key;
    return c;
}

/*! A column naming another object: a closed picker when a model is bound,
 *  plain text when one is not (a mesh can be opened without its .inp, and a
 *  picker with nothing to offer would be a dead cell).
 * @param[in] key Exact lookup/setter key copied.
 * @param[in] label Display label copied.
 * @param[in] tooltip Optional raw explanatory text copied.
 * @param[in] canPick True chooses Compound picker, false plain Text; no candidate/engine check
 *   inside helper.
 * @return Picker or text spec selected from supplied availability. No provider/candidate lookup,
 *   bounds normalization, model notification or source write.
 */
ColumnSpec refCol(bool canPick, const QString &key, const QString &label,
                  const QString &tooltip = {})
{
    return canPick ? picker(key, label, tooltip) : text(key, label, tooltip);
}

/*! The BC-type combo, in the same order the mesh-editing toolbar shows.
 * @return Editable Enum bcType column from MeshBCTypes Wall through RatingCurve inclusive,
 *   [label,stored-int] pairs in toolbar order. No source access, boundary gate or enum validation
 *   here.
 */
ColumnSpec bcTypeCol()
{
    using mesh::MeshBCTypes;
    ColumnSpec c;
    c.key = QStringLiteral("bcType");
    c.label = QCoreApplication::translate("MeshAttributeTableModel", "BC Type");
    c.editor = EditorKind::Enum;
    c.setter = c.key;
    for (int t = int(MeshBCTypes::Type::Wall); t <= int(MeshBCTypes::Type::RatingCurve); ++t) {
        c.enumValues.append(QVariantList{MeshBCTypes::label(static_cast<MeshBCTypes::Type>(t)), t});
    }
    return c;
}

/*! A `mesh::CellParamSpec::Kind::Enum` per-cell parameter as a combo column.
 *
 *  The data half of each pair is the parameter's STORED value — the same
 *  integer `mesh::cellParamValue()` returns and `applyCellParam()` accepts —
 *  so the labels stay display-only, exactly the contract bcTypeCol() follows
 *  for the BC type column. `infil.method` is the only such spec today; its
 *  values run over the mesh::InfilMethod range (None = -1 … Constant = 5),
 *  which is what CellParamSpec::min/max carry.
 * @param[in] s Borrowed CellParamSpec with UTF-8 key, labels and numeric min/max; assumes integral
 *   minimum for stored enum offset.
 * @return Editable Enum spec with each label stored as int(min)+index; enabled flag ignored locally
 *   because caller branches first. No max/count consistency validation, source access or write.
 */
ColumnSpec enumCol(const mesh::CellParamSpec &s)
{
    ColumnSpec c;
    c.key = QString::fromUtf8(s.key);
    c.label = s.label;
    c.tooltip = s.tooltip;
    c.editor = EditorKind::Enum;
    c.setter = c.key;
    c.minValue = s.min;
    c.maxValue = s.max;
    for (int i = 0; i < s.enumLabels.size(); ++i)
        c.enumValues.append(QVariantList{s.enumLabels[i], int(s.min) + i});
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
 *  applies throughout.
 * @param[in] t Resolved boundary type; unknown treated unused.
 * @param[in] key Exact boundary parameter key; group always applies, even unknown type.
 * @return True for slope/NormalFlow, head/constant-stage, flow/constant-flow, tseries/TS types,
 *   curve/RatingCurve, or group; Wall has no non-group fields. Does not establish boundary status,
 *   clear inactive values or mutate source.
 */
bool bcFieldApplies(mesh::MeshBCTypes::Type t, const QString &key)
{
    using T = mesh::MeshBCTypes::Type;
    if (key == QLatin1String("group"))
        return true;
    switch (t) {
    case T::Wall:
        return key == QLatin1String("wallDrag");
    case T::NormalFlow:
        return key == QLatin1String("slope");
    case T::SpecifiedStageConst:
        return key == QLatin1String("head");
    case T::SpecifiedStageTS:
        return key == QLatin1String("tseries");
    case T::SpecifiedFlowConst:
        return key == QLatin1String("flow");
    case T::SpecifiedFlowTS:
        return key == QLatin1String("tseries");
    case T::RatingCurve:
        return key == QLatin1String("curve");
    }
    return false;
}

/*! Endpoint vertex indices of local edge \p e on cell \p tri.
 *  Convention is delegated to mesh::edgeEndpoints for the actual cell arity;
 *  \p e must be below the cell's vertex count (3 or 4).
 * @param[in] m Borrowed consistent mesh geometry.
 * @param[in] tri Zero-based cell index, negative/out-of-range false.
 * @param[in] e Zero-based local edge, must be below actual triangle/quad vertexCount.
 * @param[out] va Required nonnull first endpoint; untouched for invalid cell/local edge, set before
 *   endpoint-bounds failure.
 * @param[out] vb Required nonnull second endpoint, same partial-output rule.
 * @return True iff both resolved vertices in mesh bounds. No finite-coordinate test or mutation;
 *   delegates edge convention to mesh::edgeEndpoints.
 */
bool edgeEndpoints(const mesh::MeshResult &m, int tri, int e, int *va, int *vb)
{
    if (tri < 0 || tri >= m.triangles.size() || e < 0)
        return false;
    const mesh::MeshTriangle &t = m.triangles[tri];
    if (e >= t.vertexCount())
        return false;
    mesh::edgeEndpoints(t, e, *va, *vb);
    return *va >= 0 && *va < m.vertices.size() && *vb >= 0 && *vb < m.vertices.size();
}

// Common million-row numeric edits need only one old double and one index
// per changed element, rather than full vertex/BC snapshots or N commands.
class MeshTableNumericCommand : public QUndoCommand
{
public:
    MeshTableNumericCommand(SWMM2DMeshLayer *layer, QByteArray key, QVector<int> indices,
                            QVector<double> oldValues, double value, const QString &label)
        : QUndoCommand(label), m_layer(layer), m_key(std::move(key)),
          m_indices(std::move(indices)), m_old(std::move(oldValues)), m_value(value)
    {
    }
    void undo() override { apply(true); }
    void redo() override { apply(false); }
private:
    void apply(bool undo)
    {
        if (!m_layer)
            return;
        mesh::AttributeEditBatch batch(m_layer, m_indices.size());
        for (int i = 0; i < m_indices.size(); ++i) {
            const double value = undo ? m_old[i] : m_value;
            const int index = m_indices[i];
            if (m_key == "z")
                m_layer->applyMeshVertexZ(index, value);
            else if (m_key == "mannings")
                m_layer->applyMeshTriangleMannings(index, value);
            else if (m_key == "initDepth")
                m_layer->applyMeshTriangleInitDepth(index, value);
            else
                m_layer->applyMeshEdgeConveyance(mesh::slotCell(index), mesh::slotLocal(index), value);
        }
    }
    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray m_key;
    QVector<int> m_indices;
    QVector<double> m_old;
    double m_value;
};

bool editedInfilRow(SWMM2DMeshLayer *layer, int row, const QByteArray &key,
                    const QVariant &value, mesh::InfilRow *next)
{
    *next = mesh::resolveInfil(layer->mesh(), row).row;
    if (key == "infil.method") {
        bool ok = false;
        const int method = value.toInt(&ok);
        if (!ok || method < int(mesh::InfilMethod::None) || method > int(mesh::InfilMethod::Constant))
            return false;
        next->method = static_cast<mesh::InfilMethod>(method);
        for (int k = 0; k < mesh::kInfilMaxParams; ++k)
            if (!mesh::infilUsesParam(next->method, k))
                next->p[k] = std::numeric_limits<double>::quiet_NaN();
    } else {
        const int slot = mesh::infilSlotForKey(next->method, key);
        if (slot < 0)
            return false;
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok)
            return false;
        next->p[slot] = number;
    }
    return true;
}

/*! Placeholder shown in a cell that does not apply to this row (a BC field on
 *  an interior edge, a per-cell parameter awaiting engine support). */

} // namespace

// ===========================================================================
// Construction / binding
// ===========================================================================

MeshAttributeTableModel::MeshAttributeTableModel(QObject *parent) : QAbstractTableModel(parent)
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
    if (m_layer)
        m_layer->setInfiltrationModel(layer);
    rebuildColumns();
    endResetModel();
}

bool MeshAttributeTableModel::canPick() const
{
    return m_modelLayer && m_modelLayer->engine();
}

QVariant MeshAttributeTableModel::pickerRef(const QString &key, const QString &current) const
{
    if (!m_modelLayer || !m_modelLayer->engine())
        return {};
    DataObjectRef ref;
    ref.engine = m_modelLayer->engine();
    ref.layer = m_modelLayer;
    ref.currentName = current;
    if (key == QLatin1String("coupledNode"))
        ref.kind = DataObjectRef::Node;
    else if (key == QLatin1String("tseries"))
        ref.kind = DataObjectRef::TimeSeries;
    // The engine resolves a 2D rating curve against the whole table namespace
    // (SurfaceRouter2D's ctx.table_names lookup), so the picker offers every
    // curve rather than only those typed RATING — narrowing it here would hide
    // choices the engine accepts.
    else if (key == QLatin1String("curve"))
        ref.kind = DataObjectRef::AnyCurve;
    else
        return {};
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
    if (m_layer)
        m_layer->setInfiltrationModel(m_modelLayer);
    m_kind = kind;
    connectLayer(layer);
    rebuildColumns();
    rebuildEdgeRows();
    endResetModel();
}

void MeshAttributeTableModel::connectLayer(SWMM2DMeshLayer *layer)
{
    if (!layer)
        return;
    connect(layer, &SWMM2DMeshLayer::attributeChanged, this,
            &MeshAttributeTableModel::onLayerAttributeChanged, Qt::UniqueConnection);
    // Bulk mutations carry no single element ref — repaint every row rather
    // than trying to work out which ones moved.
    connect(layer, &SWMM2DMeshLayer::meshEditsChanged, this, [this]() {
        const int rows = rowCount(), cols = columnCount();
        if (rows > 0 && cols > 0)
            emit dataChanged(index(0, 0), index(rows - 1, cols - 1));
    });
    // A progressive load only gains its boundary flags + vertex adjacency here,
    // and the Edge rows are built from both.
    connect(layer, &SWMM2DMeshLayer::sceneGeometryReady, this, &MeshAttributeTableModel::reload,
            Qt::UniqueConnection);
    connect(layer, &SWMM2DMeshLayer::selectionInvalidated, this, &MeshAttributeTableModel::reload,
            Qt::UniqueConnection);
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
        m_columnSpecs << ro(QStringLiteral("Index"), tr("Index"))
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
                      << num(QStringLiteral("couplingCd"), tr("Coupling Cd"), 0.001, 1.0, 3,
                             UnitKind::None,
                             tr("Coupling discharge coefficient ([2D_VERTEX_NODE_MAP] CD "
                                "column). Coupled vertices only; default 0.65."))
                      // The AREA column is mesh-length² — the engine scales it with the
                      // mesh coordinates (ft² on a US project unless the mesh file is
                      // tagged `;; UNITS: SI (m)`), so the label follows the layer.
                      << num(QStringLiteral("couplingArea"),
                             tr("Coupling Area (%1)")
                                 .arg((m_layer && m_layer->meshUnitsSI())
                                          ? QStringLiteral("m²")
                                          : unitLabel(UnitKind::Length) + QStringLiteral("²")),
                             0.0001, 1.0e6, 3, UnitKind::None,
                             tr("Coupling exchange area in the mesh's length units squared "
                                "([2D_VERTEX_NODE_MAP] AREA column). Coupled vertices only; "
                                "default 1.0."));
        break;

    case Kind::Edge:
        m_columnSpecs
            << ro(QStringLiteral("Edge"), tr("Edge"), UnitKind::None,
                  tr("Owning cell and local edge, as cell:edge (edges 0..2 on "
                     "a triangle, 0..3 on a quad). An interior edge is listed "
                     "once, under its lower slot."))
            << ro(QStringLiteral("Boundary"), tr("Boundary"), UnitKind::None,
                  tr("Whether the edge lies on the mesh outline (or a hole). "
                     "Boundary conditions apply to these edges only."))
            << ro(QStringLiteral("Length"), tr("Length (map units)"))
            << num(QStringLiteral("conveyance"), tr("Conveyance"), 0.0, 1.0, 4, UnitKind::None,
                   tr("Flux attenuation multiplier in [0, 1]; 1 = "
                      "unrestricted. Applies to interior edges too, and is "
                      "mirrored onto the neighbouring half."))
            << bcTypeCol()
            << num(QStringLiteral("wallDrag"), tr("Wall drag Cw"), 0.0, 1.0e6, 10, UnitKind::None,
                   tr("Tangential WALL friction coefficient; zero = free slip. tau/rho = -Cw |ut| ut."))
            << num(QStringLiteral("head"), tr("Stage"), -1.0e9, 1.0e9, 4, UnitKind::Length,
                   tr("Fixed water-surface elevation for a Specified Stage "
                      "(constant) boundary."))
            << num(QStringLiteral("slope"), tr("Friction Slope"), 0.0, 1.0e6, 6, UnitKind::None,
                   tr("Dimensionless friction slope for a Normal Depth boundary. "
                      "Zero conveys no flow and acts as a wall."))
            << num(QStringLiteral("flow"), tr("Flow"), -1.0e12, 1.0e12, 4, UnitKind::None,
                   tr("Discharge per metre of edge for a Specified Flow "
                      "(constant) boundary."))
            << refCol(canPick(), QStringLiteral("tseries"), tr("Time Series"),
                      tr("Time series driving a Specified Stage / Flow (TS) "
                         "boundary. Pick from the project's time series."))
            << refCol(canPick(), QStringLiteral("curve"), tr("Rating Curve"),
                      tr("Stage → flow curve for a Rating Curve boundary. Pick "
                         "from the project's curves."))
            << text(QStringLiteral("group"), tr("Group"), tr("Optional named boundary group."));
        break;

    case Kind::Cell:
        m_columnSpecs << ro(QStringLiteral("Index"), tr("Index"))
                      << ro(QStringLiteral("Vertices"), tr("Vertices"), UnitKind::None,
                            tr("Corner count: 3 for a triangle ([2D_TRIANGLES]), 4 for "
                               "a quadrilateral ([2D_QUADS])."))
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
                m_columnSpecs << ro(key, s.label, s.lengthUnit ? UnitKind::Length : UnitKind::None,
                                    s.tooltip);
            } else if (s.kind == mesh::CellParamSpec::Kind::Enum) {
                m_columnSpecs << enumCol(s);
            } else {
                m_columnSpecs << num(key, s.label, s.min, s.max, s.decimals,
                                     s.lengthUnit ? UnitKind::Length : UnitKind::None, s.tooltip);
            }
        }
        break;
    }
}

void MeshAttributeTableModel::rebuildEdgeRows()
{
    m_edgeSlots.clear();
    m_slotRow.clear();
    if (!m_layer || m_kind != Kind::Edge)
        return;
    // findEdgeNeighbour needs the vertex adjacency, which a progressive load
    // only builds in its background pass. Listing 3T rows before then would
    // double-count every interior edge, so stay empty until it is ready — the
    // sceneGeometryReady connection re-runs this.
    if (!m_layer->sceneGeometryComplete())
        return;

    const int nTri = m_layer->triangleCount();
    const auto &cells = m_layer->mesh().triangles;
    m_edgeSlots.reserve((3 * nTri + 1) / 2);
    m_slotRow.reserve(3 * nTri);
    for (int t = 0; t < nTri; ++t) {
        const int nv = cells[t].vertexCount(); // 3 edges, or 4 on a quad
        for (int e = 0; e < nv; ++e) {
            const int flat = mesh::edgeSlot(t, e);
            // Scanning in increasing flat order means the first slot reached
            // is always the lower one, which becomes the canonical row.
            if (m_slotRow.contains(flat))
                continue;
            const int row = int(m_edgeSlots.size());
            m_edgeSlots.append(flat);
            m_slotRow.insert(flat, row);
            const QPair<int, int> nbr = m_layer->findEdgeNeighbour(t, e);
            if (nbr.first >= 0 && nbr.second >= 0)
                m_slotRow.insert(mesh::edgeSlot(nbr.first, nbr.second), row);
        }
    }
}

// ===========================================================================
// Row identity
// ===========================================================================

int MeshAttributeTableModel::slotForRow(int row) const
{
    if (m_kind != Kind::Edge)
        return -1;
    if (row < 0 || row >= m_edgeSlots.size())
        return -1;
    return m_edgeSlots[row];
}

SWMMObjectRef MeshAttributeTableModel::refForRow(int row) const
{
    if (!m_layer || row < 0 || row >= rowCount())
        return {};
    const QString path = m_layer->sourcePath();
    switch (m_kind) {
    case Kind::Vertex:
        return mesh::MeshObjectRef::vertex(path, row);
    case Kind::Cell:
        return mesh::MeshObjectRef::cell(path, row);
    case Kind::Edge: {
        const int flat = slotForRow(row);
        if (flat < 0)
            return {};
        return mesh::MeshObjectRef::edge(path, mesh::slotCell(flat), mesh::slotLocal(flat));
    }
    }
    return {};
}

int MeshAttributeTableModel::rowForRef(const SWMMObjectRef &ref) const
{
    if (!m_layer)
        return -1;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_layer->sourcePath());
    QString lk;
    switch (m_kind) {
    case Kind::Vertex: {
        int v = -1;
        if (!mesh::MeshObjectRef::parseVertex(ref, &lk, &v))
            return -1;
        if (lk != wantKey || v < 0 || v >= m_layer->vertexCount())
            return -1;
        return v;
    }
    case Kind::Cell: {
        int t = -1;
        if (!mesh::MeshObjectRef::parseCell(ref, &lk, &t))
            return -1;
        if (lk != wantKey || t < 0 || t >= m_layer->triangleCount())
            return -1;
        return t;
    }
    case Kind::Edge: {
        int t = -1, e = -1;
        if (!mesh::MeshObjectRef::parseEdge(ref, &lk, &t, &e))
            return -1;
        if (lk != wantKey)
            return -1;
        // Either half of an interior pair maps to the one canonical row.
        return m_slotRow.value(mesh::edgeSlot(t, e), -1);
    }
    }
    return -1;
}

QRectF MeshAttributeTableModel::elementExtent(int row, bool *ok) const
{
    if (ok)
        *ok = false;
    if (!m_layer || row < 0 || row >= rowCount())
        return {};
    const mesh::MeshResult &m = m_layer->mesh();
    switch (m_kind) {
    case Kind::Vertex: {
        if (row >= m.vertices.size())
            return {};
        const QPointF p = m.vertices[row].xy;
        if (ok)
            *ok = true;
        return QRectF(p, p);
    }
    case Kind::Edge: {
        const int flat = slotForRow(row);
        int va = -1, vb = -1;
        if (flat < 0 || !edgeEndpoints(m, mesh::slotCell(flat), mesh::slotLocal(flat), &va, &vb))
            return {};
        if (ok)
            *ok = true;
        return QRectF(m.vertices[va].xy, m.vertices[vb].xy).normalized();
    }
    case Kind::Cell: {
        if (row >= m.triangles.size())
            return {};
        const mesh::MeshTriangle &t = m.triangles[row];
        const int nv = t.vertexCount();
        for (int k = 0; k < nv; ++k) {
            const int v = t.vertex(k);
            if (v < 0 || v >= m.vertices.size())
                return {};
        }
        // Bounding box by hand: QRectF::united() ignores a zero-size rect,
        // so uniting QRectF(c, c) per vertex would drop every vertex after
        // the first two (a quad's extent came back as one edge).
        QPointF lo = m.vertices[t.v0].xy, hi = lo;
        for (int k = 1; k < nv; ++k) {
            const QPointF c = m.vertices[t.vertex(k)].xy;
            lo.setX(qMin(lo.x(), c.x()));
            lo.setY(qMin(lo.y(), c.y()));
            hi.setX(qMax(hi.x(), c.x()));
            hi.setY(qMax(hi.y(), c.y()));
        }
        if (ok)
            *ok = true;
        return QRectF(lo, hi);
    }
    }
    return {};
}

// ===========================================================================
// QAbstractTableModel
// ===========================================================================

int MeshAttributeTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_layer)
        return 0;
    switch (m_kind) {
    case Kind::Vertex:
        return m_layer->vertexCount();
    case Kind::Cell:
        return m_layer->triangleCount();
    case Kind::Edge:
        return int(m_edgeSlots.size());
    }
    return 0;
}

int MeshAttributeTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_columnSpecs.size());
}

bool MeshAttributeTableModel::rowIsBoundaryEdge(int row) const
{
    if (!m_layer || m_kind != Kind::Edge)
        return false;
    const int flat = slotForRow(row);
    if (flat < 0)
        return false;
    return m_layer->isBoundaryEdge(mesh::slotCell(flat), mesh::slotLocal(flat));
}

bool MeshAttributeTableModel::cellInfilParamApplies(int row, const QByteArray &key) const
{
    if (!m_layer || m_kind != Kind::Cell)
        return false;
    if (!m_layer->surfaceInfiltrationEditable(row))
        return false;
    if (key == "infil.method")
        return true; // the method itself always applies
    const mesh::ResolvedInfil r = mesh::resolveInfil(m_layer->mesh(), row);
    return mesh::infilSlotForKey(r.row.method, key) >= 0;
}

mesh::InfilProvenance MeshAttributeTableModel::cellInfilProvenance(int row) const
{
    if (!m_layer || m_kind != Kind::Cell)
        return mesh::InfilProvenance::None;
    return mesh::resolveInfil(m_layer->mesh(), row).provenance;
}

QVariant MeshAttributeTableModel::data(const QModelIndex &index, int role) const
{
    if (role == openswmmvis::kAttributeSortRole && index.isValid() && m_kind == Kind::Edge &&
        index.column() == 0)
        return slotForRow(index.row());
    if (role == openswmmvis::kAttributeValueRole || role == openswmmvis::kAttributeSortRole)
        return openswmmvis::attributeValue(data(index, Qt::DisplayRole));
    if (!index.isValid() || !m_layer)
        return {};

    // An infiltration value a cell INHERITS from its region tag (or from the
    // mesh-wide '*' row) renders muted + italic, the same way the property
    // panels mark a value that is only a default. An overridden cell renders
    // normally, so "this number belongs to this cell" is visible at a glance
    // — which matters because editing an inherited cell detaches it from its
    // region until the override is removed or undone.
    if (role == Qt::FontRole || role == Qt::ForegroundRole) {
        if (m_kind != Kind::Cell || m_firstCellParamCol < 0)
            return {};
        const int col = index.column();
        if (col < m_firstCellParamCol || col >= m_columnSpecs.size())
            return {};
        if (!m_columnSpecs[col].key.startsWith(QLatin1String("infil.")))
            return {};
        const mesh::InfilProvenance p = cellInfilProvenance(index.row());
        if (p != mesh::InfilProvenance::Tag && p != mesh::InfilProvenance::Star)
            return {};
        if (role == Qt::FontRole) {
            QFont f;
            f.setItalic(true);
            return f;
        }
        return QBrush(QGuiApplication::palette().color(QPalette::Disabled, QPalette::Text));
    }

    if (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole)
        return {};

    const int row = index.row(), col = index.column();
    if (row < 0 || row >= rowCount() || col < 0 || col >= m_columnSpecs.size())
        return {};
    const ColumnSpec &spec = m_columnSpecs[col];
    const mesh::MeshResult &m = m_layer->mesh();
    const auto missing = [role](const QString &reason) -> QVariant {
        return role == Qt::ToolTipRole ? QVariant(reason) : QVariant();
    };

    switch (m_kind) {
    case Kind::Vertex: {
        if (row >= m.vertices.size())
            return {};
        const mesh::MeshVertex &v = m.vertices[row];
        if (spec.key == QLatin1String("Index"))
            return row;
        if (spec.key == QLatin1String("X"))
            return v.xy.x();
        if (spec.key == QLatin1String("Y"))
            return v.xy.y();
        if (spec.key == QLatin1String("Marker"))
            return v.marker;
        if (spec.key == QLatin1String("z"))
            return v.z;
        if (spec.key == QLatin1String("tag"))
            return v.tag;
        if (spec.key == QLatin1String("coupledNode")) {
            // EditRole hands the delegate a picker over the model's nodes;
            // DisplayRole stays a plain string so the query bar, Copy and CSV
            // export see the id and nothing else.
            if (role == Qt::EditRole) {
                const QVariant ref = pickerRef(spec.key, v.coupledNode);
                if (ref.isValid())
                    return ref;
            }
            if (role == Qt::EditRole)
                return v.coupledNode;
            return v.coupledNode.isEmpty() ? missing(tr("No coupled node assigned."))
                                           : QVariant(v.coupledNode);
        }
        // Coefficients have no physical meaning until a node is assigned.
        if (spec.key == QLatin1String("couplingCd") || spec.key == QLatin1String("couplingArea")) {
            if (v.coupledNode.isEmpty())
                return missing(tr("Not applicable: no coupled node assigned."));
            return spec.key == QLatin1String("couplingCd") ? v.couplingCd : v.couplingArea;
        }
        return {};
    }

    case Kind::Edge: {
        const int flat = slotForRow(row);
        if (flat < 0)
            return {};
        const int tri = mesh::slotCell(flat), e = mesh::slotLocal(flat);
        if (spec.key == QLatin1String("Edge"))
            return QStringLiteral("%1:%2").arg(tri).arg(e);
        if (spec.key == QLatin1String("Boundary"))
            return m_layer->isBoundaryEdge(tri, e) ? tr("Yes") : tr("No");
        if (spec.key == QLatin1String("Length")) {
            int va = -1, vb = -1;
            if (!edgeEndpoints(m, tri, e, &va, &vb))
                return {};
            return QLineF(m.vertices[va].xy, m.vertices[vb].xy).length();
        }
        const QVector<mesh::MeshEdgeBC> &bcs = m_layer->edgeBCs();
        if (flat >= bcs.size())
            return {};
        const mesh::MeshEdgeBC &bc = bcs[flat];
        if (spec.key == QLatin1String("conveyance"))
            return bc.conveyance;
        // Every remaining column is a boundary-condition field.
        if (!m_layer->isBoundaryEdge(tri, e))
            return missing(tr("Not applicable on an interior edge."));
        if (spec.key == QLatin1String("bcType")) {
            if (role == Qt::EditRole)
                return int(bc.type);
            return mesh::MeshBCTypes::label(bc.type);
        }
        // A parameter this BC type does not read carries no meaning — show it
        // as inapplicable rather than as a stale number the engine ignores.
        if (!bcFieldApplies(bc.type, spec.key))
            return missing(tr("Not used by this boundary condition."));
        if (spec.key == QLatin1String("wallDrag"))
            return bc.wallDrag;
        if (spec.key == QLatin1String("head"))
            return bc.head;
        if (spec.key == QLatin1String("slope"))
            return bc.slope;
        if (spec.key == QLatin1String("flow"))
            return bc.flow;
        if (spec.key == QLatin1String("tseries") || spec.key == QLatin1String("curve")) {
            const QString cur = (spec.key == QLatin1String("tseries")) ? bc.tseries : bc.curve;
            if (role == Qt::EditRole) {
                const QVariant ref = pickerRef(spec.key, cur);
                if (ref.isValid())
                    return ref;
            }
            if (role == Qt::EditRole)
                return cur;
            return cur.isEmpty() ? missing(tr("No reference assigned.")) : QVariant(cur);
        }
        if (spec.key == QLatin1String("group"))
            return bc.group;
        return {};
    }

    case Kind::Cell: {
        if (row >= m.triangles.size())
            return {};
        const mesh::MeshTriangle &t = m.triangles[row];
        if (spec.key == QLatin1String("Index"))
            return row;
        if (spec.key == QLatin1String("Vertices"))
            return t.vertexCount();
        if (spec.key == QLatin1String("Area"))
            return mesh::triangleArea(m, row);
        if (spec.key == QLatin1String("Centroid X") || spec.key == QLatin1String("Centroid Y")) {
            const int nv = t.vertexCount();
            for (int k = 0; k < nv; ++k) {
                const int v = t.vertex(k);
                if (v < 0 || v >= m.vertices.size())
                    return {};
            }
            // Area centroid (mesh::cellGeom) — the vertex mean for a
            // triangle, area-weighted over the sub-triangles for a quad.
            const QPointF c = mesh::cellCentroid(m, row);
            return spec.key == QLatin1String("Centroid X") ? c.x() : c.y();
        }
        if (spec.key == QLatin1String("tag"))
            return t.tag;
        // Per-cell parameter: unset reads back as the registry default, which
        // is what the engine would use; engine-pending keys have no value.
        const QByteArray key = spec.key.toUtf8();
        const mesh::CellParamSpec *cs = mesh::cellParamSpec(key);
        if (!cs)
            return {};
        const bool diagnostic =
            key == "infil.owner" || key == "infil.conflict" || key.startsWith("surface.");
        if (!cs->enabled && !diagnostic)
            return missing(cs->tooltip);
        if (!diagnostic && key.startsWith("infil.") && !m_layer->surfaceInfiltrationEditable(row))
            return missing(m.infiltrationOwner.value(row) == 0
                               ? tr("Not applicable: infiltration is disabled.")
                               : tr("Not applicable: infiltration is computed by the aquifer."));
        if (!diagnostic && key.startsWith("infil.") && !cellInfilParamApplies(row, key))
            return missing(tr("Not used by this infiltration method."));
        const double value = mesh::cellParamValue(m, row, key);
        if (diagnostic && !std::isfinite(value))
            return missing(tr("Resolved ownership data is not available."));
        if (cs->kind == mesh::CellParamSpec::Kind::Enum) {
            if (!std::isfinite(value))
                return missing(tr("No resolved value is available."));
            const int ev = int(value), label = ev - int(cs->min);
            if (label < 0 || label >= cs->enumLabels.size())
                return missing(tr("Unknown value."));
            if (role == Qt::EditRole)
                return ev;
            return cs->enumLabels[label];
        }
        if (key.startsWith("infil.") && !std::isfinite(value))
            return missing(tr("No inherited or explicit value is available."));
        // Manning's n and initial depth retain their established engine defaults.
        return std::isfinite(value) ? value : cs->defaultValue;
    }
    }
    return {};
}

Qt::ItemFlags MeshAttributeTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid() || !m_layer)
        return f;
    const int col = index.column();
    if (col < 0 || col >= m_columnSpecs.size())
        return f;
    const ColumnSpec &spec = m_columnSpecs[col];
    if (spec.editor == EditorKind::ReadOnly)
        return f;

    switch (m_kind) {
    case Kind::Vertex:
        // Coupling coefficients only exist once the vertex carries a node.
        if (spec.key == QLatin1String("couplingCd") || spec.key == QLatin1String("couplingArea")) {
            const mesh::MeshResult &m = m_layer->mesh();
            const int row = index.row();
            if (row < 0 || row >= m.vertices.size() || m.vertices[row].coupledNode.isEmpty())
                return f;
        }
        break;
    case Kind::Edge: {
        // Conveyance applies to every edge; the BC fields to boundary edges
        // only — same gating the toolbar applies.
        if (spec.key == QLatin1String("conveyance"))
            break;
        if (!rowIsBoundaryEdge(index.row()))
            return f;
        // And within a boundary edge, only the parameter its type actually
        // reads. Set the type first, then its value.
        if (spec.key != QLatin1String("bcType")) {
            const int flat = slotForRow(index.row());
            const QVector<mesh::MeshEdgeBC> &bcs = m_layer->edgeBCs();
            if (flat < 0 || flat >= bcs.size())
                return f;
            if (!bcFieldApplies(bcs[flat].type, spec.key))
                return f;
        }
        break;
    }
    case Kind::Cell: {
        // Within a cell, only the infiltration parameters its resolved method
        // actually reads. Set the method first, then its values — the same
        // order the BC columns above impose.
        const QByteArray key = spec.key.toUtf8();
        if (key.startsWith("infil.") && !cellInfilParamApplies(index.row(), key))
            return f;
        break;
    }
    }
    return f | Qt::ItemIsEditable;
}

bool MeshAttributeTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || !m_layer)
        return false;
    if (!(flags(index) & Qt::ItemIsEditable))
        return false;
    const int row = index.row(), col = index.column();
    if (row < 0 || row >= rowCount() || col < 0 || col >= m_columnSpecs.size())
        return false;
    const QByteArray key = m_columnSpecs[col].key.toUtf8();

    // A picker cell round-trips as a DataObjectRef; the id it settled on is
    // the only part the mesh stores. The normal combo offers current names;
    // direct callers and stale references still depend on edit-path validation.
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
        if (flat < 0)
            return false;
        changed = mesh::pushEdgeParamEdit(
            m_layer, {qMakePair(mesh::slotCell(flat), mesh::slotLocal(flat))}, key, v, m_canvas);
        break;
    }
    case Kind::Cell:
        if (key == "tag") {
            changed = mesh::pushCellTagEdit(m_layer, {row}, v.toString(), m_canvas);
        } else if (key.startsWith("infil.")) {
            // Infiltration is a ROW, not a set of independent columns: the
            // edited field is folded into the cell's currently-resolved row
            // and the whole thing is pushed through the one funnel that
            // snapshots provenance, so undo can put an inheriting cell back
            // to inheriting rather than to a materialised copy.
            mesh::InfilRow next;
            if (!editedInfilRow(m_layer, row, key, v, &next))
                return false;
            changed = mesh::pushCellInfilEdit(m_layer, {row}, next, m_canvas);
        } else {
            changed = mesh::pushCellParamEdit(m_layer, {row}, key, v.toDouble(), m_canvas);
        }
        break;
    }
    if (changed <= 0)
        return false;

    // The layer's attributeChanged already drove dataChanged for the row;
    // objectEdited is the user-edit-only signal other views key off.
    emit objectEdited(refForRow(row).name);
    return true;
}

void MeshAttributeTableModel::setDataForRows(int column, const QList<int> &sourceRows,
                                            const QVariant &value)
{
    if (!m_layer || column < 0 || column >= m_columnSpecs.size())
        return;
    QVector<int> rows;
    rows.reserve(sourceRows.size());
    for (int row : sourceRows)
        if (flags(index(row, column)) & Qt::ItemIsEditable)
            rows.append(row);
    if (rows.isEmpty())
        return;
    const auto &spec = m_columnSpecs[column];
    const auto key = spec.key.toUtf8();
    QVariant v = value;
    if (v.userType() == qMetaTypeId<DataObjectRef>())
        v = v.value<DataObjectRef>().currentName;
    int changed = 0;
    const bool compactNumeric = (m_kind == Kind::Vertex && key == "z") ||
        (m_kind == Kind::Edge && key == "conveyance") ||
        (m_kind == Kind::Cell && (key == "mannings" || key == "initDepth"));
    if (compactNumeric) {
        bool ok = false;
        const double number = v.toDouble(&ok);
        if ((!ok && m_kind != Kind::Cell) || !std::isfinite(number) ||
            (key == "mannings" && number <= 0.0) ||
            (key == "initDepth" && number < 0.0) ||
            (key == "conveyance" && (number < 0.0 || number > 1.0)))
            return;
        QVector<int> indices;
        QVector<double> oldValues;
        indices.reserve(rows.size());
        oldValues.reserve(rows.size());
        for (int row : rows) {
            const int index = m_kind == Kind::Edge ? slotForRow(row) : row;
            const double before = m_kind == Kind::Vertex ? m_layer->mesh().vertices[row].z
                : m_kind == Kind::Edge ? m_layer->edgeBCs()[index].conveyance
                : key == "mannings" ? m_layer->mesh().triangles[row].mannings
                                     : m_layer->mesh().triangles[row].initDepth;
            if (std::isfinite(before) && before == number)
                continue;
            indices.append(index);
            oldValues.append(m_kind == Kind::Cell && !std::isfinite(before)
                ? mesh::cellParamSpec(key)->defaultValue : before);
        }
        changed = indices.size();
        if (changed) {
            auto *command = new MeshTableNumericCommand(m_layer, key, std::move(indices),
                std::move(oldValues), number, tr("Apply %1 to %2 rows").arg(spec.label).arg(changed));
            if (m_canvas && m_canvas->undoStack())
                m_canvas->undoStack()->push(command);
            else {
                command->redo();
                delete command;
            }
        }
    } else if (m_kind == Kind::Vertex) {
        changed = mesh::pushVertexParamEdit(m_layer, rows, key, v, m_canvas);
    } else if (m_kind == Kind::Edge) {
        QVector<QPair<int, int>> edges;
        edges.reserve(rows.size());
        for (int row : rows) {
            const int slot = slotForRow(row);
            edges.append(qMakePair(mesh::slotCell(slot), mesh::slotLocal(slot)));
        }
        changed = mesh::pushEdgeParamEdit(m_layer, edges, key, v, m_canvas);
    } else if (key == "tag") {
        changed = mesh::pushCellTagEdit(m_layer, rows, v.toString(), m_canvas);
    } else if (key.startsWith("infil.")) {
        QVector<int> cells;
        QVector<mesh::InfilRow> targets;
        cells.reserve(rows.size());
        targets.reserve(rows.size());
        for (int row : rows) {
            mesh::InfilRow next;
            if (editedInfilRow(m_layer, row, key, v, &next)) {
                cells.append(row);
                targets.append(next);
            }
        }
        changed = mesh::pushCellInfilEdits(m_layer, cells, targets, m_canvas);
    }
    if (changed > 0)
        emit objectEdited(mesh::MeshObjectRef::layerKey(m_layer->sourcePath()));
}

QVariant MeshAttributeTableModel::headerData(int section, Qt::Orientation orientation,
                                             int role) const
{
    if (orientation == Qt::Horizontal && section >= 0 && section < m_columnSpecs.size()) {
        const ColumnSpec &spec = m_columnSpecs[section];
        const QString u = unitLabel(spec.unit);
        if (role == Qt::ToolTipRole) {
            if (!spec.tooltip.isEmpty())
                return spec.tooltip;
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
    if (!m_layer || m_columnSpecs.isEmpty())
        return;
    // The signal carries only the encoded name; rebuild the ref with the kind
    // this view lists and let rowForRef reject the ones that belong elsewhere.
    static const SWMMObjectRef::ObjectType kTypes[] = {
        SWMMObjectRef::MeshVertex, SWMMObjectRef::MeshEdge, SWMMObjectRef::MeshCell};
    const SWMMObjectRef ref(kTypes[int(m_kind)], refName);
    const int row = rowForRef(ref);
    if (row < 0)
        return;
    emit dataChanged(index(row, 0), index(row, int(m_columnSpecs.size()) - 1));
}
