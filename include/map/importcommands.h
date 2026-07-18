/*!
 * \file   importcommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — undo commands used by the feature-layer
 * import executor, complementing the Add*Commands in mapundostack.h.
 *
 *  - SetAdapterPropertiesCommand: generic before/after attribute write
 *    through the SWMM*PropertyAdapter Q_PROPERTY system.
 *  - SetLinkVerticesCommand: interior-vertex replacement via
 *    SWMMModelLayer::applyLinkInteriorVertices with before/after
 *    snapshots (EditVertexCommand is drag-tool-shaped: it carries
 *    auto-length state the import doesn't want).
 *
 * Node coordinate updates reuse the existing MoveNodeCommand.
 */
#ifndef OPENSWMMVIS_MAP_IMPORTCOMMANDS_H
#define OPENSWMMVIS_MAP_IMPORTCOMMANDS_H

#include "map/mapundostack.h"

#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>

/*!
 * \class SetAdapterPropertiesCommand
 * \brief Undoable bulk attribute write on one node / link / rain gage,
 *        routed through the object's property adapter so every engine
 *        setter convention (enum handling, read-modify-write tuples,
 *        loss-coeff triples, …) is reused instead of re-implemented.
 * \details Keys of \p newValues / \p oldValues are adapter Q_PROPERTY
 *          names. \p oldValues must contain the pre-change value of
 *          every key in \p newValues (captured by the executor before
 *          pushing). redo() writes newValues, undo() writes oldValues.
 *          The adapter is built fresh on every redo()/undo() so the
 *          command never holds a stale engine index.
 */
class SetAdapterPropertiesCommand : public MapCommand
{
public:
    SetAdapterPropertiesCommand(SWMMModelLayer *layer,
                                quint8          kind,      // SWMMModelLayer::kKindNode/Link/Gage
                                QString         name,
                                QVariantMap     newValues,
                                QVariantMap     oldValues,
                                MapCanvas      *canvas,
                                QUndoCommand   *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 41; }

    /*! Build the kind-appropriate concrete adapter for \p name on
     *  \p layer (junction/outfall/storage/divider subclass for nodes,
     *  per-link-type subclass for links, rain-gage adapter for gages).
     *  Returns nullptr when the object no longer exists. Caller owns
     *  the returned object. Exposed statically so the executor can use
     *  the same factory to snapshot old values. */
    [[nodiscard]] static QObject *createAdapter(SWMMModelLayer *layer,
                                                quint8 kind,
                                                const QString &name);

private:
    void apply(const QVariantMap &values);

    SWMMModelLayer *m_layer = nullptr;
    quint8          m_kind  = 0;
    QString         m_name;
    QVariantMap     m_newValues;
    QVariantMap     m_oldValues;
};

/*!
 * \class SetLinkVerticesCommand
 * \brief Undoable replacement of a link's interior polyline vertices.
 */
class SetLinkVerticesCommand : public MapCommand
{
public:
    SetLinkVerticesCommand(SWMMModelLayer   *layer,
                           QString           linkName,
                           QVector<QPointF>  oldInterior,
                           QVector<QPointF>  newInterior,
                           MapCanvas        *canvas,
                           QUndoCommand     *parent = nullptr);

    void undo() override;
    void redo() override;
    int  id()   const override { return 42; }

private:
    void apply(const QVector<QPointF> &interior);

    SWMMModelLayer   *m_layer = nullptr;
    QString           m_name;
    QVector<QPointF>  m_oldInterior;
    QVector<QPointF>  m_newInterior;
};

#endif // OPENSWMMVIS_MAP_IMPORTCOMMANDS_H
