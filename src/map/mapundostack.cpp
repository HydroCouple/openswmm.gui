#include <openswmm/engine/openswmm_infrastructure.h>
/**
 * @file      mapundostack.cpp
 * @brief     An undo/redo stack for map operations with a configurable maximum depth.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "map/mapundostack.h"
#include "map/mapcanvas.h"
#include "map/objectdefaultsapplier.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "map/crsmanager.h"
#include "core/editgeometry.h"
#include "core/unitsystem.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"

#include "curve/curveregistry.h"
#include "curve/curveprovider.h"
#include "timeseries/timeseriesregistry.h"
#include "timeseries/timeseriesprovider.h"
#include "transect/transectregistry.h"
#include "transect/transectprovider.h"

#include <QDebug>
#include <QSettings>
#include <QSet>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_infrastructure.h>

// ===========================================================================
// MapUndoStack
// ===========================================================================

MapUndoStack::MapUndoStack(QObject *parent) : QUndoStack(parent)
{
    setUndoLimit(DefaultMaxUndoCount);
}

int MapUndoStack::maxUndoCount() const
{
    return undoLimit();
}

void MapUndoStack::setMaxUndoCount(int count)
{
    if (undoLimit() != count) {
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
    : QUndoCommand(text, parent), m_canvas(canvas)
{
}

MapCanvas *MapCommand::canvas() const
{
    return m_canvas;
}

// ===========================================================================
// PanZoomCommand
// ===========================================================================

PanZoomCommand::PanZoomCommand(const MapExtent &oldExtent, const MapExtent &newExtent,
                               MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Pan/Zoom"), canvas, parent), m_oldExtent(oldExtent),
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

ChangeCRSCommand::ChangeCRSCommand(const QString &oldAuthCode, const QString &newAuthCode,
                                   MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Change CRS"), canvas, parent), m_oldAuthCode(oldAuthCode),
      m_newAuthCode(newAuthCode)
{
}

/**
 * @brief Recreate CRS from authority prefix and integer suffix after final colon.
 * @param[in] authCode Authority:integer identifier, no explicit trimming/normalization beyond
 *   QString::toInt.
 * @return Newly allocated valid SRS owned by caller, or null missing colon/invalid integer/import
 *   failure. No registry update or canvas mutation.
 */
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
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    if (SpatialReferenceSystem *srs = srsFromAuthCode(m_newAuthCode))
        m_canvas->applyCRSInternal(srs, /*ownsSRS=*/true);
}

// ===========================================================================
// AddLayerCommand
// ===========================================================================

AddLayerCommand::AddLayerCommand(OpenSWMMVisLayer *layer, int position, MapCanvas *canvas,
                                 QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Layer \"%1\"").arg(layer ? layer->name() : QString()), canvas,
                 parent),
      m_layer(layer), m_position(position)
{
}

void AddLayerCommand::undo()
{
    if (m_canvas && m_layer) {
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

RemoveLayerCommand::RemoveLayerCommand(OpenSWMMVisLayer *layer, int position, MapCanvas *canvas,
                                       QUndoCommand *parent)
    : MapCommand(QObject::tr("Remove Layer \"%1\"").arg(layer ? layer->name() : QString()), canvas,
                 parent),
      m_layer(layer), m_position(position)
{
}

RemoveLayerCommand::~RemoveLayerCommand()
{
    // Once the removal can no longer be undone nothing will put the layer back,
    // so free it (review E-02). Membership is checked as well, in case the layer
    // was re-added outside the undo stack.
    if (m_removed && m_layer && m_canvas && !m_canvas->layers().contains(m_layer.data()))
        m_layer->deleteLater();
}

void RemoveLayerCommand::undo()
{
    if (m_canvas && m_layer) {
        m_canvas->insertLayer(m_position, m_layer, /*pushUndo=*/false);
        m_removed = false;
    }
}

void RemoveLayerCommand::redo()
{
    if (m_canvas && m_layer) {
        const auto &layers = m_canvas->layers();
        int idx = layers.indexOf(m_layer.data());
        if (idx >= 0 && m_canvas->takeLayer(idx, /*pushUndo=*/false))
            m_removed = true;
    }
}

// ===========================================================================
// MoveLayerCommand
// ===========================================================================

namespace
{
/** @brief Guard an owning order snapshot without taking ownership of its layers.
 * @param[in] order Current live layer identities.
 * @return Lifetime-guarded identities in the same order. */
QList<QPointer<OpenSWMMVisLayer>> guardedLayerOrder(const QList<OpenSWMMVisLayer *> &order)
{
    QList<QPointer<OpenSWMMVisLayer>> result;
    result.reserve(order.size());
    for (auto *layer : order)
        result.append(layer);
    return result;
}
/** @brief Replay only attached snapshot survivors and preserve all later attached layers.
 * @param[in,out] canvas Borrowed canvas; null is a no-op.
 * @param[in] order Guarded historical order, possibly containing removed/deleted layers.
 * @note Removed layers are never reattached; current unmentioned layers are appended. */
void restoreLayerOrder(MapCanvas *canvas, const QList<QPointer<OpenSWMMVisLayer>> &order)
{
    if (!canvas)
        return;
    const auto current = canvas->layers();
    const QSet<OpenSWMMVisLayer *> attached(current.cbegin(), current.cend());
    QSet<OpenSWMMVisLayer *> seen;
    QList<OpenSWMMVisLayer *> restored;
    for (const auto &guard : order) {
        auto *layer = guard.data();
        if (layer && attached.contains(layer) && !seen.contains(layer)) {
            restored.append(layer);
            seen.insert(layer);
        }
    }
    for (auto *layer : current)
        if (!seen.contains(layer))
            restored.append(layer);
    if (restored != current)
        canvas->reorderLayers(restored, /*pushUndo=*/false);
}
}

MoveLayerCommand::MoveLayerCommand(int oldIndex, int newIndex, MapCanvas *canvas,
                                   QUndoCommand *parent)
    : MapCommand(QObject::tr("Move Layer"), canvas, parent)
{
    if (!canvas)
        return;
    auto order = canvas->layers();
    if (oldIndex < 0 || oldIndex >= order.size() || newIndex < 0 || newIndex >= order.size())
        return;
    m_layer = order.at(oldIndex);
    m_oldOrder = guardedLayerOrder(order);
    order.move(oldIndex, newIndex);
    m_newOrder = guardedLayerOrder(order);
}

void MoveLayerCommand::undo()
{
    if (m_canvas && m_layer && m_canvas->layers().contains(m_layer))
        restoreLayerOrder(m_canvas, m_oldOrder);
}

void MoveLayerCommand::redo()
{
    if (m_canvas && m_layer && m_canvas->layers().contains(m_layer))
        restoreLayerOrder(m_canvas, m_newOrder);
}

// ===========================================================================
// MoveSublayerCommand
// ===========================================================================

MoveSublayerCommand::MoveSublayerCommand(OpenSWMMVisLayer *host, int fromIndex, int toIndex,
                                         MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Move Sublayer"), canvas, parent), m_host(host),
      m_fromIndex(fromIndex), m_toIndex(toIndex)
{
}

void MoveSublayerCommand::undo()
{
    if (m_canvas && m_host)
        m_canvas->moveSublayer(m_host, m_toIndex, m_fromIndex, /*pushUndo=*/false);
}

void MoveSublayerCommand::redo()
{
    if (m_canvas && m_host)
        m_canvas->moveSublayer(m_host, m_fromIndex, m_toIndex, /*pushUndo=*/false);
}

// ===========================================================================
// Engine identity for index-taking commands
// ===========================================================================

namespace
{
// Deleting an object renumbers every object after it, and undoing the delete
// re-adds it at the end, so an index captured when a command was made can
// name a different object by the time the command runs. Commands keep the
// object's ID and resolve its current index on every redo/undo. Without an
// engine (no ID captured) the original index is used, as before.
using EngineIdFn = const char *(*)(SWMM_Engine, int);
using EngineIndexFn = int (*)(SWMM_Engine, const char *);

/** @brief Engine ID of object @p idx, or empty without an engine or for a bad index. */
QString engineIdAt(const SWMMModelLayer *layer, EngineIdFn idOf, int idx)
{
    SWMM_Engine eng = layer ? layer->engine() : nullptr;
    const char *id = (eng && idx >= 0) ? idOf(eng, idx) : nullptr;
    return id ? QString::fromUtf8(id) : QString();
}

/** @brief Current index of @p name (-1 if gone), or @p fallback when no ID was captured. */
int currentIndex(const SWMMModelLayer *layer, EngineIndexFn indexOf, const QString &name,
                 int fallback)
{
    if (name.isEmpty())
        return fallback;
    SWMM_Engine eng = layer ? layer->engine() : nullptr;
    return eng ? indexOf(eng, name.toUtf8().constData()) : -1;
}
} // namespace

// ===========================================================================
// MoveNodeCommand
// ===========================================================================

MoveNodeCommand::MoveNodeCommand(SWMMModelLayer *layer, int nodeIdx, double oldX, double oldY,
                                 double newX, double newY, QVector<LengthRec> lengthRecs,
                                 MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Move Node"), canvas, parent), m_layer(layer), m_nodeIdx(nodeIdx),
      m_nodeName(engineIdAt(layer, swmm_node_id, nodeIdx)), m_oldX(oldX), m_oldY(oldY),
      m_newX(newX), m_newY(newY), m_lengthRecs(std::move(lengthRecs))
{
    for (LengthRec &rec : m_lengthRecs)
        rec.linkName = engineIdAt(layer, swmm_link_id, rec.linkIdx);
}

void MoveNodeCommand::redo()
{
    if (!m_layer)
        return;
    m_layer->applyNodeMove(currentIndex(m_layer, swmm_node_index, m_nodeName, m_nodeIdx), m_newX,
                           m_newY);
    for (const LengthRec &rec : m_lengthRecs)
        m_layer->applyLinkLength(currentIndex(m_layer, swmm_link_index, rec.linkName, rec.linkIdx),
                                 rec.newLen);
}

void MoveNodeCommand::undo()
{
    if (!m_layer)
        return;
    m_layer->applyNodeMove(currentIndex(m_layer, swmm_node_index, m_nodeName, m_nodeIdx), m_oldX,
                           m_oldY);
    for (const LengthRec &rec : m_lengthRecs)
        m_layer->applyLinkLength(currentIndex(m_layer, swmm_link_index, rec.linkName, rec.linkIdx),
                                 rec.oldLen);
}

MoveGageCommand::MoveGageCommand(SWMMModelLayer *layer, int gageIdx, double oldX, double oldY,
                                 double newX, double newY, MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Move Rain Gage"), canvas, parent), m_layer(layer), m_gageIdx(gageIdx),
      m_gageName(engineIdAt(layer, swmm_gage_id, gageIdx)), m_oldX(oldX), m_oldY(oldY),
      m_newX(newX), m_newY(newY)
{
}

void MoveGageCommand::redo()
{
    if (!m_layer)
        return;
    m_layer->applyGageMove(currentIndex(m_layer, swmm_gage_index, m_gageName, m_gageIdx), m_newX,
                           m_newY);
}

void MoveGageCommand::undo()
{
    if (!m_layer)
        return;
    m_layer->applyGageMove(currentIndex(m_layer, swmm_gage_index, m_gageName, m_gageIdx), m_oldX,
                           m_oldY);
}

bool MoveGageCommand::mergeWith(const QUndoCommand *other)
{
    if (other->id() != id())
        return false;
    const auto *cmd = static_cast<const MoveGageCommand *>(other);
    if (cmd->m_layer != m_layer || cmd->m_gageIdx != m_gageIdx || cmd->m_gageName != m_gageName)
        return false;
    // Collapse to the latest terminal coordinate; the original oldX/oldY
    // stays so one undo returns to the pre-drag position.
    m_newX = cmd->m_newX;
    m_newY = cmd->m_newY;
    return true;
}

bool MoveNodeCommand::mergeWith(const QUndoCommand *other)
{
    if (other->id() != id())
        return false;
    const auto *cmd = static_cast<const MoveNodeCommand *>(other);
    if (cmd->m_layer != m_layer || cmd->m_nodeIdx != m_nodeIdx || cmd->m_nodeName != m_nodeName)
        return false;

    // Collapse the terminal coordinate and merge per-link length history:
    // for every link present in both records, carry our oldLen (earliest)
    // and the later command's newLen (latest). Links appearing in only one
    // record keep their snapshot as-is.
    m_newX = cmd->m_newX;
    m_newY = cmd->m_newY;

    for (const LengthRec &rec : cmd->m_lengthRecs) {
        bool matched = false;
        for (LengthRec &existing : m_lengthRecs) {
            if (existing.linkIdx == rec.linkIdx && existing.linkName == rec.linkName) {
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

EditVertexCommand::EditVertexCommand(SWMMModelLayer *layer, int linkIdx,
                                     QVector<QPointF> oldInterior, QVector<QPointF> newInterior,
                                     double oldLen, double newLen, bool autoLengthApplied,
                                     MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Edit Link Vertices"), canvas, parent), m_layer(layer),
      m_linkIdx(linkIdx), m_linkName(engineIdAt(layer, swmm_link_id, linkIdx)),
      m_oldInterior(std::move(oldInterior)),
      m_newInterior(std::move(newInterior)), m_oldLen(oldLen), m_newLen(newLen),
      m_autoLengthApplied(autoLengthApplied)
{
}

void EditVertexCommand::redo()
{
    if (!m_layer)
        return;
    const int linkIdx = currentIndex(m_layer, swmm_link_index, m_linkName, m_linkIdx);
    m_layer->applyLinkInteriorVertices(linkIdx, m_newInterior);
    if (m_autoLengthApplied)
        m_layer->applyLinkLength(linkIdx, m_newLen);
}

void EditVertexCommand::undo()
{
    if (!m_layer)
        return;
    const int linkIdx = currentIndex(m_layer, swmm_link_index, m_linkName, m_linkIdx);
    m_layer->applyLinkInteriorVertices(linkIdx, m_oldInterior);
    if (m_autoLengthApplied)
        m_layer->applyLinkLength(linkIdx, m_oldLen);
}

// ===========================================================================
// FlipLinkCommand
// ===========================================================================

FlipLinkCommand::FlipLinkCommand(SWMMModelLayer *layer, int linkIdx, MapCanvas *canvas,
                                 QUndoCommand *parent)
    : MapCommand(QObject::tr("Flip Link Direction"), canvas, parent), m_layer(layer),
      m_linkIdx(linkIdx), m_linkName(engineIdAt(layer, swmm_link_id, linkIdx))
{
}

// applyLinkFlip is self-inverse — flipping twice restores the original
// endpoints, vertex order, offsets and loss coefficients — so both directions
// are the same call.
void FlipLinkCommand::redo()
{
    if (m_layer)
        m_layer->applyLinkFlip(currentIndex(m_layer, swmm_link_index, m_linkName, m_linkIdx));
}

void FlipLinkCommand::undo()
{
    if (m_layer)
        m_layer->applyLinkFlip(currentIndex(m_layer, swmm_link_index, m_linkName, m_linkIdx));
}

// ===========================================================================
// EditSubcatchCommand
// ===========================================================================

EditSubcatchCommand::EditSubcatchCommand(SWMMModelLayer *layer, int catchIdx,
                                         QVector<QPointF> oldVertices, QVector<QPointF> newVertices,
                                         double oldArea, double newArea, bool applyArea,
                                         MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Edit Subcatchment Vertices"), canvas, parent), m_layer(layer),
      m_catchIdx(catchIdx), m_catchName(engineIdAt(layer, swmm_subcatch_id, catchIdx)),
      m_old(std::move(oldVertices)), m_new(std::move(newVertices)),
      m_oldArea(oldArea), m_newArea(newArea), m_applyArea(applyArea)
{
}

void EditSubcatchCommand::redo()
{
    if (!m_layer)
        return;
    const int catchIdx = currentIndex(m_layer, swmm_subcatch_index, m_catchName, m_catchIdx);
    m_layer->applySubcatchVertices(catchIdx, m_new);
    if (m_applyArea)
        m_layer->applySubcatchArea(catchIdx, m_newArea);
}

void EditSubcatchCommand::undo()
{
    if (!m_layer)
        return;
    const int catchIdx = currentIndex(m_layer, swmm_subcatch_index, m_catchName, m_catchIdx);
    m_layer->applySubcatchVertices(catchIdx, m_old);
    if (m_applyArea)
        m_layer->applySubcatchArea(catchIdx, m_oldArea);
}

// ===========================================================================
// AddNodeCommand
// ===========================================================================

AddNodeCommand::AddNodeCommand(SWMMModelLayer *layer, QString name, int nodeType, double x,
                               double y, MapCanvas *canvas, double invertElev, QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Node \"%1\"").arg(name), canvas, parent), m_layer(layer),
      m_name(std::move(name)), m_nodeType(nodeType), m_x(x), m_y(y), m_invertElev(invertElev)
{
}

void AddNodeCommand::redo()
{
    if (!m_layer || m_present)
        return;
    if (!m_layer->applyNodeAdd(m_name, m_nodeType, m_x, m_y))
        return;
    m_present = true;

    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_node_index(eng, m_name.toUtf8().constData());

    // Creation defaults first; the more specific terrain-derived invert
    // below wins (see workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md).
    ObjectDefaultsApplier::applyNodeDefaults(eng, idx, m_nodeType);
    if (!m_lidControl.isEmpty()) {
        const int control = swmm_lid_index(eng, m_lidControl.toUtf8().constData());
        if (swmm_node_set_lid(eng, idx, control, 0.0) != SWMM_OK || control < 0) {
            m_layer->rollbackTailNodeAdd(m_name);
            m_present = false;
            return;
        }
        m_layer->markEdited();
    }

    if (m_invertElev != 0.0 && idx >= 0)
        swmm_node_set_invert_elev(eng, idx, m_invertElev);
}

void AddNodeCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    // rollbackTailNodeAdd only works while the added node is still the
    // tail of the engine's node list. The no-remove-of-arbitrary-index
    // constraint is documented on the engine's swmm_node_pop_last.
    if (m_layer->rollbackTailNodeAdd(m_name))
        m_present = false;
}

// ===========================================================================
// ReorderLayersCommand (Slice AX)
// ===========================================================================

ReorderLayersCommand::ReorderLayersCommand(QList<OpenSWMMVisLayer *> oldOrder,
                                           QList<OpenSWMMVisLayer *> newOrder, MapCanvas *canvas,
                                           QUndoCommand *parent)
    : MapCommand(QObject::tr("Reorder Layers"), canvas, parent),
      m_oldOrder(guardedLayerOrder(oldOrder)), m_newOrder(guardedLayerOrder(newOrder))
{
}

void ReorderLayersCommand::undo()
{
    restoreLayerOrder(m_canvas, m_oldOrder);
}

void ReorderLayersCommand::redo()
{
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    } // already applied by caller
    restoreLayerOrder(m_canvas, m_newOrder);
}

// ===========================================================================
// ReorderCategoriesCommand (Slice AY)
// ===========================================================================

ReorderCategoriesCommand::ReorderCategoriesCommand(SWMMModelLayer *layer,
                                                   QVector<SWMMModelLayer::Category> oldOrder,
                                                   QVector<SWMMModelLayer::Category> newOrder,
                                                   QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Reorder Categories"), parent), m_layer(layer),
      m_oldOrder(std::move(oldOrder)), m_newOrder(std::move(newOrder))
{
}

void ReorderCategoriesCommand::undo()
{
    m_layer->setCategoryOrder(m_oldOrder);
}

void ReorderCategoriesCommand::redo()
{
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    } // already applied by caller
    m_layer->setCategoryOrder(m_newOrder);
}

// ===========================================================================
// ReorderObjectsCommand (Slice AY)
// ===========================================================================

ReorderObjectsCommand::ReorderObjectsCommand(SWMMModelLayer *layer, SWMMModelLayer::Category cat,
                                             QVector<int> oldOrder, QVector<int> newOrder,
                                             QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Reorder Objects"), parent), m_layer(layer), m_cat(cat),
      m_oldOrder(std::move(oldOrder)), m_newOrder(std::move(newOrder))
{
}

void ReorderObjectsCommand::undo()
{
    if (m_oldOrder.isEmpty())
        m_layer->clearObjectOrder(m_cat);
    else
        m_layer->setObjectOrder(m_cat, m_oldOrder);
}

void ReorderObjectsCommand::redo()
{
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    } // already applied by caller
    m_layer->setObjectOrder(m_cat, m_newOrder);
}

// ===========================================================================
// AddLinkCommand (Slice AE)
// ===========================================================================

AddLinkCommand::AddLinkCommand(SWMMModelLayer *layer, QString name, int linkType, QString fromNode,
                               QString toNode, QVector<QPointF> interiorVertices, MapCanvas *canvas,
                               double offsetUp, double offsetDn, QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Link"), canvas, parent), m_layer(layer), m_name(std::move(name)),
      m_linkType(linkType), m_fromNode(std::move(fromNode)), m_toNode(std::move(toNode)),
      m_interiorVertices(std::move(interiorVertices)), m_offsetUp(offsetUp), m_offsetDn(offsetDn)
{
}

void AddLinkCommand::redo()
{
    m_linkIdx = -1;
    m_present = m_layer->applyLinkAdd(m_name, m_linkType, m_fromNode, m_toNode, m_interiorVertices,
                                      &m_linkIdx);

    // Creation defaults first; auto-length / terrain offsets below win.
    // Length default is suppressed when auto-length will compute it.
    if (m_present && m_linkIdx >= 0) {
        const bool autoLen = m_canvas && m_canvas->property("autoLength").toBool();
        ObjectDefaultsApplier::applyLinkDefaults(m_layer->engine(), m_linkIdx, m_linkType, autoLen);
    }

    // Auto-length: set GIS polyline length when the canvas flag is active.
    if (m_present && m_linkIdx >= 0 && m_canvas && m_canvas->property("autoLength").toBool()) {
        const QVector<QPointF> poly = m_layer->cachedLinkPolyline(m_linkIdx);
        if (poly.size() >= 2) {
            const double len = m_layer->polylineLengthInModelUnits(poly);
            if (len > 0.0)
                m_layer->applyLinkLength(m_linkIdx, len);
        }
    }

    // Terrain-derived invert offsets.
    if (m_present && m_linkIdx >= 0 && (m_offsetUp != 0.0 || m_offsetDn != 0.0)) {
        SWMM_Engine eng = m_layer->engine();
        if (m_offsetUp != 0.0)
            swmm_link_set_offset_up(eng, m_linkIdx, m_offsetUp);
        if (m_offsetDn != 0.0)
            swmm_link_set_offset_dn(eng, m_linkIdx, m_offsetDn);
    }
    if (m_present && m_linkIdx >= 0 && m_lidLayer > 0) {
        if (swmm_lid_node_outlet_set(m_layer->engine(), m_linkIdx, m_lidLayer, m_lidTop) !=
            SWMM_OK) {
            m_layer->rollbackTailLinkAdd(m_name);
            m_present = false;
        } else
            m_layer->markEdited();
    }
}

void AddLinkCommand::undo()
{
    if (m_present)
        m_present = !m_layer->rollbackTailLinkAdd(m_name);
}

// ===========================================================================
// AddGageCommand (Slice AE)
// ===========================================================================

AddGageCommand::AddGageCommand(SWMMModelLayer *layer, QString name, double x, double y,
                               MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Rain Gage"), canvas, parent), m_layer(layer),
      m_name(std::move(name)), m_x(x), m_y(y)
{
}

void AddGageCommand::redo()
{
    m_present = m_layer->applyGageAdd(m_name, m_x, m_y);
    if (m_present) {
        SWMM_Engine eng = m_layer->engine();
        const int idx = swmm_gage_index(eng, m_name.toUtf8().constData());
        ObjectDefaultsApplier::applyGageDefaults(eng, idx);
    }
}

void AddGageCommand::undo()
{
    if (m_present)
        m_present = !m_layer->rollbackTailGageAdd(m_name);
}

// ===========================================================================
// AddSubcatchmentCommand (Slice AF)
// ===========================================================================

AddSubcatchmentCommand::AddSubcatchmentCommand(SWMMModelLayer *layer, QString name,
                                               QVector<QPointF> polygon, MapCanvas *canvas,
                                               QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Subcatchment"), canvas, parent), m_layer(layer),
      m_name(std::move(name)), m_polygon(std::move(polygon))
{
}

void AddSubcatchmentCommand::redo()
{
    m_subcatchIdx = -1;
    m_present = m_layer->applySubcatchAdd(m_name, m_polygon, &m_subcatchIdx);

    // Creation defaults first; auto-area below wins. Area default is
    // suppressed when auto-area will compute it.
    if (m_present && m_subcatchIdx >= 0) {
        const bool autoArea = m_canvas && m_canvas->property("autoLength").toBool();
        ObjectDefaultsApplier::applySubcatchDefaults(m_layer->engine(), m_subcatchIdx, autoArea);
    }

    // Auto-area: set GIS polygon area when the canvas flag is active.
    if (m_present && m_subcatchIdx >= 0 && m_canvas && m_canvas->property("autoLength").toBool()) {
        const double sqUnits = EditGeometry::polygonArea(m_polygon);
        if (sqUnits > 0.0) {
            // Convert square map units → SWMM area unit:
            //   SI  (CMS/LPS/MLD): meters → hectares  (÷ 10 000)
            //   US  (CFS/GPM/MGD): feet   → acres     (÷ 43 560)
            const bool si = UnitSystem::instance()->isSI();
            const double area = si ? sqUnits / 10000.0 : sqUnits / 43560.0;
            swmm_subcatch_set_area(m_layer->engine(), m_subcatchIdx, area);
        }
    }
}

void AddSubcatchmentCommand::undo()
{
    if (m_present)
        m_present = !m_layer->rollbackTailSubcatchAdd(m_name);
}

// ===========================================================================
// AssignSubcatchGagesCommand
// ===========================================================================

AssignSubcatchGagesCommand::AssignSubcatchGagesCommand(SWMMModelLayer *layer,
                                                       QStringList subcatchNames,
                                                       QStringList newGages, QStringList oldGages,
                                                       const QString &text, MapCanvas *canvas,
                                                       QUndoCommand *parent)
    : MapCommand(text, canvas, parent), m_layer(layer), m_subcatchNames(std::move(subcatchNames)),
      m_newGages(std::move(newGages)), m_oldGages(std::move(oldGages))
{
}

void AssignSubcatchGagesCommand::apply(const QStringList &gages)
{
    if (!m_layer || !m_layer->engine())
        return;
    SWMM_Engine eng = m_layer->engine();

    for (int i = 0; i < m_subcatchNames.size() && i < gages.size(); ++i) {
        const QString &gage = gages[i];
        if (gage.isEmpty())
            continue; // no prior gage to restore; SWMM cannot express "none"

        // Resolve afresh every time: an intervening delete may have re-packed
        // indices since this command was built.
        const int sIdx = swmm_subcatch_index(eng, m_subcatchNames[i].toUtf8().constData());
        if (sIdx < 0)
            continue; // subcatchment gone (e.g. an undone add) — clean no-op
        m_layer->applySubcatchSetGage(sIdx, gage);
    }

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene, QStringLiteral("assign-subcatch-gages"));
}

void AssignSubcatchGagesCommand::redo()
{
    apply(m_newGages);
}
void AssignSubcatchGagesCommand::undo()
{
    apply(m_oldGages);
}

// ===========================================================================
// ConfigureGageCommand
// ===========================================================================

ConfigureGageCommand::Config ConfigureGageCommand::capture(SWMMModelLayer *layer,
                                                           const QString &gageName, bool *ok)
{
    Config c;
    if (ok)
        *ok = false;
    if (!layer || !layer->engine())
        return c;

    SWMM_Engine eng = layer->engine();
    const int idx = swmm_gage_index(eng, gageName.toUtf8().constData());
    if (idx < 0)
        return c;

    swmm_gage_get_data_source(eng, idx, &c.dataSource);
    swmm_gage_get_rain_type(eng, idx, &c.rainType);
    swmm_gage_get_rain_interval(eng, idx, &c.intervalSec);
    swmm_gage_get_scale_factor(eng, idx, &c.scaleFactor);
    swmm_gage_get_snow_factor(eng, idx, &c.snowFactor);

    char buf[256] = {0};
    if (swmm_gage_get_timeseries(eng, idx, buf, static_cast<int>(sizeof(buf))) == SWMM_OK)
        c.timeseries = QString::fromUtf8(buf);

    if (ok)
        *ok = true;
    return c;
}

ConfigureGageCommand::ConfigureGageCommand(SWMMModelLayer *layer, QString gageName,
                                           Config newConfig, Config oldConfig, MapCanvas *canvas,
                                           QUndoCommand *parent)
    : MapCommand(QObject::tr("Configure Rain Gage"), canvas, parent), m_layer(layer),
      m_gageName(std::move(gageName)), m_new(std::move(newConfig)), m_old(std::move(oldConfig))
{
}

void ConfigureGageCommand::apply(const Config &c)
{
    if (!m_layer || !m_layer->engine())
        return;
    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_gage_index(eng, m_gageName.toUtf8().constData());
    if (idx < 0)
        return; // gage vanished (e.g. an undone add) — clean no-op

    swmm_gage_set_rain_type(eng, idx, c.rainType);
    swmm_gage_set_rain_interval(eng, idx, c.intervalSec);
    swmm_gage_set_scale_factor(eng, idx, c.scaleFactor);
    swmm_gage_set_snow_factor(eng, idx, c.snowFactor);

    // Order matters: swmm_gage_set_timeseries validates that the series exists
    // and sets the source itself, so it must run after the scalars and only
    // when a series is actually named.
    if (c.dataSource == SWMM_GAGE_TIMESERIES && !c.timeseries.isEmpty())
        swmm_gage_set_timeseries(eng, idx, c.timeseries.toUtf8().constData());
    else
        swmm_gage_set_data_source(eng, idx, c.dataSource);

    m_layer->markEdited();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene, QStringLiteral("configure-gage"));
}

void ConfigureGageCommand::redo()
{
    apply(m_new);
}
void ConfigureGageCommand::undo()
{
    apply(m_old);
}

// ===========================================================================
// DeleteObjectCommand — constructor + redo / undo
// ===========================================================================

DeleteObjectCommand::DeleteObjectCommand(SWMMModelLayer *layer, const QString &name,
                                         TargetKind kind, MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Delete Object"), canvas, parent), m_layer(layer), m_kind(kind)
{
    // Nothing to snapshot: redo() takes the engine's record of the delete.
    m_name = name;
}

DeleteObjectCommand::~DeleteObjectCommand()
{
    swmm_delete_record_destroy(m_record);
}

void DeleteObjectCommand::redo()
{
    swmm_delete_record_destroy(m_record);
    m_record = nullptr;
    switch (m_kind) {
    case DeleteNode:
        m_layer->applyNodeDelete(m_name, nullptr, &m_record);
        break;
    case DeleteLink:
        m_layer->applyLinkDelete(m_name, &m_record);
        break;
    case DeleteGage:
        m_layer->applyGageDelete(m_name, &m_record);
        break;
    case DeleteSubcatch:
        m_layer->applySubcatchDelete(m_name, &m_record);
        break;
    }
}

void DeleteObjectCommand::undo()
{
    restoreFromRecord();
}

void DeleteObjectCommand::restoreFromRecord()
{
    if (!m_record)
        return;
    if (!m_layer->applyDeleteRestore(m_record)) {
        qWarning() << "Undo delete: the engine refused to restore" << m_name
                   << "because the model changed since the delete; dropping this undo step.";
        setObsolete(true);
        return;
    }
    swmm_delete_record_destroy(m_record);
    m_record = nullptr;
}

// ===========================================================================
// AddAnnotationCommand
// ===========================================================================

AddAnnotationCommand::AddAnnotationCommand(OpenSWMMVisAnnotationLayer *layer,
                                           AnnotationTextItem *item, MapCanvas *canvas,
                                           QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Text Annotation"), canvas, parent), m_layer(layer), m_item(item),
      m_itemId(item ? item->id() : QString())
{
}

AddAnnotationCommand::~AddAnnotationCommand()
{
    // Only delete the item when it lives outside the layer (i.e., the
    // command holds the only reference). When m_present is true the layer
    // owns the item via its QObject parent chain.
    if (m_item && !m_present)
        delete m_item;
}

void AddAnnotationCommand::redo()
{
    if (!m_layer || m_present || !m_item)
        return;
    if (m_layer->addAnnotation(m_item)) {
        m_present = true;
        // The layer now owns the item; relinquish our raw-pointer hold so
        // the destructor doesn't double-delete if the layer drops it first.
        m_item = nullptr;
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Scene, QStringLiteral("addtext-redo"));
    }
}

void AddAnnotationCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    m_item = m_layer->takeAnnotation(m_itemId);
    if (m_item) {
        m_present = false;
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Scene, QStringLiteral("addtext-undo"));
    }
}

// ===========================================================================
// BatchDeleteCommand  (perf-plan Phase A2)
// ===========================================================================

BatchDeleteCommand::BatchDeleteCommand(SWMMModelLayer *layer, const QList<Target> &targets,
                                       MapCanvas *canvas, const QString &text)
    : BulkEditCommand(layer, text), m_layer(layer)
{
    Q_UNUSED(canvas);
    // Nothing to snapshot: redo() records each kind's batch in the engine.
    for (const Target &t : targets) {
        switch (t.kind) {
        case DeleteObjectCommand::DeleteNode:
            m_nodeNames << t.name;
            break;
        case DeleteObjectCommand::DeleteLink:
            m_linkNames << t.name;
            break;
        case DeleteObjectCommand::DeleteGage:
            m_gageNames << t.name;
            break;
        case DeleteObjectCommand::DeleteSubcatch:
            m_subcatchNames << t.name;
            break;
        }
    }
}

void BatchDeleteCommand::redo()
{
    if (!m_layer)
        return;
    SWMMModelLayer::BulkEdit guard(m_layer);
    for (SWMM_DeleteRecord *r : {&m_gageRecord, &m_linkRecord, &m_nodeRecord, &m_subcatchRecord}) {
        swmm_delete_record_destroy(*r);
        *r = nullptr;
    }
    m_layer->applyDeleteMany(m_nodeNames, m_linkNames, m_subcatchNames, m_gageNames, nullptr,
                             &m_gageRecord, &m_linkRecord, &m_nodeRecord, &m_subcatchRecord);
}

bool BatchDeleteCommand::restoreRecord(SWMM_DeleteRecord &record, const char *what, int count)
{
    if (!record)
        return true;
    if (!m_layer->applyDeleteRestore(record)) {
        qWarning() << "Undo delete: the engine refused to restore" << count << what
                   << "because the model changed since the delete.";
        return false;
    }
    swmm_delete_record_destroy(record);
    record = nullptr;
    return true;
}

void BatchDeleteCommand::undo()
{
    if (!m_layer)
        return;
    SWMMModelLayer::BulkEdit guard(m_layer);
    ObjectDefaultsApplier::BatchScope defaults;
    // Reverse of applyDeleteMany's order (nodes, links, subcatchments, gages):
    // each engine record then finds the model exactly as its batch left it.
    restoreRecord(m_gageRecord, "rain gage(s)", m_gageNames.size());
    restoreRecord(m_subcatchRecord, "subcatchment(s)", m_subcatchNames.size());
    restoreRecord(m_linkRecord, "link(s)", m_linkNames.size());
    restoreRecord(m_nodeRecord, "node(s)", m_nodeNames.size());
}

BatchDeleteCommand::~BatchDeleteCommand()
{
    swmm_delete_record_destroy(m_gageRecord);
    swmm_delete_record_destroy(m_linkRecord);
    swmm_delete_record_destroy(m_nodeRecord);
    swmm_delete_record_destroy(m_subcatchRecord);
}

// ===========================================================================
// DeleteDataObjectCommand  (Slice — GUI delete for data objects, 2026-07-22)
// ---------------------------------------------------------------------------
// Curve / TimeSeries / Transect deletion routed through the owning registry.
// See workplans/GUI_DELETE_ALL_OBJECTS_PLAN_2026-07-22.md for the roadmap that
// extends supports()/redo()/undo() to the remaining data types once the engine
// ships their swmm_*_delete APIs.
// ===========================================================================

namespace
{

/**
 * @param[in] l Optional borrowed model, null yields null.
 * @brief Lazily ensure and runtime-cast the model-owned curve registry.
 * @return Borrowed registry or null if absent/wrong runtime type; no ownership transfer. May
 *   initialize/load registry and its normal side effects through model ensure method; GUI-thread
 *   access.
 */
openswmmvis::curve::CurveRegistry *dd_curveReg(SWMMModelLayer *l)
{
    return l ? qobject_cast<openswmmvis::curve::CurveRegistry *>(l->ensureCurveRegistry())
             : nullptr;
}
/**
 * @param[in] l Optional borrowed model, null yields null.
 * @brief Lazily ensure and runtime-cast the model-owned time-series registry.
 * @return Borrowed registry or null if absent/wrong runtime type; no ownership transfer. May
 *   initialize/load registry and its normal side effects through model ensure method; GUI-thread
 *   access.
 */
openswmmvis::timeseries::TimeseriesRegistry *dd_tsReg(SWMMModelLayer *l)
{
    return l ? qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(
                   l->ensureTimeseriesRegistry())
             : nullptr;
}
/**
 * @param[in] l Optional borrowed model, null yields null.
 * @brief Lazily ensure and runtime-cast the model-owned transect registry.
 * @return Borrowed registry or null if absent/wrong runtime type; no ownership transfer. May
 *   initialize/load registry and its normal side effects through model ensure method; GUI-thread
 *   access.
 */
openswmmvis::transect::TransectRegistry *dd_txReg(SWMMModelLayer *l)
{
    return l ? qobject_cast<openswmmvis::transect::TransectRegistry *>(l->ensureTransectRegistry())
             : nullptr;
}

} // namespace

bool DeleteDataObjectCommand::supports(SWMMObjectRef::ObjectType t)
{
    return t == SWMMObjectRef::Curve || t == SWMMObjectRef::TimeSeries ||
           t == SWMMObjectRef::Transect;
}

DeleteDataObjectCommand::DeleteDataObjectCommand(SWMMModelLayer *layer, const SWMMObjectRef &ref,
                                                 MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Delete \"%1\"").arg(ref.name), canvas, parent), m_layer(layer),
      m_ref(ref)
{
    // Snapshot BEFORE redo() removes anything so undo() can rebuild the object.
    switch (m_ref.objectType) {
    case SWMMObjectRef::Curve:
        snapshotCurve();
        break;
    case SWMMObjectRef::TimeSeries:
        snapshotTimeSeries();
        break;
    case SWMMObjectRef::Transect:
        snapshotTransect();
        break;
    default:
        break;
    }
}

void DeleteDataObjectCommand::redo()
{
    // remove() is engine-authoritative since perf-plan Phase A3 (it deletes
    // the engine table/transect itself), so the old full saveToEngine()
    // reflush — which rewrote EVERY remaining provider's contents per
    // delete, O(total data) — is gone.  The undo direction still reflushes
    // (restore* below): re-creating one object is the rare path and the
    // registries have no single-provider save.
    if (!m_layer)
        return;
    switch (m_ref.objectType) {
    case SWMMObjectRef::Curve: {
        auto *reg = dd_curveReg(m_layer);
        if (!reg)
            return;
        if (auto *p = reg->findByName(m_ref.name))
            reg->remove(p);
        break;
    }
    case SWMMObjectRef::TimeSeries: {
        auto *reg = dd_tsReg(m_layer);
        if (!reg)
            return;
        if (auto *p = reg->findByName(m_ref.name))
            reg->remove(p);
        break;
    }
    case SWMMObjectRef::Transect: {
        auto *reg = dd_txReg(m_layer);
        if (!reg)
            return;
        if (auto *p = reg->findByName(m_ref.name))
            reg->remove(p);
        break;
    }
    default:
        break;
    }
}

void DeleteDataObjectCommand::undo()
{
    if (!m_layer || !m_captured)
        return;
    switch (m_ref.objectType) {
    case SWMMObjectRef::Curve:
        restoreCurve();
        break;
    case SWMMObjectRef::TimeSeries:
        restoreTimeSeries();
        break;
    case SWMMObjectRef::Transect:
        restoreTransect();
        break;
    default:
        break;
    }
}

// --- Curve ------------------------------------------------------------------

void DeleteDataObjectCommand::snapshotCurve()
{
    auto *reg = dd_curveReg(m_layer);
    if (!reg)
        return;
    auto *p = reg->findByName(m_ref.name);
    if (!p)
        return;
    m_curveType = static_cast<int>(p->type());
    m_curvePoints.clear();
    for (const auto &pt : p->points())
        m_curvePoints.append(QPointF(pt.x, pt.y));
    m_captured = true;
}

void DeleteDataObjectCommand::restoreCurve()
{
    auto *reg = dd_curveReg(m_layer);
    if (!reg || m_curveType < 0 || reg->hasName(m_ref.name))
        return;
    auto *p = reg->create(m_ref.name, static_cast<openswmmvis::curve::CurveType>(m_curveType));
    if (!p)
        return;
    QVector<openswmmvis::curve::CurvePoint> pts;
    pts.reserve(m_curvePoints.size());
    for (const auto &q : m_curvePoints)
        pts.append({q.x(), q.y()});
    p->setAllPoints(pts);
    reg->saveToEngine(m_layer->engine());
}

// --- Time series ------------------------------------------------------------

void DeleteDataObjectCommand::snapshotTimeSeries()
{
    auto *reg = dd_tsReg(m_layer);
    if (!reg)
        return;
    auto *p = reg->findByName(m_ref.name);
    if (!p)
        return;
    m_tsUnits = p->unitsLabel();
    m_tsDescription = p->description();
    m_tsSourceMode = static_cast<int>(p->sourceMode());
    m_tsFilePath = p->filePath();
    m_tsColumnSelector = p->columnSelector();
    m_tsFileMTime = p->fileMTime();
    m_tsPoints.clear();
    for (const auto &pt : p->points())
        m_tsPoints.append(qMakePair(pt.time, pt.value));
    m_captured = true;
}

void DeleteDataObjectCommand::restoreTimeSeries()
{
    auto *reg = dd_tsReg(m_layer);
    if (!reg || reg->hasName(m_ref.name))
        return;
    auto *p = reg->create(m_ref.name);
    if (!p)
        return;
    using SM = openswmmvis::timeseries::TimeseriesProvider::SourceMode;
    p->setUnitsLabel(m_tsUnits);
    p->setDescription(m_tsDescription);
    p->setSourceMode(static_cast<SM>(m_tsSourceMode));
    if (!m_tsFilePath.isEmpty())
        p->setFileSource(m_tsFilePath, m_tsColumnSelector, m_tsFileMTime);
    QVector<openswmmvis::timeseries::TimeseriesPoint> pts;
    pts.reserve(m_tsPoints.size());
    for (const auto &q : m_tsPoints)
        pts.append({q.first, q.second});
    p->setAllPoints(pts);
    reg->saveToEngine(m_layer->engine());
}

// --- Transect ---------------------------------------------------------------

void DeleteDataObjectCommand::snapshotTransect()
{
    auto *reg = dd_txReg(m_layer);
    if (!reg)
        return;
    auto *p = reg->findByName(m_ref.name);
    if (!p)
        return;
    m_txComments = p->comments();
    m_txNLeft = p->nLeftBank();
    m_txNRight = p->nRightBank();
    m_txNChannel = p->nChannel();
    m_txXLeftBank = p->xLeftBank();
    m_txXRightBank = p->xRightBank();
    m_txXLeftEncroach = p->xLeftEncroachment();
    m_txXRightEncroach = p->xRightEncroachment();
    m_txXFactor = p->stationMultiplier();
    m_txYFactor = p->elevationOffset();
    m_txLengthFactor = p->meanderFactor();
    m_txPoints.clear();
    for (const auto &pt : p->points())
        m_txPoints.append(qMakePair(pt.station, pt.elevation));
    m_captured = true;
}

void DeleteDataObjectCommand::restoreTransect()
{
    auto *reg = dd_txReg(m_layer);
    if (!reg || reg->hasName(m_ref.name))
        return;
    auto *p = reg->create(m_ref.name);
    if (!p)
        return;
    p->setComments(m_txComments);
    p->setRoughness(m_txNLeft, m_txNRight, m_txNChannel);
    p->setBankStations(m_txXLeftBank, m_txXRightBank);
    p->setEncroachmentStations(m_txXLeftEncroach, m_txXRightEncroach);
    p->setModifiers(m_txXFactor, m_txYFactor, m_txLengthFactor);
    QVector<openswmmvis::transect::TransectPoint> pts;
    pts.reserve(m_txPoints.size());
    for (const auto &q : m_txPoints)
        pts.append({q.first, q.second});
    p->setAllPoints(pts);
    reg->saveToEngine(m_layer->engine());
}

// ===========================================================================
// InsertVirtualJunctionCommand
// ===========================================================================

InsertVirtualJunctionCommand::InsertVirtualJunctionCommand(SWMMModelLayer *layer, QString linkName,
                                                           double t, QString nodeName,
                                                           QString newLinkName, MapCanvas *canvas,
                                                           QUndoCommand *parent)
    : MapCommand(QObject::tr("Insert Virtual Junction \"%1\"").arg(nodeName), canvas, parent),
      m_layer(layer), m_linkName(std::move(linkName)), m_t(t), m_nodeName(std::move(nodeName)),
      m_newLinkName(std::move(newLinkName))
{
}

void InsertVirtualJunctionCommand::redo()
{
    if (!m_layer || m_present)
        return;
    if (m_layer->applyInsertVirtualJunction(m_linkName, m_t, m_nodeName, m_newLinkName))
        m_present = true;
}

void InsertVirtualJunctionCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    // Fusing the inserted virtual junction is the exact engine-side inverse
    // of the split (verified byte-identical round-trip in the engine tests).
    if (m_layer->applyFuseVirtualJunction(m_nodeName))
        m_present = false;
}

// ===========================================================================
// InsertJunctionSplitCommand
// ===========================================================================

InsertJunctionSplitCommand::InsertJunctionSplitCommand(SWMMModelLayer *layer, QString linkName,
                                                       double t, QString nodeName,
                                                       QString newLinkName, MapCanvas *canvas,
                                                       QUndoCommand *parent)
    : MapCommand(QObject::tr("Insert Junction \"%1\"").arg(nodeName), canvas, parent),
      m_layer(layer), m_linkName(std::move(linkName)), m_t(t), m_nodeName(std::move(nodeName)),
      m_newLinkName(std::move(newLinkName))
{
}

void InsertJunctionSplitCommand::redo()
{
    if (!m_layer || m_present)
        return;
    if (m_layer->applyInsertJunctionSplit(m_linkName, m_t, m_nodeName, m_newLinkName))
        m_present = true;
}

void InsertJunctionSplitCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    // Re-fusing the conduit pair is the exact engine-side inverse of the
    // split. It can legitimately fail if the junction has since acquired a
    // third link through a non-undoable edit; m_present then stays true so a
    // later redo() does not attempt a second split.
    if (m_layer->applyFuseJunctionSplit(m_nodeName))
        m_present = false;
}

// ===========================================================================
// InsertNodeSplitCommand
// ===========================================================================

namespace
{
constexpr int kNodeTypeJunction = 0; // SWMM_NODE_JUNCTION
}

InsertNodeSplitCommand::InsertNodeSplitCommand(SWMMModelLayer *layer, QString linkName, double t,
                                               QString nodeName, QString newLinkName, int nodeType,
                                               MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Insert Node \"%1\" on \"%2\"").arg(nodeName, linkName), canvas,
                 parent),
      m_layer(layer), m_linkName(std::move(linkName)), m_t(t), m_nodeName(std::move(nodeName)),
      m_newLinkName(std::move(newLinkName)), m_nodeType(nodeType)
{
}

void InsertNodeSplitCommand::redo()
{
    if (!m_layer || m_present)
        return;
    m_warnings.clear();
    m_retyped = false;
    if (!m_layer->applyInsertJunctionSplit(m_linkName, m_t, m_nodeName, m_newLinkName))
        return;
    m_present = true;

    if (m_nodeType == kNodeTypeJunction) {
        m_retyped = true; // nothing to convert — the split's junction IS the node
        return;
    }

    QStringList cleared, warnings;
    QString error;
    if (!m_layer->applyNodeConvert(m_nodeName, m_nodeType, &cleared, &warnings, &error)) {
        m_warnings << (error.isEmpty()
                           ? QObject::tr("Could not convert \"%1\" to the requested node type.")
                                 .arg(m_nodeName)
                           : error);
        return; // the split stands; the node stays a junction
    }
    m_retyped = true;
    m_warnings = warnings;

    // Creation defaults for the new type (max depth, functional storage, …),
    // exactly what a free placement gets from AddNodeCommand.
    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (idx >= 0)
        ObjectDefaultsApplier::applyNodeDefaults(eng, idx, m_nodeType);
}

void InsertNodeSplitCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    // The fuse inverse borrows the virtual-junction path, which only accepts
    // a JUNCTION — demote first (a no-op for a junction / a refused convert).
    if (m_retyped && m_nodeType != kNodeTypeJunction) {
        if (!m_layer->applyNodeConvert(m_nodeName, kNodeTypeJunction))
            return; // leave m_present so a later redo() does not re-split
        m_retyped = false;
    }
    if (m_layer->applyFuseJunctionSplit(m_nodeName))
        m_present = false;
}

// ===========================================================================
// ConvertNodeTypeCommand
// ===========================================================================

ConvertNodeTypeCommand::ConvertNodeTypeCommand(SWMMModelLayer *layer, QString nodeName,
                                               int newNodeType, GeneratedOutfallSpec spec,
                                               MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Convert Node \"%1\"").arg(nodeName), canvas, parent), m_layer(layer),
      m_nodeName(std::move(nodeName)), m_newType(newNodeType), m_spec(std::move(spec))
{
    // Snapshot in the constructor, before anything runs: undo has to put back
    // the type the node had and the common properties the spec overwrites.
    if (!m_layer)
        return;
    SWMM_Engine eng = m_layer->engine();
    if (!eng)
        return;
    const int idx = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (idx < 0)
        return;

    swmm_node_get_type(eng, idx, &m_oldType);
    swmm_node_get_invert_elev(eng, idx, &m_oldInvert);
    swmm_node_get_max_depth(eng, idx, &m_oldMaxDepth);
    swmm_node_get_storage_area(eng, idx, &m_oldStorageArea);

    char buf[256] = {0};
    if (swmm_node_get_tag(eng, idx, buf, int(sizeof(buf))) == SWMM_OK)
        m_oldTag = QString::fromUtf8(buf);

    m_haveSnapshot = true;
}

void ConvertNodeTypeCommand::applySpec()
{
    if (!m_spec.applyOutfall || !m_layer)
        return;
    SWMM_Engine eng = m_layer->engine();
    if (!eng)
        return;
    const int idx = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (idx < 0)
        return;

    if (m_spec.hasInvert)
        swmm_node_set_invert_elev(eng, idx, m_spec.invert);
    if (m_spec.hasMaxDepth)
        swmm_node_set_max_depth(eng, idx, m_spec.maxDepth);
    swmm_node_set_outfall_type(eng, idx, m_spec.outfallType);
    swmm_node_set_outfall_flap_gate(eng, idx, m_spec.flapGate ? 1 : 0);
    if (!m_spec.tag.isEmpty())
        swmm_node_set_tag(eng, idx, m_spec.tag.toUtf8().constData());
}

void ConvertNodeTypeCommand::redo()
{
    if (!m_layer || !m_haveSnapshot || m_converted)
        return;
    m_warnings.clear();

    if (m_newType == m_oldType) {
        // Nothing to convert, but the spec may still be the point of the call.
        m_converted = true;
        applySpec();
        return;
    }

    QStringList cleared, warnings;
    QString error;
    if (!m_layer->applyNodeConvert(m_nodeName, m_newType, &cleared, &warnings, &error)) {
        m_warnings << (error.isEmpty() ? QObject::tr("Could not convert \"%1\".").arg(m_nodeName)
                                       : error);
        return;
    }
    m_converted = true;
    m_warnings = warnings;
    if (isLossy() && !cleared.isEmpty())
        m_warnings << QObject::tr("Converting \"%1\" cleared %2, which undo cannot restore.")
                          .arg(m_nodeName, cleared.join(QStringLiteral(", ")));

    applySpec();
}

void ConvertNodeTypeCommand::undo()
{
    if (!m_layer || !m_converted)
        return;

    if (m_newType != m_oldType && !m_layer->applyNodeConvert(m_nodeName, m_oldType))
        return; // stay "converted" so a later redo() does not convert twice

    SWMM_Engine eng = m_layer->engine();
    const int idx = eng ? swmm_node_index(eng, m_nodeName.toUtf8().constData()) : -1;
    if (idx >= 0) {
        swmm_node_set_invert_elev(eng, idx, m_oldInvert);
        swmm_node_set_max_depth(eng, idx, m_oldMaxDepth);
        if (m_oldType == kNodeTypeJunction)
            swmm_node_set_storage_area(eng, idx, m_oldStorageArea);
        swmm_node_set_tag(eng, idx, m_oldTag.toUtf8().constData());
    }
    m_converted = false;
}

// ===========================================================================
// FuseVirtualJunctionCommand
// ===========================================================================

FuseVirtualJunctionCommand::FuseVirtualJunctionCommand(SWMMModelLayer *layer, QString nodeName,
                                                       MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Fuse Virtual Junction \"%1\"").arg(nodeName), canvas, parent),
      m_layer(layer), m_nodeName(std::move(nodeName))
{
    // Snapshot everything a re-split cannot derive: conduit names, the
    // length-ratio split position, the grade-break invert and the map
    // coordinate.
    if (!m_layer)
        return;
    SWMM_Engine eng = m_layer->engine();
    if (!eng)
        return;
    const int ni = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (ni < 0)
        return;

    int up = -1, dn = -1;
    const int nLinks = swmm_link_count(eng);
    for (int i = 0; i < nLinks; ++i) {
        int n1 = -1, n2 = -1;
        swmm_link_get_from_node(eng, i, &n1);
        swmm_link_get_to_node(eng, i, &n2);
        if (n2 == ni)
            up = i;
        if (n1 == ni)
            dn = i;
    }
    if (up < 0 || dn < 0 || up == dn)
        return;

    double lu = 0.0, ld = 0.0;
    swmm_link_get_length(eng, up, &lu);
    swmm_link_get_length(eng, dn, &ld);
    if (lu + ld <= 0.0)
        return;

    m_upLinkName = QString::fromUtf8(swmm_link_id(eng, up));
    m_dnLinkName = QString::fromUtf8(swmm_link_id(eng, dn));
    m_t = lu / (lu + ld);
    swmm_node_get_invert_elev(eng, ni, &m_invert);
    swmm_spatial_get_node_coord(eng, ni, &m_x, &m_y);
    m_valid = true;
}

void FuseVirtualJunctionCommand::redo()
{
    if (!m_layer || !m_valid || m_present)
        return;
    if (m_layer->applyFuseVirtualJunction(m_nodeName))
        m_present = true;
}

void FuseVirtualJunctionCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    if (!m_layer->applyInsertVirtualJunction(m_upLinkName, m_t, m_nodeName, m_dnLinkName))
        return;
    m_present = false;

    // Restore the exact grade-break invert and map coordinate — the split
    // interpolates both along the (now single-gradient) merged conduit, so
    // an original slope-break node needs the snapshot values back.
    SWMM_Engine eng = m_layer->engine();
    if (!eng)
        return;
    const int ni = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (ni < 0)
        return;
    swmm_node_set_invert_elev(eng, ni, m_invert);
    m_layer->applyNodeMove(ni, m_x, m_y);
}

// ===========================================================================
// InsertInletJunctionCommand
// ===========================================================================

InsertInletJunctionCommand::InsertInletJunctionCommand(SWMMModelLayer *layer, QString linkName,
                                                       double t, QString nodeName,
                                                       QString newLinkName, QString inletId,
                                                       QString captureNode, MapCanvas *canvas,
                                                       QUndoCommand *parent)
    : MapCommand(QObject::tr("Insert Inlet Junction \"%1\"").arg(nodeName), canvas, parent),
      m_layer(layer), m_linkName(std::move(linkName)), m_t(t), m_nodeName(std::move(nodeName)),
      m_newLinkName(std::move(newLinkName)), m_inletId(std::move(inletId)),
      m_captureNode(std::move(captureNode))
{
}

void InsertInletJunctionCommand::redo()
{
    if (!m_layer || m_present)
        return;
    if (m_layer->applyInsertInletJunction(m_linkName, m_t, m_nodeName, m_newLinkName, m_inletId,
                                          m_captureNode))
        m_present = true;
}

void InsertInletJunctionCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    // swmm_inlet_junction_fuse is the exact inverse of the split: it drops
    // the usage row the split created, then fuses the pair back.
    if (m_layer->applyFuseInletJunction(m_nodeName))
        m_present = false;
}

// ===========================================================================
// FuseInletJunctionCommand
// ===========================================================================

FuseInletJunctionCommand::FuseInletJunctionCommand(SWMMModelLayer *layer, QString nodeName,
                                                   MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Fuse Inlet Junction \"%1\"").arg(nodeName), canvas, parent),
      m_layer(layer), m_nodeName(std::move(nodeName))
{
    // Same snapshot as FuseVirtualJunctionCommand plus the usage row, which
    // the fuse deletes and the re-split cannot reconstruct beyond its
    // defaults (1 inlet, 0 % clogged, no limit, AUTOMATIC placement).
    if (!m_layer)
        return;
    SWMM_Engine eng = m_layer->engine();
    if (!eng)
        return;
    const int ni = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (ni < 0)
        return;

    int up = -1, dn = -1;
    const int nLinks = swmm_link_count(eng);
    for (int i = 0; i < nLinks; ++i) {
        int n1 = -1, n2 = -1;
        swmm_link_get_from_node(eng, i, &n1);
        swmm_link_get_to_node(eng, i, &n2);
        if (n2 == ni)
            up = i;
        if (n1 == ni)
            dn = i;
    }
    if (up < 0 || dn < 0 || up == dn)
        return;

    double lu = 0.0, ld = 0.0;
    swmm_link_get_length(eng, up, &lu);
    swmm_link_get_length(eng, dn, &ld);
    if (lu + ld <= 0.0)
        return;

    if (!m_layer->inletUsageFor(SWMM_INLET_HOST_NODE, ni, &m_usage))
        return;
    if (const char *d = swmm_inlet_id(eng, m_usage.design_idx))
        m_inletId = QString::fromUtf8(d);
    if (const char *c = swmm_node_id(eng, m_usage.capture_node_idx))
        m_captureNode = QString::fromUtf8(c);
    if (m_inletId.isEmpty() || m_captureNode.isEmpty())
        return;

    m_upLinkName = QString::fromUtf8(swmm_link_id(eng, up));
    m_dnLinkName = QString::fromUtf8(swmm_link_id(eng, dn));
    m_t = lu / (lu + ld);
    swmm_node_get_invert_elev(eng, ni, &m_invert);
    swmm_spatial_get_node_coord(eng, ni, &m_x, &m_y);
    m_valid = true;
}

void FuseInletJunctionCommand::redo()
{
    if (!m_layer || !m_valid || m_present)
        return;
    if (m_layer->applyFuseInletJunction(m_nodeName))
        m_present = true;
}

void FuseInletJunctionCommand::undo()
{
    if (!m_layer || !m_present)
        return;
    if (!m_layer->applyInsertInletJunction(m_upLinkName, m_t, m_nodeName, m_dnLinkName, m_inletId,
                                           m_captureNode))
        return;
    m_present = false;

    SWMM_Engine eng = m_layer->engine();
    if (!eng)
        return;
    const int ni = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (ni < 0)
        return;
    // Restore the grade-break invert and map coordinate (the split
    // interpolates both), then the full usage row over the split's defaults.
    swmm_node_set_invert_elev(eng, ni, m_invert);
    m_layer->applyNodeMove(ni, m_x, m_y);

    SWMM_InletUsage u = m_usage;
    u.host_idx = ni; // indices may have shifted
    u.capture_node_idx = swmm_node_index(eng, m_captureNode.toUtf8().constData());
    u.design_idx = swmm_inlet_index(eng, m_inletId.toUtf8().constData());
    if (u.capture_node_idx >= 0 && u.design_idx >= 0)
        m_layer->applySetInletUsage(u);
}
