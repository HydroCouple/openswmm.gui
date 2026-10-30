/**
 * @file      meshattributetablemodel.h
 * @brief     Tabular model over one 2D mesh layer's vertices, edges or cells, so the Attribute
 *            Table dock can list and edit mesh elements the same way it lists SWMM objects.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Tabular model over one 2D mesh layer's vertices, edges or cells, so the
 * Attribute Table dock can list and edit mesh elements the same way it lists
 * SWMM objects.
 *
 * MVC contract (CLAUDE.md §5.1): the model owns NO mesh data. Rows are virtual
 * — `data()` reads straight out of `SWMM2DMeshLayer::mesh()` / `edgeBCs()` on
 * every call, and `setData()` writes through the `mesh::push*ParamEdit` helpers
 * so an edit made here is the same undoable command the mesh-editing toolbar
 * pushes. Attribute changes refresh one row and bulk edits repaint all rows;
 * scene geometry readiness or invalidated selection resets the edge index.
 *
 * Row identity:
 *  - Vertices — row == vertex index.
 *  - Cells    — row == triangle index.
 *  - Edges    — one row per UNIQUE edge. The mesh stores edges per cell
 *               slot (`mesh::edgeSlot(cell, edgeLocal)`, 3 edges on a
 *               triangle, 4 on a quad), so an interior edge occupies
 *               two slots; those collapse onto the lower slot, and
 *               `rowForRef` resolves either half to that one row.
 *
 * Scale: the model is fully virtual, so a multi-million-cell mesh costs only
 * the one-time edge row index (O(3T)); the panel's WHERE bar and
 * show-selected-only proxy do the filtering.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_MESHATTRIBUTETABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_MESHATTRIBUTETABLEMODEL_H

#include "mesh/meshinfil.h" // mesh::InfilProvenance
#include "selection/selectionmanager.h"
#include "ui/panels/swmmattributetablemodel.h" // openswmmvis::ColumnSpec

#include <QAbstractTableModel>
#include <QHash>
#include <QPointer>
#include <QRectF>
#include <QVector>

class MapCanvas;
class SWMM2DMeshLayer;
class SWMMModelLayer;

class MeshAttributeTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /*! Which family of mesh elements the rows represent. */
    enum class Kind { Vertex, Edge, Cell };

    /**
     * @param[in] parent Optional QObject owner; layer/model/canvas are borrowed and separately
     *   owned.
     * @brief Build default Vertex column schema with no source, model or canvas; zero rows,
     *   GUI-thread access only. No mesh/engine write.
     */
    explicit MeshAttributeTableModel(QObject *parent = nullptr);

    /*! Bind the model to a (layer, kind) pair; rebuilds the column schema and
     *  the edge row index, then resets. Pass a null layer to clear.
     * @param[in] layer Optional borrowed mesh source, guarded by QPointer; null clears rows.
     * @param[in] kind Row family stored even null source; valid Kind enumerator required.
     * @note Always reset, including equal pair; disconnect ALL old layer-to-model connections, bind
     *   signals, set source infiltration model from current model binding, rebuild schema and
     *   unique-edge rows. Setting infiltration model can refresh ownership diagnostics/emit layer
     *   signals; source binding is not purely visual. No mesh parameter command, undo-stack
     *   clearing or source ownership transfer.
     */
    void setSource(SWMM2DMeshLayer *layer, Kind kind);

    /**
     * @return Borrowed mesh source or null after detach/destruction, no ownership pin or
     *   scene-readiness guarantee.
     */
    [[nodiscard]] SWMM2DMeshLayer *layer() const { return m_layer.data(); }
    /**
     * @return Stored row family, initially Vertex, independent of source lifetime/count; does not
     *   inspect selection.
     */
    [[nodiscard]] Kind kind() const { return m_kind; }

    /*! Canvas whose MapUndoStack the edits are pushed onto. Null ⇒ edits still
     *  apply, just unundoably (the push helpers' headless path).
     *  Out-of-line so this header does not have to pull in MapCanvas.
     * @param[in] canvas Optional borrowed canvas retained through QPointer; supplies undo stack to
     *   future edits.
     * @note Store only, no schema/row reset or connection, ownership transfer or command push.
     *   Missing/destroyed canvas causes push helpers to execute directly without undo; existing
     *   history unaffected.
     */
    void setCanvas(MapCanvas *canvas);

    /*! The SWMM model this mesh is coupled to. Supplies the candidate lists
     *  behind the picker columns — coupled node, BC time series, BC rating
     *  curve — so normal picker use offers the current model objects. Direct setData callers
     *  remain responsible for reference validity.
     *  Without it those columns fall back to plain text entry.
     * @param[in] layer Optional borrowed SWMM model guarded by QPointer; engine handle determines
     *   current picker availability.
     * @note Changed pointer resets schema and sets mesh infiltration model, potentially refreshing
     *   ownership/notifications. Same pointer returns immediately in current comparison, including
     *   if its engine availability changed since earlier binding; rebind alone does not reliably
     *   refresh editor kinds for that transition. No subscription to model engine open/close, no
     *   undo command or ownership transfer. PickerRef queries current engine on demand.
     */
    void setModelLayer(SWMMModelLayer *layer);

    /*! Per-column metadata — the panel installs delegates from this, exactly
     *  as it does for `SWMMAttributeTableModel`.
     * @return Value copy of cached ordered editor schema. Numeric bounds/decimals presentation
     *   metadata; actual validation delegated to mesh edit funnels. Coupling-area heading snapshot
     *   uses m² for explicitly SI mesh, otherwise current global length label squared; no automatic
     *   unit-change rebuild.
     */
    [[nodiscard]] QList<openswmmvis::ColumnSpec> columnSpecs() const { return m_columnSpecs; }

    /*! Selection-bus ref naming the element on \p row, or an invalid ref.
     * @param[in] row Zero-based current vertex/cell index or canonical unique-edge row;
     *   invalid/missing source yields invalid ref.
     * @return Encoded source-path layer key plus element address; Edge uses lower canonical flat
     *   slot. No layer lifetime pin or stable identity across topology/source-path changes; source
     *   index and map coordinates unchanged.
     */
    [[nodiscard]] SWMMObjectRef refForRow(int row) const;

    /*! Inverse of refForRow. For an interior edge, EITHER slot of the pair
     *  resolves to the single row that represents it. -1 when the ref names a
     *  different layer, a different kind, or an element that is gone.
     * @param[in] ref Typed MeshObjectRef with matching source-path-derived layer key; parsing
     *   validates kind/encoding.
     * @return Current row or -1 for missing/mismatched/deleted target. Vertex/cell current bounds
     *   checked; edge uses cached slot map, so both halves resolve and no completed edge index
     *   means -1. Matching path keys establish identity, not pointer equality.
     */
    [[nodiscard]] int rowForRef(const SWMMObjectRef &ref) const;

    /*! Bounding box of \p row's element in the layer's own (map) coordinates —
     *  a degenerate point for a vertex, the segment for an edge, the triangle
     *  bbox for a cell. \p ok reports whether the row resolved; a vertex sitting
     *  exactly at the origin is a legitimate zero-size rect, so the rect alone
     *  cannot signal failure.
     * @param[in] row Current zero-based element row; bounds/vertex addresses checked.
     * @param[out] ok Optional success flag, set false first and true only for resolved geometry.
     *   Zero-area rectangles can still succeed.
     * @return Native mesh-map XY bounds with no canvas reprojection or padding; vertex point, edge
     *   normalized endpoints, cell all triangle/quad corners. Empty on failure, finite coordinates
     *   not independently checked. No sampling/write.
     */
    [[nodiscard]] QRectF elementExtent(int row, bool *ok = nullptr) const;

    // QAbstractTableModel ----------------------------------------------------
    /**
     * @param[in] parent Flat table parent; valid parent returns zero.
     * @return Zero without source; live vertex/cell count or cached unique-edge size. Edge rows
     *   remain empty until sceneGeometryComplete and next index rebuild. No per-row data
     *   allocation.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Valid parent returns zero, invalid root returns cached schema count.
     * @return Field count even without source; no live mesh/model query or rebuild.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected current model index; invalid/no source/unresolved geometry yields
     *   invalid. Foreign/stale indexes unsupported.
     * @param[in] role Display/Edit/Tooltip, cell-infiltration Font/Foreground, shared attribute
     *   value/sort roles; others invalid.
     * @return Virtual native stored/resolved value, enum label for display vs integer for edit,
     *   DataObjectRef for reference EditRole with live model engine otherwise raw name.
     *   Inapplicable/unavailable fields return invalid with tooltip reason. Most TooltipRole paths
     *   return same raw value as display rather than schema tooltip. Shared attribute roles wrap
     *   DISPLAY values; edge column-zero sort alone returns numeric flat slot.
     * @note Inherited Tag/Star infiltration styles italic/muted. Cell ownership diagnostics require
     *   finite resolved data, inactive/method-unused infiltration fields blank; other nonfinite
     *   parameters can use registry defaults. Reads have no unit conversion, engine write or undo
     *   step; source topology must remain consistent during call.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Current editable cell in this source model; out-of-range/readonly/missing
     *   context rejected.
     * @param[in] value Raw candidate or DataObjectRef, whose currentName alone is extracted without
     *   checking its source/kind or name existence.
     * @param[in] role Only EditRole accepted.
     * @return True when push helper returns positive candidate-attempt count and objectEdited
     *   emitted; false for rejection, conversion failure or no candidates. Positive result does not
     *   independently prove every setter succeeded.
     * @note Dispatch vertex/edge/tag/parameter edits through shared undo helpers, direct execution
     *   if canvas/stack absent. Infiltration edits copy entire resolved row, validate method -1..5
     *   or numeric conversion, clear unused slots on method switch, and preserve prior inheritance
     *   for undo. Editing inherited equal numbers can still create explicit override. Other cell
     *   numeric conversion uses toDouble without success flag. No local simulation-running gate,
     *   referential validation, disk save or rollback; helper/layer notifications refresh data.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /** Uniform bulk edit: compact numeric snapshots or shared multi-element commands.
     * Emits objectEdited once with the layer key when at least one candidate changes.
     */
    void setDataForRows(int column, const QList<int> &rows, const QVariant &value);
    /**
     * @param[in] index Expected current model index; invalid/no source/outside columns retains base
     *   flags.
     * @return Base plus editable for non-readonly schema subject to coupled-node requirement for
     *   Cd/area, boundary/type-specific BC applicability and editable/method-specific infiltration
     *   ownership. Conveyance editable on interior edges. Does not impose simulation-running check,
     *   min/max or general row bounds for every field; setData checks final row bounds.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Zero-based logical column; horizontal range checked, vertical numbering
     *   has no bounds check.
     * @param[in] orientation Horizontal labels/tooltips, Vertical DisplayRole returns section+1.
     * @param[in] role DisplayRole or horizontal ToolTipRole; others invalid.
     * @return Label plus live global length suffix only when UnitKind::Length, or schema tooltip
     *   preferentially then Units suffix. Other unit kinds ignored by local formatter; no value
     *   conversion or automatic headerDataChanged on global unit changes.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Full reload — rebuilds the edge row index and resets the model.
     * @note Reset model and rebuild only unique-edge rows; cached column schema/editor
     *   kind/unit-embedded labels are not rebuilt. No source/model/ownership reassignment,
     *   parameter write or undo step. Invalidates view indexes/selection; edge rows require
     *   completed scene adjacency.
     */
    void reload();

signals:
    /*! Emitted after a user-initiated cell edit commits, carrying the element's
     *  MeshObjectRef name. Distinct from `dataChanged` so listeners can tell a
     *  user edit from a synthetic refresh and avoid feedback loops — the same
     *  convention `SWMMAttributeTableModel::objectEdited` follows.
     * @param[in] refName Encoded MeshObjectRef name, or layer key for a bulk edit; no raw layer
     *   ownership or immutable topology identity.
     * @note Emitted after setData/setDataForRows positive candidate-attempt count, not external
     *   writes/undo/redo/refresh, and is not independently confirmed persistence. Receiver manages
     *   project dirty/selection feedback.
     */
    void objectEdited(const QString &refName);

private:
    /**
     * @brief Replace schema for current kind and picker availability, recording first
     *   cell-parameter column.
     * @note Cell parameters follow complete mesh registry order, disabled entries read-only.
     *   X/Y/map geometry remain read-only, bounds configure editors only. Coupling-area heading
     *   snapshots SI mesh/global units. Does not reset model itself, connect units, enumerate
     *   candidates or edit mesh; caller brackets reset as needed.
     */
    void rebuildColumns();
    /**
     * @brief Clear slot maps, then scan completed mesh cells/local edges in increasing order,
     *   collapsing each neighbor pair into lower-slot row.
     * @note Null/non-Edge/incomplete scene leaves maps empty. Uses triangleCount and vertexCount
     *   arity for triangles/quads; assumes consistent valid adjacency and mesh arrays. O(total
     *   local slots) memory/work, no reset notification itself or geometry/undo mutation.
     */
    void rebuildEdgeRows();
    /**
     * @param[in] layer Optional borrowed source matching m_layer; null no-op.
     * @note Attribute changes refresh resolved row, bulk mesh edits emit full-table dataChanged,
     *   sceneGeometryReady/selectionInvalidated reset edge index. Does not subscribe to every
     *   possible layer signal, engine availability or unit changes. Caller disconnects old/all
     *   connections before rebinding to avoid duplicate lambda. No ownership transfer.
     */
    void connectLayer(SWMM2DMeshLayer *layer);

    /**
     * @param[in] refName Encoded element name; reconstructed with CURRENT kind, then parsed/matched
     *   through rowForRef.
     * @brief Emit all-role dataChanged across matching entire row, including BC/infiltration
     *   applicability styling.
     * @note Missing source/schema or foreign/invalid ref no-op. No full reset/index rebuild,
     *   objectEdited, engine/undo write or source-path selection mutation.
     */
    void onLayerAttributeChanged(const QString &refName);

    /*! True when the mesh element on \p row is a boundary edge — the BC
     *  columns render "—" and refuse edits everywhere else. Always false for
     *  the Vertex / Cell kinds.
     * @param[in] row Canonical unique-edge row; invalid, non-Edge or no source returns false.
     * @return Layer boundary flag at decoded canonical slot; no neighbor search/index rebuild or
     *   mutation.
     */
    [[nodiscard]] bool rowIsBoundaryEdge(int row) const;

    /*! True when infiltration parameter \p key carries a value for the method
     *  resolved on cell \p row — the same per-row masking idiom
     *  rowIsBoundaryEdge() applies to the BC columns. A parameter the row's
     *  method does not read renders "—" and refuses edits, so a Curve Number
     *  cell cannot be left carrying a Horton decay constant nothing reads.
     *  Always false for the Vertex / Edge kinds.
     * @param[in] row Cell index, forwarded to source ownership/resolution; callers normally enforce
     *   row bounds.
     * @param[in] key Exact infiltration registry byte key.
     * @return False without Cell source or if surfaceInfiltrationEditable rejects ownership; method
     *   key then true, parameter key true only if slot belongs to resolved method. Does not
     *   materialize inherited row, validate parameter value or consult current table column
     *   enablement.
     */
    [[nodiscard]] bool cellInfilParamApplies(int row, const QByteArray &key) const;

    /*! Where cell \p row's infiltration row came from. Drives the muted /
     *  italic rendering that tells an inherited (region tag or '*' default)
     *  value apart from a per-cell override.
     * @param[in] row Cell index forwarded to resolveInfil, normally current valid row.
     * @return Resolved Explicit/Tag/Star/None provenance, None without Cell source. No ownership
     *   gate, override materialization or mutation.
     */
    [[nodiscard]] mesh::InfilProvenance cellInfilProvenance(int row) const;

    /*! Flat slot (`mesh::edgeSlot(cell, edgeLocal)`) for an Edge row, or -1.
     * @param[in] row Zero-based canonical edge-row coordinate.
     * @return Cached mesh::edgeSlot or -1 for non-Edge/out-of-range; does not validate source
     *   liveness or current topology independently.
     */
    [[nodiscard]] int slotForRow(int row) const;

    /*! True when a SWMM model with a live engine is bound, i.e. when the
     *  reference columns can offer a real candidate list.
     * @return True iff guarded model pointer and current engine handle nonnull; does not prove
     *   engine running/editable, candidate lists nonempty or cached schema editor kind
     *   synchronized.
     */
    [[nodiscard]] bool canPick() const;

    /*! Build the picker-cell value for \p key ("coupledNode" / "tseries" /
     *  "curve") holding \p current. Returns an invalid QVariant when no model
     *  layer is bound, which is what makes those columns degrade to text.
     * @param[in] key Exact coupledNode, tseries or curve; unknown invalid.
     * @param[in] current Current raw stored name copied even if absent from model, no
     *   trimming/validation.
     * @return Typed DataObjectRef borrowing current engine/layer: Node, TimeSeries or AnyCurve
     *   respectively, invalid without live engine/model. All curves offered regardless table type.
     *   Host/delegate must preserve source-engine lifetime; no selection, object creation or
     *   validation write.
     */
    [[nodiscard]] QVariant pickerRef(const QString &key, const QString &current) const;

    QPointer<SWMM2DMeshLayer> m_layer;
    QPointer<SWMMModelLayer> m_modelLayer;
    QPointer<MapCanvas> m_canvas;
    Kind m_kind = Kind::Vertex;

    QList<openswmmvis::ColumnSpec> m_columnSpecs;

    /*! Canonical (lower) flat slot per Edge row. Empty for other kinds. */
    QVector<int> m_edgeSlots;
    /*! Every flat slot → its row, both halves of an interior pair included. */
    QHash<int, int> m_slotRow;

    /*! Index of the first per-cell parameter column (Cell kind only); the
     *  columns from here on map 1:1 onto `mesh::cellParamSpecs()`. */
    int m_firstCellParamCol = -1;
};

#endif // OPENSWMMVIS_UI_PANELS_MESHATTRIBUTETABLEMODEL_H
