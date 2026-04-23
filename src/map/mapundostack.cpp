/*!
 * \file   mapundostack.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/mapundostack.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "map/crsmanager.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"

#include <QSettings>

// ===========================================================================
// MapUndoStack
// ===========================================================================

MapUndoStack::MapUndoStack(QObject *parent)
    : QUndoStack(parent)
{
    setUndoLimit(DefaultMaxUndoCount);
}

int MapUndoStack::maxUndoCount() const
{
    return undoLimit();
}

void MapUndoStack::setMaxUndoCount(int count)
{
    if (undoLimit() != count)
    {
        setUndoLimit(count);
        emit maxUndoCountChanged(count);
    }
}

void MapUndoStack::loadSettings()
{
    QSettings settings;
    int limit = settings.value(QStringLiteral("MapCanvas/undoLimit"), DefaultMaxUndoCount).toInt();
    setMaxUndoCount(limit);
}

void MapUndoStack::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("MapCanvas/undoLimit"), undoLimit());
}

// ===========================================================================
// MapCommand
// ===========================================================================

MapCommand::MapCommand(const QString &text, MapCanvas *canvas, QUndoCommand *parent)
    : QUndoCommand(text, parent),
      m_canvas(canvas)
{
}

MapCanvas *MapCommand::canvas() const { return m_canvas; }

// ===========================================================================
// PanZoomCommand
// ===========================================================================

PanZoomCommand::PanZoomCommand(const MapExtent &oldExtent,
                               const MapExtent &newExtent,
                               MapCanvas *canvas,
                               QUndoCommand *parent)
    : MapCommand(QObject::tr("Pan/Zoom"), canvas, parent),
      m_oldExtent(oldExtent),
      m_newExtent(newExtent)
{
}

void PanZoomCommand::undo()
{
    if (m_canvas)
        m_canvas->setExtent(m_oldExtent, /*pushUndo=*/false);
}

void PanZoomCommand::redo()
{
    if (m_canvas)
        m_canvas->setExtent(m_newExtent, /*pushUndo=*/false);
}

bool PanZoomCommand::mergeWith(const QUndoCommand *other)
{
    if (other->id() != id())
        return false;

    const auto *cmd = static_cast<const PanZoomCommand *>(other);
    m_newExtent = cmd->m_newExtent;
    return true;
}

// ===========================================================================
// ChangeCRSCommand
// ===========================================================================

ChangeCRSCommand::ChangeCRSCommand(const QString &oldAuthCode,
                                   const QString &newAuthCode,
                                   MapCanvas *canvas,
                                   QUndoCommand *parent)
    : MapCommand(QObject::tr("Change CRS"), canvas, parent),
      m_oldAuthCode(oldAuthCode),
      m_newAuthCode(newAuthCode)
{
}

static SpatialReferenceSystem *srsFromAuthCode(const QString &authCode)
{
    const int sep = authCode.lastIndexOf(QLatin1Char(':'));
    if (sep < 0)
        return nullptr;
    bool ok = false;
    const int code = authCode.mid(sep + 1).toInt(&ok);
    if (!ok)
        return nullptr;
    return SpatialReferenceSystem::fromAuthCode(authCode.left(sep), code);
}

void ChangeCRSCommand::undo()
{
    if (!m_canvas)
        return;
    // Use applyCRSInternal to avoid re-pushing to the undo stack (infinite recursion)
    if (SpatialReferenceSystem *srs = srsFromAuthCode(m_oldAuthCode))
        m_canvas->applyCRSInternal(srs, /*ownsSRS=*/true);
}

void ChangeCRSCommand::redo()
{
    if (!m_canvas)
        return;
    // Skip on first call from QUndoStack::push() — CRS already applied by setCanvasSRS()
    if (m_firstRedo) { m_firstRedo = false; return; }
    if (SpatialReferenceSystem *srs = srsFromAuthCode(m_newAuthCode))
        m_canvas->applyCRSInternal(srs, /*ownsSRS=*/true);
}

// ===========================================================================
// AddLayerCommand
// ===========================================================================

AddLayerCommand::AddLayerCommand(OpenSWMMVisLayer *layer, int position,
                                 MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Layer \"%1\"").arg(layer ? layer->name()
                                                           : QString()),
                 canvas, parent),
      m_layer(layer),
      m_position(position)
{
}

void AddLayerCommand::undo()
{
    if (m_canvas && m_layer)
    {
        // Find current position (may have shifted) and remove it.
        const auto &layers = m_canvas->layers();
        int idx = layers.indexOf(m_layer);
        if (idx >= 0)
            m_canvas->takeLayer(idx, /*pushUndo=*/false);
    }
}

void AddLayerCommand::redo()
{
    if (m_canvas && m_layer)
        m_canvas->insertLayer(m_position, m_layer, /*pushUndo=*/false);
}

// ===========================================================================
// RemoveLayerCommand
// ===========================================================================

RemoveLayerCommand::RemoveLayerCommand(OpenSWMMVisLayer *layer, int position,
                                       MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Remove Layer \"%1\"").arg(layer ? layer->name()
                                                              : QString()),
                 canvas, parent),
      m_layer(layer),
      m_position(position)
{
}

void RemoveLayerCommand::undo()
{
    if (m_canvas && m_layer)
        m_canvas->insertLayer(m_position, m_layer, /*pushUndo=*/false);
}

void RemoveLayerCommand::redo()
{
    if (m_canvas && m_layer)
    {
        const auto &layers = m_canvas->layers();
        int idx = layers.indexOf(m_layer);
        if (idx >= 0)
            m_canvas->takeLayer(idx, /*pushUndo=*/false);
    }
}

// ===========================================================================
// MoveLayerCommand
// ===========================================================================

MoveLayerCommand::MoveLayerCommand(int oldIndex, int newIndex,
                                   MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Move Layer"), canvas, parent),
      m_oldIndex(oldIndex),
      m_newIndex(newIndex)
{
}

void MoveLayerCommand::undo()
{
    if (m_canvas)
        m_canvas->moveLayer(m_newIndex, m_oldIndex, /*pushUndo=*/false);
}

void MoveLayerCommand::redo()
{
    if (m_canvas)
        m_canvas->moveLayer(m_oldIndex, m_newIndex, /*pushUndo=*/false);
}

// ===========================================================================
// MoveNodeCommand
// ===========================================================================

MoveNodeCommand::MoveNodeCommand(SWMMModelLayer *layer,
                                 int nodeIdx,
                                 double oldX, double oldY,
                                 double newX, double newY,
                                 QVector<LengthRec> lengthRecs,
                                 MapCanvas *canvas,
                                 QUndoCommand *parent)
    : MapCommand(QObject::tr("Move Node"), canvas, parent),
      m_layer(layer),
      m_nodeIdx(nodeIdx),
      m_oldX(oldX), m_oldY(oldY),
      m_newX(newX), m_newY(newY),
      m_lengthRecs(std::move(lengthRecs))
{
}

void MoveNodeCommand::redo()
{
    if (!m_layer) return;
    m_layer->applyNodeMove(m_nodeIdx, m_newX, m_newY);
    for (const LengthRec &rec : m_lengthRecs)
        m_layer->applyLinkLength(rec.linkIdx, rec.newLen);
}

void MoveNodeCommand::undo()
{
    if (!m_layer) return;
    m_layer->applyNodeMove(m_nodeIdx, m_oldX, m_oldY);
    for (const LengthRec &rec : m_lengthRecs)
        m_layer->applyLinkLength(rec.linkIdx, rec.oldLen);
}

bool MoveNodeCommand::mergeWith(const QUndoCommand *other)
{
    if (other->id() != id()) return false;
    const auto *cmd = static_cast<const MoveNodeCommand *>(other);
    if (cmd->m_layer != m_layer || cmd->m_nodeIdx != m_nodeIdx)
        return false;

    // Collapse the terminal coordinate and merge per-link length history:
    // for every link present in both records, carry our oldLen (earliest)
    // and the later command's newLen (latest). Links appearing in only one
    // record keep their snapshot as-is.
    m_newX = cmd->m_newX;
    m_newY = cmd->m_newY;

    for (const LengthRec &rec : cmd->m_lengthRecs)
    {
        bool matched = false;
        for (LengthRec &existing : m_lengthRecs)
        {
            if (existing.linkIdx == rec.linkIdx)
            {
                existing.newLen = rec.newLen;
                matched = true;
                break;
            }
        }
        if (!matched)
            m_lengthRecs.append(rec);
    }
    return true;
}

// ===========================================================================
// EditVertexCommand
// ===========================================================================

EditVertexCommand::EditVertexCommand(SWMMModelLayer *layer,
                                     int linkIdx,
                                     QVector<QPointF> oldInterior,
                                     QVector<QPointF> newInterior,
                                     double oldLen,
                                     double newLen,
                                     bool autoLengthApplied,
                                     MapCanvas *canvas,
                                     QUndoCommand *parent)
    : MapCommand(QObject::tr("Edit Link Vertices"), canvas, parent),
      m_layer(layer),
      m_linkIdx(linkIdx),
      m_oldInterior(std::move(oldInterior)),
      m_newInterior(std::move(newInterior)),
      m_oldLen(oldLen),
      m_newLen(newLen),
      m_autoLengthApplied(autoLengthApplied)
{
}

void EditVertexCommand::redo()
{
    if (!m_layer) return;
    m_layer->applyLinkInteriorVertices(m_linkIdx, m_newInterior);
    if (m_autoLengthApplied)
        m_layer->applyLinkLength(m_linkIdx, m_newLen);
}

void EditVertexCommand::undo()
{
    if (!m_layer) return;
    m_layer->applyLinkInteriorVertices(m_linkIdx, m_oldInterior);
    if (m_autoLengthApplied)
        m_layer->applyLinkLength(m_linkIdx, m_oldLen);
}

// ===========================================================================
// AddNodeCommand
// ===========================================================================

AddNodeCommand::AddNodeCommand(SWMMModelLayer *layer,
                               QString name,
                               int nodeType,
                               double x, double y,
                               MapCanvas *canvas,
                               QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Node \"%1\"").arg(name), canvas, parent),
      m_layer(layer),
      m_name(std::move(name)),
      m_nodeType(nodeType),
      m_x(x), m_y(y)
{
}

void AddNodeCommand::redo()
{
    if (!m_layer || m_present) return;
    if (m_layer->applyNodeAdd(m_name, m_nodeType, m_x, m_y))
        m_present = true;
}

void AddNodeCommand::undo()
{
    if (!m_layer || !m_present) return;
    // rollbackTailNodeAdd only works while the added node is still the
    // tail of the engine's node list. The no-remove-of-arbitrary-index
    // constraint is documented on the engine's swmm_node_pop_last.
    if (m_layer->rollbackTailNodeAdd(m_name))
        m_present = false;
}
