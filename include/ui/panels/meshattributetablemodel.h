/*!
 * \file   meshattributetablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Tabular model over one 2D mesh layer's vertices, edges or cells, so the
 * Attribute Table dock can list and edit mesh elements the same way it lists
 * SWMM objects.
 *
 * MVC contract (CLAUDE.md §5.1): the model owns NO mesh data. Rows are virtual
 * — `data()` reads straight out of `SWMM2DMeshLayer::mesh()` / `edgeBCs()` on
 * every call, and `setData()` writes through the `mesh::push*ParamEdit` helpers
 * so an edit made here is the same undoable command the mesh-editing toolbar
 * pushes. Everything the layer emits (`attributeChanged`, `meshEditsChanged`,
 * `sceneGeometryReady`) is subscribed to, so an edit made in any other view
 * refreshes the affected row here without a full reset.
 *
 * Row identity:
 *  - Vertices — row == vertex index.
 *  - Cells    — row == triangle index.
 *  - Edges    — one row per UNIQUE edge. The mesh stores edges per triangle
 *               corner (`tri * 3 + edgeLocal`), so an interior edge occupies
 *               two slots; those collapse onto the lower slot, and
 *               `rowForRef` resolves either half to that one row.
 *
 * Scale: the model is fully virtual, so a multi-million-cell mesh costs only
 * the one-time edge row index (O(3T)); the panel's WHERE bar and
 * show-selected-only proxy do the filtering.
 */
#ifndef OPENSWMMVIS_UI_PANELS_MESHATTRIBUTETABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_MESHATTRIBUTETABLEMODEL_H

#include "mesh/meshinfil.h"                      // mesh::InfilProvenance
#include "selection/selectionmanager.h"
#include "ui/panels/swmmattributetablemodel.h"   // openswmmvis::ColumnSpec

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

    explicit MeshAttributeTableModel(QObject *parent = nullptr);

    /*! Bind the model to a (layer, kind) pair; rebuilds the column schema and
     *  the edge row index, then resets. Pass a null layer to clear. */
    void setSource(SWMM2DMeshLayer *layer, Kind kind);

    [[nodiscard]] SWMM2DMeshLayer *layer() const { return m_layer.data(); }
    [[nodiscard]] Kind             kind()  const { return m_kind; }

    /*! Canvas whose MapUndoStack the edits are pushed onto. Null ⇒ edits still
     *  apply, just unundoably (the push helpers' headless path).
     *  Out-of-line so this header does not have to pull in MapCanvas. */
    void setCanvas(MapCanvas *canvas);

    /*! The SWMM model this mesh is coupled to. Supplies the candidate lists
     *  behind the picker columns — coupled node, BC time series, BC rating
     *  curve — so those cells can only ever name an object that exists.
     *  Without it those columns fall back to plain text entry. */
    void setModelLayer(SWMMModelLayer *layer);

    /*! Per-column metadata — the panel installs delegates from this, exactly
     *  as it does for `SWMMAttributeTableModel`. */
    [[nodiscard]] QList<openswmmvis::ColumnSpec> columnSpecs() const
    { return m_columnSpecs; }

    /*! Selection-bus ref naming the element on \p row, or an invalid ref. */
    [[nodiscard]] SWMMObjectRef refForRow(int row) const;

    /*! Inverse of refForRow. For an interior edge, EITHER slot of the pair
     *  resolves to the single row that represents it. -1 when the ref names a
     *  different layer, a different kind, or an element that is gone. */
    [[nodiscard]] int rowForRef(const SWMMObjectRef &ref) const;

    /*! Bounding box of \p row's element in the layer's own (map) coordinates —
     *  a degenerate point for a vertex, the segment for an edge, the triangle
     *  bbox for a cell. \p ok reports whether the row resolved; a vertex sitting
     *  exactly at the origin is a legitimate zero-size rect, so the rect alone
     *  cannot signal failure. */
    [[nodiscard]] QRectF elementExtent(int row, bool *ok = nullptr) const;

    // QAbstractTableModel ----------------------------------------------------
    int      rowCount(const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Full reload — rebuilds the edge row index and resets the model. */
    void reload();

signals:
    /*! Emitted after a user-initiated cell edit commits, carrying the element's
     *  MeshObjectRef name. Distinct from `dataChanged` so listeners can tell a
     *  user edit from a synthetic refresh and avoid feedback loops — the same
     *  convention `SWMMAttributeTableModel::objectEdited` follows. */
    void objectEdited(const QString &refName);

private:
    void rebuildColumns();
    void rebuildEdgeRows();
    void connectLayer(SWMM2DMeshLayer *layer);

    void onLayerAttributeChanged(const QString &refName);

    /*! True when the mesh element on \p row is a boundary edge — the BC
     *  columns render "—" and refuse edits everywhere else. Always false for
     *  the Vertex / Cell kinds. */
    [[nodiscard]] bool rowIsBoundaryEdge(int row) const;

    /*! True when infiltration parameter \p key carries a value for the method
     *  resolved on cell \p row — the same per-row masking idiom
     *  rowIsBoundaryEdge() applies to the BC columns. A parameter the row's
     *  method does not read renders "—" and refuses edits, so a Curve Number
     *  cell cannot be left carrying a Horton decay constant nothing reads.
     *  Always false for the Vertex / Edge kinds. */
    [[nodiscard]] bool cellInfilParamApplies(int row, const QByteArray &key) const;

    /*! Where cell \p row's infiltration row came from. Drives the muted /
     *  italic rendering that tells an inherited (region tag or '*' default)
     *  value apart from a per-cell override. */
    [[nodiscard]] mesh::InfilProvenance cellInfilProvenance(int row) const;

    /*! Flat slot (`tri * 3 + edgeLocal`) for an Edge row, or -1. */
    [[nodiscard]] int slotForRow(int row) const;

    /*! True when a SWMM model with a live engine is bound, i.e. when the
     *  reference columns can offer a real candidate list. */
    [[nodiscard]] bool canPick() const;

    /*! Build the picker-cell value for \p key ("coupledNode" / "tseries" /
     *  "curve") holding \p current. Returns an invalid QVariant when no model
     *  layer is bound, which is what makes those columns degrade to text. */
    [[nodiscard]] QVariant pickerRef(const QString &key,
                                     const QString &current) const;

    QPointer<SWMM2DMeshLayer> m_layer;
    QPointer<SWMMModelLayer>  m_modelLayer;
    QPointer<MapCanvas>       m_canvas;
    Kind                      m_kind = Kind::Vertex;

    QList<openswmmvis::ColumnSpec> m_columnSpecs;

    /*! Canonical (lower) flat slot per Edge row. Empty for other kinds. */
    QVector<int>   m_edgeSlots;
    /*! Every flat slot → its row, both halves of an interior pair included. */
    QHash<int,int> m_slotRow;

    /*! Index of the first per-cell parameter column (Cell kind only); the
     *  columns from here on map 1:1 onto `mesh::cellParamSpecs()`. */
    int m_firstCellParamCol = -1;
};

#endif // OPENSWMMVIS_UI_PANELS_MESHATTRIBUTETABLEMODEL_H
