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
#include "core/editgeometry.h"
#include "core/unitsystem.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"

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

    if (m_invertElev != 0.0) {
        SWMM_Engine eng = m_layer->engine();
        const int idx = swmm_node_index(eng, m_name.toUtf8().constData());
        if (idx >= 0)
            swmm_node_set_invert_elev(eng, idx, m_invertElev);
    }
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
    swmm_node_get_outfall_type(eng,    idx, &m_node.outfallType);
    swmm_node_get_outfall_flap_gate(eng, idx, &m_node.outfallFlapGate);
    swmm_node_get_storage_seep_rate(eng, idx, &m_node.seepRate);
    swmm_node_get_divider_type(eng,    idx, &m_node.dividerType);

    // Snapshot cascade links (identified by node index before delete).
    const int nLinks = swmm_link_count(eng);
    for (int li = 0; li < nLinks; ++li) {
        int n1 = -1, n2 = -1;
        swmm_link_get_from_node(eng, li, &n1);
        swmm_link_get_to_node(eng, li, &n2);
        if (n1 == idx || n2 == idx)
            m_cascadeLinks << snapshotLinkByIdx(eng, li);
    }
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
