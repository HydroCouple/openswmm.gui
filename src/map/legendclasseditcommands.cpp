/*!
 * \file   legendclasseditcommands.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/legendclasseditcommands.h"

#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "render/ifeaturerenderer.h"

#include <QCoreApplication>

namespace openswmmvis::map {

namespace {

// Mirrors LegendOverlay::legendItemsFor()'s per-kind dispatch — the layer
// kinds that own an IFeatureRenderer. GISRasterLayer uses IRasterRenderer
// and is excluded; its per-class edits flow through a sister command (TBD).
OpenSWMM::Render::IFeatureRenderer *rendererFor(OpenSWMMVisLayer *layer)
{
    if (!layer) return nullptr;
    if (auto *l = qobject_cast<SWMMResultsLayer *>(layer))    return l->renderer();
    if (auto *l = qobject_cast<SWMM2DResultsLayer *>(layer))  return l->renderer();
    if (auto *l = qobject_cast<SWMM2DMeshLayer *>(layer))     return l->renderer();
    if (auto *l = qobject_cast<GISVectorLayer *>(layer))      return l->renderer();
    return nullptr;
}

} // namespace

SetRendererClassColorCommand::SetRendererClassColorCommand(
    OpenSWMMVisLayer *layer,
    QString           classKey,
    QColor            newColor,
    QUndoCommand     *parent)
    : QUndoCommand(parent),
      m_layer(layer),
      m_classKey(std::move(classKey)),
      m_newColor(std::move(newColor))
{
    // Snapshot the "before" state at construction so it survives the
    // first redo() — which fires immediately when pushed onto a QUndoStack.
    if (auto *r = rendererFor(m_layer.data()))
        m_oldColor = r->colorForClass(m_classKey);

    // Human-readable label shown by QUndoView / undo menu.
    setText(QCoreApplication::translate(
        "SetRendererClassColorCommand",
        "Change legend color")
        + (m_layer ? QStringLiteral(" — %1").arg(m_layer->objectName())
                   : QString()));
}

void SetRendererClassColorCommand::applyColor(const QColor &c)
{
    if (!m_layer) return;
    if (auto *r = rendererFor(m_layer.data())) {
        r->setColorForClass(m_classKey, c);
        // No rendererChanged() signal on the layer for in-place renderer
        // mutations (the renderer is plain C++). Trigger a repaint
        // directly — every legend view + the canvas itself subscribes.
        emit m_layer->repaintRequested();
    }
}

void SetRendererClassColorCommand::redo()
{
    // QUndoStack::push calls redo() immediately; the colour edit must
    // happen exactly once per redo, including this initial call.
    applyColor(m_newColor);
    m_firstRedo = false;
}

void SetRendererClassColorCommand::undo()
{
    applyColor(m_oldColor);   // invalid = "drop override" for graduated.
}

bool SetRendererClassColorCommand::mergeWith(const QUndoCommand *other)
{
    const auto *o = dynamic_cast<const SetRendererClassColorCommand *>(other);
    if (!o) return false;
    if (o->m_layer != m_layer || o->m_classKey != m_classKey) return false;
    // Same target — collapse: keep our oldColor (the original "before"),
    // adopt the later newColor.
    m_newColor = o->m_newColor;
    return true;
}

} // namespace openswmmvis::map
