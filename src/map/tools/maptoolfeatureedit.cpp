/*!
 * \file   maptoolfeatureedit.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "map/tools/maptoolfeatureedit.h"

#include "map/featurecommands.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/featurelayer.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

using namespace openswmmvis::feature;
using openswmmvis::map::EditFeatureGeometryCommand;

namespace {

QString tr_(const char *s)
{
    return QCoreApplication::translate("FeatureEditTools", s);
}

/*! The one selected feature, or kInvalidFeatureId when zero or many are
 *  selected. Editing tools act on exactly one feature; asking the user to
 *  narrow the selection is clearer than silently picking one. */
FeatureId soleSelection(const FeatureLayer *layer)
{
    if (!layer) return kInvalidFeatureId;
    const QSet<long long> ids = layer->selectedFeatureIds();
    if (ids.size() != 1) return kInvalidFeatureId;
    return static_cast<FeatureId>(*ids.constBegin());
}

double distToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const double vx = b.x() - a.x(), vy = b.y() - a.y();
    const double wx = p.x() - a.x(), wy = p.y() - a.y();
    const double len2 = vx * vx + vy * vy;
    double t = (len2 > 0.0) ? (wx * vx + wy * vy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double dx = a.x() + t * vx - p.x();
    const double dy = a.y() + t * vy - p.y();
    return std::sqrt(dx * dx + dy * dy);
}

}   // namespace

// ===========================================================================
// Add hole
// ===========================================================================

OpenSWMMVisMapToolAddHole::OpenSWMMVisMapToolAddHole(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapToolFeatureDrawBase(QStringLiteral("Add Hole"), canvas, parent)
{
}

FeatureId OpenSWMMVisMapToolAddHole::selectedFeature() const
{
    return soleSelection(m_target.data());
}

FeatureGeometry OpenSWMMVisMapToolAddHole::buildGeometry(const Ring &ring) const
{
    FeatureLayer *layer = m_target.data();
    if (!layer) return {};

    const FeatureId id = selectedFeature();
    if (id == kInvalidFeatureId) return {};   // commit() rejects an empty result

    Feature f;
    if (!layer->feature(id, f)) return {};

    FeatureGeometry g = f.geometry;
    if (g.parts().isEmpty()) return {};

    // Attach the hole to the part that CONTAINS it. With a multi-polygon the
    // user may well be punching an island out of the second part, and guessing
    // "part 0" would silently produce an invalid geometry that validate() then
    // rejects with a confusing message.
    int host = -1;
    if (!ring.pts.isEmpty()) {
        for (int i = 0; i < g.parts().size(); ++i) {
            if (g.parts().at(i).exterior.containsPoint(ring.pts.first())) {
                host = i;
                break;
            }
        }
    }
    if (host < 0) return {};

    Ring hole = ring;
    hole.normalizeCCW();
    g.parts()[host].holes.append(hole);
    return g;
}

// ===========================================================================
// Add part
// ===========================================================================

OpenSWMMVisMapToolAddPart::OpenSWMMVisMapToolAddPart(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapToolFeatureDrawBase(QStringLiteral("Add Part"), canvas, parent)
{
}

FeatureId OpenSWMMVisMapToolAddPart::selectedFeature() const
{
    return soleSelection(m_target.data());
}

int OpenSWMMVisMapToolAddPart::minimumVertices() const
{
    FeatureLayer *layer = m_target.data();
    if (!layer) return 3;
    switch (layer->geometryType()) {
    case GeometryType::Point:
    case GeometryType::MultiPoint:      return 1;
    case GeometryType::LineString:
    case GeometryType::MultiLineString: return 2;
    default:                            return 3;
    }
}

bool OpenSWMMVisMapToolAddPart::isClosed() const
{
    FeatureLayer *layer = m_target.data();
    if (!layer) return true;
    const GeometryType t = layer->geometryType();
    return t == GeometryType::Polygon || t == GeometryType::MultiPolygon;
}

FeatureGeometry OpenSWMMVisMapToolAddPart::buildGeometry(const Ring &ring) const
{
    FeatureLayer *layer = m_target.data();
    if (!layer) return {};

    const FeatureId id = selectedFeature();
    if (id == kInvalidFeatureId) return {};

    Feature f;
    if (!layer->feature(id, f)) return {};

    // Only a Multi* layer can hold more than one part. Refusing here — rather
    // than appending and letting validate() complain about part count — lets
    // commit() report a reason the user can act on.
    const GeometryType declared = layer->geometryType();
    if (!isMultiType(declared)) return {};

    FeatureGeometry g = f.geometry;
    g.setType(declared);

    Part p;
    p.exterior = ring;
    if (isClosed()) p.exterior.normalizeCCW();
    g.addPart(p);
    return g;
}

// ===========================================================================
// Edit vertex
// ===========================================================================

OpenSWMMVisMapToolEditFeatureVertex::OpenSWMMVisMapToolEditFeatureVertex(
        MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Edit Feature Vertices"), canvas, parent)
    , m_featureId(kInvalidFeatureId)
{
}

void OpenSWMMVisMapToolEditFeatureVertex::setTargetLayer(FeatureLayer *layer)
{
    if (m_target == layer) return;
    m_target = layer;
    reloadSelection();
}

QCursor OpenSWMMVisMapToolEditFeatureVertex::cursor() const
{
    return m_hover.isValid() ? Qt::SizeAllCursor : Qt::CrossCursor;
}

void OpenSWMMVisMapToolEditFeatureVertex::activate()
{
    reloadSelection();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolEditFeatureVertex::deactivate()
{
    m_dragging.clear();
    m_hover.clear();
    m_dragged = false;
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolEditFeatureVertex::reloadSelection()
{
    m_featureId = soleSelection(m_target.data());
    m_geom = FeatureGeometry{};
    if (m_featureId != kInvalidFeatureId && m_target) {
        Feature f;
        if (m_target->feature(m_featureId, f)) m_geom = f.geometry;
    }
    m_hover.clear();
    m_dragging.clear();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-vertex-reload"));
}

Ring *OpenSWMMVisMapToolEditFeatureVertex::ringFor(const FeatureVertexRef &ref)
{
    if (ref.part < 0 || ref.part >= m_geom.parts().size()) return nullptr;
    Part &p = m_geom.parts()[ref.part];
    if (ref.ring < 0) return &p.exterior;
    if (ref.ring >= p.holes.size()) return nullptr;
    return &p.holes[ref.ring];
}

const Ring *OpenSWMMVisMapToolEditFeatureVertex::ringFor(const FeatureVertexRef &ref) const
{
    if (ref.part < 0 || ref.part >= m_geom.parts().size()) return nullptr;
    const Part &p = m_geom.parts().at(ref.part);
    if (ref.ring < 0) return &p.exterior;
    if (ref.ring >= p.holes.size()) return nullptr;
    return &p.holes.at(ref.ring);
}

FeatureVertexRef OpenSWMMVisMapToolEditFeatureVertex::handleAt(const QPoint &pos,
                                                               int tolPx) const
{
    FeatureVertexRef best;
    double bestDist = tolPx + 1.0;

    const auto scan = [&](int partIdx, int ringIdx, const Ring &r) {
        for (int i = 0; i < r.pts.size(); ++i) {
            int px = 0, py = 0;
            toPixelCoords(r.pts.at(i).x(), r.pts.at(i).y(), px, py);
            const double d = std::hypot(px - pos.x(), py - pos.y());
            if (d <= tolPx && d < bestDist) {
                bestDist = d;
                best = {partIdx, ringIdx, i};
            }
        }
    };

    for (int p = 0; p < m_geom.parts().size(); ++p) {
        const Part &part = m_geom.parts().at(p);
        scan(p, -1, part.exterior);
        for (int h = 0; h < part.holes.size(); ++h) scan(p, h, part.holes.at(h));
    }
    return best;
}

FeatureVertexRef OpenSWMMVisMapToolEditFeatureVertex::segmentAt(const QPoint &pos,
                                                                int tolPx) const
{
    FeatureVertexRef best;
    double bestDist = tolPx + 1.0;

    const auto scan = [&](int partIdx, int ringIdx, const Ring &r, bool closed) {
        const int n = r.pts.size();
        if (n < 2) return;
        const int last = closed ? n : n - 1;
        for (int i = 0; i < last; ++i) {
            const QPointF a = r.pts.at(i);
            const QPointF b = r.pts.at((i + 1) % n);
            int ax = 0, ay = 0, bx = 0, by = 0;
            toPixelCoords(a.x(), a.y(), ax, ay);
            toPixelCoords(b.x(), b.y(), bx, by);
            const double d = distToSegment(QPointF(pos), QPointF(ax, ay), QPointF(bx, by));
            if (d <= tolPx && d < bestDist) {
                bestDist = d;
                best = {partIdx, ringIdx, i};   // insert AFTER vertex i
            }
        }
    };

    const GeometryType t = m_geom.type();
    const bool closed = (t == GeometryType::Polygon || t == GeometryType::MultiPolygon);
    for (int p = 0; p < m_geom.parts().size(); ++p) {
        const Part &part = m_geom.parts().at(p);
        scan(p, -1, part.exterior, closed);
        for (int h = 0; h < part.holes.size(); ++h) scan(p, h, part.holes.at(h), true);
    }
    return best;
}

void OpenSWMMVisMapToolEditFeatureVertex::pushEdit(const QString &text, bool mergeable)
{
    FeatureLayer *layer = m_target.data();
    if (!layer || m_featureId == kInvalidFeatureId || !m_canvas) return;

    QString reason;
    if (!m_geom.validate(layer->geometryType(), &reason)) {
        emit editRejected(reason);
        m_geom = m_before;          // roll the working copy back
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-vertex-reject"));
        return;
    }

    auto *cmd = new EditFeatureGeometryCommand(layer, m_featureId, m_before, m_geom,
                                               text, mergeable, m_canvas);
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       delete cmd;

    m_before = m_geom;
}

void OpenSWMMVisMapToolEditFeatureVertex::mousePressEvent(QMouseEvent *event)
{
    FeatureLayer *layer = m_target.data();
    if (!layer || !layer->isEditable()) return;

    // The selection may have changed since the last event.
    if (soleSelection(layer) != m_featureId) reloadSelection();
    if (m_featureId == kInvalidFeatureId) return;

    const QPoint pos = event->pos();

    if (event->button() == Qt::RightButton) {
        // On a handle: delete it. On a segment: insert one.
        const FeatureVertexRef h = handleAt(pos);
        m_before = m_geom;
        if (h.isValid()) {
            Ring *r = ringFor(h);
            if (!r) return;
            r->removeVertex(h.vertex);
            pushEdit(tr_("Delete vertex"), /*mergeable=*/false);
        } else {
            const FeatureVertexRef s = segmentAt(pos);
            if (!s.isValid()) return;
            Ring *r = ringFor(s);
            if (!r) return;
            double mx = 0, my = 0;
            toMapCoords(pos.x(), pos.y(), mx, my);
            // A vertex inserted into a 3D ring is unsampled until the next
            // resample; NaN is the honest value, not the neighbours' average.
            r->insertVertex(s.vertex + 1, QPointF(mx, my),
                            std::numeric_limits<double>::quiet_NaN());
            if (layer->isThreeD() && layer->zPolicy().resampleOnEdit)
                layer->sampleZ(m_geom, m_canvas, /*densify=*/false);
            pushEdit(tr_("Insert vertex"), /*mergeable=*/false);
        }
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("feature-vertex-edit"));
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    m_dragging = handleAt(pos);
    m_dragged  = false;
    if (m_dragging.isValid()) m_before = m_geom;
}

void OpenSWMMVisMapToolEditFeatureVertex::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;

    if (!m_dragging.isValid()) {
        const FeatureVertexRef h = handleAt(event->pos());
        if (!(h == m_hover)) {
            m_hover = h;
            emit cursorChanged(cursor());
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-vertex-hover"));
        }
        return;
    }

    Ring *r = ringFor(m_dragging);
    if (!r) return;
    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    r->moveVertex(m_dragging.vertex, QPointF(mx, my));
    m_dragged = true;
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-vertex-drag"));
}

void OpenSWMMVisMapToolEditFeatureVertex::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    if (!m_dragging.isValid() || !m_dragged) {
        m_dragging.clear();
        return;
    }

    FeatureLayer *layer = m_target.data();
    if (layer && layer->isThreeD() && layer->zPolicy().resampleOnEdit)
        layer->sampleZ(m_geom, m_canvas, /*densify=*/false);

    // Mergeable: a drag produces one command per release, and consecutive
    // drags of the same vertex collapse into one undo step.
    pushEdit(tr_("Move vertex"), /*mergeable=*/true);

    m_dragging.clear();
    m_dragged = false;
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("feature-vertex-move"));
}

void OpenSWMMVisMapToolEditFeatureVertex::keyPressEvent(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Escape) return;
    if (m_dragging.isValid()) {
        m_geom = m_before;          // abandon the in-flight drag
        m_dragging.clear();
        m_dragged = false;
    } else {
        reloadSelection();
    }
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-vertex-escape"));
}

void OpenSWMMVisMapToolEditFeatureVertex::paint(QPainter *painter,
                                                const MapExtent &,
                                                const SpatialReferenceSystem *)
{
    if (!painter || m_featureId == kInvalidFeatureId) return;

    painter->save();
    const QColor accent(30, 110, 200);

    const auto drawRing = [&](const Ring &r, int partIdx, int ringIdx) {
        for (int i = 0; i < r.pts.size(); ++i) {
            int px = 0, py = 0;
            toPixelCoords(r.pts.at(i).x(), r.pts.at(i).y(), px, py);
            const FeatureVertexRef ref{partIdx, ringIdx, i};
            const bool active = (ref == m_hover) || (ref == m_dragging);
            painter->setBrush(active ? QColor(255, 165, 0) : accent);
            painter->setPen(QPen(Qt::white, 1));
            painter->drawRect(QRect(px - 4, py - 4, 8, 8));
        }
    };

    for (int p = 0; p < m_geom.parts().size(); ++p) {
        const Part &part = m_geom.parts().at(p);
        drawRing(part.exterior, p, -1);
        for (int h = 0; h < part.holes.size(); ++h) drawRing(part.holes.at(h), p, h);
    }
    painter->restore();
}

// ===========================================================================
// Move feature
// ===========================================================================

OpenSWMMVisMapToolMoveFeature::OpenSWMMVisMapToolMoveFeature(MapCanvas *canvas,
                                                              QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Move Feature"), canvas, parent)
    , m_featureId(kInvalidFeatureId)
{
}

void OpenSWMMVisMapToolMoveFeature::setTargetLayer(FeatureLayer *layer)
{
    m_target = layer;
}

QCursor OpenSWMMVisMapToolMoveFeature::cursor() const
{
    return m_moving ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
}

void OpenSWMMVisMapToolMoveFeature::deactivate()
{
    m_moving = false;
    m_featureId = kInvalidFeatureId;
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolMoveFeature::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    FeatureLayer *layer = m_target.data();
    if (!layer || !layer->isEditable()) return;

    m_featureId = soleSelection(layer);
    if (m_featureId == kInvalidFeatureId) return;

    Feature f;
    if (!layer->feature(m_featureId, f)) { m_featureId = kInvalidFeatureId; return; }

    m_before = f.geometry;
    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_anchor = QPointF(mx, my);
    m_moving = true;
    emit cursorChanged(cursor());
}

void OpenSWMMVisMapToolMoveFeature::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_moving || !m_canvas) return;
    Q_UNUSED(event);
    // The preview is the committed geometry until release: translating the
    // working copy per motion and writing on release keeps the write count at
    // one per gesture instead of one per pixel.
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("feature-move"));
}

void OpenSWMMVisMapToolMoveFeature::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_moving) return;
    m_moving = false;
    emit cursorChanged(cursor());

    FeatureLayer *layer = m_target.data();
    if (!layer || m_featureId == kInvalidFeatureId || !m_canvas) return;

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    const QPointF delta(mx - m_anchor.x(), my - m_anchor.y());
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y())) return;

    FeatureGeometry moved = m_before;
    const auto shiftRing = [&delta](Ring &r) {
        for (QPointF &p : r.pts) p += delta;
    };
    for (Part &p : moved.parts()) {
        shiftRing(p.exterior);
        for (Ring &h : p.holes) shiftRing(h);
    }

    if (layer->isThreeD() && layer->zPolicy().resampleOnEdit)
        layer->sampleZ(moved, m_canvas, /*densify=*/false);

    auto *cmd = new EditFeatureGeometryCommand(layer, m_featureId, m_before, moved,
                                               tr_("Move feature"),
                                               /*mergeable=*/false, m_canvas);
    if (m_canvas->undoStack()) m_canvas->undoStack()->push(cmd);
    else                       delete cmd;

    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("feature-move-commit"));
}

void OpenSWMMVisMapToolMoveFeature::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_moving = false;
        m_featureId = kInvalidFeatureId;
        emit cursorChanged(cursor());
    }
}
