/*!
 * \file textlayercommands.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \license GPL-3.0-or-later
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
    EditTextLayerCommand(OpenSWMMVisAnnotationLayer *layer, QJsonObject before,
                         QJsonObject after, const QString &label)
        : QUndoCommand(label), m_layer(layer), m_before(std::move(before)), m_after(std::move(after)) {}
    void undo() override { if (m_layer) m_layer->restoreLayer(m_before); }
    void redo() override { if (m_layer) m_layer->restoreLayer(m_after); }
private:
    QPointer<OpenSWMMVisAnnotationLayer> m_layer;
    QJsonObject m_before, m_after;
};
class SetSourceLabelsCommand : public QUndoCommand
{
public:
    SetSourceLabelsCommand(OpenSWMMVisLayer *layer, bool enabled)
        : QUndoCommand(QObject::tr("Change source labels")), m_layer(layer), m_before(layer->labelConfig()), m_after(m_before)
    { m_after.enabled = enabled; }
    void undo() override { if (m_layer) m_layer->setLabelConfig(m_before); }
    void redo() override { if (m_layer) m_layer->setLabelConfig(m_after); }
private:
    QPointer<OpenSWMMVisLayer> m_layer;
    OpenSWMM::Render::LabelConfig m_before, m_after;
};
#endif
