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

#include <QByteArray>
#include <QPointer>
#include <QString>
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

} // namespace mesh

#endif // OPENSWMMVIS_MAP_MESHCOMMANDS_H
