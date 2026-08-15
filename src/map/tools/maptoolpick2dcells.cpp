/*!
 * \file   maptoolpick2dcells.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptoolpick2dcells.h"

#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QRectF>
#include <QSet>

#include <algorithm>
#include <cmath>

MapToolPick2DCells::MapToolPick2DCells(MapCanvas *canvas,
                                       SelectionManager *selection,
                                       QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("pick-2d-cells"), canvas, parent),
      m_selection(selection)
{
}

void MapToolPick2DCells::setMode(Mode m)
{
    if (m_mode == m) return;
    m_mode = m;
    // Cancel any in-progress selection on mode swap.
    m_dragging  = false;
    m_drawing   = false;
    m_lassoMapPts.clear();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-mode"));
}

void MapToolPick2DCells::setTargetLayer(SWMM2DResultsLayer *layer)
{
    m_targetLayer = layer;
}

void MapToolPick2DCells::activate()
{
    OpenSWMMVisMapTool::activate();
    // Honor an explicitly bound active layer; only fall back to first-found
    // when the project window hasn't designated one.
    if (!m_targetLayer)
        m_targetLayer = findResultsLayer_();
    m_meshLayer   = findMeshLayer_();
    m_dragging = false;
    m_drawing  = false;
    m_lassoMapPts.clear();
}

void MapToolPick2DCells::deactivate()
{
    m_dragging = false;
    m_drawing  = false;
    m_pendingRightPlot = false;
    m_lassoMapPts.clear();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-deactivate"));
    OpenSWMMVisMapTool::deactivate();
}

SWMM2DResultsLayer *MapToolPick2DCells::findResultsLayer_() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (auto *r2d = qobject_cast<SWMM2DResultsLayer *>(l))
            return r2d;
    }
    return nullptr;
}

SWMM2DMeshLayer *MapToolPick2DCells::findMeshLayer_() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *m = qobject_cast<SWMM2DMeshLayer *>(l))
            if (m->isActiveMesh()) return m;
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *m = qobject_cast<SWMM2DMeshLayer *>(l)) return m;
    return nullptr;
}

QVector<int> MapToolPick2DCells::selectedCells_() const
{
    QVector<int> out;
    if (!m_selection) return out;
    // Accept every MeshCell ref (there is one 2D mesh per canvas in practice;
    // not filtering by layerKey keeps this robust when no SWMM2DMeshLayer
    // exists to supply a matching key).
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshCell) continue;
        QString lk; int tri = -1;
        if (mesh::MeshObjectRef::parseCell(ref, &lk, &tri))
            out.push_back(tri);
    }
    return out;
}

void MapToolPick2DCells::applySelection_(const QVector<int> &hits,
                                         Qt::KeyboardModifiers mods)
{
    if (!m_selection) return;
    // A mesh layer is optional: results-only views (mesh geometry lives only
    // in the HDF5 output) have no SWMM2DMeshLayer. Fall back to an empty
    // source path → MeshObjectRef::layerKey "mesh::<unsaved>" so cells are
    // still representable in the selection bus and the toolbar label / results
    // highlight still work.
    if (!m_meshLayer) m_meshLayer = findMeshLayer_();
    const QString path = m_meshLayer ? m_meshLayer->sourcePath() : QString();

    QSet<SWMMObjectRef> set;
    for (int i : hits) set.insert(mesh::MeshObjectRef::cell(path, i));

    const bool add    = mods.testFlag(Qt::ShiftModifier);
    const bool toggle = mods.testFlag(Qt::ControlModifier);
    const SelectionManager::Mode mode =
        toggle ? SelectionManager::Toggle
               : (add ? SelectionManager::Add : SelectionManager::Replace);
    m_selection->select(set, mode);
}

void MapToolPick2DCells::requestPlotAt_(const QPoint &pixel)
{
    SWMM2DResultsLayer *layer = m_targetLayer.data();
    if (!layer) layer = findResultsLayer_();
    if (!layer) return;

    QVector<int> targets = selectedCells_();
    if (targets.isEmpty()) {
        // Nothing selected — select + plot the cell under the cursor.
        const int idx = layer->pickCellAt(pixelToScene_(pixel.x(), pixel.y()));
        if (idx >= 0) {
            applySelection_({idx}, Qt::NoModifier);
            targets.push_back(idx);
        }
    }
    if (!targets.isEmpty())
        emit cellsPicked(layer, targets);
}

QPointF MapToolPick2DCells::pixelToScene_(int px, int py) const
{
    double mx = 0.0, my = 0.0;
    toMapCoords(px, py, mx, my);
    // Layer convention: scene Y is the map Y flipped (see
    // SWMM2DResultsLayer::rebuildSceneGeometry_).
    return QPointF(mx, -my);
}

// Selection picks prefer the mesh layer so they work during pure mesh editing
// (no results loaded yet) — matching the vertex / edge selectors. The results
// layer is the fallback for results-only views where the mesh geometry lives
// only in the HDF5 output and no SWMM2DMeshLayer exists.
int MapToolPick2DCells::pickCellAtScene_(const QPointF &scenePt)
{
    if (!m_meshLayer)   m_meshLayer   = findMeshLayer_();
    if (m_meshLayer)    return m_meshLayer->pickCellAt(scenePt);
    if (!m_targetLayer) m_targetLayer = findResultsLayer_();
    if (m_targetLayer)  return m_targetLayer->pickCellAt(scenePt);
    return -1;
}

QVector<int> MapToolPick2DCells::pickCellsInRectScene_(const QRectF &sceneRect)
{
    if (!m_meshLayer)   m_meshLayer   = findMeshLayer_();
    if (m_meshLayer)    return m_meshLayer->pickCellsInRect(sceneRect);
    if (!m_targetLayer) m_targetLayer = findResultsLayer_();
    if (m_targetLayer)  return m_targetLayer->pickCellsInRect(sceneRect);
    return {};
}

QVector<int> MapToolPick2DCells::pickCellsInPolygonScene_(const QPolygonF &scenePoly)
{
    if (!m_meshLayer)   m_meshLayer   = findMeshLayer_();
    if (m_meshLayer)    return m_meshLayer->pickCellsInPolygon(scenePoly);
    if (!m_targetLayer) m_targetLayer = findResultsLayer_();
    if (m_targetLayer)  return m_targetLayer->pickCellsInPolygon(scenePoly);
    return {};
}

void MapToolPick2DCells::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (event->button() != Qt::LeftButton) {
        if (event->button() == Qt::RightButton) {
            // While drawing a lasso, right-click undoes the last vertex.
            if (m_mode == Mode::Lasso && m_drawing) {
                if (!m_lassoMapPts.isEmpty()) {
                    m_lassoMapPts.removeLast();
                    if (m_lassoMapPts.isEmpty()) m_drawing = false;
                    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-undo"));
                }
                return;
            }
            // Otherwise right-click plots the current selection (or the cell
            // under the cursor when nothing is highlighted). Armed here,
            // fired on release — see m_pendingRightPlot in the header.
            m_pendingRightPlot = true;
        }
        return;
    }

    if (m_mode == Mode::Box) {
        m_startPixel   = event->pos();
        m_currentPixel = event->pos();
        m_dragging     = false;
    } else {
        // Lasso: snap-to-pixel-position and append.
        double mx = 0.0, my = 0.0;
        toMapCoords(event->pos().x(), event->pos().y(), mx, my);
        m_lassoMapPts.push_back(QPointF(mx, my));
        m_drawing = true;
        m_cursorPixel = event->pos();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-vertex"));
    }
}

void MapToolPick2DCells::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;

    if (m_mode == Mode::Box) {
        if (!(event->buttons() & Qt::LeftButton)) return;
        m_currentPixel = event->pos();
        const QPoint delta = m_currentPixel - m_startPixel;
        if (!m_dragging &&
            (std::abs(delta.x()) > kDragThreshPx || std::abs(delta.y()) > kDragThreshPx))
        {
            m_dragging = true;
        }
        if (m_dragging)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-box-drag"));
    } else if (m_drawing) {
        m_cursorPixel = event->pos();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-preview"));
    }
}

void MapToolPick2DCells::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (event->button() == Qt::RightButton) {
        if (m_pendingRightPlot) {
            m_pendingRightPlot = false;
            requestPlotAt_(event->pos());
        }
        return;
    }
    if (m_mode != Mode::Box) return;
    if (event->button() != Qt::LeftButton) return;

    QVector<int> hits;
    if (m_dragging) {
        // Build scene-space rect from the two corners.
        const QPointF a = pixelToScene_(m_startPixel.x(),   m_startPixel.y());
        const QPointF b = pixelToScene_(m_currentPixel.x(), m_currentPixel.y());
        const QRectF sceneRect = QRectF(a, b).normalized();
        hits = pickCellsInRectScene_(sceneRect);
    } else {
        // Single click — hit-test the cell under the cursor.
        const QPointF scenePt = pixelToScene_(event->pos().x(), event->pos().y());
        const int idx = pickCellAtScene_(scenePt);
        if (idx >= 0) hits.push_back(idx);
    }

    m_dragging = false;

    // Selection only highlights — right-click plots. A no-modifier single
    // click that missed every cell clears the selection.
    if (hits.isEmpty() && event->modifiers() == Qt::NoModifier) {
        if (m_selection) m_selection->clear();
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("pick2dcells-box-clear"));
        return;
    }
    applySelection_(hits, event->modifiers());
    // Invalidate AFTER the selection is applied — the repaint must be
    // scheduled against the new highlight set, not the one it replaces. Scene
    // (not Overlay alone) because the highlight lives in layer content: the
    // mesh renders through the QSG framebuffer, which only the Scene channel
    // marks dirty.
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("pick2dcells-box-end"));
}

void MapToolPick2DCells::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (m_mode != Mode::Lasso) return;
    if (event->button() != Qt::LeftButton) return;
    if (!m_drawing) return;

    // Double-click adds one vertex (the first click) — strip it before commit.
    if (!m_lassoMapPts.isEmpty())
        m_lassoMapPts.removeLast();
    if (m_lassoMapPts.size() < 3) {
        m_drawing = false;
        m_lassoMapPts.clear();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-lasso-cancel"));
        return;
    }

    QPolygonF scenePoly;
    scenePoly.reserve(m_lassoMapPts.size());
    for (const QPointF& mp : m_lassoMapPts)
        scenePoly.append(QPointF(mp.x(), -mp.y()));

    const QVector<int> hits = pickCellsInPolygonScene_(scenePoly);

    m_drawing = false;
    m_lassoMapPts.clear();

    // Selection only highlights — right-click plots.
    applySelection_(hits, event->modifiers());
    // After applySelection_, and on the Scene channel — see the box-select
    // path above for why.
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("pick2dcells-lasso-commit"));
}

void MapToolPick2DCells::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        m_dragging = false;
        m_drawing  = false;
        m_lassoMapPts.clear();
        if (m_selection) m_selection->clear();
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("pick2dcells-esc"));
        break;
    case Qt::Key_B:
        setMode(Mode::Box);
        break;
    case Qt::Key_L:
        setMode(Mode::Lasso);
        break;
    default:
        OpenSWMMVisMapTool::keyPressEvent(event);
        break;
    }
}

void MapToolPick2DCells::paint(QPainter *painter,
                                const MapExtent & /*canvasExtent*/,
                                const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (!painter || !m_canvas) return;

    painter->save();

    // Box rubber-band.
    if (m_mode == Mode::Box && m_dragging) {
        const QRect pixelRect(m_startPixel, m_currentPixel);
        QPen pen(QColor(45, 130, 220, 220));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(1);
        painter->setPen(pen);
        painter->setBrush(QColor(45, 130, 220, 60));
        painter->drawRect(pixelRect.normalized());
    }

    // Lasso preview.
    if (m_mode == Mode::Lasso && m_drawing && !m_lassoMapPts.isEmpty()) {
        QPolygon viewPoly;
        viewPoly.reserve(m_lassoMapPts.size() + 1);
        for (const QPointF& mp : m_lassoMapPts) {
            int px = 0, py = 0;
            toPixelCoords(mp.x(), mp.y(), px, py);
            viewPoly << QPoint(px, py);
        }
        viewPoly << m_cursorPixel;     // preview edge to current cursor

        QPen pen(QColor(200, 90, 35, 230));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setBrush(QColor(200, 90, 35, 50));
        painter->drawPolygon(viewPoly);

        // Vertex dots.
        painter->setBrush(QColor(200, 90, 35, 220));
        painter->setPen(Qt::NoPen);
        for (const QPoint& p : viewPoly)
            painter->drawEllipse(p, 3, 3);
    }

    painter->restore();
}
