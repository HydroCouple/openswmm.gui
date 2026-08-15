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
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/ifeaturerenderer.h"

#include <QCoreApplication>

namespace openswmmvis::map {

namespace {

// Gap B1 — single-renderer layer kinds only. SWMMResultsLayer is routed
// through its kind-qualified facade below (its rows aggregate per-kind
// renderers, so the layer-level renderer would mis-route bin keys).
// GISRasterLayer uses IRasterRenderer and is excluded; its per-class edits
// flow through a sister command (TBD).
OpenSWMM::Render::IFeatureRenderer *rendererFor(OpenSWMMVisLayer *layer)
{
    if (!layer) return nullptr;
    if (auto *l = qobject_cast<SWMM2DResultsLayer *>(layer))  return l->renderer();
    if (auto *l = qobject_cast<SWMM2DMeshLayer *>(layer))     return l->renderer();
    if (auto *l = qobject_cast<GISVectorLayer *>(layer))      return l->renderer();
    return nullptr;
}

// X4 / Gap B1 — multi-kind layers (SWMMModelLayer, SWMMResultsLayer) have
// no single IFeatureRenderer, so their per-class edits route through the
// layers' kind-qualified legend facades instead. These helpers dispatch to
// whichever path the layer supports.
QColor readClassColor(OpenSWMMVisLayer *layer, const QString &key)
{
    if (auto *r = rendererFor(layer)) return r->colorForClass(key);
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer)) return m->colorForClass(key);
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) return rl->colorForClass(key);
    return {};
}

void writeClassColor(OpenSWMMVisLayer *layer, const QString &key, const QColor &c)
{
    if (auto *r = rendererFor(layer)) {
        r->setColorForClass(key, c);
        emit layer->repaintRequested();   // renderer is plain C++ — repaint manually
    } else if (auto *m = qobject_cast<SWMMModelLayer *>(layer)) {
        m->setColorForClass(key, c);      // facade rebuilds overrides + repaints
    } else if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) {
        rl->setColorForClass(key, c);     // facade rebuilds overrides + repaints
    }
}

qreal readClassSize(OpenSWMMVisLayer *layer, const QString &key)
{
    if (auto *r = rendererFor(layer)) return r->sizeForClass(key);
    if (auto *m = qobject_cast<SWMMModelLayer *>(layer)) return m->sizeForClass(key);
    if (auto *rl = qobject_cast<SWMMResultsLayer *>(layer)) return rl->sizeForClass(key);
    return -1.0;
}

void writeClassSize(OpenSWMMVisLayer *layer, const QString &key, qreal s)
{
    if (auto *r = rendererFor(layer)) {
        r->setSizeForClass(key, s);
        emit layer->repaintRequested();
    } else if (auto *m = qobject_cast<SWMMModelLayer *>(layer)) {
        m->setSizeForClass(key, s);
    }
    // SWMMResultsLayer facade carries no size write-path yet — per-bin
    // size edits on results kinds arrive with the output-axis follow-up.
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
    m_oldColor = readClassColor(m_layer.data(), m_classKey);

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
    // Dispatches to the renderer (results/mesh/GIS) or, for the multi-kind
    // SWMMModelLayer, its legend facade — both emit a repaint.
    writeClassColor(m_layer.data(), m_classKey, c);
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

// ── SetRendererClassSizeCommand ───────────────────────────────────────

SetRendererClassSizeCommand::SetRendererClassSizeCommand(
    OpenSWMMVisLayer *layer,
    QString           classKey,
    qreal             newSize,
    QUndoCommand     *parent)
    : QUndoCommand(parent),
      m_layer(layer),
      m_classKey(std::move(classKey)),
      m_newSize(newSize)
{
    m_oldSize = readClassSize(m_layer.data(), m_classKey);

    setText(QCoreApplication::translate(
        "SetRendererClassSizeCommand", "Change legend size")
        + (m_layer ? QStringLiteral(" — %1").arg(m_layer->objectName())
                   : QString()));
}

void SetRendererClassSizeCommand::applySize(qreal s)
{
    if (!m_layer) return;
    writeClassSize(m_layer.data(), m_classKey, s);
}

void SetRendererClassSizeCommand::redo() { applySize(m_newSize); }
void SetRendererClassSizeCommand::undo() { applySize(m_oldSize); }

bool SetRendererClassSizeCommand::mergeWith(const QUndoCommand *other)
{
    const auto *o = dynamic_cast<const SetRendererClassSizeCommand *>(other);
    if (!o) return false;
    if (o->m_layer != m_layer || o->m_classKey != m_classKey) return false;
    m_newSize = o->m_newSize;
    return true;
}

} // namespace openswmmvis::map
