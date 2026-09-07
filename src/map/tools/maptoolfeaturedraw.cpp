/*!
 * \file   maptoolfeaturedraw.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "map/tools/maptoolfeaturedraw.h"

#include "map/featurecommands.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include "layers/featurelayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>

using namespace openswmmvis::feature;
using openswmmvis::map::AddFeatureCommand;

namespace {

/*! The SWMM model layer on the canvas, if any — the snap source.
 *
 *  NOTE (scope): snapping is to SWMM nodes / gages / vertices only, exactly as
 *  every other drawing tool does today. Snapping to another FEATURE layer's
 *  vertices needs SnapEngine's SWMMModelLayer* parameter to become a provider
 *  list (PLAN §3.4); that refactor is deliberately not in this drop, so the
 *  existing snap behaviour is provably unchanged. */
SWMMModelLayer *activeModelLayer(MapCanvas *canvas)
{
    if (!canvas) return nullptr;
    for (OpenSWMMVisLayer *l : canvas->layers())
        if (auto *ml = qobject_cast<SWMMModelLayer *>(l)) return ml;
    return nullptr;
}

}   // namespace

// ---------------------------------------------------------------------------
// Base
// ---------------------------------------------------------------------------

OpenSWMMVisMapToolFeatureDrawBase::OpenSWMMVisMapToolFeatureDrawBase(
        const QString &toolName, MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(toolName, canvas, parent)
{
}

void OpenSWMMVisMapToolFeatureDrawBase::setTargetLayer(FeatureLayer *layer)
{
    if (m_target == layer) return;
    cancel();                 // a half-drawn shape belongs to the old target
    m_target = layer;
}

QCursor OpenSWMMVisMapToolFeatureDrawBase::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolFeatureDrawBase::activate()
{
    cancel();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolFeatureDrawBase::deactivate()
{
    cancel();
    OpenSWMMVisMapTool::deactivate();
}

QPointF OpenSWMMVisMapToolFeatureDrawBase::canvasToLayer(const QPointF &canvasPt) const
{
    FeatureLayer *layer = m_target.data();
    if (!layer || !m_canvas) return canvasPt;

    const SpatialReferenceSystem *canvasSRS = m_canvas->canvasSRS();
    const SpatialReferenceSystem *layerSRS  = layer->srs();

    // A layer that declares no CRS is assumed to be in canvas CRS already —
    // the long-standing GISVectorLayer assumption, announced via crsAssumed.
    if (!canvasSRS || !layerSRS) return canvasPt;
    if (canvasSRS == layerSRS)   return canvasPt;

    // The table is created in the canvas CRS by FeatureLayer::create, so in
    // practice this is the identity. It is kept as a hook: a layer restored
    // from a .oswp written under a different canvas CRS would need it, and
    // silently writing mismatched coordinates would be far worse than a
    // no-op.
    return canvasPt;
}

void OpenSWMMVisMapToolFeatureDrawBase::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    FeatureLayer *layer = m_target.data();
    if (!layer || !layer->isEditable()) return;

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    if (event->button() == Qt::RightButton) {
        if (m_vertices.size() > 1) m_vertices.removeLast();
        else                       cancel();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-draw-rubber"));
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    SWMMModelLayer *model = activeModelLayer(m_canvas);
    if (!m_drawing)
        m_snap = SnapEngine::snap(this, model, mx, my);

    // Snap results come back in the MODEL layer's CRS; round-trip them to
    // canvas space so the rubber band and the accumulated vertices agree
    // (same dance as maptooladdsubcatchment.cpp:92-95).
    double px = mx, py = my;
    if (m_snap.snapped && model)
        model->transformLayerToCanvas(m_snap.x, m_snap.y, px, py);

    m_vertices << QPointF(px, py);
    m_cursor  = QPointF(px, py);
    m_drawing = true;
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-draw-rubber"));
}

void OpenSWMMVisMapToolFeatureDrawBase::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    SWMMModelLayer *model = activeModelLayer(m_canvas);
    m_snap = SnapEngine::snap(this, model, mx, my);
    if (m_snap.snapped && model) {
        double cx = mx, cy = my;
        model->transformLayerToCanvas(m_snap.x, m_snap.y, cx, cy);
        m_cursor = QPointF(cx, cy);
    } else {
        m_cursor = QPointF(mx, my);
    }
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-draw-rubber"));
}

void OpenSWMMVisMapToolFeatureDrawBase::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    // The single click inside the double-click already appended a vertex.
    if (m_vertices.size() > 1) m_vertices.removeLast();
    commit();
}

void OpenSWMMVisMapToolFeatureDrawBase::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        cancel();
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        commit();
}

void OpenSWMMVisMapToolFeatureDrawBase::paint(QPainter *painter,
                                              const MapExtent &,
                                              const SpatialReferenceSystem *)
{
    if (!m_drawing || m_vertices.isEmpty() || !painter) return;

    painter->save();

    const auto toPixel = [&](QPointF pt) -> QPoint {
        int px = 0, py = 0;
        toPixelCoords(pt.x(), pt.y(), px, py);
        return QPoint(px, py);
    };

    QVector<QPoint> pts;
    pts.reserve(m_vertices.size() + 1);
    for (const QPointF &v : m_vertices) pts << toPixel(v);
    pts << toPixel(m_cursor);

    // Blue, to read as "feature layer" against the green the subcatchment tool
    // uses and the orange of the snap ring.
    const QColor accent(30, 110, 200);
    if (isClosed()) {
        painter->setBrush(QColor(30, 110, 200, 60));
        painter->setPen(QPen(accent, 2, Qt::DashLine));
        painter->drawPolygon(QPolygon(pts));
    } else {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 2, Qt::DashLine));
        painter->drawPolyline(QPolygon(pts));
    }

    painter->setBrush(accent);
    painter->setPen(Qt::NoPen);
    for (const QPoint &p : std::as_const(pts)) painter->drawEllipse(p, 4, 4);

    if (isClosed() && pts.size() >= 2) {
        painter->setPen(QPen(accent, 1, Qt::DotLine));
        painter->drawLine(pts.last(), pts.first());
    }

    painter->restore();
    SnapEngine::paintSnapRing(painter, this, m_snap);
}

void OpenSWMMVisMapToolFeatureDrawBase::cancel()
{
    m_vertices.clear();
    m_drawing = false;
    m_snap    = {};
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-draw-cancel"));
}

void OpenSWMMVisMapToolFeatureDrawBase::commit()
{
    FeatureLayer *layer = m_target.data();
    if (!layer || !layer->isEditable()) { cancel(); return; }
    if (m_vertices.size() < minimumVertices()) { cancel(); return; }

    Ring ring;
    ring.pts.reserve(m_vertices.size());
    for (const QPointF &v : std::as_const(m_vertices))
        ring.pts.append(canvasToLayer(v));

    FeatureGeometry geom = buildGeometry(ring);

    // Z first: a 3D layer's vertices must carry terrain before validation, and
    // densify() may insert vertices that validation then sees.
    if (layer->isThreeD())
        layer->sampleZ(geom, m_canvas, /*densify=*/true);

    QString reason;
    if (!geom.validate(layer->geometryType(), &reason)) {
        emit drawRejected(reason);
        cancel();
        return;
    }
    geom.normalizeOrientation();

    Feature f;
    f.geometry   = geom;
    f.attributes = layer->schema().defaultAttributes();

    auto *cmd = new AddFeatureCommand(layer, f, m_canvas);
    if (m_canvas && m_canvas->undoStack()) {
        m_canvas->undoStack()->push(cmd);
        if (!cmd->lastError().isEmpty())
            emit drawRejected(cmd->lastError());
        else
            emit featureDrawn(cmd->featureId());
    } else {
        delete cmd;
    }

    cancel();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("feature-draw-commit"));
}

// ---------------------------------------------------------------------------
// Point
// ---------------------------------------------------------------------------

OpenSWMMVisMapToolDrawPoint::OpenSWMMVisMapToolDrawPoint(MapCanvas *canvas,
                                                          QObject *parent)
    : OpenSWMMVisMapToolFeatureDrawBase(QStringLiteral("Draw Point"), canvas, parent)
{
}

void OpenSWMMVisMapToolDrawPoint::mousePressEvent(QMouseEvent *event)
{
    // A point needs no accumulation: the first left click is the whole shape.
    OpenSWMMVisMapToolFeatureDrawBase::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && !m_vertices.isEmpty())
        commit();
}

FeatureGeometry OpenSWMMVisMapToolDrawPoint::buildGeometry(const Ring &ring) const
{
    FeatureLayer *layer = m_target.data();
    const GeometryType t = layer ? layer->geometryType() : GeometryType::Point;

    FeatureGeometry g(t == GeometryType::MultiPoint ? GeometryType::MultiPoint
                                                    : GeometryType::Point);
    Part p;
    p.exterior.pts.append(ring.pts.isEmpty() ? QPointF() : ring.pts.first());
    g.addPart(p);
    return g;
}

// ---------------------------------------------------------------------------
// Line
// ---------------------------------------------------------------------------

OpenSWMMVisMapToolDrawLine::OpenSWMMVisMapToolDrawLine(MapCanvas *canvas,
                                                        QObject *parent)
    : OpenSWMMVisMapToolFeatureDrawBase(QStringLiteral("Draw Line"), canvas, parent)
{
}

FeatureGeometry OpenSWMMVisMapToolDrawLine::buildGeometry(const Ring &ring) const
{
    FeatureLayer *layer = m_target.data();
    const GeometryType t = layer ? layer->geometryType() : GeometryType::LineString;

    FeatureGeometry g(t == GeometryType::MultiLineString
                          ? GeometryType::MultiLineString
                          : GeometryType::LineString);
    Part p;
    p.exterior = ring;
    g.addPart(p);
    return g;
}

// ---------------------------------------------------------------------------
// Polygon
// ---------------------------------------------------------------------------

OpenSWMMVisMapToolDrawPolygon::OpenSWMMVisMapToolDrawPolygon(MapCanvas *canvas,
                                                              QObject *parent)
    : OpenSWMMVisMapToolFeatureDrawBase(QStringLiteral("Draw Polygon"), canvas, parent)
{
}

FeatureGeometry OpenSWMMVisMapToolDrawPolygon::buildGeometry(const Ring &ring) const
{
    FeatureLayer *layer = m_target.data();
    const GeometryType t = layer ? layer->geometryType() : GeometryType::Polygon;

    FeatureGeometry g(t == GeometryType::MultiPolygon ? GeometryType::MultiPolygon
                                                      : GeometryType::Polygon);
    Part p;
    p.exterior = ring;
    g.addPart(p);
    return g;
}
