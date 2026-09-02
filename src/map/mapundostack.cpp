/*!
 * \file   mapundostack.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
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

#include <QSettings>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_edit.h>

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
// FlipLinkCommand
// ===========================================================================

FlipLinkCommand::FlipLinkCommand(SWMMModelLayer *layer,
                                 int linkIdx,
                                 MapCanvas *canvas,
                                 QUndoCommand *parent)
    : MapCommand(QObject::tr("Flip Link Direction"), canvas, parent),
      m_layer(layer),
      m_linkIdx(linkIdx)
{
}

// applyLinkFlip is self-inverse — flipping twice restores the original
// endpoints, vertex order, offsets and loss coefficients — so both directions
// are the same call.
void FlipLinkCommand::redo()
{
    if (m_layer) m_layer->applyLinkFlip(m_linkIdx);
}

void FlipLinkCommand::undo()
{
    if (m_layer) m_layer->applyLinkFlip(m_linkIdx);
}

// ===========================================================================
// EditSubcatchCommand
// ===========================================================================

EditSubcatchCommand::EditSubcatchCommand(SWMMModelLayer *layer,
                                         int catchIdx,
                                         QVector<QPointF> oldVertices,
                                         QVector<QPointF> newVertices,
                                         double oldArea,
                                         double newArea,
                                         bool applyArea,
                                         MapCanvas *canvas,
                                         QUndoCommand *parent)
    : MapCommand(QObject::tr("Edit Subcatchment Vertices"), canvas, parent),
      m_layer(layer),
      m_catchIdx(catchIdx),
      m_old(std::move(oldVertices)),
      m_new(std::move(newVertices)),
      m_oldArea(oldArea),
      m_newArea(newArea),
      m_applyArea(applyArea)
{
}

void EditSubcatchCommand::redo()
{
    if (!m_layer) return;
    m_layer->applySubcatchVertices(m_catchIdx, m_new);
    if (m_applyArea) m_layer->applySubcatchArea(m_catchIdx, m_newArea);
}

void EditSubcatchCommand::undo()
{
    if (!m_layer) return;
    m_layer->applySubcatchVertices(m_catchIdx, m_old);
    if (m_applyArea) m_layer->applySubcatchArea(m_catchIdx, m_oldArea);
}

// ===========================================================================
// AddNodeCommand
// ===========================================================================

AddNodeCommand::AddNodeCommand(SWMMModelLayer *layer,
                               QString name,
                               int nodeType,
                               double x, double y,
                               MapCanvas *canvas,
                               double invertElev,
                               QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Node \"%1\"").arg(name), canvas, parent),
      m_layer(layer),
      m_name(std::move(name)),
      m_nodeType(nodeType),
      m_x(x), m_y(y),
      m_invertElev(invertElev)
{
}

void AddNodeCommand::redo()
{
    if (!m_layer || m_present) return;
    if (!m_layer->applyNodeAdd(m_name, m_nodeType, m_x, m_y)) return;
    m_present = true;

    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_node_index(eng, m_name.toUtf8().constData());

    // Creation defaults first; the more specific terrain-derived invert
    // below wins (see workplans/OBJECT_CREATION_DEFAULTS_PLAN_2026-08-03.md).
    ObjectDefaultsApplier::applyNodeDefaults(eng, idx, m_nodeType);

    if (m_invertElev != 0.0 && idx >= 0)
        swmm_node_set_invert_elev(eng, idx, m_invertElev);
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

// ===========================================================================
// ReorderLayersCommand (Slice AX)
// ===========================================================================

ReorderLayersCommand::ReorderLayersCommand(QList<OpenSWMMVisLayer *> oldOrder,
                                           QList<OpenSWMMVisLayer *> newOrder,
                                           MapCanvas *canvas,
                                           QUndoCommand *parent)
    : MapCommand(QObject::tr("Reorder Layers"), canvas, parent)
    , m_oldOrder(std::move(oldOrder))
    , m_newOrder(std::move(newOrder))
{}

void ReorderLayersCommand::undo()
{
    if (m_canvas)
        m_canvas->reorderLayers(m_oldOrder, /*pushUndo=*/false);
}

void ReorderLayersCommand::redo()
{
    if (m_firstRedo) { m_firstRedo = false; return; }  // already applied by caller
    if (m_canvas)
        m_canvas->reorderLayers(m_newOrder, /*pushUndo=*/false);
}

// ===========================================================================
// ReorderCategoriesCommand (Slice AY)
// ===========================================================================

ReorderCategoriesCommand::ReorderCategoriesCommand(
    SWMMModelLayer *layer,
    QVector<SWMMModelLayer::Category> oldOrder,
    QVector<SWMMModelLayer::Category> newOrder,
    QUndoCommand *parent)
    : QUndoCommand(QObject::tr("Reorder Categories"), parent)
    , m_layer(layer)
    , m_oldOrder(std::move(oldOrder))
    , m_newOrder(std::move(newOrder))
{}

void ReorderCategoriesCommand::undo()
{
    m_layer->setCategoryOrder(m_oldOrder);
}

void ReorderCategoriesCommand::redo()
{
    if (m_firstRedo) { m_firstRedo = false; return; }  // already applied by caller
    m_layer->setCategoryOrder(m_newOrder);
}

// ===========================================================================
// ReorderObjectsCommand (Slice AY)
// ===========================================================================

ReorderObjectsCommand::ReorderObjectsCommand(
    SWMMModelLayer         *layer,
    SWMMModelLayer::Category cat,
    QVector<int>             oldOrder,
    QVector<int>             newOrder,
    QUndoCommand            *parent)
    : QUndoCommand(QObject::tr("Reorder Objects"), parent)
    , m_layer(layer)
    , m_cat(cat)
    , m_oldOrder(std::move(oldOrder))
    , m_newOrder(std::move(newOrder))
{}

void ReorderObjectsCommand::undo()
{
    if (m_oldOrder.isEmpty())
        m_layer->clearObjectOrder(m_cat);
    else
        m_layer->setObjectOrder(m_cat, m_oldOrder);
}

void ReorderObjectsCommand::redo()
{
    if (m_firstRedo) { m_firstRedo = false; return; }  // already applied by caller
    m_layer->setObjectOrder(m_cat, m_newOrder);
}

// ===========================================================================
// AddLinkCommand (Slice AE)
// ===========================================================================

AddLinkCommand::AddLinkCommand(SWMMModelLayer   *layer,
                                QString           name,
                                int               linkType,
                                QString           fromNode,
                                QString           toNode,
                                QVector<QPointF>  interiorVertices,
                                MapCanvas        *canvas,
                                double            offsetUp,
                                double            offsetDn,
                                QUndoCommand     *parent)
    : MapCommand(QObject::tr("Add Link"), canvas, parent)
    , m_layer(layer)
    , m_name(std::move(name))
    , m_linkType(linkType)
    , m_fromNode(std::move(fromNode))
    , m_toNode(std::move(toNode))
    , m_interiorVertices(std::move(interiorVertices))
    , m_offsetUp(offsetUp)
    , m_offsetDn(offsetDn)
{}

void AddLinkCommand::redo()
{
    m_linkIdx = -1;
    m_present = m_layer->applyLinkAdd(m_name, m_linkType,
                                       m_fromNode, m_toNode,
                                       m_interiorVertices, &m_linkIdx);

    // Creation defaults first; auto-length / terrain offsets below win.
    // Length default is suppressed when auto-length will compute it.
    if (m_present && m_linkIdx >= 0) {
        const bool autoLen =
            m_canvas && m_canvas->property("autoLength").toBool();
        ObjectDefaultsApplier::applyLinkDefaults(m_layer->engine(), m_linkIdx,
                                                 m_linkType, autoLen);
    }

    // Auto-length: set GIS polyline length when the canvas flag is active.
    if (m_present && m_linkIdx >= 0
            && m_canvas && m_canvas->property("autoLength").toBool()) {
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
        if (m_offsetUp != 0.0) swmm_link_set_offset_up(eng, m_linkIdx, m_offsetUp);
        if (m_offsetDn != 0.0) swmm_link_set_offset_dn(eng, m_linkIdx, m_offsetDn);
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

AddGageCommand::AddGageCommand(SWMMModelLayer *layer,
                                QString         name,
                                double          x, double y,
                                MapCanvas      *canvas,
                                QUndoCommand   *parent)
    : MapCommand(QObject::tr("Add Rain Gage"), canvas, parent)
    , m_layer(layer)
    , m_name(std::move(name))
    , m_x(x), m_y(y)
{}

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

AddSubcatchmentCommand::AddSubcatchmentCommand(SWMMModelLayer   *layer,
                                                QString           name,
                                                QVector<QPointF>  polygon,
                                                MapCanvas        *canvas,
                                                QUndoCommand     *parent)
    : MapCommand(QObject::tr("Add Subcatchment"), canvas, parent)
    , m_layer(layer)
    , m_name(std::move(name))
    , m_polygon(std::move(polygon))
{}

void AddSubcatchmentCommand::redo()
{
    m_subcatchIdx = -1;
    m_present = m_layer->applySubcatchAdd(m_name, m_polygon, &m_subcatchIdx);

    // Creation defaults first; auto-area below wins. Area default is
    // suppressed when auto-area will compute it.
    if (m_present && m_subcatchIdx >= 0) {
        const bool autoArea =
            m_canvas && m_canvas->property("autoLength").toBool();
        ObjectDefaultsApplier::applySubcatchDefaults(m_layer->engine(),
                                                     m_subcatchIdx, autoArea);
    }

    // Auto-area: set GIS polygon area when the canvas flag is active.
    if (m_present && m_subcatchIdx >= 0
            && m_canvas && m_canvas->property("autoLength").toBool()) {
        const double sqUnits = EditGeometry::polygonArea(m_polygon);
        if (sqUnits > 0.0) {
            // Convert square map units → SWMM area unit:
            //   SI  (CMS/LPS/MLD): metres → hectares  (÷ 10 000)
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
                                                       QStringList     subcatchNames,
                                                       QStringList     newGages,
                                                       QStringList     oldGages,
                                                       const QString  &text,
                                                       MapCanvas      *canvas,
                                                       QUndoCommand   *parent)
    : MapCommand(text, canvas, parent)
    , m_layer(layer)
    , m_subcatchNames(std::move(subcatchNames))
    , m_newGages(std::move(newGages))
    , m_oldGages(std::move(oldGages))
{}

void AssignSubcatchGagesCommand::apply(const QStringList &gages)
{
    if (!m_layer || !m_layer->engine())
        return;
    SWMM_Engine eng = m_layer->engine();

    for (int i = 0; i < m_subcatchNames.size() && i < gages.size(); ++i)
    {
        const QString &gage = gages[i];
        if (gage.isEmpty())
            continue;   // no prior gage to restore; SWMM cannot express "none"

        // Resolve afresh every time: an intervening delete may have re-packed
        // indices since this command was built.
        const int sIdx =
            swmm_subcatch_index(eng, m_subcatchNames[i].toUtf8().constData());
        if (sIdx < 0)
            continue;   // subcatchment gone (e.g. an undone add) — clean no-op
        m_layer->applySubcatchSetGage(sIdx, gage);
    }

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene,
                             QStringLiteral("assign-subcatch-gages"));
}

void AssignSubcatchGagesCommand::redo() { apply(m_newGages); }
void AssignSubcatchGagesCommand::undo() { apply(m_oldGages); }

// ===========================================================================
// ConfigureGageCommand
// ===========================================================================

ConfigureGageCommand::Config
ConfigureGageCommand::capture(SWMMModelLayer *layer, const QString &gageName, bool *ok)
{
    Config c;
    if (ok) *ok = false;
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

    if (ok) *ok = true;
    return c;
}

ConfigureGageCommand::ConfigureGageCommand(SWMMModelLayer *layer,
                                           QString         gageName,
                                           Config          newConfig,
                                           Config          oldConfig,
                                           MapCanvas      *canvas,
                                           QUndoCommand   *parent)
    : MapCommand(QObject::tr("Configure Rain Gage"), canvas, parent)
    , m_layer(layer)
    , m_gageName(std::move(gageName))
    , m_new(std::move(newConfig))
    , m_old(std::move(oldConfig))
{}

void ConfigureGageCommand::apply(const Config &c)
{
    if (!m_layer || !m_layer->engine())
        return;
    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_gage_index(eng, m_gageName.toUtf8().constData());
    if (idx < 0)
        return;   // gage vanished (e.g. an undone add) — clean no-op

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

void ConfigureGageCommand::redo() { apply(m_new); }
void ConfigureGageCommand::undo() { apply(m_old); }

// ===========================================================================
// DeleteObjectCommand (Slice F-2) — helpers
// ===========================================================================

namespace {

// Fetch interior-only vertices from the engine for a link.
static QVector<QPointF> fetchLinkInteriorVertices(SWMM_Engine engine, int linkIdx)
{
    int vcount = 0;
    if (swmm_spatial_get_link_vertex_count(engine, linkIdx, &vcount) != 0 || vcount <= 2)
        return {};
    QVector<double> vx(vcount), vy(vcount);
    swmm_spatial_get_link_vertices(engine, linkIdx, vx.data(), vy.data(), vcount);
    QVector<QPointF> interior;
    interior.reserve(vcount - 2);
    for (int i = 1; i < vcount - 1; ++i)
        interior << QPointF(vx[i], vy[i]);
    return interior;
}

static LinkSnapshot snapshotLinkByIdx(SWMM_Engine engine, int idx)
{
    LinkSnapshot s;
    const char *id = swmm_link_id(engine, idx);
    s.name = id ? QString::fromUtf8(id) : QString();

    int t = 0; swmm_link_get_type(engine, idx, &t); s.linkType = t;

    int n1 = -1, n2 = -1;
    swmm_link_get_from_node(engine, idx, &n1);
    swmm_link_get_to_node(engine, idx, &n2);
    const char *fn = (n1 >= 0) ? swmm_node_id(engine, n1) : nullptr;
    const char *tn = (n2 >= 0) ? swmm_node_id(engine, n2) : nullptr;
    s.fromNode = fn ? QString::fromUtf8(fn) : QString();
    s.toNode   = tn ? QString::fromUtf8(tn) : QString();

    s.interiorVertices = fetchLinkInteriorVertices(engine, idx);

    swmm_link_get_length(engine,          idx, &s.length);
    swmm_link_get_roughness(engine,       idx, &s.roughness);
    swmm_link_get_offset_up(engine,       idx, &s.offsetUp);
    swmm_link_get_offset_dn(engine,       idx, &s.offsetDn);
    swmm_link_get_crest_height(engine,    idx, &s.crestHeight);
    swmm_link_get_discharge_coeff(engine, idx, &s.dischargeCoeff);
    swmm_link_get_end_contractions(engine,idx, &s.endContractions);
    swmm_link_get_flap_gate(engine,       idx, &s.flapGate);
    swmm_link_get_pump_init_state(engine, idx, &s.pumpInitState);
    return s;
}

} // namespace

// ===========================================================================
// DeleteObjectCommand — constructor + redo / undo
// ===========================================================================

DeleteObjectCommand::DeleteObjectCommand(SWMMModelLayer *layer,
                                          const QString  &name,
                                          TargetKind      kind,
                                          MapCanvas      *canvas,
                                          QUndoCommand   *parent)
    : MapCommand(QObject::tr("Delete Object"), canvas, parent)
    , m_layer(layer)
    , m_kind(kind)
{
    // Snapshot BEFORE redo() deletes anything.
    switch (kind) {
    case DeleteNode:    snapshotNode(name);    break;
    case DeleteLink:    snapshotLink(name);    break;
    case DeleteGage:    snapshotGage(name);    break;
    case DeleteSubcatch:snapshotSubcatch(name);break;
    }
}

void DeleteObjectCommand::snapshotNode(const QString &name)
{
    SWMM_Engine eng = m_layer->engine();
    const QByteArray utf8 = name.toUtf8();
    const int idx = swmm_node_index(eng, utf8.constData());
    if (idx < 0) return;

    m_node.name = name;
    int t = 0; swmm_node_get_type(eng, idx, &t); m_node.nodeType = t;
    double nx = 0, ny = 0;
    swmm_spatial_get_node_coord(eng, idx, &nx, &ny);
    m_node.x = nx; m_node.y = ny;
    swmm_node_get_invert_elev(eng,     idx, &m_node.invertElev);
    swmm_node_get_max_depth(eng,       idx, &m_node.maxDepth);
    swmm_node_get_initial_depth(eng,   idx, &m_node.initDepth);
    swmm_node_get_surcharge_depth(eng, idx, &m_node.surchargeDepth);
    swmm_node_get_ponded_area(eng,     idx, &m_node.pondedArea);
    swmm_node_is_virtual(eng,          idx, &m_node.isVirtual);
    swmm_node_get_rim_depth(eng,       idx, &m_node.rimDepth);
    swmm_node_get_outfall_type(eng,    idx, &m_node.outfallType);
    swmm_node_get_outfall_flap_gate(eng, idx, &m_node.outfallFlapGate);
    swmm_node_get_storage_seep_rate(eng, idx, &m_node.seepRate);
    swmm_node_get_divider_type(eng,    idx, &m_node.dividerType);

    // Snapshot cascade links. This runs BEFORE the delete, so use the
    // read-only analyzer rather than scanning all L links with two getters
    // each — the scan cost 2*L engine calls per command constructed, which
    // for a selection of K nodes is O(K*L) before anything is even deleted.
    SWMM_ImpactReport report{};
    if (swmm_node_analyze_impact(eng, idx, &report) == 0) {
        for (int i = 0; i < report.n_entries; ++i) {
            const SWMM_ImpactEntry &e = report.entries[i];
            if (e.obj_type == SWMM_REF_LINK && e.cascaded)
                m_cascadeLinks << snapshotLinkByIdx(eng, e.obj_idx);
        }
    }
    swmm_impact_report_free(&report);
}

void DeleteObjectCommand::snapshotLink(const QString &name)
{
    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_link_index(eng, name.toUtf8().constData());
    if (idx < 0) return;
    m_link = snapshotLinkByIdx(eng, idx);
}

void DeleteObjectCommand::snapshotGage(const QString &name)
{
    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_gage_index(eng, name.toUtf8().constData());
    if (idx < 0) return;
    m_gage.name = name;
    swmm_spatial_get_gage_coord(eng, idx, &m_gage.x, &m_gage.y);
}

void DeleteObjectCommand::snapshotSubcatch(const QString &name)
{
    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_subcatch_index(eng, name.toUtf8().constData());
    if (idx < 0) return;
    m_subcatch.name = name;
    int pcount = 0;
    swmm_spatial_get_subcatch_polygon_count(eng, idx, &pcount);
    if (pcount > 0) {
        QVector<double> vx(pcount), vy(pcount);
        swmm_spatial_get_subcatch_polygon(eng, idx, vx.data(), vy.data(), pcount);
        m_subcatch.polygon.reserve(pcount);
        for (int i = 0; i < pcount; ++i) m_subcatch.polygon << QPointF(vx[i], vy[i]);
    }
    swmm_subcatch_get_area(eng,       idx, &m_subcatch.area);
    swmm_subcatch_get_width(eng,      idx, &m_subcatch.width);
    swmm_subcatch_get_slope(eng,      idx, &m_subcatch.slope);
    swmm_subcatch_get_imperv_pct(eng, idx, &m_subcatch.impervPct);
}

void DeleteObjectCommand::redo()
{
    switch (m_kind) {
    case DeleteNode:    m_layer->applyNodeDelete(m_node.name);       break;
    case DeleteLink:    m_layer->applyLinkDelete(m_link.name);       break;
    case DeleteGage:    m_layer->applyGageDelete(m_gage.name);       break;
    case DeleteSubcatch:m_layer->applySubcatchDelete(m_subcatch.name);break;
    }
}

void DeleteObjectCommand::undo()
{
    switch (m_kind) {
    case DeleteNode:    restoreNode();    break;
    case DeleteLink:    restoreLink();    break;
    case DeleteGage:    restoreGage();    break;
    case DeleteSubcatch:restoreSubcatch();break;
    }
}

void DeleteObjectCommand::restoreNode()
{
    // Re-add node; it will be at the new tail index.
    m_layer->applyNodeAdd(m_node.name, m_node.nodeType, m_node.x, m_node.y);

    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_node_index(eng, m_node.name.toUtf8().constData());
    if (idx >= 0) {
        swmm_node_set_invert_elev(eng,     idx, m_node.invertElev);
        swmm_node_set_max_depth(eng,       idx, m_node.maxDepth);
        swmm_node_set_initial_depth(eng,   idx, m_node.initDepth);
        swmm_node_set_surcharge_depth(eng, idx, m_node.surchargeDepth);
        swmm_node_set_pond_area(eng,       idx, m_node.pondedArea);
        if (m_node.nodeType == 1) { // Outfall
            swmm_node_set_outfall_type(eng,     idx, m_node.outfallType);
            swmm_node_set_outfall_flap_gate(eng,idx, m_node.outfallFlapGate);
        }
        if (m_node.nodeType == 2) // Storage
            swmm_node_set_storage_seep_rate(eng, idx, m_node.seepRate);
        if (m_node.nodeType == 3) // Divider
            swmm_node_set_divider_type(eng, idx, m_node.dividerType);
    }

    // Re-add cascade links; re-wire by name.
    for (const LinkSnapshot &ls : m_cascadeLinks) {
        m_layer->applyLinkAdd(ls.name, ls.linkType, ls.fromNode, ls.toNode,
                               ls.interiorVertices);
        const int li = swmm_link_index(eng, ls.name.toUtf8().constData());
        if (li < 0) continue;
        swmm_link_set_length(eng,           li, ls.length);
        swmm_link_set_roughness(eng,        li, ls.roughness);
        swmm_link_set_offset_up(eng,        li, ls.offsetUp);
        swmm_link_set_offset_dn(eng,        li, ls.offsetDn);
        swmm_link_set_flap_gate(eng,        li, ls.flapGate);
        swmm_link_set_crest_height(eng,     li, ls.crestHeight);
        swmm_link_set_discharge_coeff(eng,  li, ls.dischargeCoeff);
        swmm_link_set_end_contractions(eng, li, ls.endContractions);
        swmm_link_set_pump_init_state(eng,  li, ls.pumpInitState);
    }

    // Virtual flag LAST: the rule check needs the two conduits back, so it
    // can only pass once the cascade links above are re-wired. Restoring the
    // rim depth first keeps the ground line the node was drawn with — the
    // engine's set-virtual would otherwise carry the max depth into it.
    if (m_node.isVirtual) {
        const int idx = swmm_node_index(eng, m_node.name.toUtf8().constData());
        if (idx >= 0) {
            swmm_node_set_rim_depth(eng, idx, m_node.rimDepth);
            m_layer->applySetVirtual(m_node.name, true, nullptr);
        }
    }
}

void DeleteObjectCommand::restoreLink()
{
    m_layer->applyLinkAdd(m_link.name, m_link.linkType,
                           m_link.fromNode, m_link.toNode,
                           m_link.interiorVertices);
    SWMM_Engine eng = m_layer->engine();
    const int li = swmm_link_index(eng, m_link.name.toUtf8().constData());
    if (li < 0) return;
    swmm_link_set_length(eng,           li, m_link.length);
    swmm_link_set_roughness(eng,        li, m_link.roughness);
    swmm_link_set_offset_up(eng,        li, m_link.offsetUp);
    swmm_link_set_offset_dn(eng,        li, m_link.offsetDn);
    swmm_link_set_flap_gate(eng,        li, m_link.flapGate);
    swmm_link_set_crest_height(eng,     li, m_link.crestHeight);
    swmm_link_set_discharge_coeff(eng,  li, m_link.dischargeCoeff);
    swmm_link_set_end_contractions(eng, li, m_link.endContractions);
    swmm_link_set_pump_init_state(eng,  li, m_link.pumpInitState);
}

void DeleteObjectCommand::restoreGage()
{
    m_layer->applyGageAdd(m_gage.name, m_gage.x, m_gage.y);
}

void DeleteObjectCommand::restoreSubcatch()
{
    m_layer->applySubcatchAdd(m_subcatch.name, m_subcatch.polygon);
    SWMM_Engine eng = m_layer->engine();
    const int idx = swmm_subcatch_index(eng, m_subcatch.name.toUtf8().constData());
    if (idx < 0) return;
    swmm_subcatch_set_area(eng,       idx, m_subcatch.area);
    swmm_subcatch_set_width(eng,      idx, m_subcatch.width);
    swmm_subcatch_set_slope(eng,      idx, m_subcatch.slope);
    swmm_subcatch_set_imperv_pct(eng, idx, m_subcatch.impervPct);
}

// ===========================================================================
// AddAnnotationCommand
// ===========================================================================

AddAnnotationCommand::AddAnnotationCommand(OpenSWMMVisAnnotationLayer *layer,
                                           AnnotationTextItem *item,
                                           MapCanvas *canvas,
                                           QUndoCommand *parent)
    : MapCommand(QObject::tr("Add Text Annotation"), canvas, parent)
    , m_layer(layer)
    , m_item(item)
    , m_itemId(item ? item->id() : QString())
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
    if (!m_layer || m_present || !m_item) return;
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
    if (!m_layer || !m_present) return;
    m_item = m_layer->takeAnnotation(m_itemId);
    if (m_item) {
        m_present = false;
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Scene, QStringLiteral("addtext-undo"));
    }
}

// ===========================================================================
// DeleteDataObjectCommand  (Slice — GUI delete for data objects, 2026-07-22)
// ---------------------------------------------------------------------------
// Curve / TimeSeries / Transect deletion routed through the owning registry.
// See workplans/GUI_DELETE_ALL_OBJECTS_PLAN_2026-07-22.md for the roadmap that
// extends supports()/redo()/undo() to the remaining data types once the engine
// ships their swmm_*_delete APIs.
// ===========================================================================

namespace {

openswmmvis::curve::CurveRegistry *dd_curveReg(SWMMModelLayer *l)
{
    return l ? qobject_cast<openswmmvis::curve::CurveRegistry *>(l->ensureCurveRegistry())
             : nullptr;
}
openswmmvis::timeseries::TimeseriesRegistry *dd_tsReg(SWMMModelLayer *l)
{
    return l ? qobject_cast<openswmmvis::timeseries::TimeseriesRegistry *>(l->ensureTimeseriesRegistry())
             : nullptr;
}
openswmmvis::transect::TransectRegistry *dd_txReg(SWMMModelLayer *l)
{
    return l ? qobject_cast<openswmmvis::transect::TransectRegistry *>(l->ensureTransectRegistry())
             : nullptr;
}

} // namespace

bool DeleteDataObjectCommand::supports(SWMMObjectRef::ObjectType t)
{
    return t == SWMMObjectRef::Curve
        || t == SWMMObjectRef::TimeSeries
        || t == SWMMObjectRef::Transect;
}

DeleteDataObjectCommand::DeleteDataObjectCommand(SWMMModelLayer      *layer,
                                                 const SWMMObjectRef &ref,
                                                 MapCanvas           *canvas,
                                                 QUndoCommand        *parent)
    : MapCommand(QObject::tr("Delete \"%1\"").arg(ref.name), canvas, parent)
    , m_layer(layer)
    , m_ref(ref)
{
    // Snapshot BEFORE redo() removes anything so undo() can rebuild the object.
    switch (m_ref.objectType) {
    case SWMMObjectRef::Curve:      snapshotCurve();      break;
    case SWMMObjectRef::TimeSeries: snapshotTimeSeries(); break;
    case SWMMObjectRef::Transect:   snapshotTransect();   break;
    default: break;
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
    if (!m_layer) return;
    switch (m_ref.objectType) {
    case SWMMObjectRef::Curve: {
        auto *reg = dd_curveReg(m_layer);
        if (!reg) return;
        if (auto *p = reg->findByName(m_ref.name))
            reg->remove(p);
        break;
    }
    case SWMMObjectRef::TimeSeries: {
        auto *reg = dd_tsReg(m_layer);
        if (!reg) return;
        if (auto *p = reg->findByName(m_ref.name))
            reg->remove(p);
        break;
    }
    case SWMMObjectRef::Transect: {
        auto *reg = dd_txReg(m_layer);
        if (!reg) return;
        if (auto *p = reg->findByName(m_ref.name))
            reg->remove(p);
        break;
    }
    default: break;
    }
}

void DeleteDataObjectCommand::undo()
{
    if (!m_layer || !m_captured) return;
    switch (m_ref.objectType) {
    case SWMMObjectRef::Curve:      restoreCurve();      break;
    case SWMMObjectRef::TimeSeries: restoreTimeSeries(); break;
    case SWMMObjectRef::Transect:   restoreTransect();   break;
    default: break;
    }
}

// --- Curve ------------------------------------------------------------------

void DeleteDataObjectCommand::snapshotCurve()
{
    auto *reg = dd_curveReg(m_layer);
    if (!reg) return;
    auto *p = reg->findByName(m_ref.name);
    if (!p) return;
    m_curveType = static_cast<int>(p->type());
    m_curvePoints.clear();
    for (const auto &pt : p->points())
        m_curvePoints.append(QPointF(pt.x, pt.y));
    m_captured = true;
}

void DeleteDataObjectCommand::restoreCurve()
{
    auto *reg = dd_curveReg(m_layer);
    if (!reg || m_curveType < 0 || reg->hasName(m_ref.name)) return;
    auto *p = reg->create(m_ref.name,
                          static_cast<openswmmvis::curve::CurveType>(m_curveType));
    if (!p) return;
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
    if (!reg) return;
    auto *p = reg->findByName(m_ref.name);
    if (!p) return;
    m_tsUnits          = p->unitsLabel();
    m_tsDescription    = p->description();
    m_tsSourceMode     = static_cast<int>(p->sourceMode());
    m_tsFilePath       = p->filePath();
    m_tsColumnSelector = p->columnSelector();
    m_tsFileMTime      = p->fileMTime();
    m_tsPoints.clear();
    for (const auto &pt : p->points())
        m_tsPoints.append(qMakePair(pt.time, pt.value));
    m_captured = true;
}

void DeleteDataObjectCommand::restoreTimeSeries()
{
    auto *reg = dd_tsReg(m_layer);
    if (!reg || reg->hasName(m_ref.name)) return;
    auto *p = reg->create(m_ref.name);
    if (!p) return;
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
    if (!reg) return;
    auto *p = reg->findByName(m_ref.name);
    if (!p) return;
    m_txComments       = p->comments();
    m_txNLeft          = p->nLeftBank();
    m_txNRight         = p->nRightBank();
    m_txNChannel       = p->nChannel();
    m_txXLeftBank      = p->xLeftBank();
    m_txXRightBank     = p->xRightBank();
    m_txXLeftEncroach  = p->xLeftEncroachment();
    m_txXRightEncroach = p->xRightEncroachment();
    m_txXFactor        = p->stationMultiplier();
    m_txYFactor        = p->elevationOffset();
    m_txLengthFactor   = p->meanderFactor();
    m_txPoints.clear();
    for (const auto &pt : p->points())
        m_txPoints.append(qMakePair(pt.station, pt.elevation));
    m_captured = true;
}

void DeleteDataObjectCommand::restoreTransect()
{
    auto *reg = dd_txReg(m_layer);
    if (!reg || reg->hasName(m_ref.name)) return;
    auto *p = reg->create(m_ref.name);
    if (!p) return;
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

InsertVirtualJunctionCommand::InsertVirtualJunctionCommand(
        SWMMModelLayer *layer, QString linkName, double t,
        QString nodeName, QString newLinkName,
        MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Insert Virtual Junction \"%1\"").arg(nodeName),
                 canvas, parent),
      m_layer(layer),
      m_linkName(std::move(linkName)),
      m_t(t),
      m_nodeName(std::move(nodeName)),
      m_newLinkName(std::move(newLinkName))
{
}

void InsertVirtualJunctionCommand::redo()
{
    if (!m_layer || m_present) return;
    if (m_layer->applyInsertVirtualJunction(m_linkName, m_t,
                                            m_nodeName, m_newLinkName))
        m_present = true;
}

void InsertVirtualJunctionCommand::undo()
{
    if (!m_layer || !m_present) return;
    // Fusing the inserted virtual junction is the exact engine-side inverse
    // of the split (verified byte-identical round-trip in the engine tests).
    if (m_layer->applyFuseVirtualJunction(m_nodeName))
        m_present = false;
}

// ===========================================================================
// FuseVirtualJunctionCommand
// ===========================================================================

FuseVirtualJunctionCommand::FuseVirtualJunctionCommand(
        SWMMModelLayer *layer, QString nodeName,
        MapCanvas *canvas, QUndoCommand *parent)
    : MapCommand(QObject::tr("Fuse Virtual Junction \"%1\"").arg(nodeName),
                 canvas, parent),
      m_layer(layer),
      m_nodeName(std::move(nodeName))
{
    // Snapshot everything a re-split cannot derive: conduit names, the
    // length-ratio split position, the grade-break invert and the map
    // coordinate.
    if (!m_layer) return;
    SWMM_Engine eng = m_layer->engine();
    if (!eng) return;
    const int ni = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (ni < 0) return;

    int up = -1, dn = -1;
    const int nLinks = swmm_link_count(eng);
    for (int i = 0; i < nLinks; ++i) {
        int n1 = -1, n2 = -1;
        swmm_link_get_from_node(eng, i, &n1);
        swmm_link_get_to_node(eng, i, &n2);
        if (n2 == ni) up = i;
        if (n1 == ni) dn = i;
    }
    if (up < 0 || dn < 0 || up == dn) return;

    double lu = 0.0, ld = 0.0;
    swmm_link_get_length(eng, up, &lu);
    swmm_link_get_length(eng, dn, &ld);
    if (lu + ld <= 0.0) return;

    m_upLinkName = QString::fromUtf8(swmm_link_id(eng, up));
    m_dnLinkName = QString::fromUtf8(swmm_link_id(eng, dn));
    m_t = lu / (lu + ld);
    swmm_node_get_invert_elev(eng, ni, &m_invert);
    swmm_spatial_get_node_coord(eng, ni, &m_x, &m_y);
    m_valid = true;
}

void FuseVirtualJunctionCommand::redo()
{
    if (!m_layer || !m_valid || m_present) return;
    if (m_layer->applyFuseVirtualJunction(m_nodeName))
        m_present = true;
}

void FuseVirtualJunctionCommand::undo()
{
    if (!m_layer || !m_present) return;
    if (!m_layer->applyInsertVirtualJunction(m_upLinkName, m_t,
                                             m_nodeName, m_dnLinkName))
        return;
    m_present = false;

    // Restore the exact grade-break invert and map coordinate — the split
    // interpolates both along the (now single-gradient) merged conduit, so
    // an original slope-break node needs the snapshot values back.
    SWMM_Engine eng = m_layer->engine();
    if (!eng) return;
    const int ni = swmm_node_index(eng, m_nodeName.toUtf8().constData());
    if (ni < 0) return;
    swmm_node_set_invert_elev(eng, ni, m_invert);
    m_layer->applyNodeMove(ni, m_x, m_y);
}
