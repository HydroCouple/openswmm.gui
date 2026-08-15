/*!
 * \file   meshcommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Undoable per-cell 2D mesh attribute edits. One command covers a whole
 * selection (or a whole raster/shapefile assignment), so a bulk write is a
 * single Ctrl+Z rather than thousands.
 *
 * Every editing surface — the mesh-editing toolbar, the properties panel
 * adapter, and the Cell Data assignment dialog — pushes through
 * pushCellParamEdit() so undo behaves identically no matter where the edit
 * came from.
 */
#ifndef OPENSWMMVIS_MAP_MESHCOMMANDS_H
#define OPENSWMMVIS_MAP_MESHCOMMANDS_H

#include "map/mapundostack.h"
#include "mesh/meshedgebc.h"
#include "mesh/meshresult.h"

#include <QByteArray>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVector>

class SWMM2DMeshLayer;

/*!
 * \class MeshSetTriangleAttributeCommand
 * \brief Undoable write of one per-cell parameter across a set of triangles.
 * \details Stores the pre-edit value of every touched triangle (NaN for
 *          "was unset"). redo() writes the new values, undo() restores the
 *          old ones — both through mesh::applyCellParam, so the layer emits
 *          `attributeChanged` and every view refreshes either way. The layer
 *          is held as a QPointer: if it is closed, the command becomes inert
 *          rather than dangling.
 */
class MeshSetTriangleTagCommand : public MapCommand
{
public:
    MeshSetTriangleTagCommand(SWMM2DMeshLayer   *layer,
                              QVector<int>       triangles,
                              QString            newTag,
                              QVector<QString>   oldTags,
                              const QString     &text,
                              MapCanvas         *canvas,
                              QUndoCommand      *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 44; }

private:
    QPointer<SWMM2DMeshLayer> m_layer;
    QVector<int>              m_tris;
    QString                   m_newTag;
    QVector<QString>          m_oldTags;
};

class MeshSetTriangleAttributeCommand : public MapCommand
{
public:
    /*! \param layer     Mesh layer that owns the triangles.
     *  \param key       mesh::CellParamSpec key ("mannings", "initDepth", …).
     *  \param triangles Triangle indices to write.
     *  \param newValues Parallel to \p triangles: the value to write to each.
     *  \param oldValues Parallel to \p triangles: the pre-edit value of each
     *                   (NaN = the attribute was unset).
     *  \param text      Undo-stack label. */
    MeshSetTriangleAttributeCommand(SWMM2DMeshLayer      *layer,
                                    QByteArray            key,
                                    QVector<int>          triangles,
                                    QVector<double>       newValues,
                                    QVector<double>       oldValues,
                                    const QString        &text,
                                    MapCanvas            *canvas,
                                    QUndoCommand         *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 43; }

private:
    void apply(const QVector<double> &values);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray                m_key;
    QVector<int>              m_tris;
    QVector<double>           m_newValues;
    QVector<double>           m_oldValues;
};

/*!
 * \class MeshSetVertexAttributeCommand
 * \brief Undoable write of one per-vertex attribute across a set of vertices.
 * \details Stores a snapshot of every touched vertex's editable attribute set
 *          (z / tag / coupledNode / couplingCd / couplingArea) on both sides of
 *          the edit rather than the single changed field. That is what makes
 *          undo faithful for the interdependent ones: clearing a coupling also
 *          resets Cd and Area to the engine defaults, so restoring the node id
 *          alone would silently drop the user's coefficients.
 *
 *          The snapshot is applied field-by-field through the layer's apply*
 *          helpers, in an order that respects their preconditions (coupledNode
 *          before Cd/Area, which are rejected on an uncoupled vertex). Fields
 *          that did not move are no-ops inside the helpers.
 */
class MeshSetVertexAttributeCommand : public MapCommand
{
public:
    /*! \param layer     Mesh layer that owns the vertices.
     *  \param key       Attribute key ("z", "tag", "coupledNode",
     *                   "couplingCd", "couplingArea") — used for labelling
     *                   only; the snapshots carry the values.
     *  \param vertices  Vertex indices to write.
     *  \param newAttrs  Parallel to \p vertices: post-edit attribute snapshot.
     *  \param oldAttrs  Parallel to \p vertices: pre-edit attribute snapshot. */
    MeshSetVertexAttributeCommand(SWMM2DMeshLayer          *layer,
                                  QByteArray                key,
                                  QVector<int>              vertices,
                                  QVector<mesh::MeshVertex> newAttrs,
                                  QVector<mesh::MeshVertex> oldAttrs,
                                  const QString            &text,
                                  MapCanvas                *canvas,
                                  QUndoCommand             *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 45; }

private:
    void apply(const QVector<mesh::MeshVertex> &attrs);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray                m_key;
    QVector<int>              m_verts;
    QVector<mesh::MeshVertex> m_newAttrs;
    QVector<mesh::MeshVertex> m_oldAttrs;
};

/*!
 * \class MeshSetEdgeAttributeCommand
 * \brief Undoable write of one per-edge attribute across a set of edge slots.
 * \details Slots are stored flat (`tri * 3 + edgeLocal`) and both sides of the
 *          edit hold the WHOLE MeshEdgeBC, not just the changed field: the BC
 *          fields are interdependent through `type` (a stage value is
 *          meaningless once the type flips to Wall), so a partial restore would
 *          leave the slot in a state the user never saw.
 *
 *          Conveyance dispatches to applyMeshEdgeConveyance rather than
 *          applyMeshEdgeBC in BOTH directions, so the engine's interior-edge
 *          symmetry invariant (both halves carry the same value) survives undo.
 */
class MeshSetEdgeAttributeCommand : public MapCommand
{
public:
    /*! \param layer  Mesh layer that owns the edges.
     *  \param key    Attribute key ("conveyance", "bcType", "head", "slope",
     *                "flow", "tseries", "curve", "group", or "bc" for a whole
     *                boundary-condition write). "conveyance" selects the
     *                mirroring write path; every other key writes the slot.
     *  \param edgeSlots  Flat edge indices (`tri * 3 + edgeLocal`).
     *  \param newBCs Parallel to \p edgeSlots: post-edit slot value.
     *  \param oldBCs Parallel to \p edgeSlots: pre-edit slot value. */
    MeshSetEdgeAttributeCommand(SWMM2DMeshLayer          *layer,
                                QByteArray                key,
                                QVector<int>              edgeSlots,
                                QVector<mesh::MeshEdgeBC> newBCs,
                                QVector<mesh::MeshEdgeBC> oldBCs,
                                const QString            &text,
                                MapCanvas                *canvas,
                                QUndoCommand             *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 46; }

private:
    void apply(const QVector<mesh::MeshEdgeBC> &bcs);

    QPointer<SWMM2DMeshLayer> m_layer;
    QByteArray                m_key;
    QVector<int>              m_slots;
    QVector<mesh::MeshEdgeBC> m_newBCs;
    QVector<mesh::MeshEdgeBC> m_oldBCs;
};

namespace mesh {

/*!
 * \brief Snapshot the current values, then push one undoable edit writing
 *        \p value to every triangle in \p triangles.
 *
 * Triangles already holding \p value are dropped from the command, so a
 * no-op edit does not clutter the undo stack. With no undo stack available
 * the write still happens, just unundoably.
 *
 * \returns the number of triangles actually changed.
 */
int pushCellParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                      const QByteArray &key, double value, MapCanvas *canvas);

/*!
 * \brief Per-triangle variant for assignments that write a different value to
 *        each cell (raster sampling, shapefile field joins).
 * \param triangles Indices to write; \p values is parallel to it.
 */
int pushCellParamEdits(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                       const QVector<double> &values, const QByteArray &key,
                       const QString &text, MapCanvas *canvas);

/*!
 * \brief Undoable write of the descriptive `[2D_TRIANGLES]` TAG to every
 *        triangle in \p triangles.
 * \returns the number of triangles actually changed.
 */
int pushCellTagEdit(SWMM2DMeshLayer *layer, const QVector<int> &triangles,
                    const QString &tag, MapCanvas *canvas);

/*!
 * \brief Snapshot the current values, then push one undoable edit writing
 *        \p value into attribute \p key on every vertex in \p vertices.
 *
 * \param key   One of "z", "tag", "coupledNode", "couplingCd",
 *              "couplingArea". Unknown keys are rejected.
 * \param value Typed to match the key (double for z / Cd / area, string for
 *              tag / coupledNode).
 *
 * Vertices already holding \p value are dropped, so a no-op edit does not
 * clutter the undo stack. With no undo stack available the write still
 * happens, just unundoably.
 *
 * \returns the number of vertices actually changed.
 */
int pushVertexParamEdit(SWMM2DMeshLayer *layer, const QVector<int> &vertices,
                        const QByteArray &key, const QVariant &value,
                        MapCanvas *canvas);

/*!
 * \brief Undoable write of one edge attribute to every edge in \p edges
 *        (each a `(triIdx, edgeLocal)` pair).
 *
 * \param key   One of "conveyance", "bcType", "head", "slope", "flow",
 *              "tseries", "curve", "group". The BC keys apply to BOUNDARY
 *              edges only — interior slots in \p edges are skipped, mirroring
 *              the toolbar's gating — while "conveyance" applies to every edge.
 *
 * \returns the number of edge slots actually changed.
 */
int pushEdgeParamEdit(SWMM2DMeshLayer *layer,
                      const QVector<QPair<int, int>> &edges,
                      const QByteArray &key, const QVariant &value,
                      MapCanvas *canvas);

/*!
 * \brief Undoable write of a complete boundary condition (type plus whichever
 *        parameter that type carries) to every BOUNDARY edge in \p edges.
 *
 * Each slot keeps its own `group` label and `conveyance` — both are orthogonal
 * to the BC type and edited elsewhere. Interior edges are skipped.
 *
 * \returns the number of edge slots actually changed.
 */
int pushEdgeBCEdit(SWMM2DMeshLayer *layer,
                   const QVector<QPair<int, int>> &edges,
                   const MeshEdgeBC &bc, MapCanvas *canvas);

} // namespace mesh

#endif // OPENSWMMVIS_MAP_MESHCOMMANDS_H
