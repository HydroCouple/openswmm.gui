/**
 * @file      importcommands.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — undo commands used by the feature-layer import
 *            executor, complementing the Add*Commands in mapundostack.h.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     * @brief Capture named-object property maps for later import undo/redo without writing at
     *   construction.
     * @param[in] layer Borrowed raw model pointer, must outlive command; access serialized on GUI
     *   thread.
     * @param[in] kind kKindNode, kKindLink or kKindGage; unsupported kind cannot build adapter.
     * @param[in] name Exact engine object ID copied and resolved afresh on every apply.
     * @param[in] newValues Adapter Q_PROPERTY names and target variants; units/ranges follow each
     *   adapter property.
     * @param[in] oldValues Caller-captured complete pre-change values for all changed keys; not
     *   inferred/validated here.
     * @param[in] canvas Optional borrowed raw canvas, must outlive command if supplied.
     * @param[in] parent Optional command owner, otherwise stack owns after push.
     * @note Applies maps in QVariantMap key order, so interdependent properties must be consistent
     *   with that order. No atomic rollback or per-setter result reporting; unknown names can
     *   create transient dynamic QObject properties and have no engine effect.
     */
    SetAdapterPropertiesCommand(SWMMModelLayer *layer,
                                quint8 kind, // SWMMModelLayer::kKindNode/Link/Gage
                                QString name, QVariantMap newValues, QVariantMap oldValues,
                                MapCanvas *canvas, QUndoCommand *parent = nullptr);

    /**
     * @brief Write supplied oldValues through a newly resolved adapter; empty map/missing object
     *   no-op.
     * @note Does not reverse key order or reconstruct missing old entries; successful adapter
     *   creation leads to Scene invalidation even if writes were rejected.
     */
    void undo() override;
    /**
     * @brief Write supplied newValues through a newly resolved adapter, including initial push.
     * @note Every individual adapter retains its engine/notification conventions; command ignores
     *   write results and can partially apply.
     */
    void redo() override;
    /**
     * @return Import attribute-command discriminator 41; no mergeWith override, so commands remain
     *   separate.
     */
    int id() const override { return 41; }

    /*! Build the kind-appropriate concrete adapter for \p name on
     *  \p layer (junction/outfall/storage/divider subclass for nodes,
     *  per-link-type subclass for links, rain-gage adapter for gages).
     *  Returns nullptr when the object no longer exists. Caller owns
     *  the returned object. Exposed statically so the executor can use
     *  the same factory to snapshot old values.
     * @param[in] layer Optional borrowed live model; null/closed engine returns null.
     * @param[in] kind Supported node/link/gage kind bit, not a combined mask.
     * @param[in] name Exact engine identifier resolved at call time.
     * @return Newly allocated, parentless concrete subtype adapter with model layer attached;
     *   caller must delete it. Null for missing object, unsupported kind/type or type-getter
     *   failure. Does not create engine objects or transfer model/engine ownership. Caller must
     *   keep engine/model alive during adapter use.
     */
    [[nodiscard]] static QObject *createAdapter(SWMMModelLayer *layer, quint8 kind,
                                                const QString &name);

private:
    /**
     * @brief Resolve a temporary subtype adapter, apply map entries and delete adapter.
     * @param[in] values Ordered property-name/value map, interpreted in adapter project-unit
     *   conventions.
     * @note Empty map/missing adapter no-op. Ignores QObject/engine write statuses, permits partial
     *   writes and adapter-specific signals. Invalidates borrowed canvas Scene after any
     *   constructed adapter, independent of setter success; no rollback or cache rebuild beyond
     *   adapters.
     */
    void apply(const QVariantMap &values);

    SWMMModelLayer *m_layer = nullptr;
    quint8 m_kind = 0;
    QString m_name;
    QVariantMap m_newValues;
    QVariantMap m_oldValues;
};

/*!
 * \class SetLinkVerticesCommand
 * \brief Undoable replacement of a link's interior polyline vertices.
 */
class SetLinkVerticesCommand : public MapCommand
{
public:
    /**
     * @brief Capture old/new interior geometry by value for name-resolved import undo/redo.
     * @param[in] layer Optional borrowed raw model, must outlive command; GUI-thread access.
     * @param[in] linkName Exact link ID copied; current index resolved on each apply.
     * @param[in] oldInterior Before interior-only vertices in native model coordinates, excluding
     *   endpoint nodes.
     * @param[in] newInterior After interior-only vertices in same frame; empty clears bends.
     * @param[in] canvas Borrowed optional canvas stored by MapCommand; not directly invalidated by
     *   this implementation.
     * @param[in] parent Optional command owner, otherwise stack after push.
     * @note No length snapshot/auto-length update is performed. Does not apply geometry at
     *   construction or validate finite points.
     */
    SetLinkVerticesCommand(SWMMModelLayer *layer, QString linkName, QVector<QPointF> oldInterior,
                           QVector<QPointF> newInterior, MapCanvas *canvas,
                           QUndoCommand *parent = nullptr);

    /**
     * @brief Resolve current link by stored name and restore old interior vertices via model edit
     *   API.
     * @note Null layer/missing link no-op; returned edit status ignored, normal model
     *   geometry/edited signals come from mutation API.
     */
    void undo() override;
    /**
     * @brief Resolve current link and apply new interior vertices, including initial stack push.
     * @note No conduit length recomputation or direct canvas invalidation; mutation status ignored.
     */
    void redo() override;
    /**
     * @return Import vertex-command discriminator 42; no mergeWith override is supplied.
     */
    int id() const override { return 42; }

private:
    /**
     * @brief Resolve link ID after possible index shifts and delegate interior replacement to
     *   model.
     * @param[in] interior Native model-frame bend points only, copied by model; empty clears bends.
     * @note Null model/missing name no-op. Ignores mutation failure; engine/geometry changes and
     *   notifications follow applyLinkInteriorVertices. Caller keeps raw model alive and
     *   serialized.
     */
    void apply(const QVector<QPointF> &interior);

    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
    QVector<QPointF> m_oldInterior;
    QVector<QPointF> m_newInterior;
};

#endif // OPENSWMMVIS_MAP_IMPORTCOMMANDS_H
