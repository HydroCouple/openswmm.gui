/**
 * @file      textlayercommands.h
 * @brief     Undoable editing of text annotation layers and features.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_TEXTLAYERCOMMANDS_H
#define OPENSWMM_TEXTLAYERCOMMANDS_H
#include "layers/annotationlayer.h"
#include <QPointer>
#include <QUndoCommand>

// Snapshot commands share the authoritative layer model with the manager and
// map tool. QPointer makes undo harmless after a layer has been destroyed.
class EditTextLayerCommand : public QUndoCommand
{
public:
    /**
     * @brief Capture complete before/after annotation-layer JSON for undo/redo; construction does
     *   not restore either snapshot.
     * @param[in] layer Optional borrowed annotation layer guarded by QPointer; deletion makes
     *   undo/redo no-ops.
     * @param[in] before Value snapshot to restore on undo, expected from layer serialization.
     * @param[in] after Value snapshot to restore on redo, including immediate stack push.
     * @param[in] label Undo-menu text copied into QUndoCommand.
     * @note GUI-thread command; stack owns command after push. Restoration replaces authoritative
     *   layer state through restoreLayer, including its rebuild/notification behavior; malformed
     *   snapshots follow that method without command-level validation or rollback.
     */
    EditTextLayerCommand(OpenSWMMVisAnnotationLayer *layer, QJsonObject before, QJsonObject after,
                         const QString &label)
        : QUndoCommand(label), m_layer(layer), m_before(std::move(before)),
          m_after(std::move(after))
    {
    }
    /**
     * @brief Restore the complete captured before JSON through the live annotation layer;
     *   deleted/null layer no-op.
     * @note Does not merge concurrent edits or report restore failure. Repeated calls overwrite
     *   current layer from same snapshot; use normal QUndoStack sequencing.
     */
    void undo() override
    {
        if (m_layer)
            m_layer->restoreLayer(m_before);
    }
    /**
     * @brief Restore captured after JSON, including first redo triggered by stack push.
     * @note Null/deleted layer no-op; delegates geometry/style rebuilding and notifications to
     *   restoreLayer without independent canvas invalidation.
     */
    void redo() override
    {
        if (m_layer)
            m_layer->restoreLayer(m_after);
    }

private:
    QPointer<OpenSWMMVisAnnotationLayer> m_layer;
    QJsonObject m_before, m_after;
};
class SetSourceLabelsCommand : public QUndoCommand
{
public:
    /**
     * @brief Snapshot complete current label configuration and prepare a copy with only enabled
     *   changed.
     * @param[in] layer Required live source layer at construction; subsequently guarded by
     *   QPointer.
     * @param[in] enabled Target label-enabled flag; every other captured label setting preserved.
     * @note Does not apply at construction. GUI-thread stack command; later undo/redo restore
     *   complete captured configurations, so independent changes to other label settings are
     *   overwritten. Null constructor layer is not guarded before labelConfig read.
     */
    SetSourceLabelsCommand(OpenSWMMVisLayer *layer, bool enabled)
        : QUndoCommand(QObject::tr("Change source labels")), m_layer(layer),
          m_before(layer->labelConfig()), m_after(m_before)
    {
        m_after.enabled = enabled;
    }
    /**
     * @brief Restore captured complete original label configuration through setLabelConfig when
     *   layer survives.
     * @note Layer setter handles equality/notifications; no separate command validation, merge or
     *   canvas invalidation.
     */
    void undo() override
    {
        if (m_layer)
            m_layer->setLabelConfig(m_before);
    }
    /**
     * @brief Apply captured complete label configuration with requested enabled flag when layer
     *   survives.
     * @note Applies on initial stack push as well as later redo; deleted layer no-op.
     */
    void redo() override
    {
        if (m_layer)
            m_layer->setLabelConfig(m_after);
    }

private:
    QPointer<OpenSWMMVisLayer> m_layer;
    OpenSWMM::Render::LabelConfig m_before, m_after;
};
#endif
