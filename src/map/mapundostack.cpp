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
